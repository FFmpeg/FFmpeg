include $(SUBDIR)../config.mak

NAME = avfilter
FFLIBS = avcodec avutil swscale
FFLIBS-$(CONFIG_AVFILTER_LAVF) += avformat

HEADERS = avfilter.h

OBJS = allfilters.o                                                     \
       avfilter.o                                                       \
       defaults.o                                                       \
       formats.o                                                        \

OBJS-$(CONFIG_CROP_FILTER)    += vf_crop.o
OBJS-$(CONFIG_NULL_FILTER)    += vf_null.o

include $(SUBDIR)../subdir.mak
