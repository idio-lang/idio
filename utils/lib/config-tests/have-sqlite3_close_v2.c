/*
 * This file is testing the presence of the header file rather than,
 * necessarily, the presence of the sqlite3 library.
 */

#define _GNU_SOURCE
#include <sqlite3.h>

int main (int argc, char **argv)
{
    sqlite3 *db;
    int ret = sqlite3_close_v2 (db);
    return 0;
}
