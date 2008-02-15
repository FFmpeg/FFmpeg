include ../config.mak

CFLAGS+=-I$(SRC_PATH)/libavformat -I$(SRC_PATH)/libavcodec -I$(SRC_PATH)/libswscale

OBJS = allfilters.o \
       avfilter.o \
       avfiltergraph.o \
       avfiltergraphdesc.o \
       defaults.o \
       formats.o \
       avfiltergraphfile.o \

# TODO: real conditional compilation


OBJS-$(CONFIG_VF_CROP)       += vf_crop.o
OBJS-$(CONFIG_VF_FPS)        += vf_fps.o
OBJS-$(CONFIG_VF_HFLIP)      += vf_hflip.o
OBJS-$(CONFIG_VF_NEGATE)     += vf_negate.o
OBJS-$(CONFIG_VF_FIFO)       += vf_fifo.o
OBJS-$(CONFIG_VF_FORMAT)     += vf_format.o
OBJS-$(CONFIG_VF_OVERLAY)    += vf_overlay.o
OBJS-$(CONFIG_VF_ROTATE)     += vf_rotate.o
OBJS-$(CONFIG_VF_SCALE)      += vf_scale.o
OBJS-$(CONFIG_VF_SETPTS)     += vf_setpts.o
OBJS-$(CONFIG_VF_SLICIFY)    += vf_slicify.o
OBJS-$(CONFIG_VF_SPLIT)      += vf_split.o
OBJS-$(CONFIG_VF_TRANSPOSE)  += vf_transpose.o
OBJS-$(CONFIG_VF_VFLIP)      += vf_vflip.o
OBJS-$(CONFIG_AVFILTER_LAVF) += vsrc_movie.o

HEADERS = avfilter.h

EXTRALIBS := -L$(BUILD_ROOT)/libavcodec -lavcodec$(BUILDSUF) \
             -L$(BUILD_ROOT)/libswscale -lswscale$(BUILDSUF) \
             -L$(BUILD_ROOT)/libavutil  -lavutil$(BUILDSUF) $(EXTRALIBS)

ifeq ($(CONFIG_AVFILTER_LAVF),yes)
EXTRALIBS := -L$(BUILD_ROOT)/libavformat -lavformat$(BUILDSUF) $(EXTRALIBS)
endif

NAME=avfilter
LIBVERSION=$(LAVFILTERVERSION)
LIBMAJOR=$(LAVFILTERMAJOR)

include ../common.mak
