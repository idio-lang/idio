/*
 * This file is testing the presence of the header file rather than,
 * necessarily, the presence of the libcurl library.
 *
 * Also note that we rely on the curl_easy_option_by_name() call (and
 * related structure) which were introduced in 7.73.0.  See
 * https://curl.se/libcurl/c/curl_easy_option_by_name.html.
 */

#define _GNU_SOURCE
#include <curl/curl.h>

#if ! CURL_AT_LEAST_VERSION (7, 73, 0)
#error libcurl is older than 7.73.0
#endif

int main (int argc, char **argv)
{
    printf ("%s\n", curl_version ());
    return 0;
}
