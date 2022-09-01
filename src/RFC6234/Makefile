TOPDIR		:= $(abspath $(PWD)/..)

include $(TOPDIR)/src/Makefile.common

test_src	:= shatest.c
lib_srcs	:= $(filter-out $(test_src),$(wildcard *.c))
lib_objs	:= $(patsubst %.c,%.o,$(lib_srcs))
lib_deps	:= $(patsubst %.c,%.d,$(lib_srcs))

all : libsha.a

shatest : shatest.o libsha.a

libsha.a : libsha.a($(lib_objs))
	ranlib $@

clean :
	rm -f shatest libsha.a *.o *.d

%.o : %.c %.d

%.d: %.c
	$(CC) -MM -MP -MF $@ $(CFLAGS) -I$(SRCDIR) $< -MT $*.o -MT $@

