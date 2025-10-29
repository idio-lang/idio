TOPDIR		:= $(abspath $(shell pwd)/../../..)
TOPSRCDIR	:= $(TOPDIR)/src

MOD_NAME	:= empty

-include Makefile.system
include $(TOPSRCDIR)/Makefile.common
include $(TOPSRCDIR)/Makefile.system

CFLAGS		:= $(CFLAGS) -I$(TOPSRCDIR)
LIBS		:= $(LIBS)

include $(TOPSRCDIR)/Makefile.ext
