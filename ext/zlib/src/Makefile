TOPDIR		:= $(abspath $(shell pwd)/../../..)
TOPSRCDIR	:= $(TOPDIR)/src

MOD_NAME	:= zlib

-include Makefile.system
include $(TOPSRCDIR)/Makefile.common
include $(TOPSRCDIR)/Makefile.system

CFLAGS		:= $(CFLAGS) -I$(TOPSRCDIR) $(ZLIB_CFLAGS)
LIBS		:= $(LIBS) $(ZLIB_LIBS)

include $(TOPSRCDIR)/Makefile.ext
