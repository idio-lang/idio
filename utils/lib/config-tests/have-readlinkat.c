#define _GNU_SOURCE
#include <fcntl.h>
#include <unistd.h>

/*
 * PATH_MAX
 */
#include <sys/param.h>

int main (int argc, char **argv)
{
    int dirfd = 0;
    char *pathname = ".";
    char buf[PATH_MAX];
    readlinkat (dirfd, pathname, buf, PATH_MAX);
    return 0;
}
