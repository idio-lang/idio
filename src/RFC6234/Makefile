
test_src	:= shatest.c
lib_srcs	:= $(filter-out $(test_src),$(wildcard *.c))
lib_objs	:= $(patsubst %.c,%.o,$(lib_srcs))
lib_deps	:= $(patsubst %.c,%.d,$(lib_srcs))

# For a quiet life whilst retaining the RFC6234 source code verbatim,
# this Fedora 36 system requires:

# -D_POSIX_C_SOURCE >= 2 for getopt(3)

# -Wno-array-parameter

# and for shatest.c

# -Wno-unused-parameter

# -Wno-format-overflow

CFLAGS		:= $(CFLAGS) -std=c99 -Wall -Wextra -Wno-array-parameter -Wno-unused-parameter -Wno-format-overflow -fPIC -D_POSIX_C_SOURCE=2

all : libsha.a

shatest : shatest.o libsha.a

libsha.a : libsha.a($(lib_objs))
	ranlib $@

clean :
	rm -f shatest libsha.a *.o *.d

%.o : %.c %.d

%.d: %.c
	$(CC) -MM -MP -MF $@ $(CFLAGS) -I$(SRCDIR) $< -MT $*.o -MT $@

