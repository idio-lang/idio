#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>

int main (int argc, char **argv)
{
    size_t strnlen_r = strnlen ("hello", 2);
    return 0;
}
