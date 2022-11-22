#define _GNU_SOURCE
#include <fcntl.h>
#include <unistd.h>

/*
 * PATH_MAX is in one or the other
 */
#include <sys/param.h>
#include <limits.h>

int main (int argc, char **argv)
{
    int dirfd = 0;
    char *pathname = ".";
    char buf[PATH_MAX];
    readlinkat (dirfd, pathname, buf, PATH_MAX);
    return 0;
}
