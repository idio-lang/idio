export

TOPDIR		:= $(abspath $(PWD))
BINDIR		= $(TOPDIR)/bin
LIBDIR		= $(TOPDIR)/lib
TESTSDIR	= $(TOPDIR)/tests
DOCDIR		= $(TOPDIR)/doc

all clean lean debug tags dist-clean depend test coverage local-install clean-local-install user-install clean-user-install:
	$(MAKE) -C src $@

