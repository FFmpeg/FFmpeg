ALS_SUITE = 00 01 02 03 04 05

define FATE_ALS_SUITE
FATE_ALS += fate-mpeg4-als-conformance-$(1)
fate-mpeg4-als-conformance-$(1): CMD = crc -i $(TARGET_SAMPLES)/lossless-audio/als_$(1)_2ch48k16b.mp4
endef

$(foreach N,$(ALS_SUITE),$(eval $(call FATE_ALS_SUITE,$(N))))

FATE_ALS += fate-mpeg4-als-conformance-09

fate-mpeg4-als-conformance-09: CMD = crc -i $(TARGET_SAMPLES)/lossless-audio/als_09_512ch2k16b.mp4

FATE_ALS := $(if $(call CRC, MOV, ALS), $(FATE_ALS))
FATE_SAMPLES_AVCONV-yes += $(FATE_ALS)
fate-als: $(FATE_ALS)
