NAME    = avdevice

HEADERS = avdevice.h                                                    \
          version.h                                                     \

OBJS    = alldevices.o                                                  \
          avdevice.o                                                    \

OBJS-$(HAVE_LIBC_MSVCRT)                 += file_open.o

# input/output devices
OBJS-$(CONFIG_ALSA_INDEV)                += alsa_dec.o alsa.o
OBJS-$(CONFIG_ALSA_OUTDEV)               += alsa_enc.o alsa.o
OBJS-$(CONFIG_AVFOUNDATION_INDEV)        += avfoundation_dec.o
OBJS-$(CONFIG_BKTR_INDEV)                += bktr.o
OBJS-$(CONFIG_DV1394_INDEV)              += dv1394.o
OBJS-$(CONFIG_FBDEV_INDEV)               += fbdev.o
OBJS-$(CONFIG_JACK_INDEV)                += jack.o timefilter.o
OBJS-$(CONFIG_OSS_INDEV)                 += oss_dec.o oss.o
OBJS-$(CONFIG_OSS_OUTDEV)                += oss_enc.o oss.o
OBJS-$(CONFIG_PULSE_INDEV)               += pulse.o
OBJS-$(CONFIG_SNDIO_INDEV)               += sndio_dec.o sndio.o
OBJS-$(CONFIG_SNDIO_OUTDEV)              += sndio_enc.o sndio.o
OBJS-$(CONFIG_V4L2_INDEV)                += v4l2.o
OBJS-$(CONFIG_VFWCAP_INDEV)              += vfwcap.o
OBJS-$(CONFIG_XCBGRAB_INDEV)             += xcbgrab.o

# external libraries
OBJS-$(CONFIG_LIBCDIO_INDEV)             += libcdio.o
OBJS-$(CONFIG_LIBDC1394_INDEV)           += libdc1394.o

SKIPHEADERS-$(HAVE_ALSA_ASOUNDLIB_H)     += alsa.h
SKIPHEADERS-$(HAVE_SNDIO_H)              += sndio.h

TESTPROGS-$(CONFIG_JACK_INDEV)           += timefilter
