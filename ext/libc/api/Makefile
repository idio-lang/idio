TOPDIR	:= ../../../src

include $(TOPDIR)/Makefile.common

CFLAGS		:= -std=c99 -Wall -Wno-unused-function -Wno-unused-variable -I$(TOPDIR) -g $(CFLAGS) $(LIBFFI_INC)
LDFLAGS		:= $(LDFLAGS)
LIBS		:= -lffi $(LIBS)

ifneq ($(OS),Darwin)
DLIBS		:= libos.so libposix-regex.so
endif

clean:
	rm -f *.o
