APE_VERSIONS = 380 388 389b1 391b1 392b2 394b1

define FATE_APE_SUITE
FATE_APE += fate-lossless-monkeysaudio-$(1)-normal
fate-lossless-monkeysaudio-$(1)-normal: CMD = crc -auto_conversion_filters -i $(TARGET_SAMPLES)/lossless-audio/luckynight-mac$(1)-c2000.ape -af atrim=end_sample=73728
fate-lossless-monkeysaudio-$(1)-normal: REF = CRC=0x5d08c17e
fate-lossless-monkeysaudio-$(1)-normal: CMP = oneline
FATE_APE += fate-lossless-monkeysaudio-$(1)-extrahigh
fate-lossless-monkeysaudio-$(1)-extrahigh: CMD = crc -auto_conversion_filters -i $(TARGET_SAMPLES)/lossless-audio/luckynight-mac$(1)-c4000.ape -af atrim=end_sample=73728
fate-lossless-monkeysaudio-$(1)-extrahigh: REF = CRC=0x5d08c17e
fate-lossless-monkeysaudio-$(1)-extrahigh: CMP = oneline
endef

$(foreach N,$(APE_VERSIONS),$(eval $(call FATE_APE_SUITE,$(N))))

FATE_APE += fate-lossless-monkeysaudio-399
fate-lossless-monkeysaudio-399: CMD = md5 -i $(TARGET_SAMPLES)/lossless-audio/luckynight-partial.ape -f s16le -af aresample

FATE_APE += fate-lossless-monkeysaudio-legacy
fate-lossless-monkeysaudio-legacy: CMD = md5 -i $(TARGET_SAMPLES)/lossless-audio/NoLegacy-cut.ape -f s32le -af aresample

FATE_SAMPLES_FFMPEG-$(call DEMDEC, APE, APE, ARESAMPLE_FILTER) += $(FATE_APE)
fate-lossless-monkeysaudio: $(FATE_APE)
