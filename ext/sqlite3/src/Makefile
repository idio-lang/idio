TOPDIR		:= $(abspath $(shell pwd)/../../..)
TOPSRCDIR	:= $(TOPDIR)/src

MOD_NAME	:= sqlite3

-include Makefile.system
include $(TOPSRCDIR)/Makefile.common
include $(TOPSRCDIR)/Makefile.system

CFLAGS		:= $(CFLAGS) -I$(TOPSRCDIR) $(SQLITE3_CFLAGS)
LIBS		:= $(LIBS) $(SQLITE3_LIBS)

include $(TOPSRCDIR)/Makefile.ext
