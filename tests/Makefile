#
# Makefile for tests
# (c) 2002 Gerard Lantau
#
include ../config.mak

VPATH=$(SRC_PATH)/tests
CFLAGS=-O2 -Wall -g
REFFILE=$(SRC_PATH)/tests/ffmpeg.regression.ref

all: test

# fast regression tests for all codecs
test mpeg4 mpeg: vsynth1/0.pgm asynth1.sw
	@$(SRC_PATH)/tests/regression.sh $@ $(REFFILE)

# update the regression test with the current results
updatetest:
	cp /tmp/ffmpeg.regression $(REFFILE)

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

clean:
	rm -rf vsynth1
	rm -f asynth1.sw *~ audiogen videogen
