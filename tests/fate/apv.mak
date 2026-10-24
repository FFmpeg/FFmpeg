FATE_APV = fate-apv-422-10 fate-apv-400-10

FATE_SAMPLES_FFMPEG-$(call FRAMECRC, APV, APV, SCALE_FILTER) += $(FATE_APV)

FATE_APV_FFPROBE-$(call DEMDEC, APV, APV) += fate-apv-timestamps
FATE_SAMPLES_FFPROBE += $(FATE_APV_FFPROBE-yes)

fate-apv:	$(FATE_APV) $(FATE_APV_FFPROBE-yes)

fate-apv-422-10:	CMD = framecrc -i $(TARGET_SAMPLES)/apv/profile_422-10.apv -pix_fmt yuv422p10le -vf scale

fate-apv-400-10:	CMD = framecrc -i $(TARGET_SAMPLES)/apv/profile_400-10.apv -pix_fmt gray10le -vf scale

fate-apv-timestamps:	CMD = probeframes -of compact -show_entries frame=pts,pkt_dts $(TARGET_SAMPLES)/apv/profile_422-10.apv
