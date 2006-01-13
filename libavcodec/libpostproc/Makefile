
include ../../config.mak

VPATH=$(SRC_PATH)/libavcodec/libpostproc

LIB = $(LIBPREF)postproc$(LIBSUF)
ifeq ($(SHARED_PP),yes)
SLIBNAME = $(SLIBPREF)postproc$(SLIBSUF)
endif

OBJS=postprocess.o
SOBJS=postprocess_pic.o

CFLAGS  = $(OPTFLAGS) $(MLIB_INC) -I. -I.. $(EXTRA_INC)
# -I/usr/X11R6/include/

.SUFFIXES: .c .o

# .PHONY: all clean

.c.o:
	$(CC) -c $(CFLAGS) $(LIBOBJFLAGS) -I$(SRC_PATH)/libavcodec -I../.. -o $@ $<

all:    $(SWSLIB) $(LIB) $(SLIBNAME)

clean:
	rm -f *.o *.a *~ *$(SLIBSUF) $(LIB) $(SLIBNAME)

distclean:
	rm -f Makefile.bak *.o *.a *~ *$(SLIBSUF) .depend

dep:    depend

depend:
	$(CC) -MM $(CFLAGS) postprocess.c 1>.depend

ifeq ($(SHARED_PP),yes)
postprocess_pic.o: postprocess.c
	$(CC) -c $(CFLAGS) -fomit-frame-pointer -fPIC -DPIC -I.. -I../.. -o $@ $<

$(SLIBNAME): $(SOBJS)
	$(CC) $(SHFLAGS) -o $(SLIBNAME) $(SOBJS)
endif

$(LIB): $(OBJS)
	rm -f $@
	$(AR) rc $@ $(OBJS)
	$(RANLIB) $@

install: all
ifeq ($(SHARED_PP),yes)
ifeq ($(CONFIG_WIN32),yes)
	install $(INSTALLSTRIP) -m 755 $(SLIBNAME) "$(prefix)"
else
	install -d $(libdir)
	install $(INSTALLSTRIP) -m 755 $(SLIBNAME) $(libdir)/libpostproc-$(VERSION)$(SLIBSUF)
	ln -sf $(SLIBPREF)postproc-$(VERSION)$(SLIBSUF) $(libdir)/$(SLIBNAME)
	$(LDCONFIG) || true
endif
endif
	mkdir -p $(prefix)/include/postproc
	install -m 644 $(SRC_PATH)/libavcodec/libpostproc/postprocess.h $(prefix)/include/postproc/postprocess.h
	install -d $(libdir)/pkgconfig
	install -m 644 ../../libpostproc.pc $(libdir)/pkgconfig


#
# include dependency files if they exist
#
ifneq ($(wildcard .depend),)
include .depend
endif
