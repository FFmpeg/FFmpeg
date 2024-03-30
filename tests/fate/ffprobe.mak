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

FFPROBE_OUTPUT_MODES_TESTS = $(addprefix fate-ffprobe_, compact csv default flat ini json xml)
$(FFPROBE_OUTPUT_MODES_TESTS): $(FFPROBE_TEST_FILE)
$(FFPROBE_OUTPUT_MODES_TESTS): CMD = run $(FFPROBE_COMMAND) -of $(@:fate-ffprobe_%=%)
FFPROBE_TEST_FILE_TESTS-yes += $(FFPROBE_OUTPUT_MODES_TESTS)

FFPROBE_TEST_FILE_TESTS-$(HAVE_XMLLINT) += fate-ffprobe_xsd
fate-ffprobe_xsd: $(FFPROBE_TEST_FILE)
fate-ffprobe_xsd: CMD = run $(FFPROBE_COMMAND) -noprivate -of xml=q=1:x=1 | \
	xmllint --schema $(SRC_PATH)/doc/ffprobe.xsd -

FATE_FFPROBE-$(call FILTERDEMDECENCMUX, AEVALSRC TESTSRC ARESAMPLE, FFMETADATA, WRAPPED_AVFRAME, RAWVIDEO, NUT,   \
                                        FFMPEG LAVFI_INDEV PCM_F64BE_DECODER PCM_F64LE_DECODER PCM_S16LE_ENCODER) \
                                        += $(FFPROBE_TEST_FILE_TESTS-yes)

fate-ffprobe: $(FATE_FFPROBE-yes)

