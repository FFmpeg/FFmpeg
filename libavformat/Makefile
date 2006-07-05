#
# libavformat Makefile
# (c) 2000-2003 Fabrice Bellard
#
include ../config.mak

CFLAGS=$(OPTFLAGS) -I.. -I$(SRC_PATH) -I$(SRC_PATH)/libavutil \
       -I$(SRC_PATH)/libavcodec -DHAVE_AV_CONFIG_H -D_FILE_OFFSET_BITS=64 \
       -D_LARGEFILE_SOURCE -D_GNU_SOURCE

OBJS= utils.o cutils.o os_support.o allformats.o
CPPOBJS=

HEADERS = avformat.h avio.h rtp.h rtsp.h rtspcodes.h

# demuxers
OBJS+=mpeg.o mpegts.o mpegtsenc.o ffm.o crc.o img.o img2.o raw.o rm.o \
      avienc.o avidec.o wav.o mmf.o swf.o au.o gif.o mov.o mpjpeg.o dv.o \
      yuv4mpeg.o 4xm.o flvdec.o psxstr.o idroq.o ipmovie.o \
      nut.o wc3movie.o mp3.o westwood.o segafilm.o idcin.o flic.o \
      sierravmd.o matroska.o sol.o electronicarts.o nsvdec.o asf.o \
      ogg2.o oggparsevorbis.o oggparsetheora.o oggparseflac.o daud.o aiff.o \
      voc.o tta.o mm.o avs.o smacker.o nuv.o gxf.o oggparseogm.o

# muxers
ifeq ($(CONFIG_MUXERS),yes)
OBJS+= flvenc.o movenc.o asf-enc.o adtsenc.o
endif


ifeq ($(AMR),yes)
OBJS+= amr.o
endif

# image formats
OBJS+= pnm.o yuv.o png.o jpeg.o gifdec.o sgi.o
OBJS+= framehook.o

ifeq ($(CONFIG_VIDEO4LINUX),yes)
OBJS+= grab.o
endif

ifeq ($(CONFIG_VIDEO4LINUX2),yes)
OBJS+= v4l2.o
endif

ifeq ($(CONFIG_BKTR),yes)
OBJS+= grab_bktr.o
endif

ifeq ($(CONFIG_DV1394),yes)
OBJS+= dv1394.o
endif

ifeq ($(CONFIG_DC1394),yes)
OBJS+= dc1394.o
endif

ifeq ($(CONFIG_AUDIO_OSS),yes)
OBJS+= audio.o
endif

EXTRALIBS := -L../libavutil -lavutil$(BUILDSUF) \
             -lavcodec$(BUILDSUF) -L../libavcodec $(EXTRALIBS)

ifeq ($(CONFIG_AUDIO_BEOS),yes)
CPPOBJS+= beosaudio.o
endif

# protocols I/O
OBJS+= avio.o aviobuf.o

ifeq ($(CONFIG_PROTOCOLS),yes)
OBJS+= file.o
ifeq ($(CONFIG_NETWORK),yes)
OBJS+= udp.o tcp.o http.o rtsp.o rtp.o rtpproto.o
# BeOS and Darwin network stuff
ifeq ($(NEED_INET_ATON),yes)
OBJS+= barpainet.o
endif
endif
endif

ifeq ($(CONFIG_LIBOGG),yes)
OBJS+= ogg.o
endif

NAME=avformat
ifeq ($(BUILD_SHARED),yes)
LIBVERSION=$(LAVFVERSION)
LIBMAJOR=$(LAVFMAJOR)
endif

include $(SRC_PATH)/common.mak
