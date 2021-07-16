/**************************************************************
* Class:  CSC-415-02 Summer 2021
* Name: Team Fiore 
* Student ID:




* Project: Basic File System
*
* File: fsInit.c
*
* Description: Main driver for file system assignment.
*
* This file is where you will start and initialize your system
*
**************************************************************/

#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "fsLow.h"
#include "mfs.h"

// define constant values for our file system
#define BIT_SIZE_OF_INT (sizeof(int) * 8)
#define MAGIC_NUMBER "FIORE_FILESYSTEM"
#define MAGIC_NUMBER_LENGTH 16 // must manually change if MAGIC_NUMBER changes
#define SPACE_FREE 0
#define SPACE_USED 1
#define TYPE_DIR -1

// define the structure for a vcb
typedef struct
{
	char magicNumber[MAGIC_NUMBER_LENGTH];
	uint64_t blockSize;		 // must be this type for LBA function works
	uint64_t numberOfBlocks; // must be this type for LBA function works
	uint freespaceBlockCount;
	uint rootDirLoc;
} vcb;

// init related function prototype
char *setCleanBuffer(uint64_t);
void initVCB(uint64_t, uint64_t);
void initFreeSpace();
void initRootDir();
uint createDirectory(dirEntry *, char *);
uint64_t allocateFreeSpace(uint);
void updateFreeSpace();
void setBitOn();

// declare global values for the file system
vcb *ourVCB;
int *freespace;
uint64_t freespaceSize;

char *setCleanBuffer(uint64_t bufferSize)
{
	// allocate space for a whole block
	char *buffer = malloc(bufferSize);

	// clean the buffer completely
	memset(buffer, 0, bufferSize);

	return buffer;
}

int initFileSystem(uint64_t numberOfBlocks, uint64_t blockSize)
{
	printf("Initializing File System with %ld blocks with a block size of %ld\n", numberOfBlocks, blockSize);

	// prepare a clean buffer to read the data of the VCB
	// must preapare a whole block because LBARead() reads a whole block
	char *readBuffer = setCleanBuffer(blockSize);

	// read the FIRST block of our sample volume into the buffer
	// TODO error handle
	LBAread(readBuffer, 1, 0);

	// allocate space for our VCB and its pointer
	ourVCB = malloc(sizeof(vcb));

	// copy the needed amount of data into our VCB
	memcpy(ourVCB, readBuffer, sizeof(vcb));

	// free the unused buffer
	free(readBuffer);
	readBuffer = NULL;

	if (memcmp(ourVCB->magicNumber, MAGIC_NUMBER, MAGIC_NUMBER_LENGTH) == 0)
	{
		// allocate free space and root dir if existed
	}
	else
	{
		// initialize the VCB and other things if it is not initialized
		//printf("***MAGIC NUMBER IS BAD!: %s\n", ourVCB->magicNumber);
	}

	// Test running
	initVCB(numberOfBlocks, blockSize);
	initFreeSpace();
	initRootDir();

	return 0;
}

void exitFileSystem()
{
	// TODO close all
	printf("System exiting\n");
}

void initVCB(uint64_t numberOfBlocks, uint64_t blockSize)
{
	// initialize the default values of VCB for our file system
	memcpy(ourVCB->magicNumber, MAGIC_NUMBER, MAGIC_NUMBER_LENGTH);
	ourVCB->numberOfBlocks = numberOfBlocks;
	ourVCB->blockSize = blockSize;

	// each block needs one bit to indicate the status
	// so we divide it by 8 to get how many bytes we need
	// then we divide it by block size to get how many blocks we need
	uint block = numberOfBlocks / 8 / blockSize;

	// round up if needed
	if (numberOfBlocks % blockSize > 0)
	{
		block++;
	}
	ourVCB->freespaceBlockCount = block;

	/* TEST ourVCB */
	printf("*** VCB STATUS ***\n");
	printf("Number of Blocks: %ld\n", ourVCB->numberOfBlocks);
	printf("Block Size: %ld\n", ourVCB->blockSize);
	printf("Freespace Block Count: %d\n", ourVCB->freespaceBlockCount);

	// prepare a clean buffer to write the data of the VCB
	// must prepare a whole block because LBAWrite() writes a whole block
	char *writeBuffer = setCleanBuffer(blockSize);

	// copy the values of VCB into the buffer
	memcpy(writeBuffer, ourVCB, sizeof(vcb));

	// write the VCB into the sample volume to ensure it is physically initialized
	// TODO error handle
	LBAwrite(writeBuffer, 1, 0);
}

void initFreeSpace()
{
	freespaceSize = ourVCB->freespaceBlockCount * ourVCB->blockSize;

	// initialize the bitmap array
	freespace = malloc(freespaceSize * sizeof(int));

	// reset all bytes into 0 to repesent free
	// this is because 0 is the same as all 0 in the bit filed
	for (int i = 0; i < freespaceSize; i++)
	{
		freespace[i] = SPACE_FREE;
	}

	// set used block for this bitmap and VCB
	for (int i = 0; i < ourVCB->freespaceBlockCount + 1; i++)
	{
		freespace[0] |= (SPACE_USED << i);
	}
	updateFreeSpace();
}

// changing the bit to used status
void setBitOn(uint64_t indexOfBit)
{
	// TODO check bit is already 1
	uint targetByte = indexOfBit / BIT_SIZE_OF_INT;
	uint targetBit = indexOfBit % BIT_SIZE_OF_INT;
	freespace[targetByte] |= (SPACE_USED << targetBit);
}

// write the new freespace array back to the volume
void updateFreeSpace()
{
	char *writeBuffer = setCleanBuffer(freespaceSize);
	memcpy(writeBuffer, freespace, freespaceSize);
	LBAwrite(writeBuffer, ourVCB->freespaceBlockCount, 1);
}

uint64_t allocateFreeSpace(uint requestedSpace)
{
	// round up the exact block we need
	uint requestedBlockCount = requestedSpace / ourVCB->blockSize;
	if (requestedSpace % ourVCB->blockSize > 0)
	{
		requestedBlockCount++;
	}

	uint count = 0;
	for (uint64_t i = 0; i < ourVCB->numberOfBlocks; i++)
	{
		// bitmap function: needs to be moved into a new file
		// round down to get the exact byte it is on
		uint targetByte = i / BIT_SIZE_OF_INT;
		uint targetBit = i % BIT_SIZE_OF_INT;

		// int result = (freespace[targetByte] & (SPACE_USED << targetBit));
		// printf("\ncurrent: %d, result: %d", freespace[targetByte], result);

		if ((freespace[targetByte] & (SPACE_USED << targetBit)) == 0)
		{
			count++;
			if (count == requestedBlockCount)
			{
				for (; count > 0; count--)
				{
					setBitOn(i - count + 1);
				}
				updateFreeSpace();
				return i - requestedBlockCount + 1;
			}
		}
		else
		{
			count = 0;
		}
	}

	// not enough space or fragmentation
	return -1;
}

void initRootDir()
{
	ourVCB->rootDirLoc = createDirectory(NULL, "root");

	// write the new data of VCB
	char *writeBuffer = setCleanBuffer(ourVCB->blockSize);
	memcpy(writeBuffer, ourVCB, sizeof(vcb));
	LBAwrite(writeBuffer, 1, 0);
}

uint createDirectory(dirEntry *parent, char *name)
{
	fdDir *newDir = malloc(sizeof(fdDir));
	newDir->dirLoc = allocateFreeSpace(sizeof(fdDir));
	printf("***dirLoc: %ld\n", newDir->dirLoc);

	// initialize current directory entry .
	dirEntry *currentDirEntry = malloc(sizeof(fdDir));
	strcpy(currentDirEntry->name, name);
	currentDirEntry->entryLoc = newDir->dirLoc;
	currentDirEntry->type = TYPE_DIR;
	currentDirEntry->size = 0;

	// retrieve the current system time and assign it to new entries
	time_t currentTime = time(NULL);
	currentDirEntry->dateAccessed = currentTime;
	currentDirEntry->dateCreated = currentTime;
	currentDirEntry->dateModified = currentTime;

	// initialize parent directory entry ..
	dirEntry *parentDirEntry = malloc(sizeof(dirEntry));
	if (parent == NULL)
	{ // if parent is NULL, this is a root directory
		memcpy(parentDirEntry, currentDirEntry, sizeof(dirEntry));
	}
	else
	{ // if parent is not NULL, we copy the parent into this entry
		memcpy(parentDirEntry, parent, sizeof(dirEntry));
	}

	// place the directory entry into the directory file
	newDir->dirEntryAmount = 2;
	newDir->dirEntryList[0] = *currentDirEntry;
	newDir->dirEntryList[1] = *parentDirEntry;

	// write the directory file psycially
	LBAwrite(newDir, 5, newDir->dirLoc);

	return newDir->dirLoc;
}