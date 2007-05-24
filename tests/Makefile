#
# Makefile for tests
# (c) 2002 Fabrice Bellard
#
include ../config.mak

VPATH=$(SRC_PATH_BARE)/tests
SRC_DIR=$(SRC_PATH)/tests
BUILD_DIR=$(BUILD_ROOT)/tests
CFLAGS=-O2 -Wall -g

REFFILE1=$(SRC_DIR)/ffmpeg.regression.ref
REFFILE2=$(SRC_DIR)/rotozoom.regression.ref

SERVER_REFFILE=$(SRC_DIR)/ffserver.regression.ref

LIBAV_REFFILE=$(SRC_DIR)/libav.regression.ref

SEEK_REFFILE=$(SRC_DIR)/seek.regression.ref

all fulltest test: codectest libavtest seektest

test-server: vsynth1/00.pgm asynth1.sw
	@echo
	@echo "Unfortunately ffserver is broken and therefore its regression"
	@echo "test fails randomly. Treat the results accordingly."
	@echo
	@$(SRC_DIR)/server-regression.sh $(SERVER_REFFILE) $(SRC_DIR)/test.conf

# fast regression tests for all codecs
codectest mpeg4 mpeg ac3 snow snowll: vsynth1/00.pgm vsynth2/00.pgm asynth1.sw tiny_psnr$(EXESUF)
	@$(SRC_DIR)/regression.sh $@ $(REFFILE1) vsynth1
	@$(SRC_DIR)/regression.sh $@ $(REFFILE2) vsynth2

# fast regression for libav formats
ifeq ($(CONFIG_GPL),yes)
libavtest: vsynth1/00.pgm asynth1.sw
	@$(SRC_DIR)/regression.sh $@ $(LIBAV_REFFILE) vsynth1
else
libavtest:
	@echo
	@echo "This test requires FFmpeg to be compiled with --enable-gpl."
	@echo
endif

ifeq ($(CONFIG_SWSCALER),yes)
test-server codectest mpeg4 mpeg ac3 snow snowll libavtest: swscale_error
swscale_error:
	@echo
	@echo "This regression test is incompatible with --enable-swscaler."
	@echo
	@exit 1
endif

seektest: seek_test$(EXESUF)
	@$(SRC_DIR)/seek_test.sh $(SEEK_REFFILE)

# video generation

vsynth1/00.pgm: videogen$(EXESUF)
	@mkdir -p vsynth1
	$(BUILD_DIR)/$< 'vsynth1/'

vsynth2/00.pgm: rotozoom$(EXESUF)
	@mkdir -p vsynth2
	$(BUILD_DIR)/$< 'vsynth2/' $(SRC_DIR)/lena.pnm

videogen$(EXESUF): videogen.c
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $<

rotozoom$(EXESUF): rotozoom.c
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $<

# audio generation

asynth1.sw: audiogen$(EXESUF)
	$(BUILD_DIR)/$< $@

audiogen$(EXESUF): audiogen.c
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $<

tiny_psnr$(EXESUF): tiny_psnr.c
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $<

LIBAV = ../libavformat/libavformat.a ../libavcodec/libavcodec.a ../libavutil/libavutil.a

#FIXME cleanup shit below
seek_test$(EXESUF): seek_test.c $(LIBAV)
	$(CC) $(LDFLAGS) $(CFLAGS) -DHAVE_AV_CONFIG_H -I.. -I$(SRC_PATH)/libavformat -I$(SRC_PATH)/libavcodec -I$(SRC_PATH)/libavutil -o $@ $< $(SRC_PATH)/libavformat/libavformat.a $(SRC_PATH)/libavcodec/libavcodec.a $(SRC_PATH)/libavutil/libavutil.a $(EXTRALIBS)

distclean clean:
	rm -rf vsynth1 vsynth2 data
	rm -f asynth1.sw *~ audiogen$(EXESUF) videogen$(EXESUF) rotozoom$(EXESUF) tiny_psnr$(EXESUF)

.PHONY: all fulltest test codectest libavtest test-server seektest
.PHONY: mpeg4 mpeg ac3 snow snowll distclean clean
