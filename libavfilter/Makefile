include $(SUBDIR)../config.mak

NAME = avfilter
FFLIBS = avcodec avutil swscale
FFLIBS-$(CONFIG_AVFILTER_LAVF) += avformat

HEADERS = avfilter.h

OBJS = allfilters.o                                                     \
       avfilter.o                                                       \
       avfiltergraph.o                                                  \
       defaults.o                                                       \
       formats.o                                                        \
       graphparser.o                                                    \
       parseutils.o                                                     \

OBJS-$(CONFIG_ASPECT_FILTER)                 += vf_aspect.o
OBJS-$(CONFIG_CROP_FILTER)                   += vf_crop.o
OBJS-$(CONFIG_FORMAT_FILTER)                 += vf_format.o
OBJS-$(CONFIG_NOFORMAT_FILTER)               += vf_format.o
OBJS-$(CONFIG_NULL_FILTER)                   += vf_null.o
OBJS-$(CONFIG_PIXELASPECT_FILTER)            += vf_aspect.o
OBJS-$(CONFIG_SCALE_FILTER)                  += vf_scale.o
OBJS-$(CONFIG_SLICIFY_FILTER)                += vf_slicify.o
OBJS-$(CONFIG_UNSHARP_FILTER)                += vf_unsharp.o
OBJS-$(CONFIG_VFLIP_FILTER)                  += vf_vflip.o

OBJS-$(CONFIG_NULLSRC_FILTER)                += vsrc_nullsrc.o

OBJS-$(CONFIG_NULLSINK_FILTER)               += vsink_nullsink.o

include $(SUBDIR)../subdir.mak
