#define _GNU_SOURCE
#include <stdlib.h>

int main (int argc, char **argv)
{
    char buf[512];
    ptsname_r (0, buf, 512);
    return 0;
}
