include Makefile.common

all : local-install
	(cd $(EXTDIR)/json5/src; $(MAKE) $@)
	(cd $(EXTDIR)/expect/src; $(MAKE) $@)
	(cd $(EXTDIR)/empty/src; $(MAKE) $@)
	(cd $(EXTDIR)/SRFI-14/src; $(MAKE) $@)
ifneq ($(IDIO_LIBCURL_DEV),)
	(cd $(EXTDIR)/libcurl/src; $(MAKE) $@)
endif
ifneq ($(IDIO_ZLIB_DEV),)
	(cd $(EXTDIR)/zlib/src; $(MAKE) $@)
endif

ifneq ($(MAKECMDGOALS),clean)
# XXX put these on the same line otherwise the generator and then the
# %.d are run twice
include $(deps) Makefile.system
endif

# dummy version of Makefile.system doesn't exist yet
IDIO_SYSTEM_VERSION_MM		?= a.b
IDIO_SYSTEM_VERSION_M		?= a


ifdef MAKE_LEAN
CDEBUG		:= -O3 -UIDIO_DEBUG -UIDIO_VM_PROF ${FORTIFY_CFLAGS}
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

# AddressSanitizer integrates LeakSanitizer
asan sanitize-address :
	$(MAKE) SAN_CFLAGS="-fsanitize=address -g" SAN_LDFLAGS=-fsanitize=address

# -fsanitize=undefined is a check group which includes most suboptions
# (null, enum, ...), see
# https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html
ubsan sanitize-undefined :
	$(MAKE) SAN_CFLAGS="-fsanitize=undefined -g" SAN_LDFLAGS=-fsanitize=undefined

lean :
	$(MAKE) MAKE_LEAN=1

debug :
	$(MAKE) MAKE_DEBUG=1
	(cd $(EXTDIR)/json5/src; $(MAKE) $@)
	(cd $(EXTDIR)/expect/src; $(MAKE) $@)
	(cd $(EXTDIR)/empty/src; $(MAKE) $@)
	(cd $(EXTDIR)/SRFI-14/src; $(MAKE) $@)
ifneq ($(IDIO_LIBCURL_DEV),)
	(cd $(EXTDIR)/libcurl/src; $(MAKE) $@)
endif
ifneq ($(IDIO_ZLIB_DEV),)
	(cd $(EXTDIR)/zlib/src; $(MAKE) $@)
endif

test : local-install
	$(LI_BINDIR)/idio test

# ASAN only (really) works with libc malloc which then blows up with a
# list as long as your arm about leaked memory.  I can't identify
# where the libc malloc behaviour differs from IDIO_MALLOC.
#
# In the meanwhile, rely on valgrind and IDIO_MALLOC
verify: ;
	@echo -e "\n\n**** clean start\n\n"
	$(MAKE) dist-clean > /dev/null
	# this includes the bootstrap-build
	$(MAKE) all > /dev/null
	# post-dist-clean we have new source files...
	$(MAKE) all > /dev/null
	@echo -e "\n\n**** regular test\n\n"
	$(LI_BINDIR)/idio test
	$(MAKE) pre-compile
	@echo -e "\n\n**** regular pre-compile test\n\n"
	$(LI_BINDIR)/idio test
	$(MAKE) build-clean clean-pre-compile > /dev/null
	$(MAKE) lean > /dev/null
	@echo -e "\n\n**** lean test\n\n"
	$(LI_BINDIR)/idio test
	$(MAKE) pre-compile
	@echo -e "\n\n**** lean pre-compile test\n\n"
	$(LI_BINDIR)/idio test
	$(MAKE) build-clean clean-pre-compile > /dev/null
	ASAN_OPTIONS=log_path=idio-asan $(MAKE) sanitize-address > /dev/null
	# we can at least pick up on ASAN errors in the bootstrap
	@echo -e "\n\n**** ASAN test\n\n"
	$(LI_BINDIR)/idio zero-length
	$(MAKE) pre-compile
	# including loading cache files
	@echo -e "\n\n**** ASAN pre-compile test\n\n"
	$(LI_BINDIR)/idio zero-length
	$(MAKE) build-clean clean-pre-compile > /dev/null
	$(MAKE) debug > /dev/null
	@echo -e "\n\n**** debug test\n\n"
	$(LI_BINDIR)/idio test
	$(MAKE) pre-compile
	@echo -e "\n\n**** debug pre-compile test\n\n"
	$(LI_BINDIR)/idio test
	# run valgrind with the debug version as the output is more useful
	$(MAKE) clean-pre-compile
	@echo -e "\n\n**** valgrind test\n\n"
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
	(cd $(EXTDIR)/json5/src; $(MAKE) $@)
	(cd $(EXTDIR)/expect/src; $(MAKE) $@)
	(cd $(EXTDIR)/empty/src; $(MAKE) $@)
	(cd $(EXTDIR)/SRFI-14/src; $(MAKE) $@)
ifneq ($(IDIO_LIBCURL_DEV),)
	(cd $(EXTDIR)/libcurl/src; $(MAKE) $@)
endif
ifneq ($(IDIO_ZLIB_DEV),)
	(cd $(EXTDIR)/zlib/src; $(MAKE) $@)
endif

pre-compile: local-install
	$(LI_BINDIR)/idio --save-xenvs </dev/null

clean-pre-compile:
	rm -rf $(LI_LIBDIR)/$(IDIO_SYSTEM_VERSION_MM)/__idio__

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

user-install : local-install $(UI_BINDIR)/idio pre-compile | $(UI_LIBVERDIR)
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

system-install : local-install $(SI_BINDIR)/idio pre-compile | $(SI_LIBVERDIR)
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

clean : clean-ext-src clean-ext-api clean-rfc6234 run-clean edit-clean clean-pre-compile build-clean
	rm -f $(SRCDIR)/idio-system.h $(SRCDIR)/Makefile.system $(IDIO_SYSTEM)

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
	rm -f vgcore.* core.* core *.gcda *.gcno *.gcov gmon.out idio-asan.* idio-valgrind.* o e idio-vm-* idio-sps.* idio-gc-stats
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
	@etags --regex=@$(SRCDIR)/idio.primitive.tags $(SRCDIR)/*.[ch] $(EXTDIR)/*/src/*.[ch] --language=none --regex=@$(SRCDIR)/idio.tags  $(LIBDIR)/bootstrap/*.idio $(LIBDIR)/*.idio $(TESTSDIR)/*.idio $(EXTDIR)/*/src/*.idio

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

rebuild-usi: $(LI_BINDIR)/idio
	PATH=$$PATH:$(LI_BINDIR):$(BINDIR) gen-usi-c $(UTILSDIR)/Unicode/UnicodeData.txt
