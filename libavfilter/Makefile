include $(SUBDIR)../config.mak

NAME = avfilter
FFLIBS = avcodec avutil swscale
FFLIBS-$(CONFIG_AVFILTER_LAVF) += avformat

HEADERS = avfilter.h

OBJS = allfilters.o                                                     \
       avfilter.o                                                       \
       defaults.o                                                       \
       formats.o                                                        \

#OBJS-$(CONFIG_XXX_FILTER)    += vf_xxx.o

include $(SUBDIR)../subdir.mak
