#
# libavformat Makefile
# (c) 2000-2003 Fabrice Bellard
#
include ../config.mak

VPATH=$(SRC_PATH)/libavformat

CFLAGS=$(OPTFLAGS) -I.. -I$(SRC_PATH) -I$(SRC_PATH)/libavcodec -DHAVE_AV_CONFIG_H -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_GNU_SOURCE

OBJS= utils.o cutils.o os_support.o allformats.o
PPOBJS=

# mux and demuxes
OBJS+=mpeg.o mpegts.o mpegtsenc.o ffm.o crc.o img.o img2.o raw.o rm.o \
      avienc.o avidec.o wav.o swf.o au.o gif.o mov.o mpjpeg.o dv.o \
      yuv4mpeg.o 4xm.o flvenc.o flvdec.o movenc.o psxstr.o idroq.o ipmovie.o \
      nut.o wc3movie.o mp3.o westwood.o segafilm.o idcin.o flic.o \
      sierravmd.o matroska.o

ifeq ($(CONFIG_RISKY),yes)
OBJS+= asf.o
OBJS+= asf-enc.o
endif

ifeq ($(AMR_NB),yes)
OBJS+= amr.o
endif
ifeq ($(AMR_NB_FIXED),yes)
OBJS+= amr.o
endif
ifeq ($(AMR_WB),yes)
OBJS+= amr.o
endif

# image formats
OBJS+= pnm.o yuv.o png.o jpeg.o gifdec.o sgi.o
# file I/O
OBJS+= avio.o aviobuf.o file.o 
OBJS+= framehook.o 

ifeq ($(CONFIG_VIDEO4LINUX),yes)
OBJS+= grab.o
endif

ifeq ($(CONFIG_DV1394),yes)
OBJS+= dv1394.o
endif

ifeq ($(CONFIG_AUDIO_OSS),yes)
OBJS+= audio.o 
endif

ifeq ($(CONFIG_AUDIO_BEOS),yes)
PPOBJS+= beosaudio.o
EXTRALIBS+=-lbe -lmedia
# this should be the default !
EXTRALIBS+=-lavcodec -L../libavcodec
endif

ifeq ($(CONFIG_NETWORK),yes)
OBJS+= udp.o tcp.o http.o rtsp.o rtp.o rtpproto.o
# BeOS and Darwin network stuff
ifeq ($(NEED_INET_ATON),yes)
OBJS+= barpainet.o
endif
endif

ifeq ($(CONFIG_VORBIS),yes)
OBJS+= ogg.o
endif

ifeq ($(TARGET_ARCH_SPARC64),yes)
CFLAGS+= -mcpu=ultrasparc -mtune=ultrasparc
endif

LIB= $(LIBPREF)avformat$(LIBSUF)
ifeq ($(BUILD_SHARED),yes)
SLIB= $(SLIBPREF)avformat$(SLIBSUF)
endif

SRCS := $(OBJS:.o=.c) $(PPOBJS:.o=.cpp)

all: $(LIB) $(SLIB)

$(LIB): $(OBJS) $(PPOBJS)
	rm -f $@
	$(AR) rc $@ $(OBJS) $(PPOBJS)
	$(RANLIB) $@

$(SLIB): $(OBJS)
ifeq ($(CONFIG_WIN32),yes)
	$(CC) $(SHFLAGS) -Wl,--output-def,$(@:.dll=.def) -o $@ $(OBJS) $(PPOBJS) $(EXTRALIBS) $(VPATH)/../libavcodec/avcodec.dll
	-lib /machine:i386 /def:$(@:.dll=.def)
else
	$(CC) $(SHFLAGS) -o $@ $(OBJS) $(PPOBJS) $(EXTRALIBS)
endif

depend: $(SRCS)
	$(CC) -MM $(CFLAGS) $^ 1>.depend

ifeq ($(BUILD_SHARED),yes)
install: all install-headers
ifeq ($(CONFIG_WIN32),yes)
	install -s -m 755 $(SLIB) "$(prefix)"
else
	install -d $(prefix)/lib
	install -s -m 755 $(SLIB) $(prefix)/lib/libavformat-$(VERSION).so
	ln -sf libavformat-$(VERSION).so $(prefix)/lib/libavformat.so
	ldconfig || true
endif
else
install:
endif

installlib: all install-headers
	install -m 644 $(LIB) $(prefix)/lib

install-headers:
	mkdir -p "$(prefix)/include/ffmpeg"
	install -m 644 $(SRC_PATH)/libavformat/avformat.h $(SRC_PATH)/libavformat/avio.h \
                $(SRC_PATH)/libavformat/rtp.h $(SRC_PATH)/libavformat/rtsp.h \
                $(SRC_PATH)/libavformat/rtspcodes.h \
                "$(prefix)/include/ffmpeg"

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $< 

# BeOS: remove -Wall to get rid of all the "multibyte constant" warnings
%.o: %.cpp
	g++ $(subst -Wall,,$(CFLAGS)) -c -o $@ $< 

distclean clean: 
	rm -f *.o *.d .depend *~ *.a $(LIB)

#
# include dependency files if they exist
#
ifneq ($(wildcard .depend),)
include .depend
endif
