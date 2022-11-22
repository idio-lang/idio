#define _GNU_SOURCE
#include <fcntl.h>

int main (int argc, char **argv)
{
    int dirfd = 0;
    char *pathname = ".";
    int flags = 0;
    mode_t mode = 0;
    openat (dirfd, pathname, flags, mode);
    return 0;
}
