TOPDIR		:= $(abspath $(PWD)/../../..)

MOD_NAME	:= zlib
SHLIB_NAME	:= lib$(MOD_NAME).so

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

CFLAGS		:= -std=c99 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function -fPIC -I$(PWD) -I$(TOPDIR)/src $(CFLAGS) $(CDEBUG) $(COVFLAGS) $(LIBFFI_INC) $(ZLIB_CFLAGS)
LDFLAGS		:= -L$(PWD) $(LDFLAGS) $(COVFLAGS)
LIBS		:= $(LIBS) $(ZLIB_LIBS)

ifeq ($(OS),Darwin)
LD_DLFLAGS	:= -dynamiclib -undefined dynamic_lookup

# In 2022 Daniel Stenberg was putting out a call as to whether to
# rework to C99.  In the meanwhile there are implicit declarations to
# be handled in the C89 code base.
CFLAGS		:= $(CFLAGS) -Wno-implicit-function-declaration

else
LD_DLFLAGS	:= -shared
endif

# local install
MODULE_LI_LIBDIR	:= $(LI_LIBVERDIR)/$(MOD_NAME)
MODULE_LI_LIBVERDIR	:= $(MODULE_LI_LIBDIR)/$(MODULE_SYSTEM_VERSION_MM)
MODULE_LI_LIBARCHDIR	:= $(MODULE_LI_LIBVERDIR)/$(IDIO_SYSTEM_ARCH)

all : $(SHLIB_NAME)

coverage :
	$(MAKE) COVFLAGS="-fprofile-arcs -ftest-coverage"

clean :
	rm -f test *.so *.o *.d *.gcda *.gcno *.gcov gmon.out map o e
	rm -f *.so.*
	rm -f $(MOD_NAME)-system.h Makefile.system

debug :
	$(MAKE) MAKE_DEBUG=1

sanitize :
	$(MAKE) CDEBUG="-fsanitize=address -g" LDFLAGS="-L$(PWD) -lasan"

debug-test :
	$(MAKE) MAKE_DEBUG=1 test

sanitize-test :
	$(MAKE) test CDEBUG="-fsanitize=address -g" LDFLAGS="-L$(PWD) -lasan"

local-install : $(MODULE_LI_LIBARCHDIR)/$(SHLIB_NAME) | Makefile.system $(MODULE_LI_LIBVERDIR)
	cp $(MOD_NAME).idio $(MODULE_LI_LIBVERDIR)
	echo $(MOD_NAME)@${MODULE_SYSTEM_VERSION_MM} > $(MODULE_LI_LIBDIR)/latest

$(MODULE_LI_LIBARCHDIR)/$(SHLIB_NAME) : $(SHLIB_NAME) | $(MODULE_LI_LIBARCHDIR)
	tar cf - $^* | (cd $(MODULE_LI_LIBARCHDIR) ; tar xf -)

$(MODULE_LI_LIBARCHDIR) :
	mkdir -p $@

clean-local-install :
	rm -rf $(MODULE_LI_LIBDIR)

$(SHLIB_NAME) : $(SHLIB_NAME).${MODULE_SYSTEM_VERSION_M}
	ln -sf $^ $@

$(SHLIB_NAME).${MODULE_SYSTEM_VERSION_M} : $(SHLIB_NAME).${MODULE_SYSTEM_VERSION_MM}
	ln -sf $^ $@

$(SHLIB_NAME).${MODULE_SYSTEM_VERSION_MM} : $(LIBOBJS) $(MOD_NAME)-module.o
	$(CC) $(LDFLAGS) $(LD_DLFLAGS) $(LD_SONAME_FLAGS) $(COVFLAGS) -o $@ $^ $(LIBS)

%.o : %.d

# the include $(deps) means that, in effect, *.d are the first things
# built and some of them require $(MOD_NAME)-system.h -- and %.d doesn't
# work
$(MOD_NAME)-module.d : $(MOD_NAME)-system.h

$(MOD_NAME)-system.h Makefile.system : $(UTILSDIR)/bin/gen-idio-system
	CC=${CC} $(UTILSDIR)/bin/gen-idio-system -M $(MOD_NAME)

ifneq ($(MAKECMDGOALS),clean)
-include $(deps)
endif
