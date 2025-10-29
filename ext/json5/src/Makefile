TOPDIR		:= $(abspath $(shell pwd)/../../..)
TOPSRCDIR	:= $(TOPDIR)/src

MOD_NAME	:= json5

-include Makefile.system
include $(TOPSRCDIR)/Makefile.common
include $(TOPSRCDIR)/Makefile.system

CFLAGS		:= $(CFLAGS) -I$(TOPSRCDIR)
LIBS		:= $(LIBS)

LIBSRCS		:= $(wildcard $(MOD_NAME)-*.c) utf8.c
LIBOBJS		:= $(patsubst %.c,%.o,$(LIBSRCS))

USISRCS		:= usi.c usi-wrap.c
USIOBJS		:= $(patsubst %.c,%.o,$(USISRCS))

$(SHLIB_NAME).${MODULE_SYSTEM_VERSION_MM} : $(MOD_NAME)-api.o

include $(TOPSRCDIR)/Makefile.ext

test : usi.o test.o libjson5-bare.so
	$(CC) -o test $(LDFLAGS) $^ -ljson5-bare

libjson5-bare.so : $(LIBOBJS) $(USIOBJS)
	$(CC) $(LD_DLFLAGS) -o $@ $^

usi.c : usi.h

usi.c usi.h :
	cp $(SRCDIR)/$@ .
