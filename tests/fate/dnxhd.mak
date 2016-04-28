FATE_DNXHD = fate-dnxhd-mbaff     \
             fate-dnxhr-444       \
             fate-dnxhr-parse

FATE_SAMPLES_AVCONV-$(call DEMDEC, MOV, DNXHD) += $(FATE_DNXHD)
fate-dnxhd: $(FATE_DNXHD) $(FATE_VCODEC_DNXHD)

fate-dnxhd-mbaff: CMD = framecrc -flags +bitexact -idct simple -i $(TARGET_SAMPLES)/dnxhd/dnxhd100_cid1260.mov -pix_fmt yuv422p10le
fate-dnxhr-444:   CMD = framecrc -flags +bitexact -idct simple -i $(TARGET_SAMPLES)/dnxhd/dnxhr444_cid1270.mov -pix_fmt yuv444p10le
fate-dnxhr-parse: CMD = framecrc -flags +bitexact -idct simple -i $(TARGET_SAMPLES)/dnxhd/dnxhr_cid1274.dnxhr -pix_fmt yuv422p
