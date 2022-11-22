#define _GNU_SOURCE
#include <unistd.h>

int main (int argc, char **argv)
{
    char *target = ".";
    int newdirfd = 0;
    char *linkpath = ".";
    symlinkat (target, newdirfd, linkpath);
    return 0;
}
