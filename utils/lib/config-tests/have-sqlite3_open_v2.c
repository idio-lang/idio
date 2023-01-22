/*
 * This file is testing the presence of the header file rather than,
 * necessarily, the presence of the sqlite3 library.
 */

#define _GNU_SOURCE
#include <stdlib.h>

#include <sqlite3.h>

int main (int argc, char **argv)
{
    sqlite3 *db;
    int ret = sqlite3_open_v2 (":memory:", &db, SQLITE_OPEN_READONLY, NULL);
    return 0;
}
