include $(SUBDIR)../config.mak

NAME = avfilter
FFLIBS = avcodec avcore avutil
FFLIBS-$(CONFIG_SCALE_FILTER) += swscale

HEADERS = avfilter.h

OBJS = allfilters.o                                                     \
       avfilter.o                                                       \
       avfiltergraph.o                                                  \
       defaults.o                                                       \
       formats.o                                                        \
       graphparser.o                                                    \
       parseutils.o                                                     \

OBJS-$(CONFIG_ANULL_FILTER)                  += af_anull.o

OBJS-$(CONFIG_ANULLSRC_FILTER)               += asrc_anullsrc.o

OBJS-$(CONFIG_ANULLSINK_FILTER)              += asink_anullsink.o

OBJS-$(CONFIG_ASPECT_FILTER)                 += vf_aspect.o
OBJS-$(CONFIG_BLACKFRAME_FILTER)             += vf_blackframe.o
OBJS-$(CONFIG_CROP_FILTER)                   += vf_crop.o
OBJS-$(CONFIG_FIFO_FILTER)                   += vf_fifo.o
OBJS-$(CONFIG_FORMAT_FILTER)                 += vf_format.o
OBJS-$(CONFIG_FREI0R_FILTER)                 += vf_frei0r.o
OBJS-$(CONFIG_HFLIP_FILTER)                  += vf_hflip.o
OBJS-$(CONFIG_NOFORMAT_FILTER)               += vf_format.o
OBJS-$(CONFIG_NULL_FILTER)                   += vf_null.o
OBJS-$(CONFIG_OCV_SMOOTH_FILTER)             += vf_libopencv.o
OBJS-$(CONFIG_PAD_FILTER)                    += vf_pad.o
OBJS-$(CONFIG_PIXDESCTEST_FILTER)            += vf_pixdesctest.o
OBJS-$(CONFIG_PIXELASPECT_FILTER)            += vf_aspect.o
OBJS-$(CONFIG_SCALE_FILTER)                  += vf_scale.o
OBJS-$(CONFIG_SLICIFY_FILTER)                += vf_slicify.o
OBJS-$(CONFIG_UNSHARP_FILTER)                += vf_unsharp.o
OBJS-$(CONFIG_VFLIP_FILTER)                  += vf_vflip.o
OBJS-$(CONFIG_YADIF_FILTER)                  += vf_yadif.o

OBJS-$(CONFIG_BUFFER_FILTER)                 += vsrc_buffer.o
OBJS-$(CONFIG_COLOR_FILTER)                  += vf_pad.o
OBJS-$(CONFIG_NULLSRC_FILTER)                += vsrc_nullsrc.o

OBJS-$(CONFIG_NULLSINK_FILTER)               += vsink_nullsink.o

-include $(SUBDIR)$(ARCH)/Makefile

DIRS = x86

include $(SUBDIR)../subdir.mak
