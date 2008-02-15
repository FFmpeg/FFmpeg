CC = gcc
CFLAGS = -g -I../libavcodec -I../libavutil
LIBS = ../libavcodec/libavcodec.a ../libavutil/libavutil.a -lm -lSDL

OBJECTS = avfilter.o          \
          avfiltergraph.o     \
          vsrc_dummy.o        \
		  vsrc_ppm.o          \
		  vo_sdl.o            \
		  vf_crop.o           \
		  vf_passthrough.o    \
		  vf_rgb2bgr.o        \
		  vf_slicify.o        \

all: filter_test

filter_test: $(OBJECTS) filter_test.c
	$(CC) $(CFLAGS) $^ -o $@ $(LIBS)

$(OBJECTS): %.o: %.c
	$(CC) $(CFLAGS) -c $<
