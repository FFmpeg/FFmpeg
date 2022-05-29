# The samples were produced by simply rewrapping the official test vectors from
# their custom format into Matroska. The reference decoded outputs are from the
# newest testvectors file from RFC8251

OPUS_CELT_SAMPLES   = $(addprefix testvector, 01 11) tron.6ch.tinypkts
OPUS_HYBRID_SAMPLES = $(addprefix testvector, 05 06)
OPUS_SILK_SAMPLES   = $(addprefix testvector, 02 03 04)
OPUS_OTHER_SAMPLES  = $(addprefix testvector, 07 08 09 10 12)

define FATE_OPUS_TEST
FATE_OPUS_$(1)-$(call FILTERDEMDECENCMUX, ARESAMPLE, MATROSKA, OPUS, PCM_S16LE, PCM_S16LE, PIPE_PROTOCOL) := $(addprefix fate-opus-,$(OPUS_$(1)_SAMPLES))
FATE_OPUS += $$(FATE_OPUS_$(1)-yes)
endef

$(foreach N, CELT HYBRID SILK OTHER, $(eval $(call FATE_OPUS_TEST,$(N))))

$(FATE_OPUS): CMD = ffmpeg -i $(TARGET_SAMPLES)/opus/$(@:fate-opus-%=%).mka -f s16le -af aresample -
$(FATE_OPUS): REF = $(SAMPLES)/opus/$(@:fate-opus-%=%).dec
$(FATE_OPUS_HYBRID-yes): REF = $(SAMPLES)/opus/$(@:fate-opus-%=%)_v2.dec

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

FATE_SAMPLES_FFMPEG += $(FATE_OPUS)
fate-opus-celt: $(FATE_OPUS_CELT-yes)
fate-opus-hybrid: $(FATE_OPUS_HYBRID-yes)
fate-opus-silk: $(FATE_OPUS_SILK-yes)
fate-opus: $(FATE_OPUS)
