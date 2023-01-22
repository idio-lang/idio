#define _GNU_SOURCE
#include <fcntl.h>
#include <sys/stat.h>

int main (int argc, char **argv)
{
    int dirfd = 0;
    char *pathname = ".";
    int mode = S_IRUSR;
    int flags = 0;
    fchmodat (dirfd, pathname, mode, flags);
    return 0;
}
