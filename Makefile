export

TOPDIR		:= $(abspath $(PWD))
BINDIR		= $(TOPDIR)/bin
LIBDIR		= $(TOPDIR)/lib/idio
TESTSDIR	= $(TOPDIR)/tests
DOCDIR		= $(TOPDIR)/doc

all clean lean debug tags dist-clean depend test coverage local-install clean-local-install:
	$(MAKE) -C src $@

