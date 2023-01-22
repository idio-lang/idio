#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>

int main (int argc, char **argv)
{
    int olddirfd = 0;
    char *oldpath = ".";
    int newdirfd = 0;
    char *newpath = ".";
    renameat (olddirfd, oldpath, newdirfd, newpath);
    return 0;
}
