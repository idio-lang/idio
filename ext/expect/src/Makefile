TOPDIR		:= $(abspath $(shell pwd)/../../..)
TOPSRCDIR	:= $(TOPDIR)/src

MOD_NAME	:= expect

-include Makefile.system
include $(TOPSRCDIR)/Makefile.common
include $(TOPSRCDIR)/Makefile.system

CFLAGS		:= $(CFLAGS) -I$(TOPSRCDIR)
LIBS		:= $(LIBS) -lm

include $(TOPSRCDIR)/Makefile.ext
