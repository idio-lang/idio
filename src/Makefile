OS=$(shell uname -s)
ARCH=$(shell uname -m)

ifeq ($(OS),Darwin)
LIBFFI_INCDIR	= /usr/include/ffi
LDFLAGS		=  -rdynamic
LIBS		=-ldl
else ifeq ($(OS),Linux)
LIBFFI_INCDIR	= $(wildcard /usr/lib*/libffi-*/include)
LDFLAGS		=  -rdynamic
LIBS		=-ldl
else ifeq ($(OS),SunOS)
CC	       := gcc
LIBFFI_INCDIR	= $(wildcard /usr/local/lib/libffi-*/include)
LDFLAGS		= -L/usr/local/lib -R/usr/local/lib
LIBS=-ldl
CFLAGS		= -D__EXTENSIONS__
else ifeq ($(OS),FreeBSD)
CC	       := /usr/local/bin/gcc47
LIBFFI_INCDIR	= /usr/local/include
LDFLAGS		= -L/usr/local/lib
endif

ifndef LIBFFI_INCDIR
$(error LIBFFI_INCDIR is unset)
else
ifeq "$(LIBFFI_INCDIR)" ""
LIBFFI_INC	=
else
LIBFFI_INC	= -I$(LIBFFI_INCDIR)
endif
endif

ifdef MAKE_LEAN
CDEBUG	       := -O3 -UIDIO_DEBUG
else ifdef MAKE_DEBUG
CDEBUG	       := -DIDIO_DEBUG=3 -g
else
CDEBUG	       := -DIDIO_DEBUG -g
endif 


PROFLAGS	=
CFLAGS	       := ${CFLAGS} ${PROFLAGS} ${CDEBUG} -std=c99 -Wall -fPIC ${LIBFFI_INC}
LDFLAGS        := ${PROFLAGS} ${LDFLAGS}
LIBS	       := ${LIBS} -lffi

sources	       := $(wildcard *.c)
objects	       := $(patsubst %.c,%.o,$(sources))

ifneq ($(OS),Darwin)
DLIBS = libos.so libposix-regex.so
endif

all : tags idio
#all : idio scm1-c-structs.scm1 ${DLIBS} tags

profiled :
	$(MAKE) MAKE_LEAN=1 PROFLAGS=-pg

lean :
	$(MAKE) MAKE_LEAN=1

debug :
	$(MAKE) MAKE_DEBUG=1

include $(subst .c,.d,$(sources))

idio : $(objects)
	${CC} ${LDFLAGS} -o $@ $^ ${LIBS}

lib%.so : %.o
	${CC} -shared -o $@ $^

scm1-c-structs.scm1 : scm1-c-structs
	./scm1-c-structs > $@

clean : tidy
	rm -f *.o *.d idio scm1-c-structs *.so scm1-c-structs.scm1 tests/e TAGS

tidy :
	rm -f vgcore.* core.* core *~

tags : TAGS

TAGS : *.[ch]
	@echo TAGS: $?
	@etags *.[ch]

%.d: %.c
	$(CC) -MM -MP -MF $@ $(CFLAGS) $< -MT $*.o -MT $@

test : idio scm1-c-structs.scm1
	(cd tests; IDIO_PATH=${PWD} IDIO=${PWD}/idio ./rtest 2>e)
