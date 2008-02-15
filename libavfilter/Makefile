include ../config.mak

CFLAGS+=-I$(SRC_PATH)/libavcodec -I$(SRC_PATH)/libswscale

OBJS = avfilter.o \
       avfiltergraph.o \
       avfiltergraphdesc.o \
       defaults.o \
       formats.o \

# TODO: real conditional compilation
OBJS-yes = vf_crop.o \
           vf_fifo.o \
           vf_fps.o \
           vf_hflip.o \
           vf_negate.o \
           vf_format.o \
           vf_overlay.o \
           vf_rotate.o \
           vf_scale.o \
           vf_setpts.o \
           vf_slicify.o \
           vf_split.o \
           vf_transpose.o \
           vf_vflip.o \
           avfiltergraphfile.o \

HEADERS = avfilter.h

EXTRALIBS := -L$(BUILD_ROOT)/libavutil -lavutil$(BUILDSUF) -L$(BUILD_ROOT)/libswscale -lswscale$(BUILDSUF) -L$(BUILD_ROOT)/libavcodec -lavcodec$(BUILDSUF) $(EXTRALIBS)

NAME=avfilter
LIBVERSION=$(LAVFILTERVERSION)
LIBMAJOR=$(LAVFILTERMAJOR)

include ../common.mak
