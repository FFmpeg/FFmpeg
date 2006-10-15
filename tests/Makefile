#
# Makefile for tests
# (c) 2002 Fabrice Bellard
#
include ../config.mak

VPATH=$(SRC_PATH)/tests
CFLAGS=-O2 -Wall -g

REFFILE1=$(VPATH)/ffmpeg.regression.ref
REFFILE2=$(VPATH)/rotozoom.regression.ref

SERVER_REFFILE=$(VPATH)/ffserver.regression.ref

LIBAV_REFFILE=$(VPATH)/libav.regression.ref

all fulltest test: codectest libavtest test-server

test-server: vsynth1/00.pgm asynth1.sw
	@$(VPATH)/server-regression.sh $(SERVER_REFFILE) $(VPATH)/test.conf

# fast regression tests for all codecs
codectest mpeg4 mpeg ac3 snow snowll: vsynth1/00.pgm vsynth2/00.pgm asynth1.sw tiny_psnr$(EXESUF)
	@$(VPATH)/regression.sh $@ $(REFFILE1) vsynth1
	@$(VPATH)/regression.sh $@ $(REFFILE2) vsynth2

# fast regression for libav formats
libavtest: vsynth1/00.pgm asynth1.sw
	@echo
	@echo "This test requires FFmpeg to be compiled with --enable-gpl."
	@echo
	@$(VPATH)/regression.sh $@ $(LIBAV_REFFILE) vsynth1

# video generation

vsynth1/00.pgm: videogen$(EXESUF)
	@mkdir -p vsynth1
	./videogen 'vsynth1/'

vsynth2/00.pgm: rotozoom$(EXESUF)
	@mkdir -p vsynth2
	./rotozoom 'vsynth2/' $(VPATH)/lena.pnm

videogen$(EXESUF): videogen.c
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $<

rotozoom$(EXESUF): rotozoom.c
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $<

# audio generation

asynth1.sw: audiogen$(EXESUF)
	./audiogen $@

audiogen$(EXESUF): audiogen.c
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $<

tiny_psnr$(EXESUF): tiny_psnr.c
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

DSPCFLAGS = -O4 -fomit-frame-pointer -DHAVE_AV_CONFIG_H -I.. \
 -I$(SRC_PATH)/libavutil/ -I$(SRC_PATH)/libavcodec/i386 \
 -I$(SRC_PATH)/libavcodec/ -lm

dsptestpic: dsptest.c $(DSPDEPS)
	$(CC) -fPIC -DPIC $(DSPCFLAGS) -o $@ $<
dsptest: dsptest.c $(DSPDEPS)
	$(CC) $(DSPCFLAGS) -o $@ $<

distclean clean:
	rm -rf vsynth1 vsynth2 data
	rm -f asynth1.sw *~ audiogen$(EXESUF) videogen$(EXESUF) rotozoom$(EXESUF) tiny_psnr$(EXESUF)

.PHONY: all fulltest test codectest libavtest test-server
.PHONY: mpeg4 mpeg ac3 snow snowll distclean clean
