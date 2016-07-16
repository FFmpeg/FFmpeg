FATE_APNG += fate-apng-clock
fate-apng-clock: CMD = framecrc -i $(TARGET_SAMPLES)/apng/clock.png

FATE_APNG += fate-apng-osample
fate-apng-osample: CMD = framecrc -i $(TARGET_SAMPLES)/apng/o_sample.png

FATE_APNG-$(call DEMDEC, APNG, PNG) += $(FATE_APNG)

FATE_SAMPLES_FFMPEG += $(FATE_APNG)
fate-apng: $(FATE_APNG-yes)
