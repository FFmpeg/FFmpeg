
MPEG4_RESOLUTION_CHANGE = down-down down-up up-down up-up

fate-mpeg4-resolution-change-%: CMD = framemd5 -flags +bitexact -idct simple -i $(TARGET_SAMPLES)/mpeg4/resize_$(@:fate-mpeg4-resolution-change-%=%).h263 -sws_flags +bitexact

FATE_MPEG4-$(call FRAMEMD5, M4V, MPEG4, SCALE_FILTER) := $(addprefix fate-mpeg4-resolution-change-, $(MPEG4_RESOLUTION_CHANGE))

fate-mpeg4-bsf-unpack-bframes: CMD = md5 -i $(TARGET_SAMPLES)/mpeg4/packed_bframes.avi -flags +bitexact -fflags +bitexact -c:v copy -bsf mpeg4_unpack_bframes -f avi
FATE_MPEG4-$(call DEMMUX, AVI, AVI, MPEG4_UNPACK_BFRAMES_BSF) += fate-mpeg4-bsf-unpack-bframes

fate-mpeg4-packed: CMD = framecrc -flags +bitexact -idct simple -i $(TARGET_SAMPLES)/mpeg4/packed_bframes.avi -flags +bitexact -fflags +bitexact -fps_mode cfr
FATE_MPEG4-$(call FRAMECRC, AVI, MPEG4) += fate-mpeg4-packed

FATE_MPEG4-$(call ALLYES, FILE_PROTOCOL M4V_DEMUXER MPEG4_DECODER SCALE_FILTER \
                          RAWVIDEO_ENCODER FRAMECRC_MUXER PIPE_PROTOCOL) \
                          += fate-mpeg4-simple-studio-profile
fate-mpeg4-simple-studio-profile: CMD = framecrc -bitexact -idct simple -i $(TARGET_SAMPLES)/mpeg4/mpeg4_sstp_dpcm.m4v -sws_flags +accurate_rnd+bitexact -pix_fmt yuv422p10le -vf scale

FATE_MPEG4-$(call FRAMECRC, M4V, MPEG4) += fate-m4v
fate-m4v:     CMD = framecrc -flags +bitexact -idct simple -i $(TARGET_SAMPLES)/mpeg4/demo.m4v

FATE_MPEG4-$(call FRAMECRC, M4V, MPEG4, FPS_FILTER) += fate-m4v-cfr
fate-m4v-cfr: CMD = framecrc -flags +bitexact -idct simple -i $(TARGET_SAMPLES)/mpeg4/demo.m4v -vf fps=5

FATE_SAMPLES_AVCONV += $(FATE_MPEG4-yes)
fate-mpeg4: $(FATE_MPEG4-yes)
