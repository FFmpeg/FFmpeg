FATE_APV = fate-apv-422-10

FATE_SAMPLES_FFMPEG-$(call FRAMECRC, APV, APV, SCALE_FILTER) += $(FATE_APV)

fate-apv:	$(FATE_APV)

fate-apv-422-10:	CMD = framecrc -i $(TARGET_SAMPLES)/apv/profile_422-10.apv -pix_fmt yuv422p10le -vf scale
