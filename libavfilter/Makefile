include $(SUBDIR)../config.mak

NAME = avfilter
FFLIBS = avcodec avutil swscale
FFLIBS-$(CONFIG_AVFILTER_LAVF) += avformat

HEADERS = avfilter.h

OBJS = allfilters.o                                                     \
       avfilter.o                                                       \
       defaults.o                                                       \
       formats.o                                                        \
       parseutils.o                                                     \

OBJS-$(CONFIG_CROP_FILTER)                   += vf_crop.o
OBJS-$(CONFIG_FORMAT_FILTER)                 += vf_format.o
OBJS-$(CONFIG_NOFORMAT_FILTER)               += vf_format.o
OBJS-$(CONFIG_NULL_FILTER)                   += vf_null.o
OBJS-$(CONFIG_SCALE_FILTER)                  += vf_scale.o
OBJS-$(CONFIG_SLICIFY_FILTER)                += vf_slicify.o
OBJS-$(CONFIG_VFLIP_FILTER)                  += vf_vflip.o

include $(SUBDIR)../subdir.mak
