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
CFLAGS		:= -std=c99 -Wall -Wno-unused-function -fPIC -DIDIO_MALLOC $(CFLAGS) $(PROFLAGS) $(COVFLAGS) $(SAN_CFLAGS) $(CDEBUG) $(LIBFFI_INC)
LDFLAGS		:= $(PROFLAGS) $(COVFLAGS) $(SAN_LDFLAGS) $(LDFLAGS)
LIBS		:= -lffi $(LIBS)

ifneq ($(OS),Darwin)
DLIBS		:= libos.so libposix-regex.so
endif

all : $(BINDIR)/idio
#all : idio scm1-c-structs.scm1 $(DLIBS) tags

profiled :
	$(MAKE) PROFLAGS=-pg

coverage :
	$(MAKE) COVFLAGS="-fprofile-arcs -ftest-coverage"

sanitize-address :
	$(MAKE) SAN_CFLAGS="-fsanitize=address -g -UIDIO_MALLOC" SAN_LDFLAGS=-lasan

lean :
	$(MAKE) MAKE_LEAN=1

debug :
	$(MAKE) MAKE_DEBUG=1

test : all
	$(BINDIR)/idio test

# ASAN only (really) works with libc malloc which then blows up with a
# list as long as your arm about leaked memory.  I can't identify
# where the libc malloc behaviour differs from IDIO_MALLOC.
#
# In the meanwhile, rely on valgrind and IDIO_MALLOC
verify:
	$(MAKE) clean all test
	#-ASAN_OPTIONS=log_path=idio-asan $(MAKE) clean sanitize-address test
	$(MAKE) clean debug test
	# run valgrind with the debug version as the output is more useful
	valgrind --leak-check=full --show-leak-kinds=all --child-silent-after-fork=yes --log-file=idio-valgrind.%p $(BINDIR)/idio test
	ls -l idio-valgrind.*

doc : 
	rm -rf $(SRCKDOUTDIR)
	(cd .. ; kernel-autodoc src $(SRCKDOUTDIR))
	#cp $(SRCKDOUTDIR)/*_[ch].rst $(SRCDOCDIR)
	diff -w <(grep _[ch] $(SRCKDOUTDIR)/index.rst) <(grep _[ch] $(SRCDOCDIR)/index.rst | sed -e 's!.*/!!')
	make -C $(SRCDOCDIR) html text

$(BINDIR)/idio : $(objects) | libc-api.h $(deps)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

lib%.so : %.o
	$(CC) -shared -o $@ $^

clean : build-clean run-clean edit-clean

build-clean:
	rm -f *.o *.d $(BINDIR)/idio *.so TAGS map
	rm -rf $(EXTDIR)/*/gen

run-clean :
	rm -f vgcore.* core.* core *.gcda *.gcno *.gcov gmon.out idio-asan.* idio-valgrind.* o e vm-*
ifeq ($(OS),Darwin)
	rm -f /cores/*
else ifeq ($(OS),FreeBSD)
	rm -f idio.core
endif

edit-clean :
	rm -f $(BINDIR)/*~ $(SRCDIR)/*~ $(SRCDIR)/build-bootstrap/*~ $(LIBDIR)/*~ $(TESTSDIR)/*~ $(TESTSDIR)/*/*~ $(EXTDIR)/*/src/*~ $(UTILSDIR)/*~ $(UTILSDIR)/*/*~

dist-clean : clean
	rm -f $(SRCDIR)/idio-system.h $(SRCDIR)/Makefile.system
	rm -f $(SRCDIR)/libc-api.h
	rm -f $(LIBDIR)/libc-api.idio

tags : TAGS

TAGS : *.[ch] $(LIBDIR)/*.idio $(TESTSDIR)/*.idio
	@echo TAGS: $?
	@etags *.[ch] --language=none --regex=@idio.tags $(LIBDIR)/*.idio $(TESTSDIR)/*.idio

valgrind: $(BINDIR)/idio
	valgrind --leak-check=full --show-leak-kinds=all --child-silent-after-fork=yes --log-file=idio-valgrind.%p $(BINDIR)/idio # 2>&1 | less +/^==

	valgrind --tool=callgrind ../bin/idio counter
	qcachegrind

cppcheck:
	cppcheck --force --enable=all .

%.o : %.d

# the include $(deps) means that, in effect, *.d are the first things
# built and some of them require idio-system.h
env.d : idio-system.h

idio-system.h Makefile.system :
	CC=${CC} $(UTILSDIR)/bin/gen-idio-system

include Makefile.system

-include $(deps)

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
