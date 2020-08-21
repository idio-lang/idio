HOSTNAME	:= $(shell uname -n)
OS		:= $(shell uname -s)
ARCH		:= $(shell uname -m)
SHELL		:= bash

TOPDIR		?= $(abspath $(PWD)/..)
BINDIR		?= $(TOPDIR)/bin
LIBDIR		?= $(TOPDIR)/lib
TESTSDIR	?= $(TOPDIR)/tests
DOCDIR		?= $(TOPDIR)/doc
SRCDOCDIR	:= $(DOCDIR)/src
SRCKDOUTDIR	:= $(SRCDOCDIR)/kernel-autodoc-out

ifeq ($(OS),Darwin)

LIBFFI_INCDIR	:= /usr/include/ffi
LIBFFI_INCDIR	:= /Library/Developer/CommandLineTools/SDKs/MacOSX10.15.sdk/usr/include/ffi
LDFLAGS		:= -rdynamic
LIBS		:= -ldl

else ifeq ($(OS),Linux)

LIBFFI_INCDIR	= $(wildcard /usr/lib*/libffi-*/include)
LDFLAGS		:= -rdynamic
LIBS		:= -ldl
ifeq ($(ARCH),x86_64)
CFLAGS		:= -m64 $(CFLAGS) # -fsanitize=undefined
LDFLAGS		:= -m64 -Wl,-Map=map $(LDFLAGS)
endif

ifneq (,$(findstring centos-6,$(HOSTNAME)))
LIBS		:= -lrt $(LIBS)
endif

ifneq (,$(findstring debian-,$(HOSTNAME)))
CC		:= gcc-8
endif

ifneq (,$(findstring ubuntu-,$(HOSTNAME)))
CC		:= gcc-8
endif

else ifeq ($(OS),SunOS)

CC		:= gcc
LIBFFI_INCDIR	= $(wildcard /usr/lib/$(shell isainfo -k | grep -v i386)/libffi-*/include)
LDFLAGS		:= -L/usr/local/lib -R/usr/local/lib
LIBS		:= -ldl
CFLAGS		:= -D__EXTENSIONS__
M64		:= $(filter %64, $(shell isainfo))
ifeq ($(M64),amd64)
CFLAGS		:= -m64 $(CFLAGS)
LDFLAGS		:= -m64 $(LDFLAGS)
endif

else ifeq ($(OS),FreeBSD)

CC		:= $(wildcard /usr/local/bin/gcc4?)
LIBFFI_INCDIR	:= /usr/local/include
LDFLAGS		:= -L/usr/local/lib

endif

ifndef LIBFFI_INCDIR
$(error LIBFFI_INCDIR is unset)
else
ifeq "$(LIBFFI_INCDIR)" ""
LIBFFI_INC	:=
else
LIBFFI_INC	:= -I$(LIBFFI_INCDIR)
endif
endif

ifdef MAKE_LEAN
CDEBUG		:= -O3 -UIDIO_DEBUG -UIDIO_VM_PROF
else ifdef MAKE_DEBUG
CDEBUG		:= -DIDIO_DEBUG=3 -g -DIDIO_VM_PROF
else
CDEBUG		:= 
endif 


PROFLAGS	:=
COVFLAGS	:=
CFLAGS		:= -std=c99 -Wall -Wno-unused-function -fPIC $(CFLAGS) $(PROFLAGS) $(COVFLAGS) $(SAN_CFLAGS) $(CDEBUG) $(LIBFFI_INC)
LDFLAGS		:= $(PROFLAGS) $(COVFLAGS) $(SAN_LDFLAGS) $(LDFLAGS)
LIBS		:= -lffi $(LIBS)

sources		:= $(wildcard *.c)
objects		:= $(patsubst %.c,%.o,$(sources))

ifneq ($(OS),Darwin)
DLIBS		:= libos.so libposix-regex.so
endif

all : tags $(BINDIR)/idio
#all : idio scm1-c-structs.scm1 $(DLIBS) tags

profiled :
	$(MAKE) PROFLAGS=-pg

coverage :
	$(MAKE) COVFLAGS="-fprofile-arcs -ftest-coverage"

sanitize-address :
	$(MAKE) SAN_CFLAGS="-fsanitize=address" SAN_LDFLAGS=-lasan

lean :
	$(MAKE) MAKE_LEAN=1

debug :
	$(MAKE) MAKE_DEBUG=1

test : $(BINDIR)/idio
	$(BINDIR)/idio test

doc : 
	rm -rf $(SRCKDOUTDIR)
	(cd .. ; kernel-autodoc src $(SRCKDOUTDIR))
	#cp $(SRCKDOUTDIR)/*_[ch].rst $(SRCDOCDIR)
	diff -w <(grep _[ch] $(SRCKDOUTDIR)/index.rst) <(grep _[ch] $(SRCDOCDIR)/index.rst | sed -e 's!.*/!!')
	make -C $(SRCDOCDIR) html text

include $(subst .c,.d,$(sources))

$(BINDIR)/idio : $(objects) | $(BINDIR)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

lib%.so : %.o
	$(CC) -shared -o $@ $^

scm1-c-structs.scm1 : scm1-c-structs
	./scm1-c-structs > $@

clean : tidy
	rm -f *.o *.d $(BINDIR)/idio scm1-c-structs *.so scm1-c-structs.scm1 o e TAGS map vm-*

tidy :
	rm -f vgcore.* core.* core *~ $(LIBDIR)/*~ $(TESTSDIR)/*~ *.gcda *.gcno *.gcov gmon.out
ifeq ($(OS),Darwin)
	rm -f /cores/*
else ifeq ($(OS),FreeBSD)
	rm -f idio.core
endif

tags : TAGS

TAGS : *.[ch] $(LIBDIR)/*.idio $(TESTSDIR)/*.idio
	@echo TAGS: $?
	@etags *.[ch] --language=none --regex=@idio.tags $(LIBDIR)/*.idio $(TESTSDIR)/*.idio

%.o : %.d

%.d: %.c
	$(CC) -MM -MP -MF $@ $(CFLAGS) $< -MT $*.o -MT $@

$(BINDIR):
	@mkdir -p $(BINDIR)

valgrind: $(BINDIR)/idio
	valgrind --leak-check=full --show-leak-kinds=all --child-silent-after-fork=yes $(BINDIR)/idio # 2>&1 | less +/^==

cppcheck:
	cppcheck --force --enable=all .
