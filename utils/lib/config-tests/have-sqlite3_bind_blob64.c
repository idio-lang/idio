/*
 * This file is testing the presence of the header file rather than,
 * necessarily, the presence of the sqlite3 library.
 */

#define _GNU_SOURCE
#include <stdlib.h>

#include <sqlite3.h>

int main (int argc, char **argv)
{
    sqlite3_stmt *stmt = 0;
    int i = 0;
    void *C_v = 0;
    size_t blen = 0;
    int rc = sqlite3_bind_blob64 (stmt, i, C_v, blen, SQLITE_TRANSIENT);
    return 0;
}
