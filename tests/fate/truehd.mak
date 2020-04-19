FATE_TRUEHD-$(call DEMDEC, TRUEHD, TRUEHD) += fate-truehd-5.1
fate-truehd-5.1: CMD = md5pipe -f truehd -i $(TARGET_SAMPLES)/lossless-audio/truehd_5.1.raw -f s32le
fate-truehd-5.1: CMP = oneline
fate-truehd-5.1: REF = 95d8aac39dd9f0d7fb83dc7b6f88df35

FATE_TRUEHD-$(call DEMDEC, TRUEHD, TRUEHD) += fate-truehd-5.1-downmix-2.0
fate-truehd-5.1-downmix-2.0: CMD = md5pipe -f truehd -request_channel_layout 2 -i $(TARGET_SAMPLES)/lossless-audio/truehd_5.1.raw -f s32le
fate-truehd-5.1-downmix-2.0: CMP = oneline
fate-truehd-5.1-downmix-2.0: REF = a269aee0051d4400c9117136f08c9767

FATE_SAMPLES_AUDIO += $(FATE_TRUEHD-yes)
fate-truehd: $(FATE_TRUEHD-yes)
