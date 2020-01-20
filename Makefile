export

TOPDIR		:= $(abspath $(PWD))
BINDIR		:= $(TOPDIR)/bin
LIBDIR		:= $(TOPDIR)/lib
TESTSDIR	:= $(TOPDIR)/tests
DOCDIR		:= $(TOPDIR)/doc

all clean lean debug test tags:
	$(MAKE) -C src $@
