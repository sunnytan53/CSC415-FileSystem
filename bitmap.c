#include "mfs.h"

int setUsed(uint64_t, int *bitmap);
int setBitFree(uint64_t, int *bitmap);
int checkBit(uint64_t, int *bitmap);

// keep track of values so the method can reuse them
// use static to keep them only usable in this file
uint targetIndex = 0, targetBit = 0;

/**
 * @brief load all values and check if the bit is free or used
 * 
 * @param indexOfBlock index of the block in the volume
 * @param bitmap pointer to the bitmap
 * @return 0 for free, 1 for used
 */
int checkBit(uint64_t indexOfBlock, int *bitmap)
{
	// load up the index and bit position
	targetIndex = indexOfBlock / BIT_SIZE_OF_INT;
	targetBit = indexOfBlock % BIT_SIZE_OF_INT;

	// check to see if the bit is used or free
	return (bitmap[targetIndex] & (SPACE_USED << targetBit)) != SPACE_FREE;
}

/**
 * @brief set the bit to used only if that bit is in free
 * 
 * @param indexOfBlock index of the block in the volume
 * @param bitmap pointer to the bitmap
 * @return 0 for success, -1 for fail
 */
int setBitUsed(uint64_t indexOfBlock, int *bitmap)
{
	if (checkBit(indexOfBlock, bitmap) == SPACE_USED)
	{ // error if the bit is already in used
		//eprintf("block %ld is already used!", indexOfBlock);
		return -1;
	}

	// set the bit to used
	//dprintf("bit at %ld is set to USED", indexOfBlock);
	bitmap[targetIndex] |= (SPACE_USED << targetBit);
	return 0;
}

/**
 * @brief set the bit to free only if that bit is in used
 * 
 * @param indexOfBlock index of the block in the volume
 * @param bitmap pointer to the bitmap
 * @return 0 for success, -1 for fail
 */
int setBitFree(uint64_t indexOfBlock, int *bitmap)
{
	if (checkBit(indexOfBlock, bitmap) == SPACE_FREE)
	{ // error if the bit is already in free
		//eprintf("block %ld is already FREE!", indexOfBlock);
		return -1;
	}

	// set the bit to free
	//dprintf("bit at %ld is set to FREE", indexOfBlock);
	bitmap[targetIndex] &= ~(SPACE_USED << targetBit);
	return 0;
}