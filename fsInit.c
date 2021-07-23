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

// must matchthe size, currently it is 8 bytes
#define MAGIC_NUMBER 0x53465F45524F4946 // stands for "FIORE_FS"

int initVCB(uint64_t, uint64_t, uint);
int initFreespace();
int initRootDir();

int initFileSystem(uint64_t numberOfBlocks, uint64_t blockSize)
{
	printf("Initializing File System with %ld blocks with a block size of %ld\n", numberOfBlocks, blockSize);

	// find how many blocks of VCB needed to read
	// can't call getBlockCount() because it needs vcb to be initialized
	uint blockCountOfVCB = sizeof(vcb) / blockSize;
	if (sizeof(vcb) % blockSize > 0)
	{
		blockCountOfVCB++;
	}

	// initialize a buffer and read from the beginning block of the volume
	char *readBuffer = malloc(blockCountOfVCB * blockSize);
	if (readBuffer == NULL)
	{
		eprintf("malloc() on readBuffer");
		return -1;
	}
	LBAread(readBuffer, blockCountOfVCB, 0);

	// allocate space for our VCB and copy the data from the buffer into ourVCB
	ourVCB = malloc(sizeof(vcb));
	if (ourVCB == NULL)
	{
		eprintf("malloc() on ourVCB");
		return -1;
	}
	memcpy(ourVCB, readBuffer, sizeof(vcb));

	free(readBuffer);
	readBuffer = NULL;

	// determine the volume is formatted as our file system by checking magic number
	if (MAGIC_NUMBER == ourVCB->magicNumber)
	{
		// read the freespace from the volume into buffer
		readBuffer = malloc(ourVCB->freespaceBlockCount * ourVCB->blockSize);
		if (readBuffer == NULL)
		{
			eprintf("malloc() on readBuffer");
			return -1;
		}
		LBAread(readBuffer, ourVCB->freespaceBlockCount, ourVCB->vcbBlockCount);

		// copy the needed amount of data into freespace
		freespace = malloc(ourVCB->numberOfBlocks);
		if (freespace == NULL)
		{
			eprintf("malloc() on freespace");
			return -1;
		}
		memcpy(freespace, readBuffer, ourVCB->numberOfBlocks);

		// free the buffer for reuse
		free(readBuffer);
		readBuffer = NULL;

		// get the root directory as cwd
		readBuffer = malloc(getBlockCount(sizeof(fdDir)) * ourVCB->blockSize);
		if (readBuffer == NULL)
		{
			eprintf("malloc() on readBuffer");
			return -1;
		}
		LBAread(readBuffer, getBlockCount(sizeof(fdDir)), ourVCB->rootDirLocation);

		// malloc() the root directory pointer and copy the data in
		fsCWD = malloc(sizeof(fdDir));
		if (fsCWD == NULL)
		{
			eprintf("malloc() on root");
			return -1;
		}
		memcpy(fsCWD, readBuffer, sizeof(fdDir));

		free(readBuffer);
		readBuffer = NULL;
	}
	else
	{
		// format the volume and handle return value
		if (initVCB(numberOfBlocks, blockSize, blockCountOfVCB) != 0)
		{
			eprintf("initVCB() failed");
			return -1;
		}
		if (initFreespace() != 0)
		{
			eprintf("initFreespace() failed");
			return -1;
		}
		if (initRootDir() != 0)
		{
			eprintf("initRootDir() failed");
			return -1;
		}

		// write vcb into the disk
		updateOurVCB();
	}

	// testing vcb status
	dprintf("*** VCB STATUS ***");
	dprintf("number of blocks: %ld", ourVCB->numberOfBlocks);
	dprintf("block size: %ld", ourVCB->blockSize);
	dprintf("vcb block count: %d", ourVCB->vcbBlockCount);
	dprintf("freespace block count: %d", ourVCB->freespaceBlockCount);
	dprintf("first free block index: %ld\n\n", ourVCB->firstFreeBlockIndex);

	// setting the other status in memory
	openedDir = NULL;
	openedDirEntryIndex = 0;

	return 0;
}

void exitFileSystem()
{
	// TODO close all
	printf("System exiting\n");
}

/**
 * @brief initialize the VCB 
 * 
 * @param numberOfBlocks amount of blocks in the volume
 * @param blockSize size (in bytes) of a block
 * @param blockCountOfVCB amount of blocks used by VCB
 * @return 0 for success
 */
int initVCB(uint64_t numberOfBlocks, uint64_t blockSize, uint blockCountOfVCB)
{
	// clean the vcb from the values copied at the beginning
	memset(ourVCB, 0, sizeof(vcb));

	// initialize the default values of VCB for our file system
	ourVCB->magicNumber = MAGIC_NUMBER;
	ourVCB->numberOfBlocks = numberOfBlocks;
	ourVCB->blockSize = blockSize;
	ourVCB->vcbBlockCount = blockCountOfVCB;
	ourVCB->firstFreeBlockIndex = 0; // this step is just for in case

	// each block needs one bit to indicate the status
	// do a round up before we get block count since it cotains a division
	uint64_t bytes = numberOfBlocks / 8;
	if (numberOfBlocks % 8 > 0)
	{
		bytes++;
	}
	ourVCB->freespaceBlockCount = getBlockCount(bytes);

	return 0;
}

/**
 * @brief initialize the freespace bitmap, by default, 
 * the freespace bitmap is right behind VCB in our file system
 * 
 * @return 0 for success, -1 for fail
 */
int initFreespace()
{
	// initialize the bitmap array with all 0s to represent free by default
	// this is because int 0 is the same as all 0 in bits
	freespace = malloc(ourVCB->numberOfBlocks);
	if (freespace == NULL)
	{
		eprintf("malloc() on freespace");
		return -1;
	}
	memset(freespace, 0, ourVCB->numberOfBlocks);

	// set current used block which is taken by VCB and this bitmap
	return allocateFreespace(ourVCB->freespaceBlockCount + ourVCB->vcbBlockCount);
}

/**
 * @brief initialize the root directory
 * 
 * @return 0 for success, -1 for fail
 */
int initRootDir()
{
	// pass in NULL to represent as root directory
	fdDir *retDir = createDirectory(NULL, "/");
	if (retDir == NULL)
	{
		eprintf("createDirectory() failed");
		return -1;
	}

	// write the directory file psycially
	updateDirectory(retDir);

	// set the root directory as cwd
	fsCWD = retDir;
	ourVCB->rootDirLocation = fsCWD->directoryStartLocation;
	return 0;
}