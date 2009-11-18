include $(SUBDIR)../config.mak

NAME    = avdevice
FFLIBS  = avformat avcodec avutil

HEADERS = avdevice.h

OBJS    = alldevices.o avdevice.o

# input/output devices
OBJS-$(CONFIG_ALSA_INDEV)                += alsa-audio-common.o \
                                            alsa-audio-dec.o
OBJS-$(CONFIG_ALSA_OUTDEV)               += alsa-audio-common.o \
                                            alsa-audio-enc.o
OBJS-$(CONFIG_BKTR_INDEV)                += bktr.o
OBJS-$(CONFIG_DV1394_INDEV)              += dv1394.o
OBJS-$(CONFIG_JACK_INDEV)                += jack_audio.o
OBJS-$(CONFIG_OSS_INDEV)                 += oss_audio.o
OBJS-$(CONFIG_OSS_OUTDEV)                += oss_audio.o
OBJS-$(CONFIG_V4L2_INDEV)                += v4l2.o
OBJS-$(CONFIG_V4L_INDEV)                 += v4l.o
OBJS-$(CONFIG_VFWCAP_INDEV)              += vfwcap.o
OBJS-$(CONFIG_X11_GRAB_DEVICE_INDEV)     += x11grab.o

# external libraries
OBJS-$(CONFIG_LIBDC1394_INDEV)           += libdc1394.o

OBJS-$(CONFIG_AUDIO_BEOS_INDEV)          += beosaudio.o
OBJS-$(CONFIG_AUDIO_BEOS_OUTDEV)         += beosaudio.o

SKIPHEADERS-$(HAVE_ALSA_ASOUNDLIB_H)     += alsa-audio.h

include $(SUBDIR)../subdir.mak
