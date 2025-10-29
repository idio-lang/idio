TOPDIR		:= $(abspath $(shell pwd)/../../..)
TOPSRCDIR	:= $(TOPDIR)/src

MOD_NAME	:= libcurl

-include Makefile.system
include $(TOPSRCDIR)/Makefile.common
include $(TOPSRCDIR)/Makefile.system

CFLAGS		:= $(CFLAGS) -I$(TOPSRCDIR) $(LIBCURL_CFLAGS)
LIBS		:= $(LIBS) $(LIBCURL_LIBS)

ifeq ($(OS),Darwin)
# In 2022 Daniel Stenberg was putting out a call as to whether to
# rework to C99.  In the meanwhile there are implicit declarations to
# be handled in the C89 code base.
CFLAGS		:= $(CFLAGS) -Wno-implicit-function-declaration

else ifeq ($(OS),NetBSD)

# Without -z nodelete NetBSD will segfault during shutdown in
# OPENSSL_cleanup() from /usr/lib/libcrypto.so.14
LD_DLFLAGS	:= -shared -z nodelete

else
LD_DLFLAGS	:= -shared
endif

include $(TOPSRCDIR)/Makefile.ext
