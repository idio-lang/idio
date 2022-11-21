#define _GNU_SOURCE
#include <regex.h>

int main (int argc, char **argv)
{
    regex_t reg;
    regmatch_t matches[1];

    regexec (&reg, "", 1, &matches, REG_STARTEND);
    return 0;
}
