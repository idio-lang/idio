#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * This should be a vaguely runnable program as we need to run it on
 * WSL1 as futimesat is "Function not implemented"
 *
 * So we expect an argument, something we can futimesat()
 */

int main (int argc, char **argv)
{
    int dirfd = AT_FDCWD;

    if (argc < 2) {
	fprintf (stderr, "usage: a.out file\n");
	exit (1);
    }

    char *pathname = argv[1];
    struct stat statbuf;
    if (-1 == stat (pathname, &statbuf)) {
	perror ("stat");
	exit (1);
    }
    struct timeval times[2];
    memcpy (&times[0], &(statbuf.st_atime), sizeof (struct timeval));
    memcpy (&times[1], &(statbuf.st_mtime), sizeof (struct timeval));

    int futimesat_r = futimesat (dirfd, pathname, times);
    if (-1 == futimesat_r) {
	perror ("futimesat");
	exit (1);
    }
    return 0;
}
