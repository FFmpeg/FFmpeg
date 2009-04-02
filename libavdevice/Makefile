include $(SUBDIR)../config.mak

NAME    = avdevice
FFLIBS  = avformat avcodec avutil

HEADERS = avdevice.h

OBJS    = alldevices.o

# input/output devices
OBJS-$(CONFIG_ALSA_DEMUXER)              += alsa-audio-common.o alsa-audio-dec.o
OBJS-$(CONFIG_ALSA_MUXER)                += alsa-audio-common.o alsa-audio-enc.o
OBJS-$(CONFIG_BKTR_DEMUXER)              += bktr.o
OBJS-$(CONFIG_DV1394_DEMUXER)            += dv1394.o
OBJS-$(CONFIG_JACK_DEMUXER)              += jack_audio.o
OBJS-$(CONFIG_OSS_DEMUXER)               += oss_audio.o
OBJS-$(CONFIG_OSS_MUXER)                 += oss_audio.o
OBJS-$(CONFIG_V4L2_DEMUXER)              += v4l2.o
OBJS-$(CONFIG_V4L_DEMUXER)               += v4l.o
OBJS-$(CONFIG_VFWCAP_DEMUXER)            += vfwcap.o
OBJS-$(CONFIG_X11_GRAB_DEVICE_DEMUXER)   += x11grab.o

# external libraries
OBJS-$(CONFIG_LIBDC1394_DEMUXER)         += libdc1394.o

OBJS-$(CONFIG_AUDIO_BEOS_DEMUXER)        += beosaudio.o
OBJS-$(CONFIG_AUDIO_BEOS_MUXER)          += beosaudio.o

include $(SUBDIR)../subdir.mak
