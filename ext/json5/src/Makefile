TOPDIR		:= $(abspath $(PWD)/../../..)
include $(TOPDIR)/src/Makefile.common

ifdef MAKE_DEBUG
CDEBUG		:= -g
else
CDEBUG		:=
endif

CFLAGS		:= -std=c99 -Wall -Wno-unused-function -fPIC -I$(PWD) -I$(TOPDIR)/src $(CFLAGS) $(CDEBUG) $(LIBFFI_INC)
LDFLAGS		:= -L$(PWD) $(LDFLAGS)
LIBS		:= $(LIBS)

ifeq ($(OS),Darwin)
LD_DLFLAGS	:= -dynamiclib -undefined dynamic_lookup
else
LD_DLFLAGS	:= -shared
endif

LIBSRCS		:= json5-token.c json5-parser.c utf8.c json5-unicode.c
LIBOBJS		:= $(patsubst %.c,%.o,$(LIBSRCS))

USISRCS		:= usi.c usi-wrap.c
USIOBJS		:= $(patsubst %.c,%.o,$(USISRCS))

include Makefile.system

JSON5_SYSTEM_VERSION_M	:= $(shell echo ${JSON5_SYSTEM_VERSION_MM} | sed -e 's/\..*//')

LD_SONAME_FLAGS	:=
ifeq ($(OS),Linux)
LD_SONAME_FLAGS	:= -Wl,-soname=libjson5.so.${JSON5_SYSTEM_VERSION_M}
endif

all : libjson5.so

test : test.o libjson5-bare.so
	$(CC) -o test $(LDFLAGS) $^ -ljson5-bare

clean :
	rm -f test *.so *.o *.d
	rm -f *.so.*
	rm -f json5-system.h Makefile.system

debug :
	$(MAKE) MAKE_DEBUG=1

sanitize :
	$(MAKE) CDEBUG="-fsanitize=address -g" LDFLAGS="-L$(PWD) -lasan"

debug-test :
	$(MAKE) MAKE_DEBUG=1 test

sanitize-test :
	$(MAKE) test CDEBUG="-fsanitize=address -g" LDFLAGS="-L$(PWD) -lasan"

libjson5.so : libjson5.so.${JSON5_SYSTEM_VERSION_M}
	ln -sf $^ $@

libjson5.so.${JSON5_SYSTEM_VERSION_M} : libjson5.so.${JSON5_SYSTEM_VERSION_MM}
	ln -sf $^ $@

libjson5.so.${JSON5_SYSTEM_VERSION_MM} : $(LIBOBJS) json5-module.o json5-api.o
	$(CC) $(LD_DLFLAGS) $(LD_SONAME_FLAGS) -o $@ $^

libjson5-bare.so : $(LIBOBJS) $(USIOBJS)
	$(CC) $(LD_DLFLAGS) -o $@ $^

%.o : %.d

# the include $(deps) means that, in effect, *.d are the first things
# built and some of them require json5-system.h
json5-module.d : json5-system.h

json5-header.h Makefile.system :
	CC=${CC} $(UTILSDIR)/bin/gen-idio-system -P json5

-include $(deps)
