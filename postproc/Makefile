
include ../config.mak

SWSLIB = libswscale.a

SWSSRCS=swscale.c rgb2rgb.c yuv2rgb.c
SWSOBJS=$(SWSSRCS:.c=.o)
CS_TEST_OBJS=cs_test.o rgb2rgb.o ../cpudetect.o ../mp_msg.o ../libvo/aclib.o

CFLAGS  = $(OPTFLAGS) $(MLIB_INC) -I. -I.. $(EXTRA_INC)
# -I/usr/X11R6/include/

.SUFFIXES: .c .o

# .PHONY: all clean

.c.o:
	$(CC) -c $(CFLAGS) -I.. -o $@ $<

all:    $(SWSLIB)

$(SWSLIB):     $(SWSOBJS)
	$(AR) r $(SWSLIB) $(SWSOBJS)

clean:
	rm -f *.o *.a *~ *.so

distclean:
	rm -f Makefile.bak *.o *.a *~ *.so .depend

dep:    depend

depend:
	$(CC) -MM $(CFLAGS) $(SWSSRCS) 1>.depend

cs_test: $(CS_TEST_OBJS)
	$(CC) $(CS_TEST_OBJS) -o cs_test

swscale-example: swscale-example.o $(SWSLIB)
	$(CC) swscale-example.o $(SWSLIB) ../libmpcodecs/img_format.o -lm -o swscale-example -W -Wall
#
# include dependency files if they exist
#
ifneq ($(wildcard .depend),)
include .depend
endif
