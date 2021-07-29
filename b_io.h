/**************************************************************
* Class:  CSC-415-02 Summer 2021
* Name: Team Fiore

Haoyuan Tan(Sunny), 918274583, CiYuan53
Minseon Park, 917199574, minseon-park
Yong Chi, 920771004, ychi1
Siqi Guo, 918209895, Guo-1999

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
