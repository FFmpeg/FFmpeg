
include ../config.mak

SWSLIB = libswscale.a
SPPLIB = libpostproc.so
SPPVERSION = 0.0.1
PPLIB = libpostproc.a

SWSSRCS=swscale.c rgb2rgb.c yuv2rgb.c
SWSOBJS=$(SWSSRCS:.c=.o)
PPOBJS=postprocess.o
SPPOBJS=postprocess_pic.o
CS_TEST_OBJS=cs_test.o rgb2rgb.o ../cpudetect.o ../mp_msg.o

CFLAGS  = $(OPTFLAGS) $(MLIB_INC) -I. -I.. $(EXTRA_INC)
# -I/usr/X11R6/include/

.SUFFIXES: .c .o

# .PHONY: all clean

.c.o:
	$(CC) -c $(CFLAGS) -o $@ $<

all:    $(SWSLIB) $(PPLIB) $(SPPLIB)

$(SWSLIB):     $(SWSOBJS)
	$(AR) r $(SWSLIB) $(SWSOBJS)

clean:
	rm -f *.o *.a *~ *.so

distclean:
	rm -f Makefile.bak *.o *.a *~ *.so .depend

dep:    depend

depend:
	$(CC) -MM $(CFLAGS) $(SRCS) 1>.depend

cs_test: $(CS_TEST_OBJS)
	$(CC) $(CS_TEST_OBJS) -o cs_test

postprocess_pic.o: postprocess.c
	$(CC) -c $(CFLAGS) -fPIC -DPIC -o $@ $<

$(SPPLIB): $(SPPOBJS)
	$(CC) -shared -Wl,-soname,$(SPPLIB).0 \
	-o $(SPPLIB) $(SPPOBJS)

$(PPLIB): $(PPOBJS)
	$(AR) r $(PPLIB) $(PPOBJS)

install: all
ifeq ($(SHARED_PP),yes)
	install -d $(prefix)/lib
	install -s -m 755 $(SPPLIB) $(prefix)/lib/$(SPPLIB).$(SPPVERSION)
	ln -sf $(SPPLIB).$(SPPVERSION) $(prefix)/lib/$(SPPLIB)
	ldconfig || true
	mkdir -p $(prefix)/include/postproc
	install -m 644 postprocess.h $(prefix)/include/postproc/postprocess.h
endif

	
#
# include dependency files if they exist
#
ifneq ($(wildcard .depend),)
include .depend
endif
