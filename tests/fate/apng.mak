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

FATE_APNG-$(call FRAMECRC, APNG, APNG) += $(FATE_APNG)

FATE_SAMPLES_FFMPEG += $(FATE_APNG-yes)

# Generated here: a 1/2/4-bit palette animation whose later frames sit on an
# odd x offset, so the offset is not a byte offset in the packed row.
FATE_APNG_SMALL_BPP += fate-apng-small-bpp-1bit
fate-apng-small-bpp-1bit: CMD = framecrc -i $(TARGET_PATH)/tests/data/fate/apng-small-bpp-1bit.png -fps_mode passthrough
fate-apng-small-bpp-1bit: tests/data/fate/apng-small-bpp-1bit.png

FATE_APNG_SMALL_BPP += fate-apng-small-bpp-2bit
fate-apng-small-bpp-2bit: CMD = framecrc -i $(TARGET_PATH)/tests/data/fate/apng-small-bpp-2bit.png -fps_mode passthrough
fate-apng-small-bpp-2bit: tests/data/fate/apng-small-bpp-2bit.png

FATE_APNG_SMALL_BPP += fate-apng-small-bpp-4bit
fate-apng-small-bpp-4bit: CMD = framecrc -i $(TARGET_PATH)/tests/data/fate/apng-small-bpp-4bit.png -fps_mode passthrough
fate-apng-small-bpp-4bit: tests/data/fate/apng-small-bpp-4bit.png

FATE_APNG_SMALL_BPP-$(call FRAMECRC, APNG, APNG) += $(FATE_APNG_SMALL_BPP)
FATE_FFMPEG += $(FATE_APNG_SMALL_BPP-yes)

fate-apng: $(FATE_APNG-yes) $(FATE_APNG_SMALL_BPP-yes)
