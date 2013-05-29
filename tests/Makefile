FFSERVER_REFFILE = $(SRC_PATH)/tests/ffserver.regression.ref

VREF = tests/vsynth1/00.pgm
AREF = tests/data/asynth1.sw


$(AREF): CMP=

ffservertest: ffserver$(EXESUF) tests/vsynth1/00.pgm tests/data/asynth1.sw
	@echo
	@echo "Unfortunately ffserver is broken and therefore its regression"
	@echo "test fails randomly. Treat the results accordingly."
	@echo
	$(SRC_PATH)/tests/ffserver-regression.sh $(FFSERVER_REFFILE) $(SRC_PATH)/tests/ffserver.conf

OBJDIRS += tests/data tests/vsynth1 tests/data/filtergraphs

$(VREF): tests/videogen$(HOSTEXESUF) | tests/vsynth1
	$(M)./$< 'tests/vsynth1/'

$(AREF): tests/audiogen$(HOSTEXESUF) | tests/data
	$(M)./$< $@

tests/data/asynth-%.wav: tests/audiogen$(HOSTEXESUF) | tests/data
	$(M)./$< $@ $(subst -, ,$*)

tests/data/vsynth1.yuv: tests/videogen$(HOSTEXESUF) | tests/data
	$(M)$< $@

tests/data/vsynth2.yuv: tests/rotozoom$(HOSTEXESUF) | tests/data
	$(M)$< $(SRC_PATH)/tests/lena.pnm $@

tests/data/ffprobe-test.nut: ffmpeg$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) ./$< \
        -f lavfi -i "aevalsrc=sin(400*PI*2*t):d=0.125[out0]; testsrc=d=0.125[out1]; testsrc=s=100x100:d=0.125[out2]" \
        -f ffmetadata -i $(SRC_PATH)/tests/test.ffmeta \
        -flags +bitexact -map 0:0 -map 0:1 -map 0:2 -map_metadata 1 \
        -map_metadata:s:0 1:s:0 -map_metadata:s:1 1:s:1 \
        -vcodec rawvideo -acodec pcm_s16le \
        -y $@ 2>/dev/null

tests/data/%.sw tests/data/asynth% tests/data/vsynth%.yuv tests/vsynth%/00.pgm tests/data/%.nut: TAG = GEN

tests/data/filtergraphs/%: TAG = COPY
tests/data/filtergraphs/%: $(SRC_PATH)/tests/filtergraphs/% | tests/data/filtergraphs
	$(M)cp $< $@

CHKCFG  = $(if $($(1))$(!$(1)),$($(1)), $(error No such config: $(1)))

ALLYES  = $(strip $(call XYES, $(1)))
XYES    = $(if $(strip $(1)),                                           \
               $(if $(call CHKCFG,CONFIG_$(firstword $(1))),            \
                    $(call XYES, $(wordlist 2, $(words $(1)), $(1)))),  \
               yes)

ENCDEC  = $(call ALLYES, $(firstword $(1))_ENCODER $(lastword $(1))_DECODER  \
                         $(firstword $(2))_MUXER   $(lastword $(2))_DEMUXER)

ENCDEC2 = $(call ALLYES, $(firstword $(1))_ENCODER $(lastword $(1))_DECODER  \
                         $(firstword $(2))_ENCODER $(lastword $(2))_DECODER  \
                         $(firstword $(3))_MUXER   $(lastword $(3))_DEMUXER)

DEMDEC  = $(call ALLYES, $(1)_DEMUXER $(2:%=%_DECODER))
ENCMUX  = $(call ALLYES, $(1:%=%_ENCODER) $(2)_MUXER)

DEMMUX  = $(call ALLYES, $(1)_DEMUXER $(2)_MUXER)

FILTERDEMDEC       = $(call ALLYES, $(1)_FILTER $(2)_DEMUXER $(3)_DECODER)
FILTERDEMDECMUX    = $(call ALLYES, $(1)_FILTER $(2)_DEMUXER $(3)_DECODER $(4)_MUXER)
FILTERDEMDECENCMUX = $(call ALLYES, $(1)_FILTER $(2)_DEMUXER $(3)_DECODER $(4)_ENCODER $(5)_MUXER)

include $(SRC_PATH)/tests/fate/acodec.mak
include $(SRC_PATH)/tests/fate/vcodec.mak
include $(SRC_PATH)/tests/fate/avformat.mak
include $(SRC_PATH)/tests/fate/seek.mak

include $(SRC_PATH)/tests/fate/aac.mak
include $(SRC_PATH)/tests/fate/ac3.mak
include $(SRC_PATH)/tests/fate/adpcm.mak
include $(SRC_PATH)/tests/fate/alac.mak
include $(SRC_PATH)/tests/fate/als.mak
include $(SRC_PATH)/tests/fate/amrnb.mak
include $(SRC_PATH)/tests/fate/amrwb.mak
include $(SRC_PATH)/tests/fate/atrac.mak
include $(SRC_PATH)/tests/fate/audio.mak
include $(SRC_PATH)/tests/fate/bmp.mak
include $(SRC_PATH)/tests/fate/cdxl.mak
include $(SRC_PATH)/tests/fate/cover-art.mak
include $(SRC_PATH)/tests/fate/demux.mak
include $(SRC_PATH)/tests/fate/dfa.mak
include $(SRC_PATH)/tests/fate/dpcm.mak
include $(SRC_PATH)/tests/fate/ea.mak
include $(SRC_PATH)/tests/fate/ffmpeg.mak
include $(SRC_PATH)/tests/fate/ffprobe.mak
include $(SRC_PATH)/tests/fate/filter-audio.mak
include $(SRC_PATH)/tests/fate/filter-video.mak
include $(SRC_PATH)/tests/fate/flac.mak
include $(SRC_PATH)/tests/fate/fft.mak
include $(SRC_PATH)/tests/fate/gif.mak
include $(SRC_PATH)/tests/fate/h264.mak
include $(SRC_PATH)/tests/fate/image.mak
include $(SRC_PATH)/tests/fate/indeo.mak
include $(SRC_PATH)/tests/fate/libavcodec.mak
include $(SRC_PATH)/tests/fate/libavformat.mak
include $(SRC_PATH)/tests/fate/libavutil.mak
include $(SRC_PATH)/tests/fate/lossless-audio.mak
include $(SRC_PATH)/tests/fate/lossless-video.mak
include $(SRC_PATH)/tests/fate/microsoft.mak
include $(SRC_PATH)/tests/fate/monkeysaudio.mak
include $(SRC_PATH)/tests/fate/mp3.mak
include $(SRC_PATH)/tests/fate/mpc.mak
include $(SRC_PATH)/tests/fate/pcm.mak
include $(SRC_PATH)/tests/fate/probe.mak
include $(SRC_PATH)/tests/fate/prores.mak
include $(SRC_PATH)/tests/fate/qt.mak
include $(SRC_PATH)/tests/fate/qtrle.mak
include $(SRC_PATH)/tests/fate/real.mak
include $(SRC_PATH)/tests/fate/screen.mak
include $(SRC_PATH)/tests/fate/subtitles.mak
include $(SRC_PATH)/tests/fate/utvideo.mak
include $(SRC_PATH)/tests/fate/video.mak
include $(SRC_PATH)/tests/fate/voice.mak
include $(SRC_PATH)/tests/fate/vorbis.mak
include $(SRC_PATH)/tests/fate/vpx.mak
include $(SRC_PATH)/tests/fate/vqf.mak
include $(SRC_PATH)/tests/fate/wavpack.mak
include $(SRC_PATH)/tests/fate/wma.mak

FATE_FFMPEG += $(FATE_FFMPEG-yes) $(FATE_AVCONV) $(FATE_AVCONV-yes)
FATE-$(CONFIG_FFMPEG) += $(FATE_FFMPEG)
FATE-$(CONFIG_FFPROBE) += $(FATE_FFPROBE)

FATE_SAMPLES_AVCONV           += $(FATE_SAMPLES_AVCONV-yes)
FATE_SAMPLES_FFMPEG           += $(FATE_SAMPLES_FFMPEG-yes)
FATE_EXTERN-$(CONFIG_FFMPEG)  += $(FATE_SAMPLES_AVCONV) $(FATE_SAMPLES_FFMPEG) $(FATE_SAMPLES_FFPROBE)
FATE_EXTERN += $(FATE_EXTERN-yes)

FATE += $(FATE-yes)

RSYNC_OPTIONS-$(HAVE_RSYNC_CONTIMEOUT) += --contimeout=60
RSYNC_OPTIONS = -vrltLW --timeout=60 $(RSYNC_OPTIONS-yes)

$(FATE_FFMPEG) $(FATE_SAMPLES_AVCONV) $(FATE_SAMPLES_FFMPEG): ffmpeg$(EXESUF)

$(FATE_FFPROBE) $(FATE_SAMPLES_FFPROBE): ffprobe$(EXESUF)

ifdef SAMPLES
FATE += $(FATE_FULL) $(FATE_FULL-yes)
FATE += $(FATE_EXTERN)
fate-rsync:
	rsync $(RSYNC_OPTIONS) rsync://fate-suite.ffmpeg.org/fate-suite/ $(SAMPLES)
else
fate::
	@echo "warning: only a subset of the fate tests will be run because SAMPLES is not specified"
fate-rsync:
	@echo "use 'make fate-rsync SAMPLES=/path/to/samples' to sync the fate suite"
$(FATE_EXTERN):
	@echo "$@ requires external samples and SAMPLES not specified"; false
endif

FATE_UTILS = base64 tiny_psnr tiny_ssim

TOOL = ffmpeg

fate:: $(FATE)

$(FATE) $(FATE_TESTS-no): $(FATE_UTILS:%=tests/%$(HOSTEXESUF))
	@echo "TEST    $(@:fate-%=%)"
	$(Q)$(SRC_PATH)/tests/fate-run.sh $@ "$(SAMPLES)" "$(TARGET_EXEC)" "$(TARGET_PATH)" '$(CMD)' '$(CMP)' '$(REF)' '$(FUZZ)' '$(THREADS)' '$(THREAD_TYPE)' '$(CPUFLAGS)' '$(CMP_SHIFT)' '$(CMP_TARGET)' '$(SIZE_TOLERANCE)' '$(CMP_UNIT)' '$(GEN)'

fate-list:
	@printf '%s\n' $(sort $(FATE))

coverage.info: TAG = LCOV
coverage.info:
	$(M)lcov -q -d $(CURDIR) -b $(SRC_PATH) --capture | \
	    sed "s,$(CURDIR)/\./,$(CURDIR)/," > $@
	$(M)lcov -q --remove $@ "/usr*" -o $@

lcov:  TAG = GENHTML
lcov: coverage.info
	$(M)genhtml -q -o $(CURDIR)/lcov $<

lcov-reset: TAG = LCOV
lcov-reset:
	$(M)lcov -d $(CURDIR) --zerocounters
	$(Q)$(RM) -f coverage.info

clean:: testclean

testclean:
	$(RM) -r tests/vsynth1 tests/data tools/lavfi-showfiltfmts$(EXESUF)
	$(RM) $(CLEANSUFFIXES:%=tests/%)
	$(RM) $(TESTTOOLS:%=tests/%$(HOSTEXESUF))

-include $(wildcard tests/*.d)

.PHONY: fate* lcov lcov-reset
.INTERMEDIATE: coverage.info
