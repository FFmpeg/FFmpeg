FFPROBE_TEST_FILE=tests/data/ffprobe-test.nut
FFPROBE_COMMAND=ffprobe$(PROGSSUF)$(EXESUF) -show_streams -show_packets -show_format -show_frames -bitexact $(FFPROBE_TEST_FILE)

FATE_FFPROBE-$(CONFIG_AVDEVICE) += fate-ffprobe_compact
fate-ffprobe_compact: $(FFPROBE_TEST_FILE)
fate-ffprobe_compact: CMD = run $(FFPROBE_COMMAND) -of compact

FATE_FFPROBE-$(CONFIG_AVDEVICE) += fate-ffprobe_csv
fate-ffprobe_csv: $(FFPROBE_TEST_FILE)
fate-ffprobe_csv: CMD = run $(FFPROBE_COMMAND) -of csv

FATE_FFPROBE-$(CONFIG_AVDEVICE) += fate-ffprobe_default
fate-ffprobe_default: $(FFPROBE_TEST_FILE)
fate-ffprobe_default: CMD = run $(FFPROBE_COMMAND) -of default

FATE_FFPROBE-$(CONFIG_AVDEVICE) += fate-ffprobe_flat
fate-ffprobe_flat: $(FFPROBE_TEST_FILE)
fate-ffprobe_flat: CMD = run $(FFPROBE_COMMAND) -of flat

FATE_FFPROBE-$(CONFIG_AVDEVICE) += fate-ffprobe_ini
fate-ffprobe_ini: $(FFPROBE_TEST_FILE)
fate-ffprobe_ini: CMD = run $(FFPROBE_COMMAND) -of ini

FATE_FFPROBE-$(CONFIG_AVDEVICE) += fate-ffprobe_json
fate-ffprobe_json: $(FFPROBE_TEST_FILE)
fate-ffprobe_json: CMD = run $(FFPROBE_COMMAND) -of json

FATE_FFPROBE-$(CONFIG_AVDEVICE) += fate-ffprobe_xml
fate-ffprobe_xml: $(FFPROBE_TEST_FILE)
fate-ffprobe_xml: CMD = run $(FFPROBE_COMMAND) -of xml

FATE_FFPROBE += $(FATE_FFPROBE-yes)

fate-ffprobe: $(FATE_FFPROBE)

