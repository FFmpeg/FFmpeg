include $(SUBDIR)../config.mak

NAME    = avdevice
FFLIBS  = avformat avcodec avutil
FFLIBS-$(CONFIG_LAVFI_INDEV) += avfilter

HEADERS = avdevice.h                                                    \
          version.h                                                     \

OBJS    = alldevices.o                                                  \
          avdevice.o                                                    \

OBJS-$(HAVE_MSVCRT) += file_open.o

# input/output devices
OBJS-$(CONFIG_ALSA_INDEV)                += alsa-audio-common.o \
                                            alsa-audio-dec.o timefilter.o
OBJS-$(CONFIG_ALSA_OUTDEV)               += alsa-audio-common.o \
                                            alsa-audio-enc.o
OBJS-$(CONFIG_BKTR_INDEV)                += bktr.o
OBJS-$(CONFIG_CACA_OUTDEV)               += caca.o
OBJS-$(CONFIG_DSHOW_INDEV)               += dshow.o dshow_enummediatypes.o \
                                            dshow_enumpins.o dshow_filter.o \
                                            dshow_pin.o dshow_common.o
OBJS-$(CONFIG_DV1394_INDEV)              += dv1394.o
OBJS-$(CONFIG_FBDEV_INDEV)               += fbdev.o
OBJS-$(CONFIG_IEC61883_INDEV)            += iec61883.o
OBJS-$(CONFIG_JACK_INDEV)                += jack_audio.o timefilter.o
OBJS-$(CONFIG_LAVFI_INDEV)               += lavfi.o
OBJS-$(CONFIG_OPENAL_INDEV)              += openal-dec.o
OBJS-$(CONFIG_OSS_INDEV)                 += oss_audio.o
OBJS-$(CONFIG_OSS_OUTDEV)                += oss_audio.o
OBJS-$(CONFIG_PULSE_INDEV)               += pulse_audio_dec.o \
                                            pulse_audio_common.o
OBJS-$(CONFIG_PULSE_OUTDEV)              += pulse_audio_enc.o \
                                            pulse_audio_common.o
OBJS-$(CONFIG_SDL_OUTDEV)                += sdl.o
OBJS-$(CONFIG_SNDIO_INDEV)               += sndio_common.o sndio_dec.o
OBJS-$(CONFIG_SNDIO_OUTDEV)              += sndio_common.o sndio_enc.o
OBJS-$(CONFIG_V4L2_INDEV)                += v4l2.o v4l2-common.o timefilter.o
OBJS-$(CONFIG_V4L2_OUTDEV)               += v4l2enc.o v4l2-common.o
OBJS-$(CONFIG_V4L_INDEV)                 += v4l.o
OBJS-$(CONFIG_VFWCAP_INDEV)              += vfwcap.o
OBJS-$(CONFIG_X11GRAB_INDEV)             += x11grab.o
OBJS-$(CONFIG_XV_OUTDEV)                 += xv.o

# external libraries
OBJS-$(CONFIG_LIBCDIO_INDEV)             += libcdio.o
OBJS-$(CONFIG_LIBDC1394_INDEV)           += libdc1394.o

SKIPHEADERS-$(CONFIG_DSHOW_INDEV)        += dshow_capture.h
SKIPHEADERS-$(CONFIG_LIBPULSE)           += pulse_audio_common.h
SKIPHEADERS-$(CONFIG_V4L2_INDEV)         += v4l2-common.h
SKIPHEADERS-$(CONFIG_V4L2_OUTDEV)        += v4l2-common.h
SKIPHEADERS-$(HAVE_ALSA_ASOUNDLIB_H)     += alsa-audio.h
SKIPHEADERS-$(HAVE_SNDIO_H)              += sndio_common.h

TESTPROGS = timefilter
