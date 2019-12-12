# dcadec test samples
DCADEC_SUITE_LOSSLESS_16 = xll_51_16_192_768_0        \
                           xll_51_16_192_768_1        \

DCADEC_SUITE_LOSSLESS_24 = xll_51_24_48_768           \
                           xll_51_24_48_none          \
                           xll_71_24_48_768_0         \
                           xll_71_24_48_768_1         \
                           xll_71_24_96_768           \
                           xll_x96_51_24_96_1509      \
                           xll_xch_61_24_48_768       \

DCADEC_SUITE_LOSSY       = core_51_24_48_768_0        \
                           core_51_24_48_768_1        \
                           x96_51_24_96_1509          \
                           x96_xch_61_24_96_3840      \
                           x96_xxch_71_24_96_3840     \
                           xbr_51_24_48_3840          \
                           xbr_xch_61_24_48_3840      \
                           xbr_xxch_71_24_48_3840     \
                           xch_61_24_48_768           \
                           xxch_71_24_48_2046         \

define FATE_DCADEC_LOSSLESS_SUITE
FATE_DCADEC_LOSSLESS += fate-dca-$(1) fate-dca-$(1)-dmix_2 fate-dca-$(1)-dmix_6
fate-dca-$(1): CMD = framemd5 -i $(TARGET_SAMPLES)/dts/dcadec-suite/$(1).dtshd -c:a pcm_$(2)
fate-dca-$(1)-dmix_2: CMD = framemd5 -request_channel_layout 0x3   -i $(TARGET_SAMPLES)/dts/dcadec-suite/$(1).dtshd -c:a pcm_$(2)
fate-dca-$(1)-dmix_6: CMD = framemd5 -request_channel_layout 0x60f -i $(TARGET_SAMPLES)/dts/dcadec-suite/$(1).dtshd -c:a pcm_$(2)
endef

define FATE_DCADEC_LOSSY_SUITE
FATE_DCADEC_LOSSY += fate-dca-$(1)
fate-dca-$(1): CMD = ffmpeg -i $(TARGET_SAMPLES)/dts/dcadec-suite/$(1).dtshd -f f32le -
fate-dca-$(1): REF = $(SAMPLES)/dts/dcadec-suite/$(1).f32
endef

$(foreach N,$(DCADEC_SUITE_LOSSLESS_16),$(eval $(call FATE_DCADEC_LOSSLESS_SUITE,$(N),s16le)))
$(foreach N,$(DCADEC_SUITE_LOSSLESS_24),$(eval $(call FATE_DCADEC_LOSSLESS_SUITE,$(N),s24le)))
$(foreach N,$(DCADEC_SUITE_LOSSY),$(eval $(call FATE_DCADEC_LOSSY_SUITE,$(N))))

# lossy downmix tests
FATE_DCADEC_LOSSY += fate-dca-core_51_24_48_768_1-dmix_2
fate-dca-core_51_24_48_768_1-dmix_2: CMD = ffmpeg -request_channel_layout 0x3 -i $(TARGET_SAMPLES)/dts/dcadec-suite/core_51_24_48_768_1.dtshd -f f32le -
fate-dca-core_51_24_48_768_1-dmix_2: REF = $(SAMPLES)/dts/dcadec-suite/core_51_24_48_768_1-dmix_2.f32

FATE_DCADEC_LOSSY += fate-dca-x96_xxch_71_24_96_3840-dmix_2
fate-dca-x96_xxch_71_24_96_3840-dmix_2: CMD = ffmpeg -request_channel_layout 0x3 -i $(TARGET_SAMPLES)/dts/dcadec-suite/x96_xxch_71_24_96_3840.dtshd -f f32le -
# intentionally uses the dmix_6 reference because the sample does not contain stereo downmix coefficients
fate-dca-x96_xxch_71_24_96_3840-dmix_2: REF = $(SAMPLES)/dts/dcadec-suite/x96_xxch_71_24_96_3840-dmix_6.f32

FATE_DCADEC_LOSSY += fate-dca-x96_xxch_71_24_96_3840-dmix_6
fate-dca-x96_xxch_71_24_96_3840-dmix_6: CMD = ffmpeg -request_channel_layout 0x60f -i $(TARGET_SAMPLES)/dts/dcadec-suite/x96_xxch_71_24_96_3840.dtshd -f f32le -
fate-dca-x96_xxch_71_24_96_3840-dmix_6: REF = $(SAMPLES)/dts/dcadec-suite/x96_xxch_71_24_96_3840-dmix_6.f32

FATE_DCADEC_LOSSY += fate-dca-xch_61_24_48_768-dmix_6
fate-dca-xch_61_24_48_768-dmix_6: CMD = ffmpeg -request_channel_layout 0x60f -i $(TARGET_SAMPLES)/dts/dcadec-suite/xch_61_24_48_768.dtshd -f f32le -
fate-dca-xch_61_24_48_768-dmix_6: REF = $(SAMPLES)/dts/dcadec-suite/xch_61_24_48_768-dmix_6.f32

$(FATE_DCADEC_LOSSY): CMP = oneoff
$(FATE_DCADEC_LOSSY): CMP_UNIT = f32
$(FATE_DCADEC_LOSSY): FUZZ = 9

FATE_DCA-$(call DEMDEC, DTSHD, DCA) += $(FATE_DCADEC_LOSSLESS) $(FATE_DCADEC_LOSSY)

FATE_DCA-$(call DEMDEC, MPEGTS, DCA) += fate-dca-core
fate-dca-core: CMD = pcm -i $(TARGET_SAMPLES)/dts/dts.ts
fate-dca-core: CMP = oneoff
fate-dca-core: REF = $(SAMPLES)/dts/dts.pcm

FATE_DCA-$(call DEMDEC, DTS, DCA) += fate-dca-xll
fate-dca-xll: CMD = md5 -i $(TARGET_SAMPLES)/dts/master_audio_7.1_24bit.dts -f s24le

FATE_DCA-$(call DEMDEC, DTS, DCA) += fate-dts_es
fate-dts_es: CMD = pcm -i $(TARGET_SAMPLES)/dts/dts_es.dts
fate-dts_es: CMP = oneoff
fate-dts_es: REF = $(SAMPLES)/dts/dts_es_2.pcm

FATE_DCA-$(call ALLYES, DTS_DEMUXER DTS_MUXER DCA_CORE_BSF) += fate-dca-core-bsf
fate-dca-core-bsf: CMD = md5pipe -i $(TARGET_SAMPLES)/dts/master_audio_7.1_24bit.dts -c:a copy -bsf:a dca_core -fflags +bitexact -f dts
fate-dca-core-bsf: CMP = oneline
fate-dca-core-bsf: REF = ca22b00d8c641cd168e2f7ca8d2f340e

FATE_SAMPLES_AUDIO += $(FATE_DCA-yes)
fate-dca: $(FATE_DCA-yes)
