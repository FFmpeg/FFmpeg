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

# A large ancillary chunk before the first IDAT must not hide the animation
# from the probe (issue #26132). "plain" is a non-animated control: it has
# the same big chunk but no acTL, so png_pipe must still win.
FATE_APNG_BIGANC-$(call ALLYES, APNG_DEMUXER IMAGE_PNG_PIPE_DEMUXER FILE_PROTOCOL) += fate-apng-biganc-probe
fate-apng-biganc-probe: CMD = probefmt $(TARGET_PATH)/tests/data/fate/apng-biganc-1bit.png
fate-apng-biganc-probe: REF = apng
fate-apng-biganc-probe: CMP = oneline
fate-apng-biganc-probe: tests/data/fate/apng-biganc-1bit.png

FATE_APNG_BIGANC-$(call ALLYES, APNG_DEMUXER IMAGE_PNG_PIPE_DEMUXER FILE_PROTOCOL) += fate-apng-biganc-plain-probe
fate-apng-biganc-plain-probe: CMD = probefmt $(TARGET_PATH)/tests/data/fate/apng-biganc-plain-1bit.png
fate-apng-biganc-plain-probe: REF = png_pipe
fate-apng-biganc-plain-probe: CMP = oneline
fate-apng-biganc-plain-probe: tests/data/fate/apng-biganc-plain-1bit.png

FATE_FFPROBE += $(FATE_APNG_BIGANC-yes)

# The 4096-byte probe boundary falls inside the next chunk header, or inside
# a valid PLTE. Both must still probe as apng (issue #26132).
FATE_APNG_BND-$(call ALLYES, APNG_DEMUXER IMAGE_PNG_PIPE_DEMUXER FILE_PROTOCOL) += fate-apng-bndhdr-probe
fate-apng-bndhdr-probe: CMD = probefmt $(TARGET_PATH)/tests/data/fate/apng-bndhdr-4bit.png
fate-apng-bndhdr-probe: REF = apng
fate-apng-bndhdr-probe: CMP = oneline
fate-apng-bndhdr-probe: tests/data/fate/apng-bndhdr-4bit.png

FATE_APNG_BND-$(call ALLYES, APNG_DEMUXER IMAGE_PNG_PIPE_DEMUXER FILE_PROTOCOL) += fate-apng-bndplte-probe
fate-apng-bndplte-probe: CMD = probefmt $(TARGET_PATH)/tests/data/fate/apng-bndplte-4bit.png
fate-apng-bndplte-probe: REF = apng
fate-apng-bndplte-probe: CMP = oneline
fate-apng-bndplte-probe: tests/data/fate/apng-bndplte-4bit.png

FATE_FFPROBE += $(FATE_APNG_BND-yes)

FATE_APNG_BIGANC_FRAMES-$(call FRAMECRC, APNG, APNG) += fate-apng-biganc-frames
fate-apng-biganc-frames: CMD = framecrc -i $(TARGET_PATH)/tests/data/fate/apng-biganc-1bit.png -fps_mode passthrough
fate-apng-biganc-frames: tests/data/fate/apng-biganc-1bit.png
FATE_FFMPEG += $(FATE_APNG_BIGANC_FRAMES-yes)

fate-apng: $(FATE_APNG-yes) $(FATE_APNG_SMALL_BPP-yes) $(FATE_APNG_BIGANC-yes) $(FATE_APNG_BND-yes) $(FATE_APNG_BIGANC_FRAMES-yes)
