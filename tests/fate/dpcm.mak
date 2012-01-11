FATE_TESTS += fate-dpcm-idroq
fate-dpcm-idroq: CMD = framecrc -i $(SAMPLES)/idroq/idlogo.roq

FATE_TESTS += fate-dpcm-sierra
fate-dpcm-sierra: CMD = md5 -i $(SAMPLES)/sol/lsl7sample.sol -f s16le

FATE_TESTS += fate-dpcm-xan
fate-dpcm-xan: CMD = md5 -i $(SAMPLES)/wc4-xan/wc4_2.avi -vn -f s16le

