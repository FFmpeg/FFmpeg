EXAMPLES-$(CONFIG_DECODE_AUDIO_EXAMPLE)     += decode_audio
EXAMPLES-$(CONFIG_DECODE_VIDEO_EXAMPLE)     += decode_video
EXAMPLES-$(CONFIG_ENCODE_AUDIO_EXAMPLE)     += encode_audio
EXAMPLES-$(CONFIG_ENCODE_VIDEO_EXAMPLE)     += encode_video
EXAMPLES-$(CONFIG_FILTER_AUDIO_EXAMPLE)     += filter_audio
EXAMPLES-$(CONFIG_METADATA_EXAMPLE)         += metadata
EXAMPLES-$(CONFIG_OUTPUT_EXAMPLE)           += output
EXAMPLES-$(CONFIG_QSVDEC_EXAMPLE)           += qsvdec
EXAMPLES-$(CONFIG_TRANSCODE_AAC_EXAMPLE)    += transcode_aac

EXAMPLES     := $(EXAMPLES-yes:%=doc/examples/%$(EXESUF))
ALL_EXAMPLES := $(EXAMPLES) $(EXAMPLES-:%=doc/examples/%$(EXESUF))
PROGS        += $(EXAMPLES)

$(foreach P,$(EXAMPLES),$(eval OBJS-$(P:%$(EXESUF)=%) = $(P:%$(EXESUF)=%).o))
$(EXAMPLES): %$(EXESUF): %.o

examples: $(EXAMPLES)

$(EXAMPLES:%$(EXESUF)=%.o): | doc/examples
OBJDIRS += doc/examples

DOXY_INPUT += $(addprefix $(SRC_PATH)/, $(EXAMPLES:%$(EXESUF)=%.c))

clean::
	$(RM) $(ALL_EXAMPLES)
	$(RM) $(CLEANSUFFIXES:%=doc/examples/%)

-include $(wildcard $(EXAMPLES:%$(EXESUF)=%.d))

.PHONY: examples
