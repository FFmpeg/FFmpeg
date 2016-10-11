# This tests that the matroska demuxer supports modifying the colorspace
# properties in remuxing (-c:v copy)
# It also tests automatic insertion of the vp9_superframe bitstream filter
FATE_MATROSKA-$(call DEMMUX, MATROSKA, MATROSKA) += fate-matroska-remux
fate-matroska-remux: CMD = md5 -i $(TARGET_SAMPLES)/vp9-test-vectors/vp90-2-2pass-akiyo.webm -color_trc 4 -c:v copy -fflags +bitexact -strict -2 -f matroska
fate-matroska-remux: CMP = oneline
fate-matroska-remux: REF = 84e950f59677e306f944fca484888c5d

FATE_SAMPLES_AVCONV += $(FATE_MATROSKA-yes)
