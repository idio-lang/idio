#define _GNU_SOURCE
#include <unistd.h>

int main (int argc, char **argv)
{
    int olddirfd = 0;
    char *oldpath = ".";
    int newdirfd = 0;
    char *newpath = ".";
    int flags = 0;
    linkat (olddirfd, oldpath, newdirfd, newpath, flags);
    return 0;
}
