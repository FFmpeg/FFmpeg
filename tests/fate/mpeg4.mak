
MPEG4_RESOLUTION_CHANGE = down-down down-up up-down up-up

fate-mpeg4-resolution-change-%: CMD = framemd5 -flags +bitexact -idct simple -i $(TARGET_SAMPLES)/mpeg4/resize_$(@:fate-mpeg4-resolution-change-%=%).h263 -sws_flags +bitexact

FATE_MPEG4-$(call DEMDEC, H263, H263) := $(addprefix fate-mpeg4-resolution-change-, $(MPEG4_RESOLUTION_CHANGE))

FATE_SAMPLES_AVCONV += $(FATE_MPEG4-yes)
fate-mpeg4: $(FATE_MPEG4-yes)
