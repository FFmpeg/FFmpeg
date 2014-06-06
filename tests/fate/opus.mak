# The samples were produced by simply rewrapping the official test vectors from
# their custom format into Matroska.
# The reference files were created with our decoder and tested against the
# libopus output with the official opus_compare tool. We cannot use libopus
# output as reference directly, because the use of different resamplers would
# require too high fuzz values, which can hide bugs.
# Before adding new tests here, always make sure they pass opus_compare.

OPUS_CELT_SAMPLES   = $(addprefix testvector, 01 07 11) tron.6ch.tinypkts
OPUS_HYBRID_SAMPLES = $(addprefix testvector, 05 06)
OPUS_SILK_SAMPLES   = $(addprefix testvector, 02 03 04)
OPUS_SAMPLES        = $(addprefix testvector, 08 09 10 12)

define FATE_OPUS_TEST
FATE_OPUS     += fate-opus-$(1)
FATE_OPUS$(2) += fate-opus-$(1)
fate-opus-$(1): CMD = ffmpeg -i $(TARGET_SAMPLES)/opus/$(1).mka -f s16le -
fate-opus-$(1): REF = $(SAMPLES)/opus/$(1).dec
endef

$(foreach N,$(OPUS_CELT_SAMPLES),  $(eval $(call FATE_OPUS_TEST,$(N),_CELT)))
$(foreach N,$(OPUS_HYBRID_SAMPLES),$(eval $(call FATE_OPUS_TEST,$(N),_HYBRID)))
$(foreach N,$(OPUS_SILK_SAMPLES),  $(eval $(call FATE_OPUS_TEST,$(N),_SILK)))
$(foreach N,$(OPUS_SAMPLES),       $(eval $(call FATE_OPUS_TEST,$(N),)))

FATE_OPUS := $(sort $(FATE_OPUS))

$(FATE_OPUS): CMP = stddev
$(FATE_OPUS): CMP_UNIT = s16
$(FATE_OPUS): FUZZ = 3
fate-opus-testvector02: CMP_TARGET = 191
fate-opus-testvector03: CMP_TARGET = 139
fate-opus-testvector04: CMP_TARGET = 119
fate-opus-testvector05: CMP_TARGET = 108
fate-opus-testvector06: CMP_TARGET = 106
fate-opus-testvector08: CMP_TARGET = 6
fate-opus-testvector10: CMP_TARGET = 38
fate-opus-testvector12: CMP_TARGET = 160

$(FATE_OPUS_CELT): CMP = oneoff
$(FATE_OPUS_CELT): FUZZ = 6

FATE_SAMPLES_AVCONV-$(call DEMDEC, MATROSKA, OPUS) += $(FATE_OPUS)
fate-opus-celt: $(FATE_OPUS_CELT)
fate-opus-hybrid: $(FATE_OPUS_HYBRID)
fate-opus-silk: $(FATE_OPUS_SILK)
fate-opus: $(FATE_OPUS)
