#define _GNU_SOURCE
#include <unistd.h>

int main (int argc, char **argv)
{
    int dirfd = 0;
    char *pathname = ".";
    int flags = 0;
    unlinkat (dirfd, pathname, flags);
    return 0;
}
