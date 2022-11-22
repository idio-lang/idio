#define _GNU_SOURCE
#include <fcntl.h>
#include <sys/stat.h>

/*
 * Mac OS 10.15.7 and later define fchownat in unistd.h
 *
 * including it won't cause a problem to any one else.
 */
#include <unistd.h>

int main (int argc, char **argv)
{
    int dirfd = 0;
    char *pathname = ".";
    uid_t owner = 0;
    gid_t group = 0;
    int flags = 0;
    fchownat (dirfd, pathname, owner, group, flags);
    return 0;
}
