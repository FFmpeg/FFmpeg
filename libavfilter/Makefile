include ../config.mak

OBJS = allfilters.o \
       avfilter.o \
       defaults.o \
       formats.o \

#OBJS-$(CONFIG_XXX_FILTER)    += vf_xxx.o

HEADERS = avfilter.h

FFLIBS = avcodec avutil

FFLIBS-$(CONFIG_SWSCALER)      += swscale
FFLIBS-$(CONFIG_AVFILTER_LAVF) += avformat

NAME=avfilter

include ../common.mak
