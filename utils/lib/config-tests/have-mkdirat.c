#define _GNU_SOURCE
#include <sys/stat.h>

int main (int argc, char **argv)
{
    int dirfd = 0;
    char *pathname = ".";
    mode_t mode = 0;
    mkdirat (dirfd, pathname, mode);
    return 0;
}
