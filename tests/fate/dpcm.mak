FATE_TESTS += fate-idroq-video-dpcm
fate-idroq-video-dpcm: CMD = framecrc -i $(SAMPLES)/idroq/idlogo.roq

FATE_TESTS += fate-dpcm-xan
fate-dpcm-xan: CMD = md5 -i $(SAMPLES)/wc4-xan/wc4_2.avi -vn -f s16le

