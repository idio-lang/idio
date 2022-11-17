#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>

int main (int argc, char **argv)
{
    char *memrchr_r = memrchr ("hello", 'h', 5);
    return 0;
}
