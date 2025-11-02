TOPDIR	:= ../../../src

include $(TOPDIR)/Makefile.common

CFLAGS		:= -std=c99 -Wall -Wno-unused-function -Wno-unused-variable -I$(TOPDIR) -g $(LIBFFI_INC)
LDFLAGS		:= $(LDFLAGS)
LIBS		:= -lffi $(LIBS)

ifeq ($(OS),SunOS)
CFLAGS		:= -D_POSIX_PTHREAD_SEMANTICS -D__EXTENSIONS__ $(CFLAGS)
endif

clean:
	rm -f *.o
