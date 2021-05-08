FATE_APNG += fate-apng-clock
fate-apng-clock: CMD = framecrc -i $(TARGET_SAMPLES)/apng/clock.png

FATE_APNG += fate-apng-osample
fate-apng-osample: CMD = framecrc -i $(TARGET_SAMPLES)/apng/o_sample.png

FATE_APNG += fate-apng-dispose-previous
fate-apng-dispose-previous: CMD = framecrc -i $(TARGET_SAMPLES)/apng/apng_out_of_order_frames.png

FATE_APNG += fate-apng-dispose-background
fate-apng-dispose-background: CMD = framecrc -i $(TARGET_SAMPLES)/apng/015.png

FATE_APNG += fate-apng-dispose-background2
fate-apng-dispose-background2: CMD = framecrc -i $(TARGET_SAMPLES)/apng/alogo.png

FATE_APNG-$(call DEMDEC, APNG, APNG) += $(FATE_APNG)

FATE_SAMPLES_FFMPEG += $(FATE_APNG-yes)
fate-apng: $(FATE_APNG-yes)
