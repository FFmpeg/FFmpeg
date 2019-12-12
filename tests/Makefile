THREADS = 1
VREF = tests/vsynth1/00.pgm
AREF = tests/data/asynth1.sw

FATEW = 34
FATEH = 34

FFMPEG=ffmpeg$(PROGSSUF)$(EXESUF)

$(AREF): CMP=

APITESTSDIR := tests/api
DNNTESTSDIR := tests/dnn
FATE_OUTDIRS = tests/data tests/data/fate tests/data/filtergraphs tests/data/lavf tests/data/lavf-fate tests/data/pixfmt tests/vsynth1 $(APITESTSDIR) $(DNNTESTSDIR)
OUTDIRS += $(FATE_OUTDIRS)

$(VREF): tests/videogen$(HOSTEXESUF) | tests/vsynth1
	$(M)./$< 'tests/vsynth1/'

$(AREF): tests/audiogen$(HOSTEXESUF) | tests/data
	$(M)./$< $@

tests/data/asynth-%.wav: tests/audiogen$(HOSTEXESUF) | tests/data
	$(M)./$< $@ $(subst -, ,$*)

tests/data/vsynth1.yuv: tests/videogen$(HOSTEXESUF) | tests/data
	$(M)$< $@

tests/data/vsynth2.yuv: tests/rotozoom$(HOSTEXESUF) | tests/data
	$(M)$< $(SRC_PATH)/tests/reference.pnm $@

tests/data/vsynth_lena.yuv: tests/rotozoom$(HOSTEXESUF) | tests/data
	$(M)$< $(SAMPLES)/lena.pnm $@

tests/data/vsynth3.yuv: tests/videogen$(HOSTEXESUF) | tests/data
	$(M)$< $@ $(FATEW) $(FATEH)

tests/test_copy.ffmeta: TAG = COPY
tests/test_copy.ffmeta: tests/data
	$(M)cp -f $(SRC_PATH)/tests/test.ffmeta tests/test_copy.ffmeta

tests/data/ffprobe-test.nut: ffmpeg$(PROGSSUF)$(EXESUF) tests/test_copy.ffmeta
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< \
        -f lavfi -i "aevalsrc=sin(400*PI*2*t):d=0.125[out0]; testsrc=d=0.125[out1]; testsrc=s=100x100:d=0.125[out2]" \
        -f ffmetadata -i $(TARGET_PATH)/tests/test_copy.ffmeta \
        -flags +bitexact -fflags +bitexact -map 0:0 -map 0:1 -map 0:2 -map_metadata 1 \
        -map_metadata:s:0 1:s:0 -map_metadata:s:1 1:s:1 \
        -vcodec rawvideo -acodec pcm_s16le \
        -y $(TARGET_PATH)/$@ 2>/dev/null

tests/data/%.sw tests/data/asynth% tests/data/vsynth%.yuv tests/vsynth%/00.pgm tests/data/%.nut: TAG = GEN

tests/data/filtergraphs/%: TAG = COPY
tests/data/filtergraphs/%: $(SRC_PATH)/tests/filtergraphs/% | tests/data/filtergraphs
	$(M)cp $< $@

RUNNING_FATE := $(filter check fate%,$(filter-out fate-rsync,$(MAKECMDGOALS)))

# Check sanity of dependencies when running FATE tests.
ifneq (,$(RUNNING_FATE))
CHKCFG  = $(if $($(1))$(!$(1)),$($(1)), $(error No such config: $(1)))
endif

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

FILTERDEMDEC       = $(call ALLYES, $(1:%=%_FILTER) $(2)_DEMUXER $(3)_DECODER)
FILTERDEMDECMUX    = $(call ALLYES, $(1:%=%_FILTER) $(2)_DEMUXER $(3)_DECODER $(4)_MUXER)
FILTERDEMDECENCMUX = $(call ALLYES, $(1:%=%_FILTER) $(2)_DEMUXER $(3)_DECODER $(4)_ENCODER $(5)_MUXER)

PARSERDEMDEC       = $(call ALLYES, $(1)_PARSER $(2)_DEMUXER $(3)_DECODER)

include $(SRC_PATH)/$(APITESTSDIR)/Makefile
include $(SRC_PATH)/$(DNNTESTSDIR)/Makefile

include $(SRC_PATH)/tests/fate/acodec.mak
include $(SRC_PATH)/tests/fate/vcodec.mak

include $(SRC_PATH)/tests/fate/lavf-audio.mak
include $(SRC_PATH)/tests/fate/lavf-container.mak
include $(SRC_PATH)/tests/fate/lavf-image.mak
include $(SRC_PATH)/tests/fate/lavf-image2pipe.mak
include $(SRC_PATH)/tests/fate/lavf-video.mak
include $(SRC_PATH)/tests/fate/seek.mak

include $(SRC_PATH)/tests/fate/aac.mak
include $(SRC_PATH)/tests/fate/ac3.mak
include $(SRC_PATH)/tests/fate/adpcm.mak
include $(SRC_PATH)/tests/fate/alac.mak
include $(SRC_PATH)/tests/fate/als.mak
include $(SRC_PATH)/tests/fate/amrnb.mak
include $(SRC_PATH)/tests/fate/amrwb.mak
include $(SRC_PATH)/tests/fate/api.mak
include $(SRC_PATH)/tests/fate/apng.mak
include $(SRC_PATH)/tests/fate/atrac.mak
include $(SRC_PATH)/tests/fate/audio.mak
include $(SRC_PATH)/tests/fate/bmp.mak
include $(SRC_PATH)/tests/fate/build.mak
include $(SRC_PATH)/tests/fate/canopus.mak
include $(SRC_PATH)/tests/fate/cbs.mak
include $(SRC_PATH)/tests/fate/cdxl.mak
include $(SRC_PATH)/tests/fate/checkasm.mak
include $(SRC_PATH)/tests/fate/concatdec.mak
include $(SRC_PATH)/tests/fate/cover-art.mak
include $(SRC_PATH)/tests/fate/dca.mak
include $(SRC_PATH)/tests/fate/demux.mak
include $(SRC_PATH)/tests/fate/dfa.mak
include $(SRC_PATH)/tests/fate/dnn.mak
include $(SRC_PATH)/tests/fate/dnxhd.mak
include $(SRC_PATH)/tests/fate/dpcm.mak
include $(SRC_PATH)/tests/fate/ea.mak
include $(SRC_PATH)/tests/fate/exif.mak
include $(SRC_PATH)/tests/fate/ffmpeg.mak
include $(SRC_PATH)/tests/fate/ffprobe.mak
include $(SRC_PATH)/tests/fate/fft.mak
include $(SRC_PATH)/tests/fate/fifo-muxer.mak
include $(SRC_PATH)/tests/fate/filter-audio.mak
include $(SRC_PATH)/tests/fate/filter-video.mak
include $(SRC_PATH)/tests/fate/fits.mak
include $(SRC_PATH)/tests/fate/flac.mak
include $(SRC_PATH)/tests/fate/flvenc.mak
include $(SRC_PATH)/tests/fate/gapless.mak
include $(SRC_PATH)/tests/fate/gif.mak
include $(SRC_PATH)/tests/fate/h264.mak
include $(SRC_PATH)/tests/fate/hap.mak
include $(SRC_PATH)/tests/fate/hevc.mak
include $(SRC_PATH)/tests/fate/hlsenc.mak
include $(SRC_PATH)/tests/fate/hw.mak
include $(SRC_PATH)/tests/fate/id3v2.mak
include $(SRC_PATH)/tests/fate/image.mak
include $(SRC_PATH)/tests/fate/indeo.mak
include $(SRC_PATH)/tests/fate/libavcodec.mak
include $(SRC_PATH)/tests/fate/libavdevice.mak
include $(SRC_PATH)/tests/fate/libavformat.mak
include $(SRC_PATH)/tests/fate/libavresample.mak
include $(SRC_PATH)/tests/fate/libavutil.mak
include $(SRC_PATH)/tests/fate/libswresample.mak
include $(SRC_PATH)/tests/fate/libswscale.mak
include $(SRC_PATH)/tests/fate/lossless-audio.mak
include $(SRC_PATH)/tests/fate/lossless-video.mak
include $(SRC_PATH)/tests/fate/matroska.mak
include $(SRC_PATH)/tests/fate/microsoft.mak
include $(SRC_PATH)/tests/fate/monkeysaudio.mak
include $(SRC_PATH)/tests/fate/mov.mak
include $(SRC_PATH)/tests/fate/mp3.mak
include $(SRC_PATH)/tests/fate/mpc.mak
include $(SRC_PATH)/tests/fate/mpeg4.mak
include $(SRC_PATH)/tests/fate/mpegps.mak
include $(SRC_PATH)/tests/fate/mpegts.mak
include $(SRC_PATH)/tests/fate/mxf.mak
include $(SRC_PATH)/tests/fate/opus.mak
include $(SRC_PATH)/tests/fate/pcm.mak
include $(SRC_PATH)/tests/fate/pixfmt.mak
include $(SRC_PATH)/tests/fate/pixlet.mak
include $(SRC_PATH)/tests/fate/probe.mak
include $(SRC_PATH)/tests/fate/prores.mak
include $(SRC_PATH)/tests/fate/qt.mak
include $(SRC_PATH)/tests/fate/qtrle.mak
include $(SRC_PATH)/tests/fate/real.mak
include $(SRC_PATH)/tests/fate/screen.mak
include $(SRC_PATH)/tests/fate/segment.mak
include $(SRC_PATH)/tests/fate/source.mak
include $(SRC_PATH)/tests/fate/speedhq.mak
include $(SRC_PATH)/tests/fate/subtitles.mak
include $(SRC_PATH)/tests/fate/utvideo.mak
include $(SRC_PATH)/tests/fate/video.mak
include $(SRC_PATH)/tests/fate/voice.mak
include $(SRC_PATH)/tests/fate/vorbis.mak
include $(SRC_PATH)/tests/fate/vpx.mak
include $(SRC_PATH)/tests/fate/vqf.mak
include $(SRC_PATH)/tests/fate/wavpack.mak
include $(SRC_PATH)/tests/fate/wma.mak
include $(SRC_PATH)/tests/fate/xvid.mak

FATE_FFMPEG += $(FATE_FFMPEG-yes) $(FATE_AVCONV) $(FATE_AVCONV-yes)
FATE-$(CONFIG_FFMPEG) += $(FATE_FFMPEG)
FATE-$(CONFIG_FFPROBE) += $(FATE_FFPROBE)

FATE_SAMPLES_AVCONV           += $(FATE_SAMPLES_AVCONV-yes)
FATE_SAMPLES_FFMPEG           += $(FATE_SAMPLES_FFMPEG-yes)
FATE_EXTERN-$(CONFIG_FFMPEG)  += $(FATE_SAMPLES_AVCONV) $(FATE_SAMPLES_FFMPEG) $(FATE_SAMPLES_FFPROBE) $(FATE_SAMPLES_FASTSTART)
FATE_EXTERN += $(FATE_EXTERN-yes)

FATE += $(FATE-yes)

RSYNC_OPTIONS-$(HAVE_RSYNC_CONTIMEOUT) += --contimeout=60
RSYNC_OPTIONS = -vrltLW --timeout=60 $(RSYNC_OPTIONS-yes)

$(FATE_FFMPEG) $(FATE_SAMPLES_AVCONV) $(FATE_SAMPLES_FFMPEG): ffmpeg$(PROGSSUF)$(EXESUF)

$(FATE_FFPROBE) $(FATE_SAMPLES_FFPROBE): ffprobe$(PROGSSUF)$(EXESUF)

$(FATE_SAMPLES_FASTSTART): tools/qt-faststart$(EXESUF)

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

FATE_UTILS = base64 tiny_psnr tiny_ssim audiomatch

TOOL = ffmpeg

$(addprefix fate-, $(IGNORE_TESTS)): REPORT=ignore

fate:: $(FATE)

# Tests requiring hardware support are not included in a default fate run.
fate-hw: $(FATE_HW-yes)
FATE += $(FATE_HW-yes)

$(FATE) $(FATE_TESTS-no): export PROGSUF = $(PROGSSUF)
$(FATE) $(FATE_TESTS-no): export EXECSUF = $(EXESUF)
$(FATE) $(FATE_TESTS-no): export HOSTEXECSUF = $(HOSTEXESUF)
$(FATE) $(FATE_TESTS-no): $(FATE_UTILS:%=tests/%$(HOSTEXESUF)) | $(FATE_OUTDIRS)
	@echo "TEST    $(@:fate-%=%)"
	$(Q)$(SRC_PATH)/tests/fate-run.sh $@ "$(TARGET_SAMPLES)" "$(TARGET_EXEC)" "$(TARGET_PATH)" '$(CMD)' '$(CMP)' '$(REF)' '$(FUZZ)' '$(THREADS)' '$(THREAD_TYPE)' '$(CPUFLAGS)' '$(CMP_SHIFT)' '$(CMP_TARGET)' '$(SIZE_TOLERANCE)' '$(CMP_UNIT)' '$(GEN)' '$(HWACCEL)' '$(REPORT)' '$(KEEP)'

fate-list:
	@printf '%s\n' $(sort $(FATE))

coverage.info: TAG = LCOV
coverage.info:
	$(M)lcov -q -d $(CURDIR) -b $(patsubst src%,./,$(SRC_LINK)) --capture | \
	    sed "s,$(CURDIR)/\./,$(CURDIR)/," > $@.in
	$(M)lcov -q --remove $@.in "/usr*" > $@
	$(Q)$(RM) $@.in

lcov:  TAG = GENHTML
lcov: coverage.info
	$(M)genhtml -q -o $(CURDIR)/lcov $<

lcov-reset: TAG = LCOV
lcov-reset:
	$(M)lcov -q -d $(CURDIR) --zerocounters
	$(Q)$(RM) -f coverage.info

clean:: testclean

testclean::
	$(RM) -r tests/vsynth1 tests/data tools/lavfi-showfiltfmts$(PROGSSUF)$(EXESUF)
	$(RM) $(CLEANSUFFIXES:%=tests/%)
	$(RM) $(TESTTOOLS:%=tests/%$(HOSTEXESUF))
	$(RM) tests/pixfmts.mak tests/test_copy.ffmeta

-include $(wildcard tests/*.d)

include $(SRC_PATH)/tests/checkasm/Makefile

.PHONY: fate* lcov lcov-reset
.INTERMEDIATE: coverage.info
