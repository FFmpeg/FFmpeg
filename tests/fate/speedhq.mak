FATE_SPEEDHQ = fate-speedhq-422                                           \
               fate-speedhq-422-singlefield

fate-speedhq-422:             CMD = framecrc -flags +bitexact -f rawvideo -c:v speedhq -tag:v SHQ2 -video_size 112x64 -i $(TARGET_SAMPLES)/speedhq/progressive.shq2 -pix_fmt yuv422p
fate-speedhq-422-singlefield: CMD = framecrc -flags +bitexact -f rawvideo -c:v speedhq -tag:v SHQ2 -video_size 112x32 -i $(TARGET_SAMPLES)/speedhq/singlefield.shq2 -pix_fmt yuv422p

FATE_SAMPLES_FFMPEG-$(call DEMDEC, RAWVIDEO, SPEEDHQ) += $(FATE_SPEEDHQ)
fate-speedhq: $(FATE_SPEEDHQ)
