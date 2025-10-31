#define _GNU_SOURCE
#include <sys/time.h>

int main (int argc, char **argv)
{
    struct timeval times[2] = { {0, 0}, {0, 0} };
    futimes (0, times);
    return 0;
}
