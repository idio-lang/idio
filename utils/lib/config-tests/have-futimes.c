#define _GNU_SOURCE
#include <sys/time.h>

int main (int argc, char **argv)
{
    char *filename = ".";
    struct timeval times[2] = { {0, 0}, {0, 0} };
    futimes (filename, times);
    return 0;
}
