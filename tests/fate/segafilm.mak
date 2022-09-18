FATE_SEGAFILM-$(call REMUX, SEGAFILM, CINEPAK_DECODER ADX_PARSER) += fate-segafilm-adx-remux
fate-segafilm-adx-remux: CMD = transcode film_cpk $(TARGET_SAMPLES)/film/op-partial.cak film_cpk "-c copy" "-c copy"

FATE_SEGAFILM-$(call REMUX, SEGAFILM, CINEPAK_DECODER) += fate-segafilm-s8-remux
fate-segafilm-s8-remux: CMD = transcode film_cpk $(TARGET_SAMPLES)/film/logo-capcom.cpk film_cpk "-c copy" "-c copy"

# This tests muxing non-segafilm cinepak into segafilm.
FATE_SEGAFILM-$(call REMUX, SEGAFILM, AVI_DEMUXER CINEPAK_DECODER) += fate-segafilm-cinepak-mux
fate-segafilm-cinepak-mux: CMD = transcode avi $(TARGET_SAMPLES)/cvid/pcitva15.avi film_cpk "-map 0:v -c copy" "-c copy"

FATE_SEGAFILM-$(call TRANSCODE, RAWVIDEO CINEPAK, SEGAFILM, AVI_DEMUXER PCM_U8_DECODER ARESAMPLE_FILTER PCM_S16BE_PLANAR_ENCODER) += fate-segafilm-rawvideo-mux
fate-segafilm-rawvideo-mux: CMD = transcode avi $(TARGET_SAMPLES)/cvid/laracroft-cinepak-partial.avi film_cpk "-c:v rawvideo -pix_fmt rgb24 -af aresample -c:a pcm_s16be_planar" "-c copy"

FATE_SAMPLES_FFMPEG += $(FATE_SEGAFILM-yes)
fate-segafilm: $(FATE_SEGAFILM-yes)
