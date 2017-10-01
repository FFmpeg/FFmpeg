DOC_EXAMPLES-$(CONFIG_AVIO_DIR_CMD_EXAMPLE)      += avio_dir_cmd
DOC_EXAMPLES-$(CONFIG_AVIO_READING_EXAMPLE)      += avio_reading
DOC_EXAMPLES-$(CONFIG_DECODE_AUDIO_EXAMPLE)      += decode_audio
DOC_EXAMPLES-$(CONFIG_DECODE_VIDEO_EXAMPLE)      += decode_video
DOC_EXAMPLES-$(CONFIG_DEMUXING_DECODING_EXAMPLE) += demuxing_decoding
DOC_EXAMPLES-$(CONFIG_ENCODE_AUDIO_EXAMPLE)      += encode_audio
DOC_EXAMPLES-$(CONFIG_ENCODE_VIDEO_EXAMPLE)      += encode_video
DOC_EXAMPLES-$(CONFIG_EXTRACT_MVS_EXAMPLE)       += extract_mvs
DOC_EXAMPLES-$(CONFIG_FILTER_AUDIO_EXAMPLE)      += filter_audio
DOC_EXAMPLES-$(CONFIG_FILTERING_AUDIO_EXAMPLE)   += filtering_audio
DOC_EXAMPLES-$(CONFIG_FILTERING_VIDEO_EXAMPLE)   += filtering_video
DOC_EXAMPLES-$(CONFIG_HTTP_MULTICLIENT_EXAMPLE)  += http_multiclient
DOC_EXAMPLES-$(CONFIG_HW_DECODE_EXAMPLE)         += hw_decode
DOC_EXAMPLES-$(CONFIG_METADATA_EXAMPLE)          += metadata
DOC_EXAMPLES-$(CONFIG_MUXING_EXAMPLE)            += muxing
DOC_EXAMPLES-$(CONFIG_QSVDEC_EXAMPLE)            += qsvdec
DOC_EXAMPLES-$(CONFIG_REMUXING_EXAMPLE)          += remuxing
DOC_EXAMPLES-$(CONFIG_RESAMPLING_AUDIO_EXAMPLE)  += resampling_audio
DOC_EXAMPLES-$(CONFIG_SCALING_VIDEO_EXAMPLE)     += scaling_video
DOC_EXAMPLES-$(CONFIG_TRANSCODE_AAC_EXAMPLE)     += transcode_aac
DOC_EXAMPLES-$(CONFIG_TRANSCODING_EXAMPLE)       += transcoding

DOC_EXAMPLES       := $(DOC_EXAMPLES-yes:%=doc/examples/%$(PROGSSUF)$(EXESUF))
DOC_EXAMPLES_G     := $(DOC_EXAMPLES-yes:%=doc/examples/%$(PROGSSUF)_g$(EXESUF))
ALL_DOC_EXAMPLES   := $(DOC_EXAMPLES) $(DOC_EXAMPLES-:%=doc/examples/%$(PROGSSUF)$(EXESUF))
ALL_DOC_EXAMPLES_G := $(DOC_EXAMPLES_G) $(DOC_EXAMPLES-:%=doc/examples/%$(PROGSSUF)_g$(EXESUF))
PROGS              += $(DOC_EXAMPLES)

EXAMPLES_FILES := $(wildcard $(SRC_PATH)/doc/examples/*.c) $(SRC_PATH)/doc/examples/README
EXAMPLE_MAKEFILE := $(SRC_PATH)/doc/examples/Makefile

$(foreach P,$(DOC_EXAMPLES),$(eval OBJS-$(P:%$(PROGSSUF)$(EXESUF)=%) = $(P:%$(PROGSSUF)$(EXESUF)=%).o))
$(DOC_EXAMPLES_G): %$(PROGSSUF)_g$(EXESUF): %.o

examples: $(DOC_EXAMPLES)

$(DOC_EXAMPLES:%$(PROGSSUF)$(EXESUF)=%.o): | doc/examples
OBJDIRS += doc/examples

DOXY_INPUT += $(DOC_EXAMPLES:%$(PROGSSUF)$(EXESUF)=%.c)

examplesclean:
	$(RM) $(ALL_DOC_EXAMPLES) $(ALL_DOC_EXAMPLES_G)
	$(RM) $(CLEANSUFFIXES:%=doc/examples/%)

docclean:: examplesclean

-include $(wildcard $(DOC_EXAMPLES:%$(PROGSSUF)$(EXESUF)=%.d))

.PHONY: examples
