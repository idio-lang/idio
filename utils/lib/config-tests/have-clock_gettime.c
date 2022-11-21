#define _GNU_SOURCE
#include <time.h>

int main (int argc, char **argv)
{
    struct timespec ts;
    clock_gettime (CLOCK_MONOTONIC, &ts);
    return 0;
}
