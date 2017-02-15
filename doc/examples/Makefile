DOC_EXAMPLES-$(CONFIG_DECODE_AUDIO_EXAMPLE)     += decode_audio
DOC_EXAMPLES-$(CONFIG_DECODE_VIDEO_EXAMPLE)     += decode_video
DOC_EXAMPLES-$(CONFIG_ENCODE_AUDIO_EXAMPLE)     += encode_audio
DOC_EXAMPLES-$(CONFIG_ENCODE_VIDEO_EXAMPLE)     += encode_video
DOC_EXAMPLES-$(CONFIG_FILTER_AUDIO_EXAMPLE)     += filter_audio
DOC_EXAMPLES-$(CONFIG_METADATA_EXAMPLE)         += metadata
DOC_EXAMPLES-$(CONFIG_OUTPUT_EXAMPLE)           += output
DOC_EXAMPLES-$(CONFIG_QSVDEC_EXAMPLE)           += qsvdec
DOC_EXAMPLES-$(CONFIG_TRANSCODE_AAC_EXAMPLE)    += transcode_aac

DOC_EXAMPLES     := $(DOC_EXAMPLES-yes:%=doc/examples/%$(EXESUF))
ALL_DOC_EXAMPLES := $(DOC_EXAMPLES) $(DOC_EXAMPLES-:%=doc/examples/%$(EXESUF))
PROGS            += $(DOC_EXAMPLES)

examples: $(DOC_EXAMPLES)

$(DOC_EXAMPLES:%$(EXESUF)=%.o): | doc/examples
OBJDIRS += doc/examples

DOXY_INPUT += $(addprefix $(SRC_PATH)/, $(DOC_EXAMPLES:%$(EXESUF)=%.c))

clean::
	$(RM) $(ALL_DOC_EXAMPLES)
	$(RM) $(CLEANSUFFIXES:%=doc/examples/%)

.PHONY: examples
