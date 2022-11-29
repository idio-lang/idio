/*
 * This file is testing the presence of the header file rather than,
 * necessarily, the presence of the zlib library.
 */

#define _GNU_SOURCE
#include <zlib.h>

int main (int argc, char **argv)
{
    int level;
    z_stream strm;
    int ret = deflateInit (&strm, level);
    return 0;
}
