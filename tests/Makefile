
ifneq ($(strip $(KEEP)),)
ifneq ($(strip $(KEEP)),0)
# KEEP_FILES is normally undefined; it is set to 2 # if the user requested
# to keep all intermediate FATE-files by setting KEEP.
# For some tests it is also set to 1 if it is unset; this indicates
# that only some intermediate files (namely only non-raw files)
# are to be kept. This allows reusing these intermediate files as input
# files for other tests (mostly the seek-tests).
KEEP_FILES = 2
endif
endif

THREADS = 1
VREF = tests/vsynth1/00.pgm
AREF = tests/data/asynth1.sw

FATEW = 34
FATEH = 34

FFMPEG=ffmpeg$(PROGSSUF)$(EXESUF)

$(AREF): CMP=

APITESTSDIR := tests/api
FATE_OUTDIRS = tests/data tests/data/fate tests/data/filtergraphs tests/data/lavf tests/data/lavf-fate tests/data/pixfmt tests/vsynth1 $(APITESTSDIR)
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
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
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
                         $(firstword $(2))_MUXER   $(lastword $(2))_DEMUXER  \
                         $(3) FILE_PROTOCOL)

ENCDEC2 = $(call ALLYES, $(firstword $(1))_ENCODER $(lastword $(1))_DECODER  \
                         $(firstword $(2))_ENCODER $(lastword $(2))_DECODER  \
                         $(firstword $(3))_MUXER   $(lastword $(3))_DEMUXER  \
                         $(4) FILE_PROTOCOL)

# RAWVIDEO_ENCODER and PCM_S16LE_ENCODER corresponds to the default codecs
# for framecrc. These requirements are not always necessary.
TRANSCODE = $(call ALLYES, $(firstword $(1))_ENCODER $(lastword $(1))_DECODER \
                           $(firstword $(2))_MUXER   $(lastword $(2))_DEMUXER \
                           $(3) FILE_PROTOCOL PIPE_PROTOCOL RAWVIDEO_ENCODER  \
                           PCM_S16LE_ENCODER FRAMECRC_MUXER)

REMUX = $(call ALLYES, $(firstword $(1))_MUXER $(lastword $(1))_DEMUXER \
                       $(2) FILE_PROTOCOL PIPE_PROTOCOL FRAMECRC_MUXER)

DEMDEC  = $(call ALLYES, $(1)_DEMUXER $(2:%=%_DECODER) $(3) FILE_PROTOCOL)
ENCMUX  = $(call ALLYES, $(1:%=%_ENCODER) $(2)_MUXER   $(3))

FRAMEMD5 = $(call ALLYES, $(1)_DEMUXER $(2:%=%_DECODER) $(3)                  \
                          PCM_S16LE_ENCODER RAWVIDEO_ENCODER FRAMEMD5_MUXER   \
                          PIPE_PROTOCOL FILE_PROTOCOL)
FRAMECRC = $(call ALLYES, $(1)_DEMUXER $(2:%=%_DECODER) $(3)                  \
                          PCM_S16LE_ENCODER RAWVIDEO_ENCODER FRAMECRC_MUXER   \
                          PIPE_PROTOCOL FILE_PROTOCOL)

# Variant of DEMDEC for use with the "pcm" command.
PCM = $(call ALLYES, $(1)_DEMUXER $(2)_DECODER  $(3) FILE_PROTOCOL            \
                     PCM_S16LE_ENCODER PCM_S16LE_MUXER PIPE_PROTOCOL)

DEMMUX  = $(call ALLYES, $(1)_DEMUXER $(2)_MUXER $(3) FILE_PROTOCOL)

# Variant of FRAMECRC for the cases with -lavfi where no demuxer is involved.
FILTERFRAMECRC     = $(call ALLYES, $(1:%=%_FILTER) $(2) PCM_S16LE_ENCODER    \
                                    RAWVIDEO_ENCODER FRAMECRC_MUXER PIPE_PROTOCOL)
# Specialization of FRAMECRC to be used when filtering is involved.
FILTERDEMDEC       = $(call ALLYES, $(1:%=%_FILTER) $(2:%=%_DEMUXER) $(3:%=%_DECODER) \
                                    $(4) PCM_S16LE_ENCODER RAWVIDEO_ENCODER   \
                                    FRAMECRC_MUXER FILE_PROTOCOL PIPE_PROTOCOL)
FILTERDEMDECENCMUX = $(call ALLYES, $(1:%=%_FILTER) $(2)_DEMUXER $(3)_DECODER $(4)_ENCODER $(5)_MUXER $(6) FILE_PROTOCOL)

PARSERDEMDEC       = $(call ALLYES, $(1)_PARSER $(2)_DEMUXER $(3)_DECODER $(4) FILE_PROTOCOL)

# Allow overriding CONFIG_LARGE_TESTS via LARGE_TESTS, if set on the
# make command line.
ifeq ($(LARGE_TESTS), yes)
CONFIG_LARGE_TESTS:=yes
!CONFIG_LARGE_TESTS:=
else ifeq ($(LARGE_TESTS), no)
CONFIG_LARGE_TESTS:=
!CONFIG_LARGE_TESTS:=yes
endif

include $(SRC_PATH)/$(APITESTSDIR)/Makefile

include $(SRC_PATH)/tests/fate/acodec.mak
include $(SRC_PATH)/tests/fate/vcodec.mak

include $(SRC_PATH)/tests/fate/lavf-audio.mak
include $(SRC_PATH)/tests/fate/lavf-container.mak
include $(SRC_PATH)/tests/fate/lavf-image.mak
include $(SRC_PATH)/tests/fate/lavf-image2pipe.mak
include $(SRC_PATH)/tests/fate/lavf-video.mak
# Must be included after acodec.mak, vcodec.mak and lavf-*.mak
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
include $(SRC_PATH)/tests/fate/caf.mak
include $(SRC_PATH)/tests/fate/canopus.mak
include $(SRC_PATH)/tests/fate/cbs.mak
include $(SRC_PATH)/tests/fate/cdxl.mak
include $(SRC_PATH)/tests/fate/checkasm.mak
# Must be included after lavf-container.mak
include $(SRC_PATH)/tests/fate/concatdec.mak
include $(SRC_PATH)/tests/fate/cover-art.mak
include $(SRC_PATH)/tests/fate/dca.mak
include $(SRC_PATH)/tests/fate/demux.mak
include $(SRC_PATH)/tests/fate/dfa.mak
include $(SRC_PATH)/tests/fate/dnn.mak
include $(SRC_PATH)/tests/fate/dnxhd.mak
include $(SRC_PATH)/tests/fate/dpcm.mak
include $(SRC_PATH)/tests/fate/dvvideo.mak
include $(SRC_PATH)/tests/fate/ea.mak
include $(SRC_PATH)/tests/fate/exif.mak
# Must be included after lavf-video.mak
include $(SRC_PATH)/tests/fate/ffmpeg.mak
include $(SRC_PATH)/tests/fate/ffprobe.mak
include $(SRC_PATH)/tests/fate/fft.mak
include $(SRC_PATH)/tests/fate/fifo-muxer.mak
include $(SRC_PATH)/tests/fate/filter-audio.mak
# Must be included after vcodec.mak
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
include $(SRC_PATH)/tests/fate/imf.mak
include $(SRC_PATH)/tests/fate/indeo.mak
include $(SRC_PATH)/tests/fate/libavcodec.mak
include $(SRC_PATH)/tests/fate/libavdevice.mak
include $(SRC_PATH)/tests/fate/libavformat.mak
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
include $(SRC_PATH)/tests/fate/oma.mak
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
include $(SRC_PATH)/tests/fate/truehd.mak
include $(SRC_PATH)/tests/fate/utvideo.mak
include $(SRC_PATH)/tests/fate/vbn.mak
include $(SRC_PATH)/tests/fate/video.mak
include $(SRC_PATH)/tests/fate/voice.mak
include $(SRC_PATH)/tests/fate/vorbis.mak
include $(SRC_PATH)/tests/fate/vpx.mak
include $(SRC_PATH)/tests/fate/vqf.mak
include $(SRC_PATH)/tests/fate/wavpack.mak
include $(SRC_PATH)/tests/fate/webm-dash-manifest.mak
include $(SRC_PATH)/tests/fate/wma.mak
include $(SRC_PATH)/tests/fate/xvid.mak

FATE_FFMPEG += $(FATE_FFMPEG-yes) $(FATE_AVCONV) $(FATE_AVCONV-yes)
FATE-$(CONFIG_FFMPEG) += $(FATE_FFMPEG)
FATE-$(CONFIG_FFPROBE) += $(FATE_FFPROBE)
FATE-$(call ALLYES, FFMPEG FFPROBE) += $(FATE_FFMPEG_FFPROBE)

FATE_SAMPLES_AVCONV           += $(FATE_SAMPLES_AVCONV-yes)
FATE_SAMPLES_FFMPEG           += $(FATE_SAMPLES_FFMPEG-yes)
FATE_EXTERN-$(CONFIG_FFMPEG)  += $(FATE_SAMPLES_AVCONV) $(FATE_SAMPLES_FFMPEG)
FATE_EXTERN-$(CONFIG_FFPROBE) += $(FATE_SAMPLES_FFPROBE)
FATE_SAMPLES_FFMPEG_FFPROBE   += $(FATE_SAMPLES_FFMPEG_FFPROBE-yes)
FATE_EXTERN-$(call ALLYES, FFMPEG FFPROBE) += $(FATE_SAMPLES_FFMPEG_FFPROBE)
FATE_EXTERN                   += $(FATE_EXTERN-yes) $(FATE_SAMPLES_FASTSTART)

FATE += $(FATE-yes)

RSYNC_OPTIONS-$(HAVE_RSYNC_CONTIMEOUT) += --contimeout=60
RSYNC_OPTIONS = -vrltLW --timeout=60 $(RSYNC_OPTIONS-yes)

$(FATE_FFMPEG) $(FATE_FFMPEG_FFPROBE) $(FATE_SAMPLES_AVCONV) $(FATE_SAMPLES_FFMPEG) $(FATE_SAMPLES_FFMPEG_FFPROBE): ffmpeg$(PROGSSUF)$(EXESUF)

$(FATE_FFPROBE) $(FATE_FFMPEG_FFPROBE) $(FATE_SAMPLES_FFPROBE) $(FATE_SAMPLES_FFMPEG_FFPROBE): ffprobe$(PROGSSUF)$(EXESUF)

$(FATE_SAMPLES_FASTSTART): tools/qt-faststart$(EXESUF)
$(FATE_SAMPLES_DUMP_DATA) $(FATE_SAMPLES_DUMP_DATA-yes): tools/venc_data_dump$(EXESUF)
$(FATE_SAMPLES_SCALE_SLICE): tools/scale_slice_test$(EXESUF)

ifdef SAMPLES
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

$(FATE): export PROGSUF = $(PROGSSUF)
$(FATE): export EXECSUF = $(EXESUF)
$(FATE): export HOSTEXECSUF = $(HOSTEXESUF)
$(FATE): $(FATE_UTILS:%=tests/%$(HOSTEXESUF)) | $(FATE_OUTDIRS)
	@echo "TEST    $(@:fate-%=%)"
	$(Q)$(SRC_PATH)/tests/fate-run.sh $@ "$(TARGET_SAMPLES)" "$(TARGET_EXEC)" "$(TARGET_PATH)" '$(CMD)' '$(CMP)' '$(REF)' '$(FUZZ)' '$(THREADS)' '$(THREAD_TYPE)' '$(CPUFLAGS)' '$(CMP_SHIFT)' '$(CMP_TARGET)' '$(SIZE_TOLERANCE)' '$(CMP_UNIT)' '$(GEN)' '$(HWACCEL)' '$(REPORT)' '$(KEEP_FILES)'

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
