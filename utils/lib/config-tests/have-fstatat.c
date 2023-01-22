#define _GNU_SOURCE
#include <sys/stat.h>

int main (int argc, char **argv)
{
    int dirfd = 0;
    char *pathname = ".";
    struct stat statbuf;
    int flags = 0;
    fstatat (dirfd, pathname, &statbuf, flags);
    return 0;
}
