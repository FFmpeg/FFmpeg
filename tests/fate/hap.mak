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


#Test bsf conversion
FATE_HAPQA_EXTRACT_BSF += fate-hapqa-extract-snappy1-to-hapq
fate-hapqa-extract-snappy1-to-hapq: CMD = framecrc -i $(TARGET_SAMPLES)/hap/HAPQA_Snappy_1chunk_127x1.mov -c:v copy -bsf:v hapqa_extract=texture=color -tag:v HapY -metadata:s:v:0 encoder="HAPQ"

FATE_HAPQA_EXTRACT_BSF += fate-hapqa-extract-snappy16-to-hapq
fate-hapqa-extract-snappy16-to-hapq: CMD = framecrc -i $(TARGET_SAMPLES)/hap/HAPQA_Snappy_16chunk_127x1.mov -c:v copy -bsf:v hapqa_extract=texture=color -tag:v HapY -metadata:s:v:0 encoder="HAPQ"

FATE_HAPQA_EXTRACT_BSF += fate-hapqa-extract-snappy1-to-hapalphaonly
fate-hapqa-extract-snappy1-to-hapalphaonly: CMD = framecrc -i $(TARGET_SAMPLES)/hap/HAPQA_Snappy_1chunk_127x1.mov -c:v copy -bsf:v hapqa_extract=texture=alpha -tag:v HapA -metadata:s:v:0 encoder="HAPAlphaOnly"

FATE_HAPQA_EXTRACT_BSF += fate-hapqa-extract-snappy16-to-hapalphaonly
fate-hapqa-extract-snappy16-to-hapalphaonly: CMD = framecrc -i $(TARGET_SAMPLES)/hap/HAPQA_Snappy_16chunk_127x1.mov -c:v copy -bsf:v hapqa_extract=texture=alpha -tag:v HapA -metadata:s:v:0 encoder="HAPAlphaOnly"


#Test bsf conversion and mov
tests/data/hapq_nosnappy.mov: TAG = GEN
tests/data/hapq_nosnappy.mov: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< \
	-i $(TARGET_SAMPLES)/hap/HAPQA_NoSnappy_127x1.mov -nostdin -c:v copy -bsf:v hapqa_extract=texture=color \
        -tag:v HapY -metadata:s:v:0 encoder="HAPQ" $(TARGET_PATH)/$@ -y 2>/dev/null

tests/data/hapalphaonly_nosnappy.mov: TAG = GEN
tests/data/hapalphaonly_nosnappy.mov: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< \
	-i $(TARGET_SAMPLES)/hap/HAPQA_NoSnappy_127x1.mov -nostdin -c:v copy -bsf:v hapqa_extract=texture=alpha \
        -tag:v HapA -metadata:s:v:0 encoder="HAPAlpha Only" $(TARGET_PATH)/$@ -y 2>/dev/null


FATE_HAPQA_EXTRACT_BSF_FFPROBE += fate-hapqa-extract-nosnappy-to-hapq-mov
fate-hapqa-extract-nosnappy-to-hapq-mov: tests/data/hapq_nosnappy.mov
fate-hapqa-extract-nosnappy-to-hapq-mov: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_packets -show_data_hash adler32 -show_streams -select_streams v -v 0 $(TARGET_PATH)/tests/data/hapq_nosnappy.mov

FATE_HAPQA_EXTRACT_BSF_FFPROBE += fate-hapqa-extract-nosnappy-to-hapalphaonly-mov
fate-hapqa-extract-nosnappy-to-hapalphaonly-mov: tests/data/hapalphaonly_nosnappy.mov
fate-hapqa-extract-nosnappy-to-hapalphaonly-mov: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_packets -show_data_hash adler32 -show_streams -select_streams v -v 0 $(TARGET_PATH)/tests/data/hapalphaonly_nosnappy.mov


FATE_SAMPLES_FFMPEG-$(call ALLYES, MOV_DEMUXER HAPQA_EXTRACT_BSF MOV_MUXER) += $(FATE_HAPQA_EXTRACT_BSF)
FATE_SAMPLES_FFPROBE += $(FATE_HAPQA_EXTRACT_BSF_FFPROBE)

fate-hapqa-extract-bsf: $(FATE_HAPQA_EXTRACT_BSF) $(FATE_HAPQA_EXTRACT_BSF_FFPROBE)
