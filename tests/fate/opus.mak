# The samples were produced by simply rewrapping the official test vectors from
# their custom format into Matroska. The reference decoded outputs are from the
# newest testvectors file from RFC8251

OPUS_CELT_SAMPLES   = $(addprefix testvector, 01 11) tron.6ch.tinypkts
OPUS_HYBRID_SAMPLES = $(addprefix testvector, 05 06)
OPUS_SILK_SAMPLES   = $(addprefix testvector, 02 03 04)
OPUS_SAMPLES        = $(addprefix testvector, 07 08 09 10 12)

define FATE_OPUS_TEST
FATE_OPUS     += fate-opus-$(1)
FATE_OPUS$(2) += fate-opus-$(1)
fate-opus-$(1): CMD = ffmpeg -i $(TARGET_SAMPLES)/opus/$(1).mka -f s16le -
fate-opus-$(1): REF = $(SAMPLES)/opus/$(1)$(2).dec
endef

$(foreach N,$(OPUS_CELT_SAMPLES),  $(eval $(call FATE_OPUS_TEST,$(N))))
$(foreach N,$(OPUS_HYBRID_SAMPLES),$(eval $(call FATE_OPUS_TEST,$(N),_v2)))
$(foreach N,$(OPUS_SILK_SAMPLES),  $(eval $(call FATE_OPUS_TEST,$(N))))
$(foreach N,$(OPUS_SAMPLES),       $(eval $(call FATE_OPUS_TEST,$(N),)))

FATE_OPUS := $(sort $(FATE_OPUS))

$(FATE_OPUS): CMP = stddev
$(FATE_OPUS): CMP_UNIT = s16
$(FATE_OPUS): FUZZ = 3
fate-opus-testvector01:      CMP_TARGET = 0
fate-opus-testvector02:      CMP_TARGET = 191
fate-opus-testvector03:      CMP_TARGET = 139
fate-opus-testvector04:      CMP_TARGET = 119
fate-opus-testvector05:      CMP_TARGET = 108
fate-opus-testvector06:      CMP_TARGET = 106
fate-opus-testvector07:      CMP_TARGET = 0
fate-opus-testvector08:      CMP_TARGET = 6
fate-opus-testvector09:      CMP_TARGET = 0
fate-opus-testvector10:      CMP_TARGET = 38
fate-opus-testvector11:      CMP_TARGET = 0
fate-opus-testvector12:      CMP_TARGET = 160
fate-opus-tron.6ch.tinypkts: CMP_SHIFT = 1440
fate-opus-tron.6ch.tinypkts: CMP_TARGET = 0

$(FATE_OPUS_CELT): CMP = oneoff
$(FATE_OPUS_CELT): FUZZ = 6

FATE_SAMPLES_AVCONV-$(call DEMDEC, MATROSKA, OPUS) += $(FATE_OPUS)
fate-opus-celt: $(FATE_OPUS_CELT)
fate-opus-hybrid: $(FATE_OPUS_HYBRID)
fate-opus-silk: $(FATE_OPUS_SILK)
fate-opus: $(FATE_OPUS)
