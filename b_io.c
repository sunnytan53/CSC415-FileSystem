#include <stdlib.h> // for malloc
#include <string.h> // for memcpy
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "b_io.h"
#include "mfs.h"
#include <pthread.h>

#define MAXFCBS 20
#define BUFSIZE 512
#define FUNC_READ 1
#define FUNC_WRITE 2

// static mutex for only b_io to avoid race condition
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct b_fcb
{
	int linuxFd;			   // holds the systems file descriptor
	char *buf;				   // holds the open file buffer
	uint64_t index;			   // holds current index of the buffer
	uint64_t buflen;		   // holds how many valid bytes are in the buffer
	fdDir *parent;			   // holds the parent directory of the file
	unsigned short entryIndex; // holds the index of the entry in directory
	unsigned char detector;	   // holds a value to detect its functionality
} b_fcb;

b_fcb fcbArray[MAXFCBS];

int startup = 0; //Indicates that this has not been initialized

//Method to initialize our file system
void b_init()
{
	//init fcbArray to all free
	for (int i = 0; i < MAXFCBS; i++)
	{
		fcbArray[i].linuxFd = -1; //indicates a free fcbArray
	}

	startup = 1;
}

//Method to get a free FCB element
int b_getFCB()
{
	for (int i = 0; i < MAXFCBS; i++)
	{
		if (fcbArray[i].linuxFd == -1)
		{
			fcbArray[i].linuxFd = -2; // used but not assigned
			return i;				  //Not thread safe
		}
	}
	return (-1); //all in use
}

// Interface to open a buffered file
// Modification of interface for this assignment, flags match the Linux flags for open
// O_RDONLY, O_WRONLY, or O_RDWR didn't use
int b_open(char *path, int flags)
{
	//don't call b_close() since that requires an initialized fcb
	if (startup == 0)
		b_init(); //Initialize our system

	// lock the mutex and handle error
	if (pthread_mutex_lock(&mutex) < 0)
	{
		eprintf("\nMutex failed to lock!");
		return -1;
	}

	// get our own file descriptor
	int returnFd = b_getFCB();
	if (fcbArray[returnFd].linuxFd != -2)
	{ // unexpected error
		eprintf("b_getFCB()");
		return -1;
	}
	fcbArray[returnFd].linuxFd = returnFd; // Save the linux file descriptor

	// unlock the mutex and handle error
	if (pthread_mutex_unlock(&mutex) < 0)
	{
		eprintf("\nMutex failed to unlock!");
		return -1;
	}

	// NOTE: we assume the destination may hold path so we will do something like fs_mkdir()
	char *pathBeforeLastSlash = malloc(strlen(path) + 1);
	if (pathBeforeLastSlash == NULL)
	{
		eprintf("malloc() on pathBeforeLastSlash");
		return -1;
	}
	strcpy(pathBeforeLastSlash, path);
	char *trueFileName = getPathByLastSlash(pathBeforeLastSlash);

	// find the directory that is going to store the file
	fcbArray[returnFd].parent = getDirByPath(pathBeforeLastSlash);

	// error handle and avaliable space check
	if (fcbArray[returnFd].parent == NULL || fcbArray[returnFd].parent->dirEntryAmount > MAX_AMOUNT_OF_ENTRIES)
	{ // avoiding memory leak
		fcbArray[returnFd].linuxFd = -1;
		free(pathBeforeLastSlash);
		free(trueFileName);
		pathBeforeLastSlash = NULL;
		trueFileName = NULL;
		return -1;
	}

	// free the unused buffer
	free(pathBeforeLastSlash);
	pathBeforeLastSlash = NULL;

	// find the first avaliable space
	for (int i = 2; i < MAX_AMOUNT_OF_ENTRIES; i++)
	{
		if (fcbArray[returnFd].parent->entryList[i].space == SPACE_FREE)
		{
			fcbArray[returnFd].entryIndex = i;

			// copy the string into the item info
			fcbArray[returnFd].parent->entryList[i].d_reclen = sizeof(struct fs_diriteminfo);
			fcbArray[returnFd].parent->entryList[i].fileType = TYPE_FILE;
			fcbArray[returnFd].parent->entryList[i].space = SPACE_USED;

			// truncate the name if it exceeds the max length
			// slightly different than the directory one
			if (strlen(trueFileName) > (MAX_NAME_LENGTH - 1))
			{
				char *shortName = malloc(MAX_NAME_LENGTH);
				if (shortName == NULL)
				{
					eprintf("malloc() on shortName");
					return -1;
				}

				// make sure it only contains one less than the max for null terminator
				strncpy(shortName, trueFileName, MAX_NAME_LENGTH - 1);
				shortName[MAX_NAME_LENGTH - 1] = '\0';

				free(trueFileName);
				trueFileName = shortName;
			}
			strcpy(fcbArray[returnFd].parent->entryList[i].d_name, trueFileName);
			break;
		}
	}

	// free the unused buffer
	free(trueFileName);
	trueFileName = NULL;

	// allocate our buffer
	fcbArray[returnFd].buf = malloc(BUFSIZE);
	if (fcbArray[returnFd].buf == NULL)
	{
		eprintf("malloc() on fcbArray[returnFd].buf");
		fcbArray[returnFd].linuxFd = -1;
		return -1;
	}

	// have not read anything yet
	fcbArray[returnFd].buflen = BUFSIZE;
	fcbArray[returnFd].index = 0;
	fcbArray[returnFd].detector = 0;
	return (returnFd); // all set
}

// Filling the callers request is broken into three parts
// Part 1 is what can be filled from the current buffer, which may or may not be enough
// Part 2 is after using what was left in our buffer there is still 1 or more block
//        size chunks needed to fill the callers request.  This represents the number of
//        bytes in multiples of the blocksize.
// Part 3 is a value less than blocksize which is what remains to copy to the callers buffer
//        after fulfilling part 1 and part 2.  This would always be filled from a refill
//        of our buffer.
//  +-------------+------------------------------------------------+--------+
//  |             |                                                |        |
//  | filled from |  filled direct in multiples of the block size  | filled |
//  | existing    |                                                | from   |
//  | buffer      |                                                |refilled|
//  |             |                                                | buffer |
//  |             |                                                |        |
//  | Part1       |  Part 2                                        | Part3  |
//  +-------------+------------------------------------------------+--------+

// Interface to read a buffer
// int b_read(int fd, char *buffer, int count)
// {
// 	int bytesRead;			 // for our reads
// 	int bytesReturned;		 // what we will return
// 	int part1, part2, part3; // holds the three potential copy lengths

// 	if (startup == 0)
// 		b_init(); //Initialize our system

// 	// check that fd is between 0 and (MAXFCBS-1)
// 	if ((fd < 0) || (fd >= MAXFCBS))
// 	{
// 		return (-1); //invalid file descriptor
// 	}

// 	if (fcbArray[fd].linuxFd == -1) //File not open for this descriptor
// 	{
// 		return -1;
// 	}

// 	// number of bytes available to copy from buffer
// 	int remain = fcbArray[fd].buflen - fcbArray[fd].index;
// 	part3 = 0;			 //only used if count > BUFSIZE
// 	if (remain >= count) //we have enough in buffer
// 	{
// 		part1 = count; // completely buffered
// 		part2 = 0;
// 	}
// 	else
// 	{
// 		part1 = remain; //spanning buffer (or first read)
// 		part2 = count - remain;
// 	}

// 	if (part1 > 0) // memcpy part 1
// 	{
// 		memcpy(buffer, fcbArray[fd].buf + fcbArray[fd].index, part1);
// 		fcbArray[fd].index = fcbArray[fd].index + part1;
// 	}

// 	if (part2 > 0) //We need to read to copy more bytes to user
// 	{
// 		// Handle special case where user is asking for more than a buffer worth
// 		if (part2 > BUFSIZE)
// 		{
// 			int blocks = part2 / BUFSIZE; // calculate number of blocks they want
// 			bytesRead = read(fcbArray[fd].linuxFd, buffer + part1, blocks * BUFSIZE);
// 			part3 = bytesRead;
// 			part2 = part2 - part3; //part 2 is now < BUFSIZE, or file is exhausted
// 		}

// 		//try to read BUFSIZE bytes into our buffer
// 		bytesRead = read(fcbArray[fd].linuxFd, fcbArray[fd].buf, BUFSIZE);

// 		// error handling here...  if read fails

// 		fcbArray[fd].index = 0;
// 		fcbArray[fd].buflen = bytesRead; //how many bytes are actually in buffer

// 		if (bytesRead < part2) // not even enough left to satisfy read
// 			part2 = bytesRead;

// 		if (part2 > 0) // memcpy bytesRead
// 		{
// 			memcpy(buffer + part1 + part3, fcbArray[fd].buf + fcbArray[fd].index, part2);
// 			fcbArray[fd].index = fcbArray[fd].index + part2;
// 		}
// 	}
// 	bytesReturned = part1 + part2 + part3;
// 	return (bytesReturned);
// }

// Interface to write a buffer
int b_write(int fd, char *buffer, int count)
{
	if (startup == 0)
		b_init(); //Initialize our system

	// check that fd is between 0 and (MAXFCBS-1)
	if ((fd < 0) || (fd >= MAXFCBS))
	{
		return (-1); //invalid file descriptor
	}

	// file not open for this descriptor
	if (fcbArray[fd].linuxFd == -1)
	{
		return -1;
	}

	// initialize the detector
	if (fcbArray[fd].detector == 0)
	{
		fcbArray[fd].detector = FUNC_WRITE;
	}

	// it shouldn't do another functionalities
	if (fcbArray[fd].detector != FUNC_WRITE)
	{
		eprintf("no mix use of functionalities!");
		return -1;
	}

	// if it just reaches EOF, we skip the copy process
	// this is a rare case for file with mutiples of the buffer sizes
	if (count != 0)
	{
		// calculate the index to determine realloc()
		uint64_t newIndex = fcbArray[fd].index + count;
		if (newIndex > fcbArray[fd].buflen)
		{
			// realloc() with the new length
			fcbArray[fd].buflen += BUFSIZE;
			fcbArray[fd].buf = realloc(fcbArray[fd].buf, fcbArray[fd].buflen);
			if (fcbArray[fd].buf == NULL)
			{
				eprintf("realloc() on fcbArray[fd].buf");
				return -1;
			}
		}

		// copy into our buffer and set the new index
		memcpy(fcbArray[fd].buf + fcbArray[fd].index, buffer, count);
		fcbArray[fd].index = newIndex;
	}
	return 0;
}

//moves offset based on whence and returns the resulting offset
// int b_seek(int fd, off_t offset, int whence)
// {
// 	//set num to return based on offset and whence
// 	int num;
// 	if (whence == SEEK_SET)
// 	{
// 		num = offset;
// 	}
// 	else if (whence == SEEK_CUR)
// 	{
// 		num = offset + ((fcbArray[fd].block - 1) * BUFSIZE);
// 	}
// 	else if (whence == SEEK_END)
// 	{
// 		num = offset + fcbArray[fd].file->sizeInBytes;
// 	}

// 	//move the file offset
// 	int block = num / BUFSIZE;
// 	int off = num % BUFSIZE;
// 	int index = fcbArray[fd].file->directBlockPointers[block - 1];

// 	LBAread(fcbArray[fd].buf, 1, index);
// 	fcbArray[fd].offset = off;

// 	return num;
// }

// Interface to Close the file
// we decide to do the allocation here since it is easy to check the condition
void b_close(int fd)
{
	if (fcbArray[fd].detector == FUNC_WRITE)
	{
		// set the size as the index
		// no need to add 1 because it is the next index not the last
		fcbArray[fd].parent->entryList[fcbArray[fd].entryIndex].size = fcbArray[fd].index;

		// allocate the space in memory and use it for LBAwrite()
		uint blockCount = getBlockCount(fcbArray[fd].index);
		uint start = allocateFreespace(blockCount);
		if (start == -1)
		{ // stop allocating and mark the entry as free
			fcbArray[fd].parent->entryList[fcbArray[fd].entryIndex].space = SPACE_FREE;

			// avoid memory leaking
			fcbArray[fd].linuxFd = -1;
			free(fcbArray[fd].buf);
			fcbArray[fd].buf = NULL;
		}

		// write the file into the volume and set start location for the entry
		fcbArray[fd].parent->entryList[fcbArray[fd].entryIndex].entryStartLocation = start;
		LBAwrite(fcbArray[fd].buf, blockCount, start);

		// update the directory 
		updateDirectory(fcbArray[fd].parent);
	}
	else if (fcbArray[fd].detector == FUNC_READ)
	{
	}
	else
	{
		eprintf("unexpected detector");
		return;
	}

	free(fcbArray[fd].buf);	   // free the associated buffer
	fcbArray[fd].buf = NULL;   // Safety First
	fcbArray[fd].linuxFd = -1; // return this FCB to list of available FCB's
}