include ../config.mak

NAME=avdevice
LIBVERSION=$(LAVDVERSION)
LIBMAJOR=$(LAVDMAJOR)

EXTRALIBS := -L$(BUILD_ROOT)/libavutil   -lavutil$(BUILDSUF)  \
             -L$(BUILD_ROOT)/libavcodec  -lavcodec$(BUILDSUF) \
             -L$(BUILD_ROOT)/libavformat -lavformat$(BUILDSUF) $(EXTRALIBS)

CFLAGS += -I$(SRC_PATH)/libavcodec -I$(SRC_PATH)/libavformat

OBJS = alldevices.o

HEADERS = avdevice.h

# input/output devices
OBJS-$(CONFIG_BKTR_DEMUXER)              += bktr.o
OBJS-$(CONFIG_DV1394_DEMUXER)            += dv1394.o
OBJS-$(CONFIG_OSS_DEMUXER)               += audio.o
OBJS-$(CONFIG_OSS_MUXER)                 += audio.o
OBJS-$(CONFIG_V4L2_DEMUXER)              += v4l2.o
OBJS-$(CONFIG_V4L_DEMUXER)               += v4l.o
OBJS-$(CONFIG_X11_GRAB_DEVICE_DEMUXER)   += x11grab.o

# external libraries
OBJS-$(CONFIG_LIBDC1394_DEMUXER)         += libdc1394.o

CPPOBJS-$(CONFIG_AUDIO_BEOS_DEMUXER)     += beosaudio.o
CPPOBJS-$(CONFIG_AUDIO_BEOS_MUXER)       += beosaudio.o

include ../common.mak
