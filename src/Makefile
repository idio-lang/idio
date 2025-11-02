-include Makefile.system
include Makefile.common

EXTENSION_DIRS	:= $(wildcard $(EXTDIR)/*)

SHLIB_NAME	:= libidio.so

LIBSRCS		:= $(filter-out idio.c,$(sources))
LIBOBJS		:= $(patsubst %.c,%.o,$(LIBSRCS))

EXESRCS		:= idio.c
EXEOBJS		:= $(patsubst %.c,%.o,$(EXESRCS))

all : local-install
	$(MAKE) -C $(EXTDIR)/json5/src $@
	$(MAKE) -C $(EXTDIR)/expect/src $@
	$(MAKE) -C $(EXTDIR)/empty/src $@
	$(MAKE) -C $(EXTDIR)/SRFI-14/src $@
ifneq ($(IDIO_LIBCURL_DEV),)
	$(MAKE) -C $(EXTDIR)/libcurl/src $@
endif
ifneq ($(IDIO_ZLIB_DEV),)
	$(MAKE) -C $(EXTDIR)/zlib/src $@
endif
ifneq ($(IDIO_SQLITE3_DEV),)
	$(MAKE) -C $(EXTDIR)/sqlite3/src $@
endif

# dummy version if Makefile.system doesn't exist yet
IDIO_SYSTEM_VERSION_MM		?= a.b
IDIO_SYSTEM_VERSION_M		?= a

# system install
SI_BINDIR	?= $(IDIO_SYSTEM_BINDIR)
SI_LIBDIR	?= $(IDIO_SYSTEM_LIBDIR)
SI_LIBVERDIR	?= $(SI_LIBDIR)/${IDIO_SYSTEM_VERSION_MM}

$(LI_LIBVERDIR)/%.idio : $(LIBDIR)/%.idio
	cp $^ $@

local-install : $(LI_BINDIR)/idio | $(LI_BINDIR) $(LI_LIBVERDIR)/bootstrap $(patsubst $(LIBDIR)/%,$(LI_LIBVERDIR)/%,$(isources))
	$(MAKE) -C $(EXTDIR)/json5/src $@
	$(MAKE) -C $(EXTDIR)/expect/src $@
	$(MAKE) -C $(EXTDIR)/empty/src $@
	$(MAKE) -C $(EXTDIR)/SRFI-14/src $@
ifneq ($(IDIO_LIBCURL_DEV),)
	$(MAKE) -C $(EXTDIR)/libcurl/src $@
endif
ifneq ($(IDIO_ZLIB_DEV),)
	$(MAKE) -C $(EXTDIR)/zlib/src $@
endif
ifneq ($(IDIO_SQLITE3_DEV),)
	$(MAKE) -C $(EXTDIR)/sqlite3/src $@
endif

pre-compile: local-install
	$(LI_BINDIR)/idio --save-xenvs </dev/null

clean-pre-compile:
	rm -rf $(LI_LIBDIR)/$(IDIO_SYSTEM_VERSION_MM)/__idio__

$(SRCDIR)/rfc6234.o : $(SRCDIR)/RFC6234/libsha.a

$(SRCDIR)/RFC6234/libsha.a :
	$(MAKE) -C RFC6234 libsha.a

$(LI_BINDIR)/idio : $(LI_BINDIR)/idio.${IDIO_SYSTEM_VERSION_MM}
	ln -sf $(notdir $^) $@

$(LI_BINDIR)/idio.${IDIO_SYSTEM_VERSION_MM} : \
		$(EXEOBJS) \
		$(LI_SHLIBDIR)/$(SHLIB_NAME) \
		| Makefile.system $(LI_BINDIR) libc-api.h idio-system.h idio-config.h
	$(CC) $(LDFLAGS) -Wl,-rpath,'$$ORIGIN/../lib' -o $@ $^ $(LIBS)

$(LI_SHLIBDIR)/$(SHLIB_NAME) : $(LI_SHLIBDIR)/$(SHLIB_NAME).${IDIO_SYSTEM_VERSION_M}
	ln -sf $(notdir $^) $@

$(LI_SHLIBDIR)/$(SHLIB_NAME).${IDIO_SYSTEM_VERSION_M} : $(LI_SHLIBDIR)/$(SHLIB_NAME).${IDIO_SYSTEM_VERSION_MM}
	ln -sf $(notdir $^) $@

$(LI_SHLIBDIR)/$(SHLIB_NAME).${IDIO_SYSTEM_VERSION_MM} : $(SHLIB_NAME).${IDIO_SYSTEM_VERSION_MM} | $(LI_SHLIBDIR)
	cp $^ $@

$(SHLIB_NAME).${IDIO_SYSTEM_VERSION_MM} : $(LIBOBJS) $(SRCDIR)/RFC6234/libsha.a
ifeq ($(OS),NetBSD)
	# NetBSD doesn't like the Idio library having the same soname
	# as the real sqlite3 library (and will not look for the
	# symbols in the real library)
	$(CC) $(LDFLAGS) $(LD_DLFLAGS) $(COVFLAGS) -o $@ $^ $(LIBS)
else
	$(CC) $(LDFLAGS) $(LD_DLFLAGS) $(IDIO_LD_SONAME_FLAGS) $(COVFLAGS) -o $@ $^ $(LIBS)
endif

$(LI_BINDIR) $(LI_SHLIBDIR) $(LI_LIBVERDIR) $(LI_LIBVERDIR)/bootstrap :
	mkdir -p $@

clean-local-install :
	rm -rf $(LI_TOPDIR)

user-install : local-install $(UI_BINDIR)/idio pre-compile | $(UI_LIBVERDIR)
	(cd $(LI_LIBDIR); tar cf - ${IDIO_SYSTEM_VERSION_MM}) | (cd $(UI_LIBDIR); tar xf -)

$(UI_BINDIR)/idio : $(UI_BINDIR)/idio.${IDIO_SYSTEM_VERSION_MM}
	ln -sf $(notdir $^) $@

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
	ln -sf $(notdir $^) $@

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

include Makefile.rules

clean-system-install :
	rm -f $(SI_BINDIR)/idio.${IDIO_SYSTEM_VERSION_MM} $(SI_BINDIR)/idio
	rm -rf $(SI_LIBVERDIR)

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
	rm -f *.o *.d *.so *.so.* map
	rm -rf $(EXTDIR)/*/gen/*

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
$(deps) : idio-config.h idio-system.h libc-api.h

# $(IDIO_SYSTEM) : $(UTILSDIR)/bin/gen-idio-system src/vm-asm.h
# 	CC=${CC} $(UTILSDIR)/bin/gen-idio-system $(IDIO_SYSTEM)

idio-system.h Makefile.system $(IDIO_SYSTEM) : $(UTILSDIR)/bin/gen-idio-system $(EXTENSION_DIRS)
	CC=${CC} $(UTILSDIR)/bin/gen-idio-system $(IDIO_SYSTEM)

idio-config.h : $(UTILSDIR)/bin/gen-idio-config
	CC=${CC} $(UTILSDIR)/bin/gen-idio-config

libc-api.h :
	$(MAKE) -f Makefile.bootstrap $@

rebuild-usi: $(LI_BINDIR)/idio
	PATH=$$PATH:$(LI_BINDIR):$(BINDIR) gen-usi-c $(UTILSDIR)/Unicode/UnicodeData.txt

ifneq ($(MAKECMDGOALS),clean)
# XXX put these on the same line otherwise the generator and then the
# %.d are run twice
-include $(deps)
endif
