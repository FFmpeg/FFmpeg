include $(SUBDIR)../config.mak

NAME    = avdevice

HEADERS = avdevice.h                                                    \
          version.h                                                     \

OBJS    = alldevices.o                                                  \
          avdevice.o                                                    \
          utils.o                                                       \

OBJS-$(HAVE_LIBC_MSVCRT)                 += file_open.o

# input/output devices
OBJS-$(CONFIG_ALSA_INDEV)                += alsa_dec.o alsa.o timefilter.o
OBJS-$(CONFIG_ALSA_OUTDEV)               += alsa_enc.o alsa.o
OBJS-$(CONFIG_AVFOUNDATION_INDEV)        += avfoundation.o
OBJS-$(CONFIG_BKTR_INDEV)                += bktr.o
OBJS-$(CONFIG_CACA_OUTDEV)               += caca.o
OBJS-$(CONFIG_DECKLINK_OUTDEV)           += decklink_enc.o decklink_enc_c.o decklink_common.o
OBJS-$(CONFIG_DECKLINK_INDEV)            += decklink_dec.o decklink_dec_c.o decklink_common.o
OBJS-$(CONFIG_DSHOW_INDEV)               += dshow_crossbar.o dshow.o dshow_enummediatypes.o \
                                            dshow_enumpins.o dshow_filter.o \
                                            dshow_pin.o dshow_common.o
OBJS-$(CONFIG_DV1394_INDEV)              += dv1394.o
OBJS-$(CONFIG_FBDEV_INDEV)               += fbdev_dec.o \
                                            fbdev_common.o
OBJS-$(CONFIG_FBDEV_OUTDEV)              += fbdev_enc.o \
                                            fbdev_common.o
OBJS-$(CONFIG_GDIGRAB_INDEV)             += gdigrab.o
OBJS-$(CONFIG_IEC61883_INDEV)            += iec61883.o
OBJS-$(CONFIG_JACK_INDEV)                += jack.o timefilter.o
OBJS-$(CONFIG_LAVFI_INDEV)               += lavfi.o
OBJS-$(CONFIG_OPENAL_INDEV)              += openal-dec.o
OBJS-$(CONFIG_OPENGL_OUTDEV)             += opengl_enc.o
OBJS-$(CONFIG_OSS_INDEV)                 += oss_dec.o oss.o
OBJS-$(CONFIG_OSS_OUTDEV)                += oss_enc.o oss.o
OBJS-$(CONFIG_PULSE_INDEV)               += pulse_audio_dec.o \
                                            pulse_audio_common.o timefilter.o
OBJS-$(CONFIG_PULSE_OUTDEV)              += pulse_audio_enc.o \
                                            pulse_audio_common.o
OBJS-$(CONFIG_QTKIT_INDEV)               += qtkit.o
OBJS-$(CONFIG_SDL2_OUTDEV)               += sdl2.o
OBJS-$(CONFIG_SNDIO_INDEV)               += sndio_dec.o sndio.o
OBJS-$(CONFIG_SNDIO_OUTDEV)              += sndio_enc.o sndio.o
OBJS-$(CONFIG_V4L2_INDEV)                += v4l2.o v4l2-common.o timefilter.o
OBJS-$(CONFIG_V4L2_OUTDEV)               += v4l2enc.o v4l2-common.o
OBJS-$(CONFIG_V4L_INDEV)                 += v4l.o
OBJS-$(CONFIG_VFWCAP_INDEV)              += vfwcap.o
OBJS-$(CONFIG_XCBGRAB_INDEV)             += xcbgrab.o
OBJS-$(CONFIG_XV_OUTDEV)                 += xv.o

# external libraries
OBJS-$(CONFIG_LIBCDIO_INDEV)             += libcdio.o
OBJS-$(CONFIG_LIBDC1394_INDEV)           += libdc1394.o

# Windows resource file
SLIBOBJS-$(HAVE_GNU_WINDRES)             += avdeviceres.o

SKIPHEADERS                              += decklink_common.h
SKIPHEADERS-$(CONFIG_DECKLINK)           += decklink_enc.h decklink_dec.h \
                                            decklink_common_c.h
SKIPHEADERS-$(CONFIG_DSHOW_INDEV)        += dshow_capture.h
SKIPHEADERS-$(CONFIG_FBDEV_INDEV)        += fbdev_common.h
SKIPHEADERS-$(CONFIG_FBDEV_OUTDEV)       += fbdev_common.h
SKIPHEADERS-$(CONFIG_LIBPULSE)           += pulse_audio_common.h
SKIPHEADERS-$(CONFIG_V4L2_INDEV)         += v4l2-common.h
SKIPHEADERS-$(CONFIG_V4L2_OUTDEV)        += v4l2-common.h
SKIPHEADERS-$(HAVE_ALSA_ASOUNDLIB_H)     += alsa.h
SKIPHEADERS-$(HAVE_SNDIO_H)              += sndio.h

TESTPROGS-$(CONFIG_JACK_INDEV)           += timefilter
