FATE_NOTCHLC += fate-notchlc-alpha-ramp
fate-notchlc-alpha-ramp: CMD = framemd5 -i $(TARGET_SAMPLES)/notchlc/alpha-ramp.mov -pix_fmt yuva444p12le -vf scale

FATE_NOTCHLC += fate-notchlc-alpha-blocks
fate-notchlc-alpha-blocks: CMD = framemd5 -i $(TARGET_SAMPLES)/notchlc/alpha-blocks.mov -pix_fmt yuva444p12le -vf scale

FATE_NOTCHLC += fate-notchlc-alpha-macroblocks
fate-notchlc-alpha-macroblocks: CMD = framemd5 -i $(TARGET_SAMPLES)/notchlc/alpha-macroblocks.mov -pix_fmt yuva444p12le -vf scale

FATE_NOTCHLC += fate-notchlc-alpha-ramp-oddsize
fate-notchlc-alpha-ramp-oddsize: CMD = framemd5 -i $(TARGET_SAMPLES)/notchlc/alpha-ramp-500x300.mov -pix_fmt yuva444p12le -vf scale

FATE_NOTCHLC += fate-notchlc-chroma-alpha
fate-notchlc-chroma-alpha: CMD = framemd5 -i $(TARGET_SAMPLES)/notchlc/chroma-alpha.mov -pix_fmt yuva444p12le -vf scale

FATE_NOTCHLC-$(call FRAMEMD5, MOV, NOTCHLC, SCALE_FILTER) += $(FATE_NOTCHLC)

FATE_SAMPLES_FFMPEG += $(FATE_NOTCHLC-yes)
fate-notchlc: $(FATE_NOTCHLC-yes)
