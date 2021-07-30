include ../../../src/Makefile.common

CFLAGS		:= -std=c99 -Wall -Wno-unused-function -Wno-unused-variable -g $(CFLAGS) $(LIBFFI_INC)
LDFLAGS		:= $(LDFLAGS)
LIBS		:= -lffi $(LIBS)

ifneq ($(OS),Darwin)
DLIBS		:= libos.so libposix-regex.so
endif

