FATE_VP3-$(call DEMDEC, MATROSKA, THEORA) += fate-theora-coeff-level64
fate-theora-coeff-level64: CMD = framecrc -flags +bitexact -i $(TARGET_SAMPLES)/vp3/coeff_level64.mkv

FATE_VP3-$(call DEMDEC, OGG, THEORA) += fate-theora-offset
fate-theora-offset: CMD = framecrc -flags +bitexact -i $(TARGET_SAMPLES)/vp3/offset_test.ogv

FATE_VP3-$(call DEMDEC, AVI, VP3) += fate-vp31
fate-vp31: CMD = framecrc -flags +bitexact -i $(TARGET_SAMPLES)/vp3/vp31.avi

FATE_SAMPLES_AVCONV += $(FATE_VP3-yes)
fate-vp3: $(FATE_VP3-yes)

FATE_SAMPLES_AVCONV-$(call DEMDEC, AVI, VP5) += fate-vp5
fate-vp5: CMD = framecrc -flags +bitexact -i $(TARGET_SAMPLES)/vp5/potter512-400-partial.avi -an

FATE_VP6-$(call DEMDEC, EA, VP6) += fate-vp60
fate-vp60: CMD = framecrc -flags +bitexact -i $(TARGET_SAMPLES)/ea-vp6/g36.vp6

FATE_VP6-$(call DEMDEC, EA, VP6) += fate-vp61
fate-vp61: CMD = framecrc -flags +bitexact -i $(TARGET_SAMPLES)/ea-vp6/MovieSkirmishGondor.vp6 -t 4

FATE_VP6-$(call DEMDEC, FLV, VP6A) += fate-vp6a
fate-vp6a: CMD = framecrc -flags +bitexact -i $(TARGET_SAMPLES)/flash-vp6/300x180-Scr-f8-056alpha.flv

FATE_VP6-$(call DEMDEC, FLV, VP6A) += fate-vp6a-skip_alpha
fate-vp6a-skip_alpha: CMD = framecrc -flags +bitexact -skip_alpha 1 -i $(TARGET_SAMPLES)/flash-vp6/300x180-Scr-f8-056alpha.flv

FATE_VP6-$(call DEMDEC, FLV, VP6F) += fate-vp6f
fate-vp6f: CMD = framecrc -flags +bitexact -i $(TARGET_SAMPLES)/flash-vp6/clip1024.flv

FATE_VP8-$(call DEMDEC, FLV, VP8) += fate-vp8-alpha
fate-vp8-alpha: CMD = framecrc -i $(TARGET_SAMPLES)/vp8_alpha/vp8_video_with_alpha.webm -vcodec copy

FATE_VP8-$(call DEMDEC, WEBM_DASH_MANIFEST, VP8) += fate-webm-dash-manifest
fate-webm-dash-manifest: CMD = run ffmpeg -f webm_dash_manifest -i $(TARGET_SAMPLES)/vp8/dash_video1.webm -f webm_dash_manifest -i $(TARGET_SAMPLES)/vp8/dash_video2.webm -f webm_dash_manifest -i $(TARGET_SAMPLES)/vp8/dash_audio1.webm -f webm_dash_manifest -i $(TARGET_SAMPLES)/vp8/dash_audio2.webm -c copy -map 0 -map 1 -map 2 -map 3 -f webm_dash_manifest -adaptation_sets "id=0,streams=0,1 id=1,streams=2,3" -

FATE_VP8-$(call DEMDEC, WEBM_DASH_MANIFEST, VP8) += fate-webm-dash-manifest-unaligned-video-streams
fate-webm-dash-manifest-unaligned-video-streams: CMD = run ffmpeg -f webm_dash_manifest -i $(TARGET_SAMPLES)/vp8/dash_video1.webm -f webm_dash_manifest -i $(TARGET_SAMPLES)/vp8/dash_video3.webm -c copy -map 0 -map 1 -f webm_dash_manifest -adaptation_sets "id=0,streams=0,1" -

FATE_VP8-$(call DEMDEC, WEBM_DASH_MANIFEST, VP8) += fate-webm-dash-manifest-unaligned-audio-streams
fate-webm-dash-manifest-unaligned-audio-streams: CMD = run ffmpeg -f webm_dash_manifest -i $(TARGET_SAMPLES)/vp8/dash_audio1.webm -f webm_dash_manifest -i $(TARGET_SAMPLES)/vp8/dash_audio3.webm -c copy -map 0 -map 1 -f webm_dash_manifest -adaptation_sets "id=0,streams=0,1" -

FATE_VP8-$(call DEMDEC, WEBM_DASH_MANIFEST, VP8) += fate-webm-dash-manifest-representations
fate-webm-dash-manifest-representations: CMD = run ffmpeg -f webm_dash_manifest -i $(TARGET_SAMPLES)/vp8/dash_video1.webm -f webm_dash_manifest -i $(TARGET_SAMPLES)/vp8/dash_video4.webm -c copy -map 0 -map 1 -f webm_dash_manifest -adaptation_sets "id=0,streams=0,1" -

FATE_VP8-$(call DEMDEC, WEBM_DASH_MANIFEST, VP8) += fate-webm-dash-manifest-live
fate-webm-dash-manifest-live: CMD = run ffmpeg -f webm_dash_manifest -live 1 -i $(TARGET_SAMPLES)/vp8/dash_live_video_360.hdr -f webm_dash_manifest -live 1 -i $(TARGET_SAMPLES)/vp8/dash_live_audio_171.hdr -c copy -map 0 -map 1 -f webm_dash_manifest -live 1 -adaptation_sets "id=0,streams=0 id=1,streams=1" -chunk_start_index 1 -chunk_duration_ms 5000 -time_shift_buffer_depth 7200 -minimum_update_period 60 -debug_mode 1 -

FATE_SAMPLES_AVCONV += $(FATE_VP6-yes)
fate-vp6: $(FATE_VP6-yes)

FATE_SAMPLES_AVCONV-$(call DEMDEC, AVI, VP7) += fate-vp7
fate-vp7: CMD = framecrc -flags +bitexact -i $(TARGET_SAMPLES)/vp7/potter-40.vp7 -frames 30 -an

VP8_SUITE = 001 002 003 004 005 006 007 008 009 010 011 012 013 014 015 016 017

define FATE_VP8_SUITE
FATE_VP8-$(CONFIG_IVF_DEMUXER) += fate-vp8-test-vector$(2)-$(1)
fate-vp8-test-vector$(2)-$(1): CMD = framemd5 $(3) -i $(TARGET_SAMPLES)/vp8-test-vectors-r1/vp80-00-comprehensive-$(1).ivf
fate-vp8-test-vector$(2)-$(1): REF = $(SRC_PATH)/tests/ref/fate/vp8-test-vector-$(1)
endef

define FATE_VP8_FULL
$(foreach N,$(VP8_SUITE),$(eval $(call FATE_VP8_SUITE,$(N),$(1),$(2))))

# FIXME this file contains two frames with identical timestamps,
# so ffmpeg drops one of them
FATE_VP8-$(CONFIG_IVF_DEMUXER) += fate-vp8-sign-bias$(1)
fate-vp8-sign-bias$(1): CMD = framemd5 $(2) -i $(TARGET_SAMPLES)/vp8/sintel-signbias.ivf
fate-vp8-sign-bias$(1): REF = $(SRC_PATH)/tests/ref/fate/vp8-sign-bias

FATE_VP8-$(CONFIG_MATROSKA_DEMUXER) += fate-vp8-size-change$(1)
fate-vp8-size-change$(1): CMD = framemd5 $(2) -flags +bitexact -i $(TARGET_SAMPLES)/vp8/frame_size_change.webm -frames:v 30 -sws_flags bitexact+bilinear
fate-vp8-size-change$(1): REF = $(SRC_PATH)/tests/ref/fate/vp8-size-change
endef

$(eval $(call FATE_VP8_FULL))

FATE_SAMPLES_AVCONV-$(CONFIG_VP8_DECODER) += $(FATE_VP8-yes)
fate-vp8: $(FATE_VP8-yes)

define FATE_VP9_SUITE
FATE_VP9-$(CONFIG_MATROSKA_DEMUXER) += fate-vp9$(2)-$(1)
fate-vp9$(2)-$(1): CMD = framemd5 $(3) -i $(TARGET_SAMPLES)/vp9-test-vectors/vp90-2-$(1).webm
fate-vp9$(2)-$(1): REF = $(SRC_PATH)/tests/ref/fate/vp9-$(1)
endef

define FATE_VP9_PROFILE_SUITE
FATE_VP9-$(CONFIG_MATROSKA_DEMUXER) += fate-vp9p$(2)-$(1)
fate-vp9p$(2)-$(1): CMD = framemd5 -i $(TARGET_SAMPLES)/vp9-test-vectors/vp9$(2)-2-$(1).webm $(3)
fate-vp9p$(2)-$(1): REF = $(SRC_PATH)/tests/ref/fate/vp9p$(2)-$(1)
endef

VP9_Q = 00 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 \
        16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 \
        32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 \
        48 49 50 51 52 53 54 55 56 57 58 59 60 61 62 63
VP9_SHARP = 1 2 3 4 5 6 7
VP9_SIZE_A = 08 10 16 18 32 34 64 66
VP9_SIZE_B = 196 198 200 202 208 210 224 226
VP9_CHROMA_SUBSAMPLE = 422 440 444
VP9_HIGH_BITDEPTH = 10 12

define FATE_VP9_FULL
$(foreach Q,$(VP9_Q),$(eval $(call FATE_VP9_SUITE,00-quantizer-$(Q))))
$(foreach SHARP,$(VP9_SHARP),$(eval $(call FATE_VP9_SUITE,01-sharpness-$(SHARP))))
$(foreach W,$(VP9_SIZE_A),$(eval $(foreach H,$(VP9_SIZE_A),$(eval $(call FATE_VP9_SUITE,02-size-$(W)x$(H))))))
$(foreach W,$(VP9_SIZE_B),$(eval $(foreach H,$(VP9_SIZE_B),$(eval $(call FATE_VP9_SUITE,03-size-$(W)x$(H))))))
$(eval $(call FATE_VP9_SUITE,03-deltaq))
$(foreach SS,$(VP9_CHROMA_SUBSAMPLE),$(eval $(call FATE_VP9_PROFILE_SUITE,04-yuv$(SS),1,)))
$(foreach BD,$(VP9_HIGH_BITDEPTH),$(eval $(call FATE_VP9_PROFILE_SUITE,20-$(BD)bit-yuv420,2,-pix_fmt yuv420p$(BD)le)))
$(foreach BD,$(VP9_HIGH_BITDEPTH),$(eval $(foreach SS,$(VP9_CHROMA_SUBSAMPLE),$(eval $(call FATE_VP9_PROFILE_SUITE,20-$(BD)bit-yuv$(SS),3,-pix_fmt yuv$(SS)p$(BD)le)))))
$(eval $(call FATE_VP9_SUITE,06-bilinear))
$(eval $(call FATE_VP9_SUITE,09-lf_deltas))
$(eval $(call FATE_VP9_SUITE,10-show-existing-frame))
$(eval $(call FATE_VP9_SUITE,10-show-existing-frame2))
$(eval $(call FATE_VP9_SUITE,15-segkey_adpq))
$(eval $(call FATE_VP9_SUITE,16-intra-only))
$(eval $(call FATE_VP9_SUITE,2pass-akiyo))
$(eval $(call FATE_VP9_SUITE,parallelmode-akiyo))
$(eval $(call FATE_VP9_SUITE,segmentation-aq-akiyo))
$(eval $(call FATE_VP9_SUITE,segmentation-sf-akiyo))
$(eval $(call FATE_VP9_SUITE,tiling-pedestrian))
$(eval $(call FATE_VP9_SUITE,trac3849))
$(eval $(call FATE_VP9_SUITE,trac4359))
endef

$(eval $(call FATE_VP9_FULL))
FATE_VP9-$(CONFIG_IVF_DEMUXER) += fate-vp9-05-resize
fate-vp9-05-resize: CMD = framemd5 -i $(TARGET_SAMPLES)/vp9-test-vectors/vp90-2-05-resize.ivf -s 352x288 -sws_flags bitexact+bilinear
fate-vp9-05-resize: REF = $(SRC_PATH)/tests/ref/fate/vp9-05-resize

FATE_SAMPLES_AVCONV-$(CONFIG_VP9_DECODER) += $(FATE_VP9-yes)
fate-vp9: $(FATE_VP9-yes)
