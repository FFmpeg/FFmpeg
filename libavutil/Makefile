#
# libavutil Makefile
#
include ../config.mak

VPATH=$(SRC_PATH)/libavutil

# NOTE: -I.. is needed to include config.h
CFLAGS=$(OPTFLAGS) -DHAVE_AV_CONFIG_H -DBUILD_AVUTIL -I.. -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_GNU_SOURCE

#FIXME: This should be in configure/config.mak
ifeq ($(CONFIG_WIN32),yes)
    LDFLAGS=-Wl,--output-def,$(@:.dll=.def),--out-implib,lib$(SLIBNAME:$(SLIBSUF)=.dll.a)
endif

OBJS= mathematics.o \
      integer.o \
      rational.o \
      intfloat_readwrite.o \


ifeq ($(TARGET_ARCH_SPARC64),yes)
CFLAGS+= -mcpu=ultrasparc -mtune=ultrasparc
endif

SRCS := $(OBJS:.o=.c)

NAME=avutil
ifeq ($(BUILD_SHARED),yes)
LIBVERSION=$(LAVUVERSION)
LIBMAJOR=$(LAVUMAJOR)
endif

all: $(LIB) $(SLIBNAME)

$(LIB): $(OBJS)
	rm -f $@
	$(AR) rc $@ $(OBJS)
	$(RANLIB) $@

$(SLIBNAME): $(OBJS)
	$(CC) $(SHFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(EXTRALIBS) $(AMREXTRALIBS)
ifeq ($(CONFIG_WIN32),yes)
	-lib /machine:i386 /def:$(@:.dll=.def)
endif

%.o: %.c
	$(CC) $(CFLAGS) $(LIBOBJFLAGS) -c -o $@ $<

depend: $(SRCS)
	$(CC) -MM $(CFLAGS) $^ 1>.depend

dep:	depend

clean:
	rm -f *.o *.d *~ *.a *.lib *.so *.dylib *.dll \
	      *.lib *.def *.dll.a *.exp

distclean: clean
	rm -f .depend


ifeq ($(BUILD_SHARED),yes)
install: all install-headers
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
else
install:
endif

installlib: all install-headers
	install -m 644 $(LIB) "$(libdir)"

install-headers:
	mkdir -p "$(prefix)/include/ffmpeg"
	install -m 644 $(SRC_PATH)/libavutil/avutil.h \
	               $(SRC_PATH)/libavutil/common.h \
	               $(SRC_PATH)/libavutil/mathematics.h \
	               $(SRC_PATH)/libavutil/integer.h \
	               $(SRC_PATH)/libavutil/rational.h \
	               $(SRC_PATH)/libavutil/intfloat_readwrite.h \
                "$(prefix)/include/ffmpeg"
	install -d "$(libdir)/pkgconfig"
	install -m 644 ../libavutil.pc "$(libdir)/pkgconfig"

#
# include dependency files if they exist
#
ifneq ($(wildcard .depend),)
include .depend
endif
