TOPDIR		:= $(abspath $(PWD)/..)
TOPSRCDIR	:= $(TOPDIR)/src

include $(TOPSRCDIR)/Makefile.common

# Code from an RFC from 2011 doesn't pass muster
CFLAGS		:= $(filter-out -Wall -Werror,$(CFLAGS))

test_src	:= shatest.c
lib_srcs	:= $(filter-out $(test_src),$(sources))
lib_objs	:= $(patsubst %.c,%.o,$(lib_srcs))
lib_deps	:= $(patsubst %.c,%.d,$(lib_srcs))

all : libsha.a

shatest : shatest.o libsha.a

libsha.a : libsha.a($(lib_objs))
	ranlib $@

clean :
	rm -f shatest libsha.a *.o *.d
