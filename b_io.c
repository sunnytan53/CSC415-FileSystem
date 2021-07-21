#include <stdlib.h>			// for malloc
#include <string.h>			// for memcpy
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "b_io.h"
#include "mfs.h"

#define MAXFCBS 20
#define BUFSIZE 512

typedef struct b_fcb
	{
    fs_dir* file;       //the opened file
    int block;          //the block index
	int linuxFd;	//holds the systems file descriptor
	char * buf;		//holds the open file buffer
	int index;		//holds the current position in the buffer
    int offset;         //current position in the buffer
	int buflen;		//holds how many valid bytes are in the buffer
	} b_fcb;
	
b_fcb fcbArray[MAXFCBS];

int startup = 0;	//Indicates that this has not been initialized

//Method to initialize our file system
void b_init ()
	{
	//init fcbArray to all free
	for (int i = 0; i < MAXFCBS; i++)
		{
		fcbArray[i].linuxFd = -1; //indicates a free fcbArray
		}
		
	startup = 1;
	}

//Method to get a free FCB element
int b_getFCB ()
	{
	for (int i = 0; i < MAXFCBS; i++)
		{
		if (fcbArray[i].linuxFd == -1)
			{
			fcbArray[i].linuxFd = -2; // used but not assigned
			return i;		//Not thread safe
			}
		}
	return (-1);  //all in use
	}
	
// Interface to open a buffered file
// Modification of interface for this assignment, flags match the Linux flags for open
// O_RDONLY, O_WRONLY, or O_RDWR
int b_open (char * filename, int flags)
	{
	int fd;
	int returnFd;
	
	//*** TODO ***:  Modify to save or set any information needed
	//
	//
	
	
	if (startup == 0) b_init();  //Initialize our system
	
	// lets try to open the file before I do too much other work
	
	fd = open (filename, flags);
	if (fd  == -1)
		return (-1);		//error opening filename
		
	//Should have a mutex here
	returnFd = b_getFCB();				// get our own file descriptor
										// check for error - all used FCB's
	fcbArray[returnFd].linuxFd = fd;	// Save the linux file descriptor
	//	release mutex
	
	//allocate our buffer
	fcbArray[returnFd].buf = malloc (BUFSIZE);
	if (fcbArray[returnFd].buf  == NULL)
		{
		// very bad, we can not allocate our buffer
		close (fd);							// close linux file
		fcbArray[returnFd].linuxFd = -1; 	//Free FCB
		return -1;
		}
		
	fcbArray[returnFd].buflen = 0; 			// have not read anything yet
	fcbArray[returnFd].index = 0;			// have not read anything yet
	return (returnFd);						// all set
	}

// Interface to read a buffer	
int b_read (int fd, char * buffer, int count)
	{
	int bytesRead;				// for our reads
	int bytesReturned;			// what we will return
	int part1, part2, part3;	// holds the three potential copy lengths
	
	if (startup == 0) b_init();  //Initialize our system

	// check that fd is between 0 and (MAXFCBS-1)
	if ((fd < 0) || (fd >= MAXFCBS))
		{
		return (-1); 					//invalid file descriptor
		}
		
	if (fcbArray[fd].linuxFd == -1)		//File not open for this descriptor
		{
		return -1;
		}	
		
	
	// number of bytes available to copy from buffer
	int remain = fcbArray[fd].buflen - fcbArray[fd].index;	
	part3 = 0;				//only used if count > BUFSIZE
	if (remain >= count)  	//we have enough in buffer
		{
		part1 = count;		// completely buffered
		part2 = 0;
		}
	else
		{
		part1 = remain;				//spanning buffer (or first read)
		part2 = count - remain;
		}
				
	if (part1 > 0)	// memcpy part 1
		{
		memcpy (buffer, fcbArray[fd].buf + fcbArray[fd].index, part1);
		fcbArray[fd].index = fcbArray[fd].index + part1;
		}
		
	if (part2 > 0)		//We need to read to copy more bytes to user
		{
		// Handle special case where user is asking for more than a buffer worth
		if (part2 > BUFSIZE)
			{
			int blocks = part2 / BUFSIZE; // calculate number of blocks they want
			bytesRead = read (fcbArray[fd].linuxFd, buffer+part1, blocks*BUFSIZE);
			part3 = bytesRead;
			part2 = part2 - part3;  //part 2 is now < BUFSIZE, or file is exhausted
			}				
		
		//try to read BUFSIZE bytes into our buffer
		bytesRead = read (fcbArray[fd].linuxFd, fcbArray[fd].buf, BUFSIZE);
		
		// error handling here...  if read fails
		
		fcbArray[fd].index = 0;
		fcbArray[fd].buflen = bytesRead; //how many bytes are actually in buffer
		
		if (bytesRead < part2) // not even enough left to satisfy read
			part2 = bytesRead;
			
		if (part2 > 0)	// memcpy bytesRead
			{
			memcpy (buffer+part1+part3, fcbArray[fd].buf + fcbArray[fd].index, part2);
			fcbArray[fd].index = fcbArray[fd].index + part2;
			}
			
		}
	bytesReturned = part1 + part2 + part3;
	return (bytesReturned);	
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

    // Interface to write a buffer	
int b_write (int fd, char * buffer, int count)
	{
	if (startup == 0) b_init();  //Initialize our system

	// check that fd is between 0 and (MAXFCBS-1)
	if ((fd < 0) || (fd >= MAXFCBS))
		{
		return (-1); 					//invalid file descriptor
		}
		
	if (fcbArray[fd].linuxFd == -1)		//File not open for this descriptor
		{
		return -1;
		}	
		
	//*** TODO ***:  Write buffered write function to accept the data and # bytes provided
	//               You must use the Linux System Calls and you must buffer the data
	//				 in 512 byte chunks and only write in 512 byte blocks.
	int part1, part2, bytesCopied;

	if (count < BUFSIZE) {
		if ((fcbArray[fd].buflen + count) <= BUFSIZE) {  // Check that our buffer + count doesn't exceed BUFSIZE
			// Copy count bytes into our buffer 
			memcpy(fcbArray[fd].buf + fcbArray[fd].index, buffer, count);
			
			// Adjust buflen and index
			fcbArray[fd].index += count;
			fcbArray[fd].buflen += count;
			
			// Set bytes copied
			bytesCopied = count;
		} else { // Our buffer is near full
			part1 = BUFSIZE - fcbArray[fd].buflen; // Amount to copy
			
			// Copy part1 bytes into our buffer
			memcpy(fcbArray[fd].buf + fcbArray[fd].index, buffer, part1);
			
			// Adjust buflen and index
			fcbArray[fd].buflen += part1; 
			fcbArray[fd].index += part1; 
			
			part2 = count - part1; // Next part to copy

			// If our buffer is full, write to file in BUFSIZE chunk
			if (fcbArray[fd].buflen == BUFSIZE) {
				write(fcbArray[fd].linuxFd, fcbArray[fd].buf, BUFSIZE);
			}

			// Copy next part into our buffer
			memcpy(fcbArray[fd].buf, buffer+part1, part2);
			
			// Adjust buflen and index
			fcbArray[fd].buflen = part2;
			fcbArray[fd].index = part2;
			
			// Set bytes copied
			bytesCopied = part1 + part2;
		}
	} else { // Count is greater than BUFSIZE
		int amountToCopy = BUFSIZE-fcbArray[fd].buflen; // First amount to copy
		
		// Copy first amount to our buffer
		memcpy(fcbArray[fd].buf + fcbArray[fd].index, buffer, amountToCopy);
		
		// Our buffer should be full, so write to file in BUFSIZE chunk
		write(fcbArray[fd].linuxFd, fcbArray[fd].buf, BUFSIZE);
		
		int leftOver1 = count - amountToCopy; // Bytes leftover after write
		int blocks = leftOver1 / BUFSIZE; // To determine next write amount
		
		// Write to file directly from caller's buffer in blocks*BUFSIZE chunk
		int bytesWritten = write(fcbArray[fd].linuxFd, buffer+amountToCopy, blocks*BUFSIZE);
		
		int leftOver2 = count - amountToCopy - bytesWritten; // Bytes leftover after second write
		
		// Copy leftover bytes into our buffer
		memcpy(fcbArray[fd].buf, buffer+(count-leftOver2), leftOver2);
		
		// Adjust buflen and index
		fcbArray[fd].buflen = fcbArray[fd].index = leftOver2;
		
		// Set bytes copied
		bytesCopied = amountToCopy + leftOver2;
	}

	return bytesCopied;

	//Remove the following line and replace with your buffered write function.
	//return (write(fcbArray[fd].linuxFd, buffer, count));
	}

//moves offset based on whence and returns the resulting offset
int b_seek(int fd, off_t offset, int whence) {
    //set num to return based on offset and whence
    int num;
    if(whence == SEEK_SET) {
        num = offset;
    } else if(whence == SEEK_CUR) {
        num = offset + ((fcbArray[fd].block - 1) * BUFSIZE);
    } else if(whence == SEEK_END) {
        num = offset + fcbArray[fd].file->sizeInBytes;
    }

    //move the file offset
    int block = num / BUFSIZE;
    int off = num % BUFSIZE;
    int index = fcbArray[fd].file->directBlockPointers[block - 1];

    LBAread(fcbArray[fd].buf, 1, index);
    fcbArray[fd].offset = off;

    return num;
}

// Interface to Close the file	
void b_close (int fd)
	{
	write(fcbArray[fd].linuxFd, fcbArray[fd].buf, fcbArray[fd].buflen); // Write last bytes left in buffer
	close (fcbArray[fd].linuxFd);		// close the linux file handle
	free (fcbArray[fd].buf);			// free the associated buffer
	fcbArray[fd].buf = NULL;			// Safety First
	fcbArray[fd].linuxFd = -1;			// return this FCB to list of available FCB's 
	}