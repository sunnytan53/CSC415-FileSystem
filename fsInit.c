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

#include "fsLow.h"
#include "mfs.h"
#include <math.h>

// define the structure for a vcb
typedef struct vcb
{
	char magicNumber[16];	 // can be altered later
	uint64_t numberOfBlocks; // must be this type for LBA function works
	uint64_t blockSize;		 // must be this type for LBA function works
	uint64_t feespaceLength;
} vcb_t, *vcb_p;

// init related function prototype
void initVCB(uint64_t, uint64_t);
void initFreeSpace(uint64_t, uint64_t);
void initRootDir();
char *setCleanBuffer(uint64_t);

// define global values for the file system
vcb_p ourVCB;
int *freespace;

// define a constant magic number for our file system
const char MAGIC_NUMBER[] = "FIORE_FILESYSTEM";

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
	// must preapare because LBARead() reads a whole block
	char *readBuffer = setCleanBuffer(blockSize);

	// read the FIRST block of our sample volume into the buffer
	// TODO error handle
	LBAread(readBuffer, 1, 0);

	// allocate space for our VCB and its pointer
	ourVCB = malloc(sizeof(vcb_t));

	// copy the needed amount of data into our VCB
	memcpy(ourVCB, readBuffer, sizeof(vcb_p));

	// free the unused buffer
	free(readBuffer);
	readBuffer = NULL;

	// initialize the VCB and other things if it is not initialized
	// this must be done by checking the magic number of the volume
	// this ensure the volume is the type of our file system
	if (memcmp(ourVCB->magicNumber, MAGIC_NUMBER, strlen(MAGIC_NUMBER)) != 0)
	{
		initVCB(numberOfBlocks, blockSize);
		initFreeSpace(numberOfBlocks, blockSize);
		initRootDir();
	}

	return 0;
}

void exitFileSystem()
{
	printf("System exiting\n");
}

void initVCB(uint64_t numberOfBlocks, uint64_t blockSize)
{
	// initialize the default values of VCB for our file system
	memcpy(ourVCB->magicNumber, MAGIC_NUMBER, strlen(MAGIC_NUMBER));
	ourVCB->numberOfBlocks = numberOfBlocks;
	ourVCB->blockSize = blockSize;
	ourVCB->feespaceLength = ceil(numberOfBlocks / sizeof(int));

	// prepare a clean buffer to write the data of the VCB
	// must prepare because LBAWrite() writes a whole block
	char *writeBuffer = setCleanBuffer(blockSize);

	// copy the values of VCB into the buffer
	memcpy(writeBuffer, ourVCB, sizeof(vcb_t));

	// write the VCB into the sample volume to ensure it is physically initialized`
	// TODO error handle
	LBAwrite(writeBuffer, 1, 0);
}

void initFreeSpace(uint64_t numberOfBlocks, uint64_t blockSize)
{
	int length = ourVCB->feespaceLength;

	// initialize the bitmap array
	freespace = malloc(length * sizeof(int));

	// reset all bits into 0 to repesent free
	// now the bits in each byte is 0
	for (int i = 0; i < length; i++)
	{
		freespace[i] = 0;
	}

	// mark the used block of a new volume before writing
	int bitmapBlockCount = ceil(length / sizeof(int));
	for (int i = 0; i < 1 + bitmapBlockCount; i++)
	{
		// bitmap function: needs to be moved into a new file
		int targetByte = floor(i / sizeof(int));
		int targetBit = i % sizeof(int);

		int bitNum = (int)pow(2, targetBit);
		freespace[targetByte] |= bitNum;
	}

	// write the bitmap into the blocks
	char *writeBuffer = setCleanBuffer(length);
	memcpy(writeBuffer, freespace, length);
	LBAwrite(writeBuffer, bitmapBlockCount, 1);
}

void initRootDir()
{
}
