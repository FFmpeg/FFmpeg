FATE_TESTS += fate-duck-dk3
fate-duck-dk3: CMD = md5 -i $(SAMPLES)/duck/sop-audio-only.avi -f s16le

FATE_TESTS += fate-duck-dk4
fate-duck-dk4: CMD = md5 -i $(SAMPLES)/duck/salsa-audio-only.avi -f s16le

FATE_TESTS += fate-ea-mad-pcm-planar
fate-ea-mad-pcm-planar: CMD = framecrc -i $(SAMPLES)/ea-mad/xeasport.mad

FATE_TESTS += fate-film-cvid-pcm-stereo-8bit
fate-film-cvid-pcm-stereo-8bit: CMD = framecrc -i $(SAMPLES)/film/logo-capcom.cpk

FATE_TESTS += fate-iff-pcm
fate-iff-pcm: CMD = md5 -i $(SAMPLES)/iff/Bells -f s16le

FATE_TESTS += fate-pcm_dvd
fate-pcm_dvd: CMD = framecrc -i $(SAMPLES)/pcm-dvd/coolitnow-partial.vob -vn

FATE_TESTS += fate-qt-rawpcm-8bit-mono-unsigned
fate-qt-rawpcm-8bit-mono-unsigned: CMD = md5 -i $(SAMPLES)/qt-surge-suite/surge-1-8-raw.mov -f s16le

FATE_TESTS += fate-qt-rawpcm-8bit-stereo-unsigned
fate-qt-rawpcm-8bit-stereo-unsigned: CMD = md5 -i $(SAMPLES)/qt-surge-suite/surge-2-8-raw.mov -f s16le

FATE_TESTS += fate-qt-rawpcm-16bit-stereo-signed-be
fate-qt-rawpcm-16bit-stereo-signed-be: CMD = md5 -i $(SAMPLES)/qt-surge-suite/surge-2-16-B-twos.mov -f s16le

FATE_TESTS += fate-qt-rawpcm-16bit-stereo-signed-le
fate-qt-rawpcm-16bit-stereo-signed-le: CMD = md5 -i $(SAMPLES)/qt-surge-suite/surge-2-16-L-sowt.mov -f s16le

FATE_TESTS += fate-w64
fate-w64: CMD = crc -i $(SAMPLES)/w64/w64-pcm16.w64
