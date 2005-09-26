
include ../../config.mak

VPATH=$(SRC_PATH)/libavcodec/libpostproc

ifeq ($(SHARED_PP),yes)
SPPLIBNAME = $(SLIBPREF)postproc$(SLIBSUF)
ifeq ($(CONFIG_DARWIN),yes)
SPPLIBMAJOR = $(SLIBPREF)postproc.$(SPPMAJOR)$(SLIBSUF)
SPPLIB = $(SLIBPREF)postproc.$(SPPVERSION)$(SLIBSUF)
SHFLAGS += -Wl,-install_name,$(libdir)/$(SPPLIBNAME),-current_version,$(SPPVERSION),-compatibility_version,$(SPPVERSION)
else
SPPLIBMAJOR = $(SPPLIBNAME).$(SPPMAJOR)
SPPLIB = $(SPPLIBNAME).$(SPPVERSION)
SHFLAGS = -shared -Wl,-soname,$(SPPLIB)
endif
endif
PPLIB = $(LIBPREF)postproc$(LIBSUF)

PPOBJS=postprocess.o
SPPOBJS=postprocess_pic.o

CFLAGS  = $(OPTFLAGS) $(MLIB_INC) -I. -I.. $(EXTRA_INC)
# -I/usr/X11R6/include/

.SUFFIXES: .c .o

# .PHONY: all clean

.c.o:
	$(CC) -c $(CFLAGS) $(LIBOBJFLAGS) -I$(SRC_PATH)/libavcodec -I../.. -o $@ $<

all:    $(SWSLIB) $(PPLIB) $(SPPLIBNAME)

clean:
	rm -f *.o *.a *~ *$(SLIBSUF) $(PPLIB) $(SPPLIBNAME)

distclean:
	rm -f Makefile.bak *.o *.a *~ *$(SLIBSUF) .depend

dep:    depend

depend:
	$(CC) -MM $(CFLAGS) postprocess.c 1>.depend

ifeq ($(SHARED_PP),yes)
postprocess_pic.o: postprocess.c
	$(CC) -c $(CFLAGS) -fomit-frame-pointer -fPIC -DPIC -I.. -I../.. -o $@ $<

$(SPPLIBNAME): $(SPPOBJS)
	$(CC) $(SHFLAGS) -o $(SPPLIBNAME) $(SPPOBJS)
endif

$(PPLIB): $(PPOBJS)
	rm -f $@
	$(AR) rc $@ $(PPOBJS)
	$(RANLIB) $@

install: all
ifeq ($(SHARED_PP),yes)
ifeq ($(CONFIG_WIN32),yes)
	install $(INSTALLSTRIP) -m 755 $(SPPLIBNAME) "$(prefix)"
else
	install -d $(libdir)
	install $(INSTALLSTRIP) -m 755 $(SPPLIBNAME) $(libdir)/$(SPPLIB)
	ln -sf $(SPPLIB) $(libdir)/$(SPPLIBNAME)
	ln -sf $(SPPLIB) $(libdir)/$(SPPLIBMAJOR)
	install -d $(libdir)/pkgconfig
	install -m 644 ../../libpostproc.pc $(libdir)/pkgconfig
	$(LDCONFIG) || true
endif
endif
	mkdir -p $(prefix)/include/postproc
	install -m 644 $(SRC_PATH)/libavcodec/libpostproc/postprocess.h $(prefix)/include/postproc/postprocess.h


#
# include dependency files if they exist
#
ifneq ($(wildcard .depend),)
include .depend
endif
