FATE_DPCM += fate-dpcm-idroq
fate-dpcm-idroq: CMD = framecrc -i $(SAMPLES)/idroq/idlogo.roq

FATE_DPCM += fate-dpcm-sierra
fate-dpcm-sierra: CMD = md5 -i $(SAMPLES)/sol/lsl7sample.sol -f s16le

FATE_DPCM += fate-dpcm-xan
fate-dpcm-xan: CMD = md5 -i $(SAMPLES)/wc4-xan/wc4_2.avi -vn -f s16le

FATE_TESTS += $(FATE_DPCM)
fate-dpcm: $(FATE_DPCM)
