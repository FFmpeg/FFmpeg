#
# libavformat Makefile
# (c) 2000, 2001, 2002 Fabrice Bellard
#
include ../config.mak

VPATH=$(SRC_PATH)/libavformat

CFLAGS= $(OPTFLAGS) -Wall -g -I.. -I$(SRC_PATH) -I$(SRC_PATH)/libavcodec -DHAVE_AV_CONFIG_H -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_GNU_SOURCE

OBJS= utils.o cutils.o allformats.o
PPOBJS=

# mux and demuxes
OBJS+=mpeg.o mpegts.o ffm.o crc.o img.o raw.o rm.o \
      avienc.o avidec.o wav.o swf.o au.o gif.o mov.o mpjpeg.o dv.o \
      yuv4mpeg.o 4xm.o

ifeq ($(CONFIG_RISKY),yes)
OBJS+= asf.o
endif

# image formats
OBJS+= pnm.o yuv.o png.o jpeg.o gifdec.o
# file I/O
OBJS+= avio.o aviobuf.o file.o 
OBJS+= framehook.o 

ifeq ($(BUILD_STRPTIME),yes)
OBJS+= strptime.o
endif

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

LIB= $(LIBPREF)avformat$(LIBSUF)

SRCS := $(OBJS:.o=.c) $(PPOBJS:.o=.cpp)

all: $(LIB)

$(LIB): $(OBJS) $(PPOBJS)
	rm -f $@
	$(AR) rc $@ $(OBJS) $(PPOBJS)
	$(RANLIB) $@

depend: $(SRCS)
	$(CC) -MM $(CFLAGS) $^ 1>.depend

installlib: all
	install -m 644 $(LIB) $(prefix)/lib
	mkdir -p $(prefix)/include/ffmpeg
	install -m 644 $(SRC_PATH)/libavformat/avformat.h $(SRC_PATH)/libavformat/avio.h \
                $(SRC_PATH)/libavformat/rtp.h $(SRC_PATH)/libavformat/rtsp.h \
                $(SRC_PATH)/libavformat/rtspcodes.h \
                $(prefix)/include/ffmpeg

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $< 

# BeOS: remove -Wall to get rid of all the "multibyte constant" warnings
%.o: %.cpp
	g++ $(subst -Wall,,$(CFLAGS)) -c -o $@ $< 

clean: 
	rm -f *.o *.d .depend *~ *.a $(LIB)

#
# include dependency files if they exist
#
ifneq ($(wildcard .depend),)
include .depend
endif
