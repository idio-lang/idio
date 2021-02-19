export

TOPDIR		:= $(abspath $(PWD))
BINDIR		= $(TOPDIR)/bin
LIBDIR		= $(TOPDIR)/lib
TESTSDIR	= $(TOPDIR)/tests
DOCDIR		= $(TOPDIR)/doc

all clean lean debug tags dist-clean:
	$(MAKE) -C src $@

test : $(BINDIR)/idio
	@PATH=${PATH}:${BINDIR} IDIOLIB=${LIBDIR} idio test

$(BINDIR)/idio : all
