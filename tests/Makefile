#
# Makefile for tests
# (c) 2002 Fabrice Bellard
#
include ../config.mak

VPATH=$(SRC_PATH)/tests
CFLAGS=-O2 -Wall -g
REFFILE=$(SRC_PATH)/tests/ffmpeg.regression.ref
LIBAV_REFFILE=$(SRC_PATH)/tests/libav.regression.ref

all: test

# fast regression tests for all codecs
test mpeg4 mpeg: vsynth1/0.pgm asynth1.sw
	@$(SRC_PATH)/tests/regression.sh $@ $(REFFILE)

# fast regression for libav formats
libavtest: vsynth1/0.pgm asynth1.sw
	@$(SRC_PATH)/tests/regression.sh $@ $(LIBAV_REFFILE)

# video generation

vsynth1/0.pgm: videogen
	@mkdir -p vsynth1
	./videogen 'vsynth1/'

videogen: videogen.c
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $<

# audio generation

asynth1.sw: audiogen
	./audiogen $@

audiogen: audiogen.c
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $<

DSPDEPS = $(SRC_PATH)/libavcodec/i386/dsputil_mmx.c \
 $(SRC_PATH)/libavcodec/i386/dsputil_mmx_avg.h \
 $(SRC_PATH)/libavcodec/i386/fdct_mmx.c \
 $(SRC_PATH)/libavcodec/i386/idct_mmx.c \
 $(SRC_PATH)/libavcodec/i386/motion_est_mmx.c \
 $(SRC_PATH)/libavcodec/i386/simple_idct_mmx.c \
 $(SRC_PATH)/libavcodec/dsputil.c \
 $(SRC_PATH)/libavcodec/dsputil.h \
 $(SRC_PATH)/libavcodec/simple_idct.c

dsptestpic: dsptest.c $(DSPDEPS)
	$(CC) -fPIC -DPIC -O4 -fomit-frame-pointer -DHAVE_AV_CONFIG_H -I.. -I$(SRC_PATH)/libavcodec/i386 -I$(SRC_PATH)/libavcodec/ -o $@ $< -lm
dsptest: dsptest.c $(DSPDEPS)
	$(CC) -O4 -fomit-frame-pointer -DHAVE_AV_CONFIG_H -I.. -I$(SRC_PATH)/libavcodec/i386 -I$(SRC_PATH)/libavcodec/ -o $@ $< -lm

clean:
	rm -rf vsynth1
	rm -f asynth1.sw *~ audiogen videogen
