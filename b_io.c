#include <stdlib.h> // for malloc
#include <string.h> // for memcpy
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "b_io.h"
#include "mfs.h"
#include <pthread.h>

#define MAXFCBS 20
#define B_CHUNK_SIZE 512
#define FUNC_READ 1
#define FUNC_WRITE 2

// static mutex for only b_io to avoid race condition
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct b_fcb
{
	int fd;					 // holds the systems file descriptor
	char *buf;				 // holds the open file buffer
	uint64_t index;			 // holds current index of the buffer
	uint64_t buflen;		 // holds how many valid bytes are in the buffer
	fdDir *parent;			 // holds the parent directory of the file
	char *trueFileName;		 // holds the true file name not the path
	unsigned short detector; // holds the functionality of the method
} b_fcb;

b_fcb fcbArray[MAXFCBS];

int startup = 0; //Indicates that this has not been initialized

/**
 * @brief initializa our io of file system
 * 
 */
void b_init()
{
	//init fcbArray to all free
	for (int i = 0; i < MAXFCBS; i++)
	{
		fcbArray[i].fd = -1; //indicates a free fcbArray
	}

	startup = 1;
}

/**
 * @brief get a file descriptor for an opened file
 * 
 * @return 0-MAXFCBS. -1 for no avaliable fd 
 */
int b_getFCB()
{
	for (int i = 0; i < MAXFCBS; i++)
	{
		if (fcbArray[i].fd == -1)
		{
			fcbArray[i].fd = -2; // used but not assigned
			return i;			 //Not thread safe
		}
	}
	return (-1); //all in use
}

/**
 * @brief open the file and set up parent and file name
 * 
 * @param path the whole path to the file
 * @param flags not used, we rather use a detector for default
 * @return 0-MAXFCBS for a fd, -1 for fail
 */
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
	if (fcbArray[returnFd].fd != -2)
	{ // unexpected error
		eprintf("b_getFCB()");
		return -1;
	}
	fcbArray[returnFd].fd = returnFd; // Save the linux file descriptor

	// unlock the mutex and handle error
	if (pthread_mutex_unlock(&mutex) < 0)
	{
		eprintf("\nMutex failed to unlock!");
		return -1;
	}

	// NOTE: we assume the destination may hold path
	// so we will do something like fs_mkdir()
	char *pathBeforeLastSlash = malloc(strlen(path) + 1);
	if (pathBeforeLastSlash == NULL)
	{
		eprintf("malloc() on pathBeforeLastSlash");
		return -1;
	}
	strcpy(pathBeforeLastSlash, path);
	fcbArray[returnFd].trueFileName = getPathByLastSlash(pathBeforeLastSlash);

	// stop if there is no given file name
	if (strcmp(fcbArray[returnFd].trueFileName, "") == 0)
	{ // avoiding memory leak
		printf("\nname should be given!!!\n");
		fcbArray[returnFd].fd = -1;
		free(pathBeforeLastSlash);
		pathBeforeLastSlash = NULL;
		return -1;
	}

	// find the directory that is going to store the file
	fcbArray[returnFd].parent = getDirByPath(pathBeforeLastSlash);

	// error handle and avaliable space check
	if (fcbArray[returnFd].parent == NULL)
	{ // avoiding memory leak
		fcbArray[returnFd].fd = -1;
		free(pathBeforeLastSlash);
		pathBeforeLastSlash = NULL;
		return -1;
	}

	// free the unused buffer
	free(pathBeforeLastSlash);
	pathBeforeLastSlash = NULL;

	// allocate our buffer later because b_read() and b_write() has different situation
	// have not read anything yet
	fcbArray[returnFd].buflen = 0;
	fcbArray[returnFd].index = 0;
	fcbArray[returnFd].detector = 0;
	return (returnFd); // all set
}

// we chose to rewrite b_read() so it fits with fsshell.c
// we could use the template to read all bytes into buffer
// but this is not consistent with the lixux read()
// so we only read one block each call which is the same as linex read()
/**
 * @brief read our buffer into the passed in buffer by given block size
 * 
 * @param argfd fd of the file to retrieve data
 * @param buffer buffer to copy data to
 * @param count size of the given buffer
 * @return 0-count for read bytes count, -1 for fail 
 */
int b_read(int argfd, char *buffer, int count)
{
	if (startup == 0)
		b_init(); //Initialize our system

	if ((argfd < 0) || (argfd >= MAXFCBS) || fcbArray[argfd].fd == -1 || count < 0)
	{
		return (-1);
	}

	// initialize the detector the first time it calls this function
	// and find the file and records its data and size for reading
	if (fcbArray[argfd].detector == 0)
	{
		fcbArray[argfd].detector = FUNC_READ;
		int i = 0;
		for (; i < MAX_AMOUNT_OF_ENTRIES; i++)
		{
			if (fcbArray[argfd].parent->entryList[i].space == SPACE_USED &&
				fcbArray[argfd].parent->entryList[i].fileType == TYPE_FILE &&
				strcmp(fcbArray[argfd].parent->entryList[i].d_name, fcbArray[argfd].trueFileName) == 0)
			{
				// since we are giving a buffer, the size will be buflen
				fcbArray[argfd].buflen = fcbArray[argfd].parent->entryList[i].size;

				// NOTE: this can easily cause problem if buffer size is different
				// must keep LBAread(), vcb, and b_io's buffer size the same
				// getBlockCount() depends on vcb's buffer size
				uint blockCount = getBlockCount(fcbArray[argfd].buflen);
				fcbArray[argfd].buf = malloc(blockCount * B_CHUNK_SIZE);
				if (fcbArray[argfd].buf == NULL)
				{
					eprintf("malloc() on fcbArray[argfd].buf");
					fcbArray[argfd].fd = -1;
					return -1;
				}

				// reading the data into the buffer for outside to read
				LBAread(fcbArray[argfd].buf, blockCount, fcbArray[argfd].parent->entryList[i].entryStartLocation);
				break;
			}
		}

		// handle error of not find files
		if (i == MAX_AMOUNT_OF_ENTRIES)
		{
			printf("\n%s is not existed in volume\n", fcbArray[argfd].trueFileName);
			return -1;
		}
	}

	// it shouldn't do another functionality
	if (fcbArray[argfd].detector != FUNC_READ)
	{
		eprintf("no mix use of functionality!");
		return -1;
	}

	// two conditions based on the remianing bytes
	// NOTE: since it is outside buffer reading, we use the its size, which is count
	int bytesToRead = 0;
	uint64_t remainingBytes = fcbArray[argfd].buflen - fcbArray[argfd].index;
	if (remainingBytes != 0)
	{
		if (remainingBytes > count)
		{
			bytesToRead = count;
		}
		else
		{
			bytesToRead = remainingBytes;
		}
		ldprintf("bytesToRead: %d", bytesToRead);

		// copy the data using index as offset to change start location
		memcpy(buffer, fcbArray[argfd].buf + fcbArray[argfd].index, bytesToRead);
		fcbArray[argfd].index += bytesToRead;
	}
	return bytesToRead;
}

/**
 * @brief wirte the data from the passed in buffer into our buffer
 * 
 * @param argfd fd of the file to store data
 * @param buffer buffer with data we want to copy in
 * @param count size of the given buffer
 * @return 0-count for written bytes count, -1 for fail 
 */
int b_write(int argfd, char *buffer, int count)
{
	if (startup == 0)
		b_init(); //Initialize our system

	if ((argfd < 0) || (argfd >= MAXFCBS) || fcbArray[argfd].fd == -1 || count < 0)
	{
		return (-1);
	}

	// initialize the detector the first time it calls this function
	if (fcbArray[argfd].detector == 0)
	{
		fcbArray[argfd].detector = FUNC_WRITE;

		// check if there is no more place to store files in parent directory
		if (fcbArray[argfd].parent->dirEntryAmount >= MAX_AMOUNT_OF_ENTRIES)
		{
			fcbArray[argfd].fd = -1;
			return -1;
		}

		// check if there is already a same name of file
		for (int i = 0; i < MAX_AMOUNT_OF_ENTRIES; i++)
		{
			if (fcbArray[argfd].parent->entryList[i].space == SPACE_USED &&
				strcmp(fcbArray[argfd].parent->entryList[i].d_name, fcbArray[argfd].trueFileName) == 0)
			{
				printf("\nsame name of directory or file existed\n");
				fcbArray[argfd].fd = -1;
				return -1;
			}
		}

		// allocate the buffer with the first size
		fcbArray[argfd].buf = malloc(B_CHUNK_SIZE);
		if (fcbArray[argfd].buf == NULL)
		{
			eprintf("malloc() on fcbArray[returnFd].buf");
			fcbArray[argfd].fd = -1;
			return -1;
		}
		fcbArray[argfd].buflen += B_CHUNK_SIZE;
	}

	// it shouldn't do another functionality
	if (fcbArray[argfd].detector != FUNC_WRITE)
	{
		eprintf("no mix use of functionality!");
		return -1;
	}

	// if it just reaches EOF, we skip the copy process
	// this is a rare case for file with mutiples of the buffer sizes
	if (count != 0)
	{
		// calculate the index to determine realloc()
		uint64_t newIndex = fcbArray[argfd].index + count;
		if (newIndex > fcbArray[argfd].buflen)
		{
			// realloc() with the new length
			fcbArray[argfd].buflen += B_CHUNK_SIZE;
			fcbArray[argfd].buf = realloc(fcbArray[argfd].buf, fcbArray[argfd].buflen);
			if (fcbArray[argfd].buf == NULL)
			{
				eprintf("realloc() on fcbArray[argfd].buf");
				return -1;
			}
		}

		// copy into our buffer and set the new index
		memcpy(fcbArray[argfd].buf + fcbArray[argfd].index, buffer, count);
		fcbArray[argfd].index = newIndex;
	}
	return 0;
}

/**
 * @brief close the fd and mark it free
 * 
 * @param argfd the fd of the file to be closes
 */
void b_close(int argfd)
{
	// write the buffer in if it is FUNC_WRITE
	// this is due to how we design b_write()
	if (fcbArray[argfd].detector == FUNC_WRITE &&
		fcbArray[argfd].fd != -1)
	{
		writeIntoVolume(argfd);
	}

	// free all associated malloc() pointer
	if (fcbArray[argfd].buf != NULL)
	{
		free(fcbArray[argfd].buf);
		fcbArray[argfd].buf = NULL;
	}
	if (fcbArray[argfd].parent != NULL)
	{
		free(fcbArray[argfd].parent);
		fcbArray[argfd].parent = NULL;
	}
	if (fcbArray[argfd].trueFileName != NULL)
	{
		free(fcbArray[argfd].trueFileName);
		fcbArray[argfd].trueFileName = NULL;
	}
	fcbArray[argfd].fd = -1;
}

/**
 * @brief write the buffer of the file into volume (used with b_write())
 * 
 * @param argfd fd to retrieve data
 */
void writeIntoVolume(int argfd)
{
	// find the first avaliable space and put it in
	for (int i = 2; i < MAX_AMOUNT_OF_ENTRIES; i++)
	{
		if (fcbArray[argfd].parent->entryList[i].space == SPACE_FREE)
		{
			// allocate the space in memory and use it for LBAwrite()
			uint blockCount = getBlockCount(fcbArray[argfd].index);
			uint64_t start = allocateFreespace(blockCount);
			if (start == -1)
			{ // avoid memory leaking
				eprintf("allocateFreespace() on start");
				fcbArray[argfd].fd = -1;
				free(fcbArray[argfd].buf);
				fcbArray[argfd].buf = NULL;
			}

			// write the file into the volume and set start location for the entry
			fcbArray[argfd].parent->entryList[i].entryStartLocation = start;
			LBAwrite(fcbArray[argfd].buf, blockCount, start);

			// now we need to add the info into the entry list
			fcbArray[argfd].parent->dirEntryAmount++;
			fcbArray[argfd].parent->entryList[i].d_reclen = sizeof(struct fs_diriteminfo);
			fcbArray[argfd].parent->entryList[i].fileType = TYPE_FILE;
			fcbArray[argfd].parent->entryList[i].space = SPACE_USED;
			fcbArray[argfd].parent->entryList[i].size = fcbArray[argfd].index;

			// truncate the name if it exceeds the max length
			// slightly different than the directory one
			if (strlen(fcbArray[argfd].trueFileName) > (MAX_NAME_LENGTH - 1))
			{
				char *shortName = malloc(MAX_NAME_LENGTH);
				if (shortName == NULL)
				{ // unexpected error
					eprintf("malloc() on shortName");
					return;
				}

				// make sure it only contains one less than the max for null terminator
				strncpy(shortName, fcbArray[argfd].trueFileName, MAX_NAME_LENGTH - 1);
				shortName[MAX_NAME_LENGTH - 1] = '\0';

				free(fcbArray[argfd].trueFileName);
				fcbArray[argfd].trueFileName = shortName;
			}
			strcpy(fcbArray[argfd].parent->entryList[i].d_name, fcbArray[argfd].trueFileName);

			// update the directory
			updateDirectory(fcbArray[argfd].parent);
			break;
		}
	}
}
