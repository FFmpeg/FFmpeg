include ../config.mak

CFLAGS+=-I$(SRC_PATH)/libavcodec -I$(SRC_PATH)/libswscale

OBJS = avfilter.o \
       avfiltergraph.o \
       avfiltergraphdesc.o \
       defaults.o \

# TODO: real conditional compilation
OBJS-yes = vf_crop.o \
           vf_fifo.o \
           vf_fps.o \
           vf_negate.o \
           vf_overlay.o \
           vf_passthrough.o \
           vf_scale.o \
           vf_slicify.o \
           vf_split.o \
           vf_vflip.o \

HEADERS = avfilter.h

EXTRALIBS := -L$(BUILD_ROOT)/libavutil -lavutil$(BUILDSUF) $(EXTRALIBS)

NAME=avfilter
LIBVERSION=$(LAVFILTERVERSION)
LIBMAJOR=$(LAVFILTERMAJOR)

include ../common.mak
