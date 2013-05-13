FATE_VP3-$(call DEMDEC, MATROSKA, THEORA) += fate-theora-coeff-level64
fate-theora-coeff-level64: CMD = framecrc -flags +bitexact -i $(SAMPLES)/vp3/coeff_level64.mkv

FATE_VP3-$(call DEMDEC, AVI, VP3) += fate-vp31
fate-vp31: CMD = framecrc -flags +bitexact -i $(SAMPLES)/vp3/vp31.avi

FATE_SAMPLES_AVCONV += $(FATE_VP3-yes)
fate-vp3: $(FATE_VP3-yes)

FATE_SAMPLES_AVCONV-$(call DEMDEC, AVI, VP5) += fate-vp5
fate-vp5: CMD = framecrc -flags +bitexact -i $(SAMPLES)/vp5/potter512-400-partial.avi -an

FATE_VP6-$(call DEMDEC, EA, VP6) += fate-vp60
fate-vp60: CMD = framecrc -flags +bitexact -i $(SAMPLES)/ea-vp6/g36.vp6

FATE_VP6-$(call DEMDEC, EA, VP6) += fate-vp61
fate-vp61: CMD = framecrc -flags +bitexact -i $(SAMPLES)/ea-vp6/MovieSkirmishGondor.vp6 -t 4

FATE_VP6-$(call DEMDEC, FLV, VP6A) += fate-vp6a
fate-vp6a: CMD = framecrc -flags +bitexact -i $(SAMPLES)/flash-vp6/300x180-Scr-f8-056alpha.flv

FATE_VP6-$(call DEMDEC, FLV, VP6F) += fate-vp6f
fate-vp6f: CMD = framecrc -flags +bitexact -i $(SAMPLES)/flash-vp6/clip1024.flv

FATE_VP8-$(call DEMDEC, FLV, VP8) += fate-vp8-alpha
fate-vp8-alpha: CMD = framecrc -i $(SAMPLES)/vp8_alpha/vp8_video_with_alpha.webm -vcodec copy

FATE_SAMPLES_AVCONV += $(FATE_VP6-yes)
fate-vp6: $(FATE_VP6-yes)

VP8_SUITE = 001 002 003 004 005 006 007 008 009 010 011 012 013 014 015 016 017

define FATE_VP8_SUITE
FATE_VP8-$(CONFIG_IVF_DEMUXER) += fate-vp8-test-vector$(2)-$(1)
fate-vp8-test-vector$(2)-$(1): CMD = framemd5 $(3) -i $(SAMPLES)/vp8-test-vectors-r1/vp80-00-comprehensive-$(1).ivf
fate-vp8-test-vector$(2)-$(1): REF = $(SRC_PATH)/tests/ref/fate/vp8-test-vector-$(1)
endef

define FATE_VP8_FULL
$(foreach N,$(VP8_SUITE),$(eval $(call FATE_VP8_SUITE,$(N),$(1),$(2))))

# FIXME this file contains two frames with identical timestamps,
# so ffmpeg drops one of them
FATE_VP8-$(CONFIG_IVF_DEMUXER) += fate-vp8-sign-bias$(1)
fate-vp8-sign-bias$(1): CMD = framemd5 $(2) -i $(SAMPLES)/vp8/sintel-signbias.ivf
fate-vp8-sign-bias$(1): REF = $(SRC_PATH)/tests/ref/fate/vp8-sign-bias

FATE_VP8-$(CONFIG_MATROSKA_DEMUXER) += fate-vp8-size-change$(1)
fate-vp8-size-change$(1): CMD = framemd5 $(2) -flags +bitexact -i $(SAMPLES)/vp8/frame_size_change.webm -frames:v 30 -sws_flags bitexact+bilinear
fate-vp8-size-change$(1): REF = $(SRC_PATH)/tests/ref/fate/vp8-size-change
endef

$(eval $(call FATE_VP8_FULL))
$(eval $(call FATE_VP8_FULL,-emu-edge,-flags +emu_edge))
FATE_SAMPLES_AVCONV-$(CONFIG_VP8_DECODER) += $(FATE_VP8-yes)
fate-vp8: $(FATE_VP8-yes)
