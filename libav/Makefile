#
# libavformat Makefile
# (c) 2000, 2001, 2002 Fabrice Bellard
#
include ../config.mak

VPATH=$(SRC_PATH)/libav

CFLAGS= $(OPTFLAGS) -Wall -g -I.. -I$(SRC_PATH) -I$(SRC_PATH)/libavcodec -DHAVE_AV_CONFIG_H -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_GNU_SOURCE

OBJS= utils.o 

# mux and demuxes
OBJS+=mpeg.o mpegts.o ffm.o crc.o img.o raw.o rm.o asf.o \
      avienc.o avidec.o wav.o swf.o au.o gif.o mov.o jpeg.o
# file I/O
OBJS+= avio.o aviobuf.o file.o 

ifeq ($(CONFIG_VIDEO4LINUX),yes)
OBJS+= grab.o
endif

ifeq ($(CONFIG_AUDIO_OSS),yes)
OBJS+= audio.o 
endif

ifeq ($(CONFIG_NETWORK),yes)
OBJS+= udp.o tcp.o http.o rtsp.o rtp.o rtpproto.o
endif

LIB= libavformat.a

all: $(LIB)

$(LIB): $(OBJS)
	rm -f $@
	$(AR) rcs $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $< 

clean: 
	rm -f *.o *~ *.a 
