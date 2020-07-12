EXAMPLES-$(CONFIG_AVIO_LIST_DIR_EXAMPLE)     += avio_list_dir
EXAMPLES-$(CONFIG_AVIO_READING_EXAMPLE)      += avio_reading
EXAMPLES-$(CONFIG_DECODE_AUDIO_EXAMPLE)      += decode_audio
EXAMPLES-$(CONFIG_DECODE_VIDEO_EXAMPLE)      += decode_video
EXAMPLES-$(CONFIG_DEMUXING_DECODING_EXAMPLE) += demuxing_decoding
EXAMPLES-$(CONFIG_ENCODE_AUDIO_EXAMPLE)      += encode_audio
EXAMPLES-$(CONFIG_ENCODE_VIDEO_EXAMPLE)      += encode_video
EXAMPLES-$(CONFIG_EXTRACT_MVS_EXAMPLE)       += extract_mvs
EXAMPLES-$(CONFIG_FILTER_AUDIO_EXAMPLE)      += filter_audio
EXAMPLES-$(CONFIG_FILTERING_AUDIO_EXAMPLE)   += filtering_audio
EXAMPLES-$(CONFIG_FILTERING_VIDEO_EXAMPLE)   += filtering_video
EXAMPLES-$(CONFIG_HTTP_MULTICLIENT_EXAMPLE)  += http_multiclient
EXAMPLES-$(CONFIG_HW_DECODE_EXAMPLE)         += hw_decode
EXAMPLES-$(CONFIG_METADATA_EXAMPLE)          += metadata
EXAMPLES-$(CONFIG_MUXING_EXAMPLE)            += muxing
EXAMPLES-$(CONFIG_QSVDEC_EXAMPLE)            += qsvdec
EXAMPLES-$(CONFIG_REMUXING_EXAMPLE)          += remuxing
EXAMPLES-$(CONFIG_RESAMPLING_AUDIO_EXAMPLE)  += resampling_audio
EXAMPLES-$(CONFIG_SCALING_VIDEO_EXAMPLE)     += scaling_video
EXAMPLES-$(CONFIG_TRANSCODE_AAC_EXAMPLE)     += transcode_aac
EXAMPLES-$(CONFIG_TRANSCODING_EXAMPLE)       += transcoding
EXAMPLES-$(CONFIG_VAAPI_ENCODE_EXAMPLE)      += vaapi_encode
EXAMPLES-$(CONFIG_VAAPI_TRANSCODE_EXAMPLE)   += vaapi_transcode

EXAMPLES       := $(EXAMPLES-yes:%=doc/examples/%$(PROGSSUF)$(EXESUF))
EXAMPLES_G     := $(EXAMPLES-yes:%=doc/examples/%$(PROGSSUF)_g$(EXESUF))
ALL_EXAMPLES   := $(EXAMPLES) $(EXAMPLES-:%=doc/examples/%$(PROGSSUF)$(EXESUF))
ALL_EXAMPLES_G := $(EXAMPLES_G) $(EXAMPLES-:%=doc/examples/%$(PROGSSUF)_g$(EXESUF))
PROGS          += $(EXAMPLES)

EXAMPLE_MAKEFILE := $(SRC_PATH)/doc/examples/Makefile
EXAMPLES_FILES := $(wildcard $(SRC_PATH)/doc/examples/*.c) $(SRC_PATH)/doc/examples/README $(EXAMPLE_MAKEFILE)

$(foreach P,$(EXAMPLES),$(eval OBJS-$(P:%$(PROGSSUF)$(EXESUF)=%) = $(P:%$(PROGSSUF)$(EXESUF)=%).o))
$(EXAMPLES_G): %$(PROGSSUF)_g$(EXESUF): %.o

examples: $(EXAMPLES)

$(EXAMPLES:%$(PROGSSUF)$(EXESUF)=%.o): | doc/examples
OUTDIRS += doc/examples

DOXY_INPUT += $(EXAMPLES:%$(PROGSSUF)$(EXESUF)=%.c)

install: install-examples

install-examples: $(EXAMPLES_FILES)
	$(Q)mkdir -p "$(DATADIR)/examples"
	$(INSTALL) -m 644 $(EXAMPLES_FILES) "$(DATADIR)/examples"
	$(INSTALL) -m 644 $(EXAMPLE_MAKEFILE:%=%.example) "$(DATADIR)/examples/Makefile"

uninstall: uninstall-examples

uninstall-examples:
	$(RM) -r "$(DATADIR)/examples"

examplesclean:
	$(RM) $(ALL_EXAMPLES) $(ALL_EXAMPLES_G)
	$(RM) $(CLEANSUFFIXES:%=doc/examples/%)

docclean:: examplesclean

-include $(wildcard $(EXAMPLES:%$(PROGSSUF)$(EXESUF)=%.d))

.PHONY: examples
