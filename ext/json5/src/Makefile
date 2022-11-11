TOPDIR		:= $(abspath $(PWD)/../../..)

ifneq ($(MAKECMDGOALS),clean)
include $(TOPDIR)/src/Makefile.system
include Makefile.system
endif

# dummy versions if Makefile.system doesn't exist yet
MODULE_SYSTEM_VERSION_MM	?= a.b
MODULE_SYSTEM_VERSION_M		?= a

include $(TOPDIR)/src/Makefile.common

ifdef MAKE_DEBUG
CDEBUG		:= -DIDIO_DEBUG=3 -g
else
CDEBUG		:=
endif

CFLAGS		:= -std=c99 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function -fPIC -I$(PWD) -I$(TOPDIR)/src $(CFLAGS) $(CDEBUG) $(COVFLAGS) $(LIBFFI_INC)
LDFLAGS		:= -L$(PWD) $(LDFLAGS) $(COVFLAGS)
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

# local install
MODULE_LI_LIBDIR	:= $(LI_LIBVERDIR)/json5
MODULE_LI_LIBVERDIR	:= $(MODULE_LI_LIBDIR)/$(MODULE_SYSTEM_VERSION_MM)
MODULE_LI_LIBARCHDIR	:= $(MODULE_LI_LIBVERDIR)/$(IDIO_SYSTEM_ARCH)

all : libjson5.so

coverage :
	$(MAKE) COVFLAGS="-fprofile-arcs -ftest-coverage"

test : usi.c test.o libjson5-bare.so
	$(CC) -o test $(LDFLAGS) $^ -ljson5-bare

clean :
	rm -f test *.so *.o *.d *.gcda *.gcno *.gcov gmon.out map o e
	rm -f *.so.*
	rm -f json5-system.h Makefile.system
	rm -f usi.[ch]

debug :
	$(MAKE) MAKE_DEBUG=1

sanitize :
	$(MAKE) CDEBUG="-fsanitize=address -g" LDFLAGS="-L$(PWD) -lasan"

debug-test :
	$(MAKE) MAKE_DEBUG=1 test

sanitize-test :
	$(MAKE) test CDEBUG="-fsanitize=address -g" LDFLAGS="-L$(PWD) -lasan"

local-install : $(MODULE_LI_LIBARCHDIR)/libjson5.so | Makefile.system $(MODULE_LI_LIBVERDIR)
	cp json5.idio $(MODULE_LI_LIBVERDIR)
	echo json5@${MODULE_SYSTEM_VERSION_MM} > $(MODULE_LI_LIBDIR)/latest

$(MODULE_LI_LIBARCHDIR)/libjson5.so : libjson5.so | $(MODULE_LI_LIBARCHDIR)
	tar cf - $^* | (cd $(MODULE_LI_LIBARCHDIR) ; tar xf -)

$(MODULE_LI_LIBARCHDIR) :
	mkdir -p $@

clean-local-install :
	rm -rf $(MODULE_LI_LIBDIR)

libjson5.so : libjson5.so.${MODULE_SYSTEM_VERSION_M}
	ln -sf $^ $@

libjson5.so.${MODULE_SYSTEM_VERSION_M} : libjson5.so.${MODULE_SYSTEM_VERSION_MM}
	ln -sf $^ $@

libjson5.so.${MODULE_SYSTEM_VERSION_MM} : $(LIBOBJS) json5-module.o json5-api.o
	$(CC) $(LDFLAGS) $(LD_DLFLAGS) $(LD_SONAME_FLAGS) $(COVFLAGS) -o $@ $^

libjson5-bare.so : $(LIBOBJS) $(USIOBJS)
	$(CC) $(LD_DLFLAGS) -o $@ $^

%.o : %.d

usi.c : usi.h

usi.c usi.h :
	cp $(SRCDIR)/$@ .

# the include $(deps) means that, in effect, *.d are the first things
# built and some of them require json5-system.h -- and %.d doesn't
# work
json5-module.d : json5-system.h

json5-system.h Makefile.system : $(UTILSDIR)/bin/gen-idio-system
	CC=${CC} $(UTILSDIR)/bin/gen-idio-system -M json5

ifneq ($(MAKECMDGOALS),clean)
-include $(deps)
endif
