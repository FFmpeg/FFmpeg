#
# Makefile for tests
# (c) 2002 Fabrice Bellard
#
include ../config.mak

VPATH=$(SRC_PATH)/tests
CFLAGS=-O2 -Wall -g

REFFILE1=$(SRC_PATH)/tests/ffmpeg.regression.ref
REFFILE2=$(SRC_PATH)/tests/rotozoom.regression.ref

SERVER_REFFILE=$(SRC_PATH)/tests/ffserver.regression.ref

LIBAV_REFFILE=$(SRC_PATH)/tests/libav.regression.ref

all fulltest: codectest libavtest test-server
test: codectest libavtest

test-server: vsynth1/0.pgm asynth1.sw
	@$(SRC_PATH)/tests/server-regression.sh $(SERVER_REFFILE) $(SRC_PATH)/tests/test.conf

# fast regression tests for all codecs
codectest mpeg4 mpeg ac3: vsynth1/0.pgm vsynth2/0.pgm asynth1.sw tiny_psnr
	@$(SRC_PATH)/tests/regression.sh $@ $(REFFILE1) vsynth1
	@$(SRC_PATH)/tests/regression.sh $@ $(REFFILE2) vsynth2

# fast regression for libav formats
libavtest: vsynth1/0.pgm asynth1.sw
	@$(SRC_PATH)/tests/regression.sh $@ $(LIBAV_REFFILE) vsynth1

# video generation

vsynth1/0.pgm: videogen
	@mkdir -p vsynth1
	./videogen 'vsynth1/'

vsynth2/0.pgm: rotozoom
	@mkdir -p vsynth2
	./rotozoom 'vsynth2/' $(SRC_PATH)/tests/lena.pnm

videogen: videogen.c
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $<

rotozoom: rotozoom.c
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $<

# audio generation

asynth1.sw: audiogen
	./audiogen $@

audiogen: audiogen.c
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $<

tiny_psnr: tiny_psnr.c
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $<
        
DSPDEPS = $(SRC_PATH)/libavcodec/i386/dsputil_mmx.c \
 $(SRC_PATH)/libavcodec/i386/dsputil_mmx_avg.h \
 $(SRC_PATH)/libavcodec/i386/dsputil_mmx_rnd.h \
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
	rm -rf vsynth1 vsynth2 data
	rm -f asynth1.sw *~ audiogen videogen rotozoom tiny_psnr 
