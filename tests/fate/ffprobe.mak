tests/data/ffprobe-test.nut: ffmpeg$(PROGSSUF)$(EXESUF) tests/test_copy.ffmeta | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
        -f lavfi -i "aevalsrc=sin(400*PI*2*t):d=0.125[out0]; testsrc=d=0.125[out1]; testsrc=s=100x100:d=0.125[out2]" \
        -f ffmetadata -i $(TARGET_PATH)/tests/test_copy.ffmeta \
        -flags +bitexact -fflags +bitexact -map 0:0 -map 0:1 -map 0:2 -map_metadata 1 \
        -map_metadata:s:0 1:s:0 -map_metadata:s:1 1:s:1 \
        -vcodec rawvideo -acodec pcm_s16le \
        -y $(TARGET_PATH)/$@ 2>/dev/null

FFPROBE_TEST_FILE=tests/data/ffprobe-test.nut
FFPROBE_COMMAND=ffprobe$(PROGSSUF)$(EXESUF) -show_streams -show_packets -show_format -show_frames -bitexact $(TARGET_PATH)/$(FFPROBE_TEST_FILE) -print_filename $(FFPROBE_TEST_FILE)

FATE_FFPROBE-$(call ALLYES, AVDEVICE ARESAMPLE_FILTER) += fate-ffprobe_compact
fate-ffprobe_compact: $(FFPROBE_TEST_FILE)
fate-ffprobe_compact: CMD = run $(FFPROBE_COMMAND) -of compact

FATE_FFPROBE-$(call ALLYES, AVDEVICE ARESAMPLE_FILTER) += fate-ffprobe_csv
fate-ffprobe_csv: $(FFPROBE_TEST_FILE)
fate-ffprobe_csv: CMD = run $(FFPROBE_COMMAND) -of csv

FATE_FFPROBE-$(call ALLYES, AVDEVICE ARESAMPLE_FILTER) += fate-ffprobe_default
fate-ffprobe_default: $(FFPROBE_TEST_FILE)
fate-ffprobe_default: CMD = run $(FFPROBE_COMMAND) -of default

FATE_FFPROBE-$(call ALLYES, AVDEVICE ARESAMPLE_FILTER) += fate-ffprobe_flat
fate-ffprobe_flat: $(FFPROBE_TEST_FILE)
fate-ffprobe_flat: CMD = run $(FFPROBE_COMMAND) -of flat

FATE_FFPROBE-$(call ALLYES, AVDEVICE ARESAMPLE_FILTER) += fate-ffprobe_ini
fate-ffprobe_ini: $(FFPROBE_TEST_FILE)
fate-ffprobe_ini: CMD = run $(FFPROBE_COMMAND) -of ini

FATE_FFPROBE-$(call ALLYES, AVDEVICE ARESAMPLE_FILTER) += fate-ffprobe_json
fate-ffprobe_json: $(FFPROBE_TEST_FILE)
fate-ffprobe_json: CMD = run $(FFPROBE_COMMAND) -of json

FATE_FFPROBE-$(call ALLYES, AVDEVICE ARESAMPLE_FILTER) += fate-ffprobe_xml
fate-ffprobe_xml: $(FFPROBE_TEST_FILE)
fate-ffprobe_xml: CMD = run $(FFPROBE_COMMAND) -of xml

FATE_FFPROBE_SCHEMA-$(call ALLYES, AVDEVICE ARESAMPLE_FILTER) += fate-ffprobe_xsd
fate-ffprobe_xsd: $(FFPROBE_TEST_FILE)
fate-ffprobe_xsd: CMD = run $(FFPROBE_COMMAND) -noprivate -of xml=q=1:x=1 | \
	xmllint --schema $(SRC_PATH)/doc/ffprobe.xsd -

FATE_FFPROBE-$(HAVE_XMLLINT) += $(FATE_FFPROBE_SCHEMA-yes)
FATE_FFPROBE += $(FATE_FFPROBE-yes)

fate-ffprobe: $(FATE_FFPROBE)

