VREF = tests/vsynth1/00.pgm
AREF = tests/data/asynth1.sw

OBJDIRS += tests/data tests/vsynth1

# Required due to missing automatic dependency tracking for HOSTOBJS.
tests/rotozoom.o tests/videogen.o: tests/utils.c

tests/vsynth1/00.pgm: tests/videogen$(HOSTEXESUF) | tests/vsynth1
	$(M)./$< 'tests/vsynth1/'

tests/data/asynth1.sw: tests/audiogen$(HOSTEXESUF) | tests/data
	$(M)./$< $@

tests/data/asynth-%.wav: tests/audiogen$(HOSTEXESUF) | tests/data
	$(M)./$< $@ $(subst -, ,$*)

tests/data/vsynth1.yuv: tests/videogen$(HOSTEXESUF) | tests/data
	$(M)$< >$@

tests/data/vsynth2.yuv: tests/rotozoom$(HOSTEXESUF) | tests/data
	$(M)$< $(SRC_PATH)/tests/lena.pnm >$@

tests/data/asynth% tests/data/vsynth%.yuv tests/vsynth%/00.pgm: TAG = GEN

include $(SRC_PATH)/tests/fate/acodec.mak
include $(SRC_PATH)/tests/fate/vcodec.mak

include $(SRC_PATH)/tests/fate/aac.mak
include $(SRC_PATH)/tests/fate/ac3.mak
include $(SRC_PATH)/tests/fate/adpcm.mak
include $(SRC_PATH)/tests/fate/als.mak
include $(SRC_PATH)/tests/fate/amrnb.mak
include $(SRC_PATH)/tests/fate/amrwb.mak
include $(SRC_PATH)/tests/fate/atrac.mak
include $(SRC_PATH)/tests/fate/audio.mak
include $(SRC_PATH)/tests/fate/bmp.mak
include $(SRC_PATH)/tests/fate/cdxl.mak
include $(SRC_PATH)/tests/fate/dct.mak
include $(SRC_PATH)/tests/fate/demux.mak
include $(SRC_PATH)/tests/fate/dfa.mak
include $(SRC_PATH)/tests/fate/dpcm.mak
include $(SRC_PATH)/tests/fate/ea.mak
include $(SRC_PATH)/tests/fate/fft.mak
include $(SRC_PATH)/tests/fate/h264.mak
include $(SRC_PATH)/tests/fate/image.mak
include $(SRC_PATH)/tests/fate/indeo.mak
include $(SRC_PATH)/tests/fate/libavcodec.mak
include $(SRC_PATH)/tests/fate/libavutil.mak
include $(SRC_PATH)/tests/fate/lossless-audio.mak
include $(SRC_PATH)/tests/fate/lossless-video.mak
include $(SRC_PATH)/tests/fate/microsoft.mak
include $(SRC_PATH)/tests/fate/mp3.mak
include $(SRC_PATH)/tests/fate/mpc.mak
include $(SRC_PATH)/tests/fate/pcm.mak
include $(SRC_PATH)/tests/fate/probe.mak
include $(SRC_PATH)/tests/fate/prores.mak
include $(SRC_PATH)/tests/fate/qt.mak
include $(SRC_PATH)/tests/fate/qtrle.mak
include $(SRC_PATH)/tests/fate/real.mak
include $(SRC_PATH)/tests/fate/screen.mak
include $(SRC_PATH)/tests/fate/utvideo.mak
include $(SRC_PATH)/tests/fate/video.mak
include $(SRC_PATH)/tests/fate/voice.mak
include $(SRC_PATH)/tests/fate/vorbis.mak
include $(SRC_PATH)/tests/fate/vpx.mak
include $(SRC_PATH)/tests/fate/vqf.mak
include $(SRC_PATH)/tests/fate/wavpack.mak
include $(SRC_PATH)/tests/fate/wma.mak

FATE_LAVF    = $(LAVF_TESTS:%=fate-lavf-%)
FATE_LAVFI   = $(LAVFI_TESTS:%=fate-lavfi-%)
FATE_SEEK    = $(SEEK_TESTS:seek_%=fate-seek-%)

FATE_AVCONV += $(FATE_LAVF)                                             \
               $(FATE_LAVFI)                                            \
               $(FATE_SEEK)                                             \

FATE_AVCONV += $(FATE_AVCONV-yes)
FATE-$(CONFIG_AVCONV) += $(FATE_AVCONV)

FATE-$(CONFIG_AVCODEC)  += $(FATE_LIBAVCODEC)

FATE_SAMPLES-$(CONFIG_AVCONV) += $(FATE_SAMPLES_AVCONV)
FATE_SAMPLES += $(FATE_SAMPLES-yes)

FATE += $(FATE-yes)
FATE += $(FATE_LIBAVUTIL)

$(FATE_AVCONV) $(FATE_SAMPLES_AVCONV): avconv$(EXESUF)

$(FATE_LAVF):  $(AREF) $(VREF)
$(FATE_LAVFI): $(VREF) tools/lavfi-showfiltfmts$(EXESUF)
$(FATE_SEEK):  fate-acodec fate-vsynth2 fate-lavf libavformat/seek-test$(EXESUF)

$(FATE_LAVF):    CMD = lavftest
$(FATE_LAVFI):   CMD = lavfitest
$(FATE_SEEK):    CMD = seektest

fate-lavf:   $(FATE_LAVF)
fate-lavfi:  $(FATE_LAVFI)
fate-seek:   $(FATE_SEEK)

ifdef SAMPLES
FATE += $(FATE_SAMPLES)
fate-rsync:
	rsync -vaLW rsync://fate-suite.libav.org/fate-suite/ $(SAMPLES)
else
fate-rsync:
	@echo "use 'make fate-rsync SAMPLES=/path/to/samples' to sync the fate suite"
$(FATE_SAMPLES):
	@echo "$@ requires external samples and SAMPLES not specified"; false
endif

FATE_UTILS = base64 tiny_psnr

fate: $(FATE)

$(FATE): $(FATE_UTILS:%=tests/%$(HOSTEXESUF))
	@echo "TEST    $(@:fate-%=%)"
	$(Q)$(SRC_PATH)/tests/fate-run.sh $@ "$(SAMPLES)" "$(TARGET_EXEC)" "$(TARGET_PATH)" '$(CMD)' '$(CMP)' '$(REF)' '$(FUZZ)' '$(THREADS)' '$(THREAD_TYPE)' '$(CPUFLAGS)' '$(CMP_SHIFT)' '$(CMP_TARGET)' '$(SIZE_TOLERANCE)' '$(CMP_UNIT)'

fate-list:
	@printf '%s\n' $(sort $(FATE))

clean:: testclean

testclean:
	$(RM) -r tests/vsynth1 tests/vsynth2 tests/data
	$(RM) $(CLEANSUFFIXES:%=tests/%)
	$(RM) $(TESTTOOLS:%=tests/%$(HOSTEXESUF))

-include $(wildcard tests/*.d)

.PHONY: fate*
