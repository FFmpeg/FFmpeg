include ../config.mak

CFLAGS+=-I$(SRC_PATH)/libavcodec

OBJS = avfilter.o \
       avfiltergraph.o \
       avfiltergraphdesc.o \
       defaults.o \

# TODO: real conditional compilation
OBJS-yes = vsrc_dummy.o \
           vsrc_ppm.o \
           vf_crop.o \
           vf_fps.o \
           vf_overlay.o \
           vf_passthrough.o \
           vf_rgb2bgr.o \
           vf_slicify.o \
           vf_vflip.o \

HEADERS = avfilter.h

EXTRALIBS := -L$(BUILD_ROOT)/libavutil -lavutil$(BUILDSUF) $(EXTRALIBS)

NAME=avfilter
LIBVERSION=$(LAVFILTERVERSION)
LIBMAJOR=$(LAVFILTERMAJOR)

include ../common.mak
