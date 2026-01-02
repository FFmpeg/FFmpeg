
MPEG4_RESOLUTION_CHANGE = down-down down-up up-down up-up

fate-mpeg4-resolution-change-%: CMD = framemd5 -flags +bitexact -idct simple -i $(TARGET_SAMPLES)/mpeg4/resize_$(@:fate-mpeg4-resolution-change-%=%).h263 -sws_flags +bitexact

FATE_MPEG4-$(call FRAMEMD5, M4V, MPEG4, MPEG4VIDEO_PARSER SCALE_FILTER) := $(addprefix fate-mpeg4-resolution-change-, $(MPEG4_RESOLUTION_CHANGE))

fate-mpeg4-bsf-unpack-bframes: CMD = md5 -i $(TARGET_SAMPLES)/mpeg4/packed_bframes.avi -flags +bitexact -fflags +bitexact -c:v copy -bsf mpeg4_unpack_bframes -f avi
FATE_MPEG4-$(call DEMMUX, AVI, AVI, MPEG4_UNPACK_BFRAMES_BSF) += fate-mpeg4-bsf-unpack-bframes

fate-mpeg4-packed: CMD = framecrc -flags +bitexact -idct simple -i $(TARGET_SAMPLES)/mpeg4/packed_bframes.avi -flags +bitexact -fflags +bitexact -fps_mode cfr
FATE_MPEG4-$(call FRAMECRC, AVI, MPEG4, MPEG4VIDEO_PARSER) += fate-mpeg4-packed

FATE_MPEG4-$(call FRAMECRC, M4V, MPEG4, MPEG4VIDEO_PARSER SCALE_FILTER) \
                          += fate-mpeg4-simple-studio-profile
fate-mpeg4-simple-studio-profile: CMD = framecrc -bitexact -idct simple -i $(TARGET_SAMPLES)/mpeg4/mpeg4_sstp_dpcm.m4v -sws_flags +accurate_rnd+bitexact -pix_fmt yuv422p10le -vf scale

FATE_MPEG4-$(call FRAMECRC, M4V, MPEG4, MPEG4VIDEO_PARSER) += fate-m4v
fate-m4v:     CMD = framecrc -flags +bitexact -idct simple -i $(TARGET_SAMPLES)/mpeg4/demo.m4v

FATE_MPEG4-$(call FRAMECRC, M4V, MPEG4, MPEG4VIDEO_PARSER FPS_FILTER) += fate-m4v-cfr
fate-m4v-cfr: CMD = framecrc -flags +bitexact -idct simple -i $(TARGET_SAMPLES)/mpeg4/demo.m4v -vf fps=5

# Test seeking in fragmented MP4 with separate audio/video fragments
# Seeks to 1.04s and extracts 1 frame - should land on I-frame at 1.0s with fix,
# lands at start (0s) without fix due to get_frag_time() bug
FATE_MPEG4-$(call FRAMECRC, MOV, H264, AAC_DECODER) += fate-mpeg4-fragmented-seek
fate-mpeg4-fragmented-seek: CMD = framecrc -use_mfra_for pts -ss 1.04 -copyts -noaccurate_seek -i $(TARGET_SAMPLES)/mpeg4/fragmented.mp4 -frames:v 1 -an

FATE_SAMPLES_AVCONV += $(FATE_MPEG4-yes)
fate-mpeg4: $(FATE_MPEG4-yes)
