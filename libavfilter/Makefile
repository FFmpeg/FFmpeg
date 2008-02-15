include ../config.mak

CFLAGS+=-I$(SRC_PATH)/libavcodec

OBJS = avfilter.o \
       avfiltergraph.o \

# TODO: real conditional compilation
OBJS-yes = vsrc_dummy.o \
           vsrc_ppm.o \
           vo_sdl.o \
           vf_crop.o \
           vf_passthrough.o \
           vf_rgb2bgr.o \
           vf_slicify.o \

HEADERS = avfilter.h

EXTRALIBS := -L$(BUILD_ROOT)/libavutil -lavutil$(BUILDSUF) $(EXTRALIBS)

NAME=avfilter
LIBVERSION=$(LAVFILTERVERSION)
LIBMAJOR=$(LAVFILTERMAJOR)

include ../common.mak

clean::
	rm -f \
	   filter_test $(TESTS)

tests: filter_test

filter_test: filter_test.o $(LIB)
	$(CC) $(CFLAGS) $< -o $@ $(LIB) -lm -lSDL

.PHONY: tests
