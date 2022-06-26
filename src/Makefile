include Makefile.common

ifdef MAKE_LEAN
CDEBUG		:= -O3 -UIDIO_DEBUG -UIDIO_VM_PROF
else ifdef MAKE_DEBUG
CDEBUG		:= -DIDIO_DEBUG=3 -g -DIDIO_VM_PROF
else
CDEBUG		:=
endif


PROFLAGS	:=
COVFLAGS	:=
CFLAGS		:= -std=c99 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function -fPIC -DIDIO_MALLOC $(CFLAGS) $(PROFLAGS) $(COVFLAGS) $(SAN_CFLAGS) $(CDEBUG) $(LIBFFI_INC)
LDFLAGS		:= $(PROFLAGS) $(COVFLAGS) $(SAN_LDFLAGS) $(LDFLAGS)
LIBS		:= $(LIBS)

ifneq ($(OS),Darwin)
DLIBS		:= libos.so libposix-regex.so
endif

all : local-install
	(cd $(EXTDIR)/json5/src; $(MAKE) $@)
	(cd $(EXTDIR)/expect/src; $(MAKE) $@)
	(cd $(EXTDIR)/empty/src; $(MAKE) $@)

ifneq ($(MAKECMDGOALS),clean)
# XXX put these on the same line otherwise the generator and then the
# %.d are run twice
include $(deps) Makefile.system
endif

# dummy version of Makefile.system doesn't exist yet
IDIO_SYSTEM_VERSION_MM		?= a.b
IDIO_SYSTEM_VERSION_M		?= a

# system install
SI_BINDIR	?= $(IDIO_SYSTEM_BINDIR)
SI_LIBDIR	?= $(IDIO_SYSTEM_LIBDIR)
SI_LIBVERDIR	?= $(SI_LIBDIR)/${IDIO_SYSTEM_VERSION_MM}



profiled :
	$(MAKE) PROFLAGS=-pg

coverage :
	$(MAKE) COVFLAGS="-fprofile-arcs -ftest-coverage"

# adding -DIDIO_DEBUG=3 to SAN_CFLAGS causes a memory leak in
# __vasprintf_internal ...
asan sanitize-address :
	$(MAKE) SAN_CFLAGS="-fsanitize=address -g" SAN_LDFLAGS=-fsanitize=address

lean :
	$(MAKE) MAKE_LEAN=1

debug :
	$(MAKE) MAKE_DEBUG=1
	(cd $(EXTDIR)/json5/src; $(MAKE) $@)
	(cd $(EXTDIR)/expect/src; $(MAKE) $@)
	(cd $(EXTDIR)/empty/src; $(MAKE) $@)

test : local-install
	$(LI_BINDIR)/idio test

# ASAN only (really) works with libc malloc which then blows up with a
# list as long as your arm about leaked memory.  I can't identify
# where the libc malloc behaviour differs from IDIO_MALLOC.
#
# In the meanwhile, rely on valgrind and IDIO_MALLOC
verify: dist-clean
	$(MAKE) test
	#-ASAN_OPTIONS=log_path=idio-asan $(MAKE) clean sanitize-address test
	$(MAKE) clean debug test
	# run valgrind with the debug version as the output is more useful
	valgrind --leak-check=full --show-leak-kinds=all --child-silent-after-fork=yes --log-file=idio-valgrind.%p $(LI_BINDIR)/idio test
	ls -l idio-valgrind.*

doc :
	rm -rf $(SRCKDOUTDIR)
	(cd .. ; kernel-autodoc src $(SRCKDOUTDIR))
	#cp $(SRCKDOUTDIR)/*_[ch].rst $(SRCDOCDIR)
	diff -w <(grep _[ch] $(SRCKDOUTDIR)/index.rst) <(grep _[ch] $(SRCDOCDIR)/index.rst | sed -e 's!.*/!!')
	make -C $(SRCDOCDIR) html text

local-install : $(LI_BINDIR)/idio | $(LI_BINDIR) $(LI_LIBVERDIR)
	cp -r $(LIBDIR)/*.idio $(LIBDIR)/bootstrap $(LI_LIBVERDIR)
	(cd $(EXTDIR)/json5/src; $(MAKE) local-install)
	(cd $(EXTDIR)/expect/src; $(MAKE) local-install)
	(cd $(EXTDIR)/empty/src; $(MAKE) local-install)

pre-compile: $(LI_BINDIR)/idio
	$(LI_BINDIR)/idio $(UTILSDIR)/bin/pre-compile-lib

$(SRCDIR)/rfc6234.o : $(SRCDIR)/RFC6234/libsha.a

$(SRCDIR)/RFC6234/libsha.a :
	$(MAKE) -C RFC6234 libsha.a

$(LI_BINDIR)/idio : $(LI_BINDIR)/idio.${IDIO_SYSTEM_VERSION_MM}
	ln -sf idio.${IDIO_SYSTEM_VERSION_MM} $@

$(LI_BINDIR)/idio.${IDIO_SYSTEM_VERSION_MM} : $(objects) $(SRCDIR)/RFC6234/libsha.a | Makefile.system $(LI_BINDIR) libc-api.h idio-system.h idio-config.h
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

$(LI_BINDIR) $(LI_LIBVERDIR) :
	mkdir -p $@

clean-local-install :
	rm -rf $(LI_TOPDIR)

user-install : local-install $(UI_BINDIR)/idio | $(UI_LIBVERDIR)
	(cd $(LI_LIBDIR); tar cf - ${IDIO_SYSTEM_VERSION_MM}) | (cd $(UI_LIBDIR); tar xf -)

$(UI_BINDIR)/idio : $(UI_BINDIR)/idio.${IDIO_SYSTEM_VERSION_MM}
	ln -sf idio.${IDIO_SYSTEM_VERSION_MM} $@

$(UI_BINDIR)/idio.${IDIO_SYSTEM_VERSION_MM} : $(LI_BINDIR)/idio.${IDIO_SYSTEM_VERSION_MM} | $(UI_BINDIR)
ifeq ($(OS),SunOS)
ifeq (/usr/sbin/install,$(shell which install))
	install -f $(UI_BINDIR) $^
else
	install $^ $@
endif
else
	install $^ $@
endif

$(UI_BINDIR) $(UI_LIBVERDIR) :
	mkdir -p $@

clean-user-install :
	rm -f $(UI_BINDIR)/idio.${IDIO_SYSTEM_VERSION_MM} $(UI_BINDIR)/idio
	rm -rf $(UI_LIBVERDIR)

system-install : local-install $(SI_BINDIR)/idio | $(SI_LIBVERDIR)
	(cd $(LI_LIBDIR); tar cf - ${IDIO_SYSTEM_VERSION_MM}) | (cd $(SI_LIBDIR); tar xf -)

$(SI_BINDIR)/idio : $(SI_BINDIR)/idio.${IDIO_SYSTEM_VERSION_MM}
	ln -sf idio.${IDIO_SYSTEM_VERSION_MM} $@

$(SI_BINDIR)/idio.${IDIO_SYSTEM_VERSION_MM} : $(LI_BINDIR)/idio.${IDIO_SYSTEM_VERSION_MM}
ifeq ($(OS),SunOS)
ifeq (/usr/sbin/install,$(shell which install))
	install -f $(SI_BINDIR) $^
else
	install $^ $@
endif
else
	install $^ $@
endif

$(SI_LIBVERDIR) :
	mkdir -p $@

clean-system-install :
	rm -f $(SI_BINDIR)/idio.${IDIO_SYSTEM_VERSION_MM} $(SI_BINDIR)/idio
	rm -rf $(SI_LIBVERDIR)

lib%.so : %.o
	$(CC) -shared -o $@ $^

clean : build-clean clean-ext-src clean-ext-api clean-rfc6234 run-clean edit-clean
	rm -f $(SRCDIR)/idio-system.h $(SRCDIR)/Makefile.system $(LIBDIR)/system.idio

clean-ext-src: $(EXTDIR)/*/src
	for dir in $^ ; do \
	    (cd $${dir}; ${MAKE} clean) ; \
	done

clean-ext-api: $(EXTDIR)/*/api
	for dir in $^ ; do \
	    (cd $${dir}; ${MAKE} clean) ; \
	done

clean-rfc6234:
	$(MAKE) -C RFC6234 clean

build-clean:
	rm -f *.o *.d *.so map
	rm -rf $(EXTDIR)/*/gen

run-clean :
	rm -f vgcore.* core.* core *.gcda *.gcno *.gcov gmon.out idio-asan.* idio-valgrind.* o e idio-vm-* idio-gc-stats
ifeq ($(OS),Darwin)
	rm -f /cores/*
else ifeq ($(OS),FreeBSD)
	rm -f idio.core
endif

edit-clean :
	rm -f $(shell find $(TOPDIR) -name \*~)

dist-clean : clean clean-local-install
	rm -f $(SRCDIR)/libc-api.h
	rm -f $(SRCDIR)/idio-config.h
	rm -f $(LIBDIR)/libc-api.idio
	rm -rf $(DOCDIR)/ref/build $(DOCDIR)/ref/source

tags : TAGS

TAGS : $(SRCDIR)/*.[ch] $(LIBDIR)/bootstrap/*.idio $(LIBDIR)/*.idio $(TESTSDIR)/*.idio
	@echo TAGS: $?
	@etags --regex=@$(SRCDIR)/idio.primitive.tags $(SRCDIR)/*.[ch] --language=none --regex=@$(SRCDIR)/idio.tags  $(LIBDIR)/bootstrap/*.idio $(LIBDIR)/*.idio $(TESTSDIR)/*.idio

valgrind: local-install
	valgrind --leak-check=full --show-leak-kinds=all --child-silent-after-fork=yes --log-file=idio-valgrind.%p $(LI_BINDIR)/idio # 2>&1 | less +/^==

	valgrind --tool=callgrind ../bin/idio counter
	qcachegrind

cppcheck:
	cppcheck --force --enable=all .

IDIO_SYSTEM = $(LIBDIR)/idio-system.idio

# the include $(deps) means that, in effect, *.d are the first things
# built and some of them require idio-system.h -- and %.d doesn't work
compile.d env.d file-handle.d idio.d libc-api.d vars.d : idio-system.h
$(deps) : idio-config.h

# $(IDIO_SYSTEM) : $(UTILSDIR)/bin/gen-idio-system src/vm-asm.h
# 	CC=${CC} $(UTILSDIR)/bin/gen-idio-system $(IDIO_SYSTEM)

idio-system.h Makefile.system $(IDIO_SYSTEM) : $(UTILSDIR)/bin/gen-idio-system $(sources) $(filter-out $(IDIO_SYSTEM),$(isources)) $(SRCDIR)/version
	CC=${CC} $(UTILSDIR)/bin/gen-idio-system $(IDIO_SYSTEM)

idio-config.h : $(UTILSDIR)/bin/gen-idio-config
	CC=${CC} $(UTILSDIR)/bin/gen-idio-config

# Bootstrap
#
# This $(objects) dependent on libc-api.h serves two purposes: firstly
# it forces the actual creation in the specific rule below (using some
# probably wrong but at least consistently wrong values in
# build-bootstrap/libc-api.h) and secondly, as we'll have just created
# a new libc-api.h file, we'll force the recompilation of everything
# again using the correct values.
$(objects) : libc-api.h

libc-api.h :
	$(MAKE) -f Makefile.bootstrap $@
