
include ../../config.mak

ifeq ($(SHARED_PP),yes)
SPPLIB = $(SLIBPREF)postproc$(SLIBSUF)
SPPVERSION = 0.0.1
endif
PPLIB = $(LIBPREF)postproc$(LIBSUF)

PPOBJS=postprocess.o
SPPOBJS=postprocess_pic.o

CFLAGS  = $(OPTFLAGS) $(MLIB_INC) -I. -I.. $(EXTRA_INC)
# -I/usr/X11R6/include/

.SUFFIXES: .c .o

# .PHONY: all clean

.c.o:
	$(CC) -c $(CFLAGS) -I.. -I../.. -o $@ $<

all:    $(SWSLIB) $(PPLIB) $(SPPLIB)

clean:
	rm -f *.o *.a *~ *.so $(PPLIB) $(SPPLIB)

distclean:
	rm -f Makefile.bak *.o *.a *~ *.so .depend

dep:    depend

depend:
	$(CC) -MM $(CFLAGS) postprocess.c 1>.depend

ifeq ($(SHARED_PP),yes)
postprocess_pic.o: postprocess.c
	$(CC) -c $(CFLAGS) -fomit-frame-pointer -fPIC -DPIC -I.. -I../.. -o $@ $<

$(SPPLIB): $(SPPOBJS)
	$(CC) -shared -Wl,-soname,$(SPPLIB).0 \
	-o $(SPPLIB) $(SPPOBJS)
endif

$(PPLIB): $(PPOBJS)
	rm -f $@
	$(AR) rc $@ $(PPOBJS)
	$(RANLIB) $@

install: all
ifeq ($(SHARED_PP),yes)
ifeq ($(CONFIG_WIN32),yes)
       install $(INSTALLSTRIP) -m 755 $(SPPLIB) "$(prefix)"
else
	install -d $(prefix)/lib
	install $(INSTALLSTRIP) -m 755 $(SPPLIB) $(prefix)/lib/$(SPPLIB).$(SPPVERSION)
	ln -sf $(SPPLIB).$(SPPVERSION) $(prefix)/lib/$(SPPLIB)
	ldconfig || true
endif
endif
	mkdir -p $(prefix)/include/postproc
	install -m 644 postprocess.h $(prefix)/include/postproc/postprocess.h


#
# include dependency files if they exist
#
ifneq ($(wildcard .depend),)
include .depend
endif
