#define _GNU_SOURCE
#include <fcntl.h>
#include <unistd.h>

int main (int argc, char **argv)
{
    int dirfd = 0;
    char *pathname = ".";
    int mode = 0;
    int flags = AT_EACCESS;
    faccessat (dirfd, pathname, mode, flags);
    return 0;
}
