AREF = fate-acodec-aref
VREF = fate-vsynth1-vref fate-vsynth2-vref
REFS = $(AREF) $(VREF)

$(VREF): avconv$(EXESUF) tests/vsynth1/00.pgm tests/vsynth2/00.pgm
$(AREF): avconv$(EXESUF) tests/data/asynth1.sw

tests/vsynth1/00.pgm: tests/videogen$(HOSTEXESUF)
	@mkdir -p tests/vsynth1
	$(M)./$< 'tests/vsynth1/'

tests/vsynth2/00.pgm: tests/rotozoom$(HOSTEXESUF)
	@mkdir -p tests/vsynth2
	$(M)./$< 'tests/vsynth2/' $(SRC_PATH)/tests/lena.pnm

tests/data/asynth1.sw: tests/audiogen$(HOSTEXESUF)
	@mkdir -p tests/data
	$(M)./$< $@

tests/data/asynth-16000-1.sw: tests/audiogen$(HOSTEXESUF)
	@mkdir -p tests/data
	$(M)./$< $@ 16000 1

tests/data/asynth%.sw tests/vsynth%/00.pgm: TAG = GEN

include $(SRC_PATH)/tests/fate/aac.mak
include $(SRC_PATH)/tests/fate/ac3.mak
include $(SRC_PATH)/tests/fate/adpcm.mak
include $(SRC_PATH)/tests/fate/als.mak
include $(SRC_PATH)/tests/fate/amrnb.mak
include $(SRC_PATH)/tests/fate/amrwb.mak
include $(SRC_PATH)/tests/fate/atrac.mak
include $(SRC_PATH)/tests/fate/audio.mak
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
include $(SRC_PATH)/tests/fate/wma.mak

FATE_ACODEC  = $(ACODEC_TESTS:%=fate-acodec-%)
FATE_VSYNTH1 = $(VCODEC_TESTS:%=fate-vsynth1-%)
FATE_VSYNTH2 = $(VCODEC_TESTS:%=fate-vsynth2-%)
FATE_VCODEC  = $(FATE_VSYNTH1) $(FATE_VSYNTH2)
FATE_LAVF    = $(LAVF_TESTS:%=fate-lavf-%)
FATE_LAVFI   = $(LAVFI_TESTS:%=fate-lavfi-%)
FATE_SEEK    = $(SEEK_TESTS:seek_%=fate-seek-%)

FATE = $(FATE_ACODEC)                                                   \
       $(FATE_VCODEC)                                                   \
       $(FATE_LAVF)                                                     \
       $(FATE_SEEK)                                                     \

FATE-$(CONFIG_AVFILTER) += $(FATE_LAVFI)

FATE += $(FATE-yes)

$(filter-out %-aref,$(FATE_ACODEC)): $(AREF)
$(filter-out %-vref,$(FATE_VSYNTH1)): fate-vsynth1-vref
$(filter-out %-vref,$(FATE_VSYNTH2)): fate-vsynth2-vref
$(FATE_LAVF):   $(REFS)
$(FATE_LAVFI):  $(REFS) tools/lavfi-showfiltfmts$(EXESUF)
$(FATE_SEEK):   fate-codec fate-lavf libavformat/seek-test$(EXESUF)

$(FATE_ACODEC):  CMD = codectest acodec
$(FATE_VSYNTH1): CMD = codectest vsynth1
$(FATE_VSYNTH2): CMD = codectest vsynth2
$(FATE_LAVF):    CMD = lavftest
$(FATE_LAVFI):   CMD = lavfitest
$(FATE_SEEK):    CMD = seektest

fate-codec:  fate-acodec fate-vcodec
fate-acodec: $(FATE_ACODEC)
fate-vcodec: $(FATE_VCODEC)
fate-lavf:   $(FATE_LAVF)
fate-lavfi:  $(FATE_LAVFI)
fate-seek:   $(FATE_SEEK)

ifdef SAMPLES
FATE += $(FATE_TESTS) $(FATE_TESTS-yes)
fate-rsync:
	rsync -vaLW rsync://fate-suite.libav.org/fate-suite/ $(SAMPLES)
else
fate-rsync:
	@echo "use 'make fate-rsync SAMPLES=/path/to/samples' to sync the fate suite"
$(FATE_TESTS):
	@echo "SAMPLES not specified, cannot run FATE"
endif

FATE_UTILS = base64 tiny_psnr

fate: $(FATE)

$(FATE): avconv$(EXESUF) $(FATE_UTILS:%=tests/%$(HOSTEXESUF))
	@echo "TEST    $(@:fate-%=%)"
	$(Q)$(SRC_PATH)/tests/fate-run.sh $@ "$(SAMPLES)" "$(TARGET_EXEC)" "$(TARGET_PATH)" '$(CMD)' '$(CMP)' '$(REF)' '$(FUZZ)' '$(THREADS)' '$(THREAD_TYPE)'

fate-list:
	@printf '%s\n' $(sort $(FATE))

clean:: testclean

testclean:
	$(RM) -r tests/vsynth1 tests/vsynth2 tests/data
	$(RM) $(CLEANSUFFIXES:%=tests/%)
	$(RM) $(TESTTOOLS:%=tests/%$(HOSTEXESUF))

-include $(wildcard tests/*.d)

.PHONY: fate*
