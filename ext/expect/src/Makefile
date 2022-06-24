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
LIBS		:= $(LIBS) -lm

ifeq ($(OS),Darwin)
LD_DLFLAGS	:= -dynamiclib -undefined dynamic_lookup
else
LD_DLFLAGS	:= -shared
endif

# local install
MODULE_LI_LIBDIR	:= $(LI_LIBVERDIR)/expect
MODULE_LI_LIBVERDIR	:= $(MODULE_LI_LIBDIR)/$(MODULE_SYSTEM_VERSION_MM)
MODULE_LI_LIBARCHDIR	:= $(MODULE_LI_LIBVERDIR)/$(IDIO_SYSTEM_ARCH)

all : libexpect.so

coverage :
	$(MAKE) COVFLAGS="-fprofile-arcs -ftest-coverage"

clean :
	rm -f test *.so *.o *.d *.gcda *.gcno *.gcov gmon.out map o e
	rm -f *.so.*
	rm -f expect-system.h Makefile.system

debug :
	$(MAKE) MAKE_DEBUG=1

sanitize :
	$(MAKE) CDEBUG="-fsanitize=address -g" LDFLAGS="-L$(PWD) -fsanitize=address"

debug-test :
	$(MAKE) MAKE_DEBUG=1 test

sanitize-test :
	$(MAKE) test CDEBUG="-fsanitize=address -g" LDFLAGS="-L$(PWD) -fsanitize=address"

local-install : $(MODULE_LI_LIBARCHDIR)/libexpect.so | Makefile.system $(MODULE_LI_LIBVERDIR)
	cp expect.idio $(MODULE_LI_LIBVERDIR)
	echo expect@${MODULE_SYSTEM_VERSION_MM} > $(MODULE_LI_LIBDIR)/latest

$(MODULE_LI_LIBARCHDIR)/libexpect.so : libexpect.so | $(MODULE_LI_LIBARCHDIR)
	tar cf - $^* | (cd $(MODULE_LI_LIBARCHDIR) ; tar xf -)

$(MODULE_LI_LIBARCHDIR) :
	mkdir -p $@

clean-local-install :
	rm -rf $(MODULE_LI_LIBDIR)

libexpect.so : libexpect.so.${MODULE_SYSTEM_VERSION_M}
	ln -sf $^ $@

libexpect.so.${MODULE_SYSTEM_VERSION_M} : libexpect.so.${MODULE_SYSTEM_VERSION_MM}
	ln -sf $^ $@

libexpect.so.${MODULE_SYSTEM_VERSION_MM} : expect-module.o
	$(CC) $(LDFLAGS) $(LD_DLFLAGS) $(LD_SONAME_FLAGS) $(COVFLAGS) -o $@ $^ $(LIBS)

%.o : %.d

# the include $(deps) means that, in effect, *.d are the first things
# built and some of them require expect-system.h -- and %.d doesn't
# work
expect-module.d : expect-system.h

expect-system.h Makefile.system : $(UTILSDIR)/bin/gen-idio-system
	CC=${CC} $(UTILSDIR)/bin/gen-idio-system -M expect

ifneq ($(MAKECMDGOALS),clean)
-include $(deps)
endif
