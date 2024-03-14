FATE_SAMPLES_PCM-$(call DEMDEC, WAV, PCM_U8, ARESAMPLE_FILTER) += fate-iff-pcm
fate-iff-pcm: CMD = md5 -i $(TARGET_SAMPLES)/iff/Bells -f s16le -af aresample

FATE_SAMPLES_PCM-$(call DEMDEC, MPEGPS, PCM_DVD, ARESAMPLE_FILTER) += fate-pcm_dvd
fate-pcm_dvd: CMD = framecrc -i $(TARGET_SAMPLES)/pcm-dvd/coolitnow-partial.vob -vn -af aresample

FATE_SAMPLES_PCM-$(call DEMDEC, EA, PCM_S16LE_PLANAR, ARESAMPLE_FILTER) += fate-pcm-planar
fate-pcm-planar: CMD = framecrc -i $(TARGET_SAMPLES)/ea-mad/xeasport.mad -vn -af aresample

FATE_SAMPLES_PCM-$(call DEMDEC, MOV, PCM_S16BE) += fate-pcm_s16be-stereo
fate-pcm_s16be-stereo: CMD = md5 -i $(TARGET_SAMPLES)/qt-surge-suite/surge-2-16-B-twos.mov -f s16le

FATE_SAMPLES_PCM-$(call DEMDEC, MOV, PCM_S16LE) += fate-pcm_s16le-stereo
fate-pcm_s16le-stereo: CMD = md5 -i $(TARGET_SAMPLES)/qt-surge-suite/surge-2-16-L-sowt.mov -f s16le

FATE_SAMPLES_PCM-$(call DEMDEC, MOV, PCM_U8, ARESAMPLE_FILTER) += fate-pcm_u8-mono
fate-pcm_u8-mono: CMD = md5 -i $(TARGET_SAMPLES)/qt-surge-suite/surge-1-8-raw.mov -f s16le -af aresample

FATE_SAMPLES_PCM-$(call DEMDEC, MOV, PCM_U8, ARESAMPLE_FILTER) += fate-pcm_u8-stereo
fate-pcm_u8-stereo: CMD = md5 -i $(TARGET_SAMPLES)/qt-surge-suite/surge-2-8-raw.mov -f s16le -af aresample

FATE_SAMPLES_PCM-$(call DEMDEC, W64, PCM_S16LE) += fate-w64
fate-w64: CMD = crc -i $(TARGET_SAMPLES)/w64/w64-pcm16.w64

FATE_PCM-$(call ENCMUX, PCM_S24DAUD, DAUD) += fate-dcinema-encode
fate-dcinema-encode: tests/data/asynth-96000-6.wav
fate-dcinema-encode: SRC = tests/data/asynth-96000-6.wav
fate-dcinema-encode: CMD = enc_dec_pcm daud framemd5 s16le $(SRC) -c:a pcm_s24daud -frames:a 20

FATE_SAMPLES_PCM-$(call TRANSCODE, PCM_DVD, MPEG2VOB MPEGPS, TRUEHD_DEMUXER TRUEHD_DECODER PCM_S24LE_ENCODER) += fate-pcm_dvd-24-7.1-48000
fate-pcm_dvd-24-7.1-48000: CMD = transcode truehd $(TARGET_SAMPLES)/truehd/atmos.thd vob "-c:a pcm_dvd" "-c:a pcm_s24le"

FATE_SAMPLES_PCM-$(call TRANSCODE, PCM_DVD, MPEG2VOB MPEGPS, MXF_DEMUXER PCM_S16LE_DECODER) += fate-pcm_dvd-16-7.1-48000
fate-pcm_dvd-16-7.1-48000: CMD = transcode mxf $(TARGET_SAMPLES)/mxf/Sony-00001.mxf vob "-map 0:a -c:a pcm_dvd"

FATE_SAMPLES_PCM-$(call TRANSCODE, PCM_DVD, MPEG2VOB MPEGPS, DAUD_DEMUXER PCM_S24DAUD_DECODER) += fate-pcm_dvd-16-5.1-96000
fate-pcm_dvd-16-5.1-96000: CMD = transcode daud $(TARGET_SAMPLES)/d-cinema/THX_Science_FLT_1920-partial.302 vob "-c:a pcm_dvd"

FATE_SAMPLES_PCM-$(call TRANSCODE, PCM_DVD, MPEG2VOB MPEGPS, TRUEHD_DEMUXER TRUEHD_DECODER PCM_S24LE_ENCODER) += fate-pcm_dvd-24-5.1-48000
fate-pcm_dvd-24-5.1-48000: CMD = transcode truehd $(TARGET_SAMPLES)/lossless-audio/truehd_5.1.raw vob "-c:a pcm_dvd" "-c:a pcm_s24le -t 0.2"

FATE_SAMPLES_PCM-$(call TRANSCODE, PCM_DVD, MPEG2VOB MPEGPS, MATROSKA_DEMUXER FLAC_DECODER) += fate-pcm_dvd-16-5.1-48000
fate-pcm_dvd-16-5.1-48000: CMD = transcode matroska $(TARGET_SAMPLES)/mkv/flac_channel_layouts.mka vob "-map 0:a:1 -c:a pcm_dvd" "-t 0.2"

FATE_SAMPLES_PCM-$(call TRANSCODE, PCM_DVD, MPEG2VOB MPEGPS, FLAC_DEMUXER FLAC_PARSER FLAC_DECODER PCM_S24LE_ENCODER) += fate-pcm_dvd-24-2-48000
fate-pcm_dvd-24-2-48000: CMD = transcode flac $(TARGET_SAMPLES)/filter/seq-3341-7_seq-3342-5-24bit.flac vob "-c:a pcm_dvd" "-c:a pcm_s24le -t 0.2"

FATE_SAMPLES_PCM-$(call TRANSCODE, PCM_DVD, MPEG2VOB MPEGPS, WAV_DEMUXER PCM_S16LE_DECODER) += fate-pcm_dvd-16-2-48000
fate-pcm_dvd-16-2-48000: CMD = transcode wav $(TARGET_SAMPLES)/wav/200828-005.wav vob "-c:a pcm_dvd" "-t 0.2"

FATE_SAMPLES_PCM-$(call TRANSCODE, PCM_DVD, MPEG2VOB MPEGPS, MXF_DEMUXER PCM_S24LE_DECODER PCM_S24LE_ENCODER) += fate-pcm_dvd-24-1-48000
fate-pcm_dvd-24-1-48000: CMD = transcode mxf $(TARGET_SAMPLES)/mxf/omneon_8.3.0.0_xdcam_startc_footer.mxf vob "-map 0:a:0 -c:a pcm_dvd" "-c:a pcm_s24le"

FATE_SAMPLES_PCM-$(call TRANSCODE, PCM_DVD, MPEG2VOB MPEGPS, MXF_DEMUXER PCM_S16LE_DECODER) += fate-pcm_dvd-16-1-48000
fate-pcm_dvd-16-1-48000: CMD = transcode mxf $(TARGET_SAMPLES)/mxf/opatom_missing_index.mxf vob "-c:a pcm_dvd"

FATE_PCM-$(call TRANSCODE, PCM_DVD, MPEG2VOB MPEGPS, WAV_DEMUXER PCM_S16LE_DECODER) += fate-pcm_dvd-16-1-96000
fate-pcm_dvd-16-1-96000: tests/data/asynth-96000-1.wav
fate-pcm_dvd-16-1-96000: CMD = transcode wav $(TARGET_PATH)/tests/data/asynth-96000-1.wav vob "-c:a pcm_dvd" "-t 0.2"

FATE_FFMPEG += $(FATE_PCM-yes)
FATE_SAMPLES_AVCONV += $(FATE_SAMPLES_PCM-yes)
fate-pcm: $(FATE_PCM-yes) $(FATE_SAMPLES_PCM-yes)
