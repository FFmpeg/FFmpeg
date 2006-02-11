
include ../../config.mak

VPATH=$(SRC_PATH)/libavcodec/libpostproc

NAME=postproc
ifeq ($(BUILD_SHARED),yes)
LIBVERSION=$(SPPVERSION)
LIBMAJOR=$(SPPMAJOR)
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
	rm -f *.o *.d *~ *.a *.lib *.so *.dylib *.dll

distclean: clean
	rm -f .depend

dep:    depend

depend:
	$(CC) -MM $(CFLAGS) postprocess.c 1>.depend

ifeq ($(BUILD_SHARED),yes)
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
ifeq ($(BUILD_SHARED),yes)
ifeq ($(CONFIG_WIN32),yes)
	install $(INSTALLSTRIP) -m 755 $(SLIBNAME) "$(prefix)"
else
	install -d $(libdir)
	install $(INSTALLSTRIP) -m 755 $(SLIBNAME) \
		$(libdir)/$(SLIBNAME_WITH_VERSION)
	ln -sf $(SLIBNAME_WITH_VERSION) \
		$(libdir)/$(SLIBNAME_WITH_MAJOR)
	ln -sf $(SLIBNAME_WITH_VERSION) \
		$(libdir)/$(SLIBNAME)
	$(LDCONFIG) || true
endif
endif

installlib: all
	install -m 644 $(LIB) "$(libdir)"

install-headers:
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
