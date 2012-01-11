FATE_PCM += fate-ea-mad-pcm-planar
fate-ea-mad-pcm-planar: CMD = framecrc -i $(SAMPLES)/ea-mad/xeasport.mad

FATE_PCM += fate-film-cvid-pcm-stereo-8bit
fate-film-cvid-pcm-stereo-8bit: CMD = framecrc -i $(SAMPLES)/film/logo-capcom.cpk

FATE_PCM += fate-iff-pcm
fate-iff-pcm: CMD = md5 -i $(SAMPLES)/iff/Bells -f s16le

FATE_PCM += fate-pcm_dvd
fate-pcm_dvd: CMD = framecrc -i $(SAMPLES)/pcm-dvd/coolitnow-partial.vob -vn

FATE_PCM += fate-pcm_s16be-stereo
fate-pcm_s16be-stereo: CMD = md5 -i $(SAMPLES)/qt-surge-suite/surge-2-16-B-twos.mov -f s16le

FATE_PCM += fate-pcm_s16le-stereo
fate-pcm_s16le-stereo: CMD = md5 -i $(SAMPLES)/qt-surge-suite/surge-2-16-L-sowt.mov -f s16le

FATE_PCM += fate-pcm_u8-mono
fate-pcm_u8-mono: CMD = md5 -i $(SAMPLES)/qt-surge-suite/surge-1-8-raw.mov -f s16le

FATE_PCM += fate-pcm_u8-stereo
fate-pcm_u8-stereo: CMD = md5 -i $(SAMPLES)/qt-surge-suite/surge-2-8-raw.mov -f s16le

FATE_PCM += fate-w64
fate-w64: CMD = crc -i $(SAMPLES)/w64/w64-pcm16.w64

FATE_TESTS += $(FATE_PCM)
fate-pcm: $(FATE_PCM)
