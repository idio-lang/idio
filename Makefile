export

# sudo make system-install may not define PWD
PWD		?= $(shell pwd)

TOPDIR		:= $(abspath $(PWD))
BINDIR		= $(TOPDIR)/bin
LIBDIR		= $(TOPDIR)/lib
TESTSDIR	= $(TOPDIR)/tests
DOCDIR		= $(TOPDIR)/doc

all clean lean debug tags dist-clean depend test coverage local-install clean-local-install user-install clean-user-install system-install clean-system-install pre-compile clean-pre-compile:
	$(MAKE) -C src $@

