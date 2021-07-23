#include "mfs.h"
#include "bitmap.c"

// OUTPUT TERMINAL COMMAND
// Hexdump/hexdump.linux SampleVolume --count 1 --start 12

/**
 * @brief find contigous free blocks for request to mark them as used
 * and it is responsible for updating the freespace into the volume
 * 
 * @param requestedBlock amount of blocks to be occupied 
 * @return 0-∞ for starting block, -1 for fail
 */
uint64_t allocateFreespace(uint64_t requestedBlock)
{
    // handle error of invalid requestedBlock
    if (requestedBlock < 1)
    {
        eprintf("invalid arg");
        return -1;
    }

    uint64_t count = 0;

    // use the firstFreeBlockIndex to save time
    // this can save a lot of time when there are a lot of files in the volume
    for (uint64_t i = ourVCB->firstFreeBlockIndex; i < ourVCB->numberOfBlocks; i++)
    {
        if (checkBit(i, freespace) == SPACE_FREE)
        {
            count++;
            if (count == requestedBlock)
            {
                // set the bit of these contigous blocks to used
                for (uint64_t j = 0; j < count; j++)
                {
                    // handle error when setBitUsed get in errors
                    if (setBitUsed(i - j, freespace) != 0)
                    {
                        // this won't run if checkBit() works as expected
                        eprintf("setBitUsed() failed, bit at %ld", j);

                        // if current index of j gets in error, go back to the previous one
                        for (j--; j >= 0; j--)
                        {
                            setBitFree(i - j, freespace);
                        }
                        return -1;
                    }
                }

                // check if the first free block is occupied to determine if we need to update
                if (checkBit(ourVCB->firstFreeBlockIndex, freespace) == SPACE_USED)
                {
                    // if so, the ith block is the last occupied block
                    // so we start checking i+1th block to save time
                    for (uint64_t k = i + 1; k < ourVCB->numberOfBlocks; k++)
                    {
                        if (checkBit(k, freespace) == SPACE_FREE)
                        {
                            // set the new first free block index and update ourVCB
                            dprintf("first free block index changes to %ld", k);
                            ourVCB->firstFreeBlockIndex = k;
                            updateOurVCB();
                            break;
                        }
                    }
                }

                // return the starting block index of this allocated space
                updateFreespace();
                uint64_t val = i - requestedBlock + 1;
                dprintf("returning block index: %ld\n", val);
                return val;
            }
        }
        else
        {
            count = 0;
        }
    }

    // not enough space
    printf("not enough space");
    return -1;
}

/**
 * @brief psycially write ourVCB into the volume
 * 
 * @return 0 for success, -1 for fail
 */
int updateOurVCB()
{
    ldprintf("updating ourVCB\n");
    return updateByLBAwrite(ourVCB, sizeof(vcb), 0);
}

/**
 * @brief psycially write freespace bitmap into the volume
 * 
 * @return 0 for success, -1 for fail
 */
int updateFreespace()
{
    ldprintf("updating freespace\n");

    // do a round up since it needs to turn bits into bytes
    uint64_t bytes = ourVCB->numberOfBlocks / 8;
    if (ourVCB->numberOfBlocks % 8 > 0)
    {
        bytes++;
    }

    // starts right behind vcb
    return updateByLBAwrite(freespace, bytes, ourVCB->vcbBlockCount);
}

/**
 * @brief psycially write the directory into the volume
 * 
 * @return 0 for success, -1 for fail
 */
int updateDirectory(fdDir *dirp)
{
    ldprintf("updating directory %s", dirp->dirName);
    int retVal = updateByLBAwrite(dirp, dirp->d_reclen, dirp->directoryStartLocation);

    // read the data again if it is updating cwd
    if (fsCWD != NULL && dirp->directoryStartLocation == fsCWD->directoryStartLocation)
    {
        memcpy(fsCWD, dirp, sizeof(fdDir));
    }
}

/**
 * @brief base of updating volume using LBAwrite()
 * 
 * @param toWrite pointer to the element
 * @param count block count
 * @param start the beginning block
 * @return 0 for success, -1 for fail
 */
int updateByLBAwrite(void *toWrite, uint64_t size, uint start)
{
    // set up a clean buffer to copy data
    uint blockCount = getBlockCount(size);
    uint64_t fullBlockSize = blockCount * ourVCB->blockSize;
    char *writeBuffer = malloc(fullBlockSize);
    if (writeBuffer == NULL)
    {
        eprintf("malloc() on writeBuffer");
        return -1;
    }
    memset(writeBuffer, 0, fullBlockSize);

    // copy the data and then write using LBAwrite()
    memcpy(writeBuffer, toWrite, size);
    LBAwrite(writeBuffer, blockCount, start);

    ldprintf("size : %d", size);
    ldprintf("block count : %d", blockCount);
    ldprintf("start : %d", start);

    free(writeBuffer);
    writeBuffer = NULL;
    return 0;
}

/**
 * @brief round up while doing division
 * 
 * @param num number to be divided
 * @param divider number to be divided by
 * @return rounded up result
 */
uint getBlockCount(uint64_t num)
{
    int result = num / ourVCB->blockSize;
    if (num % ourVCB->blockSize > 0)
    {
        result++;
    }
    return result;
}

/**
 * @brief create a directory in the volume
 * 
 * @param parent a pointer to the parent directory entry
 * @param name name of this new directory
 * @return pointer to created directory, NULL for fail
 */
fdDir *createDirectory(struct fs_diriteminfo *parent, char *name)
{
    // malloc() a new directory and clean it up
    fdDir *newDir = malloc(sizeof(fdDir));
    if (newDir == NULL)
    {
        eprintf("malloc() on newDir");
        return NULL;
    }
    memset(newDir, 0, sizeof(fdDir));

    // initialize the directory and allocate the space for it
    uint dirBlockCount = getBlockCount(sizeof(fdDir));
    int retVal = allocateFreespace(dirBlockCount);
    if (retVal < 0)
    {
        eprintf("allocateFreespace()");
        return NULL;
    }
    newDir->directoryStartLocation = retVal;
    newDir->d_reclen = sizeof(fdDir);
    newDir->dirEntryAmount = 2;

    // truncate the name if it exceeds the max length
    if (strlen(name) > (MAX_NAME_LENGTH - 1))
    {
        char *shortName = malloc(MAX_NAME_LENGTH);
        if (shortName == NULL)
        {
            eprintf("malloc() on shortName");
            return NULL;
        }

        // make sure it only contains one less than the max for null terminator
        strncpy(shortName, name, MAX_NAME_LENGTH - 1);
        shortName[MAX_NAME_LENGTH - 1] = '\0';
        strcpy(newDir->dirName, shortName);

        free(shortName);
        shortName = NULL;
    }
    else
    {
        strcpy(newDir->dirName, name);
    }

    // initialize current directory entry .
    strcpy(newDir->entryList[0].d_name, ".");
    newDir->entryList[0].fileType = TYPE_DIR;
    newDir->entryList[0].space = SPACE_USED;
    newDir->entryList[0].entryStartLocation = retVal;
    newDir->entryList[0].d_reclen = sizeof(struct fs_diriteminfo);
    newDir->entryList[0].size = sizeof(fdDir);

    // initialize parent directory entry ..
    if (parent == NULL)
    { // if parent is NULL, this is a root directory
        memcpy(newDir->entryList + 1, newDir->entryList, sizeof(struct fs_diriteminfo));
    }
    else
    { // if parent is not NULL, we copy the parent into this entry
        memcpy(newDir->entryList + 1, parent, sizeof(struct fs_diriteminfo));
    }

    // parent directory needs to be renamed to ..
    strcpy(newDir->entryList[1].d_name, "..");

    // loop through the rest of entries to mark them as free
    // this step can be ignored since we clean newDir above and SPACE_FREE = 0
    for (int i = 2; i < MAX_AMOUNT_OF_ENTRIES; i++)
    {
        newDir->entryList[i].space = SPACE_FREE;
    }

    return newDir;
}

/**
 * @brief check if the path points to a file
 * 
 * @param path path to check
 * @return 1 for true, 0 for false, -1 for error
 */
int fs_isFile(char *path)
{
    // keep a copy of fsCWD and openedDir
    fdDir *copyOfCWD = fsCWD;
    ldprintf("copy of fsCWD: %X", fsCWD);

    // replace cwd by openedDir if a directory is open
    int dirIsOpened = 0;
    if (openedDir != NULL)
    {
        dirIsOpened = 1;
        fsCWD = openedDir;
    }

    // make a copy and substring before the last slash
    char *pathBeforeLastSlash = malloc(strlen(path) + 1);
    if (pathBeforeLastSlash == NULL)
    {
        eprintf("malloc() on pathBeforeLastSlash");
        return -1;
    }
    strcpy(pathBeforeLastSlash, path);
    char *filename = getPathByLastSlash(pathBeforeLastSlash);

    // find the directory that is expected for holding that file
    fdDir *retPtr = getDirByPath(pathBeforeLastSlash);

    int result = 0;

    // if the path is not even in a directory, then we don't need to check anymore
    if (retPtr != NULL)
    {
        // check if the item is inside this directory
        for (int i = 0; i < MAX_AMOUNT_OF_ENTRIES; i++)
        { // make sure that is a used space, a directory and name matched
            // todo seperate into three lines
            if (retPtr->entryList[i].space == SPACE_USED && retPtr->entryList[i].fileType == TYPE_FILE && strcmp(retPtr->entryList[i].d_name, filename) == 0)
            {
                result = 1;
            }
        }
    }

    // make sure we reset and also free the retPtr
    if (dirIsOpened)
    {
        fsCWD = copyOfCWD;
    }
    ldprintf("reseted fsCWD: %X", fsCWD);

    free(retPtr);
    free(pathBeforeLastSlash);
    free(filename);
    retPtr = NULL;
    pathBeforeLastSlash = NULL;
    filename = NULL;

    return result;
}

/**
 * @brief check if the path points to a directory
 * 
 * @param path path to check
 * @return 1 for true, 0 for false
 */
int fs_isDir(char *path)
{
    // keep a copy of fsCWD and openedDir
    fdDir *copyOfCWD = fsCWD;
    ldprintf("origianl fsCWD: %X", fsCWD);

    // replace cwd by openedDir if a directory is open
    int dirIsOpened = 0;
    if (openedDir != NULL)
    {
        dirIsOpened = 1;
        fsCWD = openedDir;
    }

    // getDirByPath() already checks TYPE_DIR while running
    fdDir *retPtr = getDirByPath(path);
    int result = 0;
    if (retPtr != NULL)
    {
        result = 1;
    }

    // make sure we reset and also free the retPtr
    if (dirIsOpened)
    {
        fsCWD = copyOfCWD;
        retPtr = NULL;
    }
    ldprintf("reseted fsCWD: %X", fsCWD);

    free(retPtr);
    return result;
}

/**
 * @brief get the root directory pointer
 * 
 * @return a pointer to root directory in memory
 */
fdDir *getRoot()
{
    // read the root directory into buffer
    char *readBuffer = malloc(getBlockCount(sizeof(fdDir)) * ourVCB->blockSize);
    if (readBuffer == NULL)
    {
        eprintf("malloc() on readBuffer");
        return NULL;
    }
    LBAread(readBuffer, getBlockCount(sizeof(fdDir)), ourVCB->rootDirLocation);

    // malloc() the root directory pointer and copy the data in
    fdDir *root = malloc(sizeof(fdDir));
    if (root == NULL)
    {
        eprintf("malloc() on root");
        return NULL;
    }
    memcpy(root, readBuffer, sizeof(fdDir));

    free(readBuffer);
    readBuffer = NULL;
    return root;
}

/**
 * @brief open the directory based on the path from cwd
 * 
 * @param name the absolute path
 * @return a fdDir pointer of that directory, NULL for not found or fail
 */
fdDir *fs_opendir(const char *name)
{

    // copy the name to avoid modifying it
    char *path = malloc(strlen(name) + 1);
    if (path == NULL)
    {
        eprintf("malloc()");
        return NULL;
    }
    strcpy(path, name);
    openedDir = getDirByPath(path);

    // set the entry index to 0 for fs_readDir() works
    openedDirEntryIndex = 0;

    free(path);
    path = NULL;
    return openedDir;
}

/**
 * @brief get a directory pointer from cwd
 * 
 * @param name name of the path
 * @return direcotry pointer, NULL for error or not found
 */
fdDir *getDirByPath(char *name)
{
    fdDir *getDir = malloc(sizeof(fdDir));
    if (getDir == NULL)
    {
        eprintf("malloc() on getDir");
        return NULL;
    }

    // copy the cwd recorded by the file system
    memcpy(getDir, fsCWD, sizeof(fdDir));

    // make a copy of name to avoid modifying it using strtok()
    char *copyOfName = malloc(strlen(name) + 1);
    if (copyOfName == NULL)
    {
        eprintf("malloc() on copyOfName");
        return NULL;
    }
    strcpy(copyOfName, name);

    // split the string by the delimeter
    char *token = strtok(copyOfName, "/");

    // loop through the entry list to find the directory
    while (token != NULL)
    {
        // if token is . or empty, it means current directory
        if (strcmp(token, ".") == 0 || strcmp(token, "") == 0)
        {
            // do nothing
        }
        else
        { // loop through the entry list of getDir to see if any matches
            // skip the first one since we do the case above
            int i = 1;
            for (; i < MAX_AMOUNT_OF_ENTRIES; i++)
            { // make sure that is a used space, a directory and name matched
                if (getDir->entryList[i].space == SPACE_USED && getDir->entryList[i].fileType == TYPE_DIR && strcmp(getDir->entryList[i].d_name, token) == 0)
                {
                    free(getDir);
                    getDir = getDirByEntry(getDir->entryList + i);
                    break;
                }
            }

            // if it didn't find a directory, which should fail
            if (i == MAX_AMOUNT_OF_ENTRIES)
            {
                return NULL;
            }
        }
        token = strtok(NULL, "/");
    }
    return getDir;
}

/**
 * @brief get a directory reference based on the entry
 * 
 * @param entry the entry we want to get reference as a directory
 * @return a directory pointer, NULL for fail
 */
fdDir *getDirByEntry(struct fs_diriteminfo *entry)
{
    if (entry->fileType != TYPE_DIR)
    {
        return NULL;
    }

    // preapare a buffer for reading directories using LBAread()
    uint fdDirBlockCount = getBlockCount(sizeof(fdDir));
    char *readBuffer = malloc(fdDirBlockCount * ourVCB->blockSize);
    if (readBuffer == NULL)
    {
        eprintf("malloc() on readBuffer");
        return NULL;
    }
    fdDir *retDir = malloc(sizeof(fdDir));
    if (retDir == NULL)
    {
        eprintf("malloc() on retDir");
        return NULL;
    }

    LBAread(readBuffer, fdDirBlockCount, entry->entryStartLocation);
    memcpy(retDir, readBuffer, sizeof(fdDir));

    return retDir;
}

/**
 * @brief find the name of the cwd for printing
 * 
 * @param buf a buffer to copy path
 * @param size max size of the path
 * @return a buffer pointer for success, NULL for fail
 */
char *fs_getcwd(char *buf, size_t size)
{
    // clean the buffer because it was expected malloc() only
    strcpy(buf, "");

    // malloc() a temp buffer
    char *tempBuffer = malloc(size);
    if (tempBuffer == NULL)
    {
        eprintf("malloc() on tempHolder");
        return NULL;
    }

    // make a copy to loop through the directory
    fdDir *copiedDir = malloc(sizeof(fdDir));
    if (copiedDir == NULL)
    {
        eprintf("malloc() on copiedDir");
        return NULL;
    }
    memcpy(copiedDir, fsCWD, sizeof(fdDir));

    // loops backward until we reach the root to get the full path
    while (copiedDir->directoryStartLocation != ourVCB->rootDirLocation)
    {
        strcpy(tempBuffer, "/");
        strcat(tempBuffer, copiedDir->dirName);
        strcat(tempBuffer, buf);
        strcpy(buf, tempBuffer);

        // get the parent directory pointer
        fdDir *tempPtr = getDirByEntry(copiedDir->entryList + 1);

        // free the original copy of directory and assign the new one
        free(copiedDir);
        copiedDir = tempPtr;
    }

    // if nothing is inside, just use slash to represent the root directory
    if (strcmp(buf, "") == 0)
    {
        strcpy(buf, "./");
    }
    else
    { // if there is already string, we cat the root referece to the front
        strcpy(tempBuffer, ".");
        strcat(tempBuffer, buf);
        strcpy(buf, tempBuffer);
    }

    // dprintf("cwd is %s", buf); // pwd command takes over its job!!!
    free(copiedDir);
    free(tempBuffer);
    copiedDir = NULL;
    tempBuffer = NULL;
    return buf;
}

/**
 * @brief read the directory entry list
 * 
 * @param dirp a pointer to that directory
 * @return a pointer to the first entry
 */
struct fs_diriteminfo *fs_readdir(fdDir *dirp)
{
    for (int i = openedDirEntryIndex; i < MAX_AMOUNT_OF_ENTRIES; i++)
    {
        // find the first entry and mark the index
        if (dirp->entryList[i].space == SPACE_USED)
        {
            openedDirEntryIndex = i + 1;
            return dirp->entryList + i;
        }
    }

    // return NULL and reset if no more entries
    return NULL;
}

/**
 * @brief clear the access and memory allocated on the directory
 * 
 * @param dirp a pointer to a directory to be closed
 * @return 0 for success
 */
int fs_closedir(fdDir *dirp)
{
    free(dirp);
    openedDir = NULL;
    openedDirEntryIndex = 0;
    return 0;
}

/**
 * @brief load up the status of file on opened directory
 * 
 * @param path the path to a file or directory
 * @param buf buffer to store the status
 * @return 0 for success, -1 for fail
 */
int fs_stat(const char *path, struct fs_stat *buf)
{
    // should not fail because it is using with a existed directory
    for (int i = 0; i < MAX_AMOUNT_OF_ENTRIES; i++)
    {
        if (openedDir->entryList[i].space == SPACE_USED && strcmp(openedDir->entryList[i].d_name, path) == 0)
        {
            buf->st_blksize = ourVCB->blockSize;
            buf->st_size = openedDir->entryList[i].size;
            buf->st_blocks = getBlockCount(buf->st_size);
            // todo for time managements
            return 0;
        }
    }

    // not found, but should not happen
    return -1;
}

/**
 * @brief set current cwd
 * 
 * @param buf path to the directory
 * @return 0 for success, -1 for fail
 */
int fs_setcwd(char *buf)
{
    // get the toGo directory
    fdDir *toGo = getDirByPath(buf);
    if (toGo == NULL)
    {
        return -1;
    }

    dprintf("previous fsCWD: %s", fsCWD->dirName);

    // free the original directory in memory and set it to toGo
    free(fsCWD);
    fsCWD = toGo;

    dprintf("current fsCWD: %s\n", fsCWD->dirName);
    return 0;
}

/**
 * @brief get the path before the last slash
 * 
 * @param name a copy of path (will be turned into the new path)
 * @return a char* of the left path, NULL for fail
 */
char *getPathByLastSlash(char *path)
{
    char *lastSlash = strrchr(path, '/');
    int cutIndex = lastSlash - path;
    if (lastSlash == NULL)
    {
        cutIndex = 0;
    }

    ldprintf("cutIndex: %d", cutIndex);

    // prepare the new pointer to replace and return
    char *pathBeforeLastSlash = malloc(cutIndex + 1);
    if (pathBeforeLastSlash == NULL)
    {
        eprintf("malloc() on pathBeforeLastSlash");
        return NULL;
    }
    char *leftPath = malloc(strlen(path) - cutIndex);
    if (pathBeforeLastSlash == NULL)
    {
        eprintf("malloc() on leftPath");
        return NULL;
    }

    if (lastSlash == NULL)
    {
        strcpy(pathBeforeLastSlash, ".");
        strcpy(leftPath, path);
    }
    else
    {
        strncpy(pathBeforeLastSlash, path, cutIndex);
        pathBeforeLastSlash[cutIndex] = '\0';
        strcpy(leftPath, lastSlash + 1);
    }

    // place back the path into original path buffer
    strcpy(path, pathBeforeLastSlash);

    ldprintf("path before last slash is %s", path);
    ldprintf("the left path is %s\n", leftPath);

    return leftPath;
}

/**
 * @brief make a directory in the volume based on cwd
 * 
 * @param pathname path to the new directory
 * @param mode haven't used...
 * @return 0 for success, -1 for fail
 */
int fs_mkdir(const char *pathname, mode_t mode)
{
    // make a copy and use that for substring
    char *pathBeforeLastSlash = malloc(strlen(pathname) + 1);
    if (pathBeforeLastSlash == NULL)
    {
        eprintf("malloc() on pathBeforeLastSlash");
        return -1;
    }
    strcpy(pathBeforeLastSlash, pathname);

    // substring and get the left path as new directory name
    char *newDirName = getPathByLastSlash(pathBeforeLastSlash);
    if (newDirName == NULL)
    {
        eprintf("getPathByLastSlash() failed");
        return -1;
    }

    // get the directory pointer
    fdDir *parent = getDirByPath(pathBeforeLastSlash);
    if (parent == NULL)
    {
        printf("%s is not exisited from cwd\n", pathBeforeLastSlash);

        // avoid memory leak
        free(pathBeforeLastSlash);
        free(newDirName);
        pathBeforeLastSlash = NULL;
        newDirName = NULL;
        return -1;
    }

    // create directory if the amount is not reaching the max
    int retVal = 0;
    if (parent->dirEntryAmount < MAX_AMOUNT_OF_ENTRIES)
    {
        dprintf("creating new directory %s", newDirName);

        // create the new directory
        fdDir *createdDir = createDirectory(parent->entryList, newDirName);

        // skip if it has same name with existed one
        for (int i = 0; i < MAX_AMOUNT_OF_ENTRIES; i++)
        {
            if (parent->entryList[i].space == SPACE_USED && strcmp(parent->entryList[i].d_name, newDirName) == 0)
            {
                releaseFreespace(createdDir->directoryStartLocation, getBlockCount(createdDir->d_reclen));
                printf("same directory name existed!\n");

                // avoid memory leak
                free(pathBeforeLastSlash);
                free(newDirName);
                free(parent);
                pathBeforeLastSlash = NULL;
                newDirName = NULL;
                parent = NULL;
                return -1;
            }
        }

        // find the first avaliable space and put the data in
        for (int i = 2; i < MAX_AMOUNT_OF_ENTRIES; i++)
        {
            if (parent->entryList[i].space == SPACE_FREE)
            {
                parent->dirEntryAmount++;
                parent->entryList[i].d_reclen = sizeof(struct fs_diriteminfo);
                parent->entryList[i].fileType = TYPE_DIR;
                parent->entryList[i].entryStartLocation = createdDir->directoryStartLocation;
                parent->entryList[i].space = SPACE_USED;
                parent->entryList[i].size = sizeof(fdDir);
                strcpy(parent->entryList[i].d_name, createdDir->dirName);

                // write the two changed files back into the disk
                updateDirectory(createdDir);
                updateDirectory(parent);
                break;
            }
        }
    }
    else
    {
        printf("reaches the max of entries of a directory\n");
        retVal = -1;
    }

    free(pathBeforeLastSlash);
    free(newDirName);
    free(parent);
    pathBeforeLastSlash = NULL;
    newDirName = NULL;
    parent = NULL;
    return retVal;
}

/**
 * @brief remove a directory and file
 * 
 * @param pathname path to the director and file
 * @return 0 for success, -1 for fail
 */
int fs_rmdir(const char *pathname)
{
    // no need to worry pathname will be modified
    // because getDirByPath() manipulate the copy of it
    fdDir *target = getDirByPath((char *)pathname);

    // we can't remove a unexisited directory or the root directory
    if (target == NULL)
    {
        eprintf("getDirByEntry() on target");
        return -1;
    }
    else if (target->directoryStartLocation == ourVCB->rootDirLocation)
    {
        printf("root can't be removed\n");

        // avoid memory leak
        free(target);
        target = NULL;

        return -1;
    }

    // get the parent reference to remove the directory in the parent
    fdDir *parent = getDirByEntry(target->entryList + 1);
    if (parent == NULL)
    {
        eprintf("getDirByEntry() on parent");
        return -1;
    }

    // remove directories other than . and ..
    // . links to this deleted file and .. links to the parent
    if (target->dirEntryAmount > 2)
    {
        for (int i = 2; i < MAX_AMOUNT_OF_ENTRIES; i++)
        {
            if (target->entryList[i].space == SPACE_USED)
            {
                // copy the path and cat with the entry name with slash
                char *entryPath = malloc(strlen(pathname) + strlen(target->entryList[i].d_name) + 2);
                if (entryPath == NULL)
                {
                    eprintf("malloc() on entryPath");
                    return -1;
                }
                strcpy(entryPath, pathname);
                strcat(entryPath, "/");
                strcat(entryPath, target->entryList[i].d_name);

                // either remove directory or delete file
                if (fs_isDir(entryPath))
                {
                    // fs_rmdir shouldn't fail so only check errors
                    if (fs_rmdir(entryPath) != 0)
                    {
                        eprintf("fs_rmdir()");
                        return -1;
                    }
                }
                // else if (fs_isFile(entryPath))
                // {
                // if (fs_delete(entryPath) != 0)
                // {
                //     return -1;
                // }
                // }
                else
                {
                    return -1;
                }
            }
        }
    }

    // redirect cwd to the parent if the directory is going to be deleted
    if (target->directoryStartLocation == fsCWD->directoryStartLocation)
    {
        printf("\n*** cwd is being removed, redirect to parent ***\n");
        fs_setcwd("..");
    }

    // find the entry in the parent and set it as free
    for (int i = 0; i < MAX_AMOUNT_OF_ENTRIES; i++)
    {
        if (strcmp(parent->entryList[i].d_name, target->dirName) == 0)
        {
            parent->entryList[i].space = SPACE_FREE;
            parent->dirEntryAmount--;
            updateDirectory(parent);
            break;
        }
    }

    // release the blocks occupied by the directory
    if (releaseFreespace(target->directoryStartLocation, getBlockCount(target->d_reclen)) != 0)
    {
        eprintf("releaseFreespace() falied");
        return -1;
    }

    printf("%s : %s was removed\n", pathname, target->dirName);

    // testing freespace on removing
    // printf("used block index: ");
    // for (int i = 0; i < ourVCB->numberOfBlocks; i++)
    // {
    //     if (checkBit(i, freespace) == 1) {
    //         printf("%d ", i);
    //     }
    // }
    // printf("\n");

    free(target);
    free(parent);
    target = NULL;
    parent = NULL;

    return 0;
}

/**
 * @brief release the blocks in the freespace bitmap
 * 
 * @param start the starting index
 * @param count amount of blocks
 * @return 0 for success, -1 for fail
 */
int releaseFreespace(uint64_t start, uint64_t count)
{
    // handle error of invalid count, generally not goint to happen
    if (start < (ourVCB->freespaceBlockCount + ourVCB->vcbBlockCount) || count < 1 || start + count > ourVCB->numberOfBlocks)
    {
        eprintf("invalid arg");
        return -2;
    }

    for (uint64_t i = 0; i < count; i++)
    {
        // handle error when setBitFree get in errors
        if (setBitFree(start + i, freespace) != 0)
        {
            // this won't run if checkBit() works as expected
            eprintf("setBitFree() failed, bit at %ld", start + i);

            // we should mark those back in order to recover
            for (i--; i >= 0; i--)
            {
                setBitUsed(start + i, freespace);
            }
            return -1;
        }
    }

    // simply compare if the freed block is before the freeblock index
    if (start < ourVCB->firstFreeBlockIndex)
    {
        // set the new first free block index and update ourVCB
        dprintf("first free block index changes to %ld\n", start);
        ourVCB->firstFreeBlockIndex = start;
        updateOurVCB();
    }

    // return the starting block index of this allocated space
    updateFreespace();
    return 0;
}