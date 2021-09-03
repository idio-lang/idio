export

TOPDIR		:= $(abspath $(PWD))
BINDIR		= $(TOPDIR)/bin
LIBDIR		= $(TOPDIR)/lib/idio
TESTSDIR	= $(TOPDIR)/tests
DOCDIR		= $(TOPDIR)/doc

all clean lean debug tags dist-clean test coverage:
	$(MAKE) -C src $@

