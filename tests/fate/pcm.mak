FATE_SAMPLES_PCM-$(call DEMDEC, WAV, PCM_U8) += fate-iff-pcm
fate-iff-pcm: CMD = md5 -i $(TARGET_SAMPLES)/iff/Bells -f s16le -af aresample

FATE_SAMPLES_PCM-$(call DEMDEC, MPEGPS, PCM_DVD) += fate-pcm_dvd
fate-pcm_dvd: CMD = framecrc -i $(TARGET_SAMPLES)/pcm-dvd/coolitnow-partial.vob -vn -af aresample

FATE_SAMPLES_PCM-$(call DEMDEC, EA, PCM_S16LE_PLANAR) += fate-pcm-planar
fate-pcm-planar: CMD = framecrc -i $(TARGET_SAMPLES)/ea-mad/xeasport.mad -vn -af aresample

FATE_SAMPLES_PCM-$(call DEMDEC, MOV, PCM_S16BE) += fate-pcm_s16be-stereo
fate-pcm_s16be-stereo: CMD = md5 -i $(TARGET_SAMPLES)/qt-surge-suite/surge-2-16-B-twos.mov -f s16le

FATE_SAMPLES_PCM-$(call DEMDEC, MOV, PCM_S16LE) += fate-pcm_s16le-stereo
fate-pcm_s16le-stereo: CMD = md5 -i $(TARGET_SAMPLES)/qt-surge-suite/surge-2-16-L-sowt.mov -f s16le

FATE_SAMPLES_PCM-$(call DEMDEC, MOV, PCM_U8) += fate-pcm_u8-mono
fate-pcm_u8-mono: CMD = md5 -i $(TARGET_SAMPLES)/qt-surge-suite/surge-1-8-raw.mov -f s16le -af aresample

FATE_SAMPLES_PCM-$(call DEMDEC, MOV, PCM_U8) += fate-pcm_u8-stereo
fate-pcm_u8-stereo: CMD = md5 -i $(TARGET_SAMPLES)/qt-surge-suite/surge-2-8-raw.mov -f s16le -af aresample

FATE_SAMPLES_PCM-$(call DEMDEC, W64, PCM_S16LE) += fate-w64
fate-w64: CMD = crc -i $(TARGET_SAMPLES)/w64/w64-pcm16.w64

FATE_PCM-$(call ENCMUX, PCM_S24DAUD, DAUD) += fate-dcinema-encode
fate-dcinema-encode: tests/data/asynth-96000-6.wav
fate-dcinema-encode: SRC = tests/data/asynth-96000-6.wav
fate-dcinema-encode: CMD = enc_dec_pcm daud framemd5 s16le $(SRC) -c:a pcm_s24daud -frames:a 20

FATE_FFMPEG += $(FATE_PCM-yes)
FATE_SAMPLES_AVCONV += $(FATE_SAMPLES_PCM-yes)
fate-pcm: $(FATE_PCM-yes) $(FATE_SAMPLES_PCM-yes)
