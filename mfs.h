/**************************************************************
* Class:  CSC-415-02 Summer 2021
* Name: Team Fiore
* Student ID:




* File: mfs.h
*
* Description: 
*	This is the file system interface.
*	This is the interface needed by the driver to interact with
*	your filesystem.
*
**************************************************************/
#ifndef _MFS_H
#define _MFS_H
#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "b_io.h"
#include "fsLow.h"

#ifndef uint64_t
typedef u_int64_t uint64_t;
#endif
#ifndef uint32_t
typedef u_int32_t uint32_t;
#endif

#define FS_DEBUG	   // comment out to completely remove all debug info
#define LIMIT_FS_DEBUG // comment out to show all debug info

// comment out FS_DEBUG to print NO debug info
#ifdef FS_DEBUG
#define dprintf(fmt, args...) fprintf(stdout, "\nDEBUG: %s:%d - %s(): " fmt, \
									  __FILE__, __LINE__, __func__, ##args)
#else
#define dprintf(fmt, args...) // do nothing
#endif

// ldprintf() can hide by simpling adding "l" to original dprintf
// comment out LIMIT_FS_DEBUG to print ALL debug info
#ifdef LIMIT_FS_DEBUG
#define ldprintf(fmt, args...) // do nothing
#else
#define ldprintf(fmt, args...) fprintf(stdout, "\nDEBUG: %s:%d - %s(): " fmt, \
									   __FILE__, __LINE__, __func__, ##args)
#endif

// print error which shows unexpected error
// all check with eprintf() normally get a NULL, so we can skip free()
#define eprintf(fmt, args...) fprintf(stderr, "\nERROR: %s:%d - %s(): " fmt, \
									  __FILE__, __LINE__, __func__, ##args)


// used with bitmap
#define SPACE_FREE 0
#define SPACE_USED 1
#define BIT_SIZE_OF_INT (sizeof(int) * 8)

#define TYPE_DIR 0
#define TYPE_FILE 1
#define MAX_NAME_LENGTH 256
struct fs_diriteminfo
{
	unsigned short d_reclen; /* length of this record */
	unsigned char fileType;
	unsigned char space;		  // determine this entry is free or used
	uint64_t entryStartLocation;  // LBA of the entry, either a file or directory
	uint64_t size;				  // the exact size of the file occupies
	char d_name[MAX_NAME_LENGTH]; /* filename max filename is 255 characters */
};

#define MAX_AMOUNT_OF_ENTRIES 8
typedef struct
{
	unsigned short d_reclen;		 /*length of this record */
	uint64_t directoryStartLocation; /*Starting LBA of directory */
	unsigned short dirEntryAmount;	 // amount of undeleted entries
	char dirName[MAX_NAME_LENGTH];	 // name of this directory
	struct fs_diriteminfo entryList[MAX_AMOUNT_OF_ENTRIES];
	// unsigned short dirEntryPosition; // we keep it as global value
} fdDir;

typedef struct
{
	uint64_t magicNumber;
	uint64_t blockSize;
	uint64_t numberOfBlocks;	  // also represents bitmap length
	uint vcbBlockCount;			  // also represents freespace start location
	uint freespaceBlockCount;	  // used for check when it is not the first run
	uint64_t firstFreeBlockIndex; // used for check when it is not the first run
	uint64_t rootDirLocation;	  // can be calculated by adding the other two counts
} vcb;

// vcb and freespace related function
fdDir *createDirectory(struct fs_diriteminfo *, char *);
uint64_t allocateFreespace(uint64_t requestedBlock);
int updateOurVCB();
int updateFreespace();
int updateDirectory(fdDir *);
int updateByLBAwrite(void *, uint64_t, uint);
uint getBlockCount(uint64_t);
fdDir *getDirByPath(char *);
char *getPathByLastSlash(char *);
fdDir *getDirByEntry(struct fs_diriteminfo *);
fdDir *getRoot();
int releaseFreespace(uint64_t, uint64_t);

// global values to keep track on our file system
vcb *ourVCB;
int *freespace;
fdDir *fsCWD;
fdDir *openedDir;
uint64_t openedDirEntryIndex;

int fs_mkdir(const char *pathname, mode_t mode);
int fs_rmdir(const char *pathname);
fdDir *fs_opendir(const char *name);
struct fs_diriteminfo *fs_readdir(fdDir *dirp);
int fs_closedir(fdDir *dirp);

char *fs_getcwd(char *buf, size_t size);
int fs_setcwd(char *buf);	   //linux chdir
int fs_isFile(char *path);	   //return 1 if file, 0 otherwise
int fs_isDir(char *path);	   //return 1 if directory, 0 otherwise
int fs_delete(char *filename); //removes a file

struct fs_stat
{
	off_t st_size;		  /* total size, in bytes */
	blksize_t st_blksize; /* blocksize for file system I/O */
	blkcnt_t st_blocks;	  /* number of 512B blocks allocated */
	time_t st_accesstime; /* time of last access */
	time_t st_modtime;	  /* time of last modification */
	time_t st_createtime; /* time of last status change */
};

int fs_stat(const char *path, struct fs_stat *buf);

#endif