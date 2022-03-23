fate-vbn-%: VBN_FILE = tests/data/$(subst fate-vbn-,,$(@)).vbn
fate-vbn-%: PIX_FMT = rgba
fate-vbn-raw-rgb24: PIX_FMT = rgb24
fate-vbn-%: SRC = $(TARGET_SAMPLES)/png1/lena-$(PIX_FMT).png
fate-vbn-%: CMD = refcmp_metadata_files psnr $(PIX_FMT) $(VBN_FILE) $(SRC)

fate-vbn-dxt1: tests/data/dxt1.vbn
fate-vbn-dxt5: tests/data/dxt5.vbn
fate-vbn-raw-rgba: tests/data/raw-rgba.vbn
fate-vbn-raw-rgb24: tests/data/raw-rgb24.vbn

FATE_VBN += fate-vbn-dxt1
FATE_VBN += fate-vbn-dxt5
FATE_VBN += fate-vbn-raw-rgba
FATE_VBN += fate-vbn-raw-rgb24

tests/data/dxt1.vbn: TAG = GEN
tests/data/dxt1.vbn: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin -i $(TARGET_SAMPLES)/png1/lena-rgba.png -nostdin -c:v vbn -format dxt1 $(TARGET_PATH)/$@ -y 2>/dev/null

tests/data/dxt5.vbn: TAG = GEN
tests/data/dxt5.vbn: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin -i $(TARGET_SAMPLES)/png1/lena-rgba.png -nostdin -c:v vbn -format dxt5 $(TARGET_PATH)/$@ -y 2>/dev/null

tests/data/raw-rgba.vbn: TAG = GEN
tests/data/raw-rgba.vbn: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin -i $(TARGET_SAMPLES)/png1/lena-rgba.png -nostdin -c:v vbn -format raw $(TARGET_PATH)/$@ -y 2>/dev/null

tests/data/raw-rgb24.vbn: TAG = GEN
tests/data/raw-rgb24.vbn: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin -i $(TARGET_SAMPLES)/png1/lena-rgb24.png -nostdin -c:v vbn -format raw $(TARGET_PATH)/$@ -y 2>/dev/null

VBN_REFCMP_DEPS = PSNR_FILTER METADATA_FILTER VBN_ENCODER VBN_DECODER IMAGE2_MUXER IMAGE2_DEMUXER PNG_DECODER

FATE_SAMPLES_FFMPEG-$(call ALLYES, $(VBN_REFCMP_DEPS)) += $(FATE_VBN)
fate-vbn: $(FATE_VBN)
