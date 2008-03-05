include ../config.mak

CFLAGS+=-I$(SRC_PATH)/libavformat -I$(SRC_PATH)/libavcodec -I$(SRC_PATH)/libswscale

OBJS = allfilters.o \
       avfilter.o \
       defaults.o \
       formats.o \

#OBJS-$(CONFIG_XXX_FILTER)    += vf_xxx.o

HEADERS = avfilter.h

EXTRALIBS := -L$(BUILD_ROOT)/libavcodec -lavcodec$(BUILDSUF) \
             -L$(BUILD_ROOT)/libavutil  -lavutil$(BUILDSUF) $(EXTRALIBS)

ifeq ($(CONFIG_SWSCALER),yes)
EXTRALIBS+=-L$(BUILD_ROOT)/libswscale
EXTRALIBS+=-lswscale$(BUILDSUF)
endif

ifeq ($(CONFIG_AVFILTER_LAVF),yes)
EXTRALIBS := -L$(BUILD_ROOT)/libavformat -lavformat$(BUILDSUF) $(EXTRALIBS)
endif

NAME=avfilter
LIBVERSION=$(LAVFILTERVERSION)
LIBMAJOR=$(LAVFILTERMAJOR)

include ../common.mak
