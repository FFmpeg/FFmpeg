#
# libavformat Makefile
# (c) 2000, 2001, 2002 Fabrice Bellard
#
include ../config.mak

VPATH=$(SRC_PATH)/libavformat

CFLAGS= $(OPTFLAGS) -Wall -g -I.. -I$(SRC_PATH) -I$(SRC_PATH)/libavcodec -DHAVE_AV_CONFIG_H -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_GNU_SOURCE

OBJS= utils.o cutils.o allformats.o

# mux and demuxes
OBJS+=mpeg.o mpegts.o ffm.o crc.o img.o raw.o rm.o asf.o \
      avienc.o avidec.o wav.o swf.o au.o gif.o mov.o jpeg.o dv.o framehook.o
# file I/O
OBJS+= avio.o aviobuf.o file.o 

ifeq ($(BUILD_STRPTIME),yes)
OBJS+= strptime.o
endif

ifeq ($(CONFIG_VIDEO4LINUX),yes)
OBJS+= grab.o
endif

ifeq ($(CONFIG_AUDIO_OSS),yes)
OBJS+= audio.o 
endif

ifeq ($(CONFIG_AUDIO_BEOS),yes)
OBJS+= beosaudio.o
endif

ifeq ($(CONFIG_NETWORK),yes)
OBJS+= udp.o tcp.o http.o rtsp.o rtp.o rtpproto.o
# BeOS network stuff
ifeq ($(NEED_INET_ATON),yes)
OBJS+= barpainet.o
endif
endif

ifeq ($(CONFIG_VORBIS),yes)
OBJS+= ogg.o
endif

LIB= libavformat.a

all: $(LIB)

$(LIB): $(OBJS)
	rm -f $@
	$(AR) rc $@ $(OBJS)
	$(RANLIB) $@

installlib: all
	install -m 644 $(LIB) $(prefix)/lib
	mkdir -p $(prefix)/include/ffmpeg
	install -m 644 $(SRC_PATH)/libav/avformat.h $(SRC_PATH)/libav/avio.h \
                $(SRC_PATH)/libav/rtp.h $(SRC_PATH)/libav/rtsp.h \
                $(SRC_PATH)/libav/rtspcodes.h \
                $(prefix)/include/ffmpeg

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $< 

# BeOS: remove -Wall to get rid of all the "multibyte constant" warnings
%.o: %.cpp
	g++ $(subst -Wall,,$(CFLAGS)) -c -o $@ $< 

clean: 
	rm -f *.o *~ *.a 
