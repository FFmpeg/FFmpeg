
include ../config.mak

LIBNAME = libpostproc.a

SRCS=postprocess.c swscale.c rgb2rgb.c yuv2rgb.c
OBJS=$(SRCS:.c=.o)
CS_TEST_OBJS=cs_test.o rgb2rgb.o ../cpudetect.o ../mp_msg.o

CFLAGS  = $(OPTFLAGS) $(MLIB_INC) -I. -I.. -Wall $(EXTRA_INC)
# -I/usr/X11R6/include/

.SUFFIXES: .c .o

# .PHONY: all clean

.c.o:
	$(CC) -c $(CFLAGS) -o $@ $<

$(LIBNAME):     $(OBJS)
	$(AR) r $(LIBNAME) $(OBJS)

all:    $(LIBNAME)

clean:
	rm -f *.o *.a *~

distclean:
	rm -f Makefile.bak *.o *.a *~ .depend

dep:    depend

depend:
	$(CC) -MM $(CFLAGS) $(SRCS) 1>.depend

cs_test: $(CS_TEST_OBJS)
	$(CC) $(CS_TEST_OBJS) -o cs_test

#
# include dependency files if they exist
#
ifneq ($(wildcard .depend),)
include .depend
endif
