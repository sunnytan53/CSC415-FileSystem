/**************************************************************
* Class:  CSC-415-02 Summer 2021
* Name: Team Fiore
* Student ID:




* Project: File System Project
*
* File: b_io.h
*
* Description: Interface of basic I/O functions
*
**************************************************************/

#ifndef _B_IO_H
#define _B_IO_H
#include <fcntl.h>

int b_open(char *filename, int flags);
int b_read(int argfd, char *buffer, int count);
int b_write(int argfd, char *buffer, int count);
void b_close(int argfd);
void writeIntoVolume(int argfd);

#endif
