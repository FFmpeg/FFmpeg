# test exif metadata in TIFF images
FATE_SAMPLES_EXIF-$(call DEMDEC, IMAGE2, TIFF) += fate-exif-image-tiff
fate-exif-image-tiff: CMD = probeframes $(TARGET_SAMPLES)/exif/image_small.tiff

# test exif metadata in JPG images
FATE_SAMPLES_EXIF-$(call DEMDEC, IMAGE2, MJPEG) += fate-exif-image-jpg
fate-exif-image-jpg: CMD = probeframes $(TARGET_SAMPLES)/exif/image_small.jpg

# test exif metadata in MP3 with embedded JPEG images
FATE_SAMPLES_EXIF-$(call ALLYES, MP3_DEMUXER IMAGE2_DEMUXER MJPEG_DECODER) += fate-exif-image-embedded
fate-exif-image-embedded: CMD = probeframes $(TARGET_SAMPLES)/exif/embedded_small.mp3

# add all -yes targets to the tested targets
FATE_SAMPLES_FFMPEG += $(FATE_SAMPLES_EXIF-yes)
