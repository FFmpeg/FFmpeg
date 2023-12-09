# These two are animated JXL files
FATE_JPEGXL_ANIM_DEMUX += fate-jxl-anim-demux-newton
fate-jxl-anim-demux-newton: CMD = framecrc -i $(TARGET_SAMPLES)/jxl/newton.jxl -c copy
FATE_JPEGXL_ANIM_DEMUX += fate-jxl-anim-demux-icos4d
fate-jxl-anim-demux-icos4d: CMD = framecrc -i $(TARGET_SAMPLES)/jxl/icos4d.jxl -c copy

# These two are not animated JXL. They are here to check false positives.
FATE_JPEGXL_ANIM_DEMUX += fate-jxl-anim-demux-belgium
fate-jxl-anim-demux-belgium: CMD = framecrc -i $(TARGET_SAMPLES)/jxl/belgium.jxl -c copy
FATE_JPEGXL_ANIM_DEMUX += fate-jxl-anim-demux-lenna256
fate-jxl-anim-demux-lenna256: CMD = framecrc -i $(TARGET_SAMPLES)/jxl/lenna-256.jxl -c copy

FATE_JPEGXL_ANIM_DEMUX += $(FATE_JPEGXL_ANIM_DEMUX-yes)

FATE_SAMPLES_FFMPEG-$(call FRAMECRC, JPEGXL_ANIM) += $(FATE_JPEGXL_ANIM_DEMUX)
fate-jxl-anim-demux: $(FATE_JPEGXL_ANIM_DEMUX)

# parser tests
FATE_JPEGXL_PARSE += fate-jxl-small-ext-box
fate-jxl-small-ext-box: CMD = framecrc -i $(TARGET_SAMPLES)/jxl/l.jxl -c copy

FATE_JPEGXL_PARSE += fate-jxl-multiframe-permuted-toc
fate-jxl-multiframe-permuted-toc: CMD = framecrc -i $(TARGET_SAMPLES)/jxl/orange.jxl -c copy

FATE_JPEGXL_PARSE += $(FATE_JPEGXL_PARSE-yes)
FATE_SAMPLES_FFMPEG-$(call FRAMECRC, IMAGE_JPEGXL_PIPE, , JPEGXL_PARSER) += $(FATE_JPEGXL_PARSE)
fate-jxl-parse: $(FATE_JPEGXL_PARSE)
