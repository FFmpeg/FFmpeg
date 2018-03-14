FATE_HAP += fate-hap1
fate-hap1: CMD = framecrc -i $(TARGET_SAMPLES)/hap/hap1.mov

FATE_HAP += fate-hap5
fate-hap5: CMD = framecrc -i $(TARGET_SAMPLES)/hap/hap5.mov

FATE_HAP += fate-hapy
fate-hapy: CMD = framecrc -i $(TARGET_SAMPLES)/hap/hapy.mov

FATE_HAP += fate-hap-chunk
fate-hap-chunk: CMD = framecrc -i $(TARGET_SAMPLES)/hap/hapy-12-chunks.mov

FATE_HAP += fate-hapqa-nosnappy-127x71
fate-hapqa-nosnappy-127x71: CMD = framecrc -i $(TARGET_SAMPLES)/hap/HAPQA_NoSnappy_127x1.mov

FATE_HAP += fate-hapqa-snappy1-127x71
fate-hapqa-snappy1-127x71: CMD = framecrc -i $(TARGET_SAMPLES)/hap/HAPQA_Snappy_1chunk_127x1.mov

FATE_HAP += fate-hapqa-snappy16-127x71
fate-hapqa-snappy16-127x71: CMD = framecrc -i $(TARGET_SAMPLES)/hap/HAPQA_Snappy_16chunk_127x1.mov

FATE_HAP += fate-hap-alpha-only-nosnappy-128x72
fate-hap-alpha-only-nosnappy-128x72: CMD = framecrc -i $(TARGET_SAMPLES)/hap/HapAlphaOnly_NoSnappy_128x72.mov -pix_fmt gray8

FATE_HAP += fate-hap-alpha-only-snappy-127x71
fate-hap-alpha-only-snappy-127x71: CMD = framecrc -i $(TARGET_SAMPLES)/hap/HapAlphaOnly_snappy1chunk_127x71.mov -pix_fmt gray8

FATE_SAMPLES_AVCONV-$(call DEMDEC, MOV, HAP) += $(FATE_HAP)
fate-hap: $(FATE_HAP)


fate-hapenc%: CMD = framemd5 -f image2 -c:v pgmyuv -i $(TARGET_PATH)/tests/vsynth1/%02d.pgm -sws_flags +accurate_rnd+bitexact -vframes 5 -c:v hap ${OPTS}

FATE_HAPENC += fate-hapenc-hap-none
fate-hapenc-hap-none: OPTS = -pix_fmt rgba -format hap -compressor none

FATE_HAPENC += fate-hapenc-hapa-none
fate-hapenc-hapa-none: OPTS = -pix_fmt rgba -format hap_alpha -compressor none

FATE_HAPENC += fate-hapenc-hapq-none
fate-hapenc-hapq-none: OPTS = -pix_fmt rgba -format hap_q -compressor none

$(FATE_HAPENC): $(VREF)

FATE_AVCONV-$(call ENCMUX, HAP, MOV) += $(FATE_HAPENC)
fate-hapenc: $(FATE_HAPENC)
