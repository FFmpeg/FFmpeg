# This tests that a 16-bit pcm_dvd stream is correctly remuxed in mpegps
FATE_MPEGPS-$(call DEMMUX, MPEGPS, MPEG1SYSTEM) += fate-mpegps-remuxed-pcm-demux
fate-mpegps-remuxed-pcm-demux: $(TARGET_SAMPLES)/mpegps/pcm_aud.mpg
fate-mpegps-remuxed-pcm-demux: CMD = stream_remux "mpeg" "$(TARGET_SAMPLES)/mpegps/pcm_aud.mpg" "mpeg" "-map 0:a:0" "-codec copy"

FATE_SAMPLES_FFMPEG += $(FATE_MPEGPS-yes)
fate-mpegps: $(FATE_MPEGPS-yes)
