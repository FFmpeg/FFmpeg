FATE_DNXHD = fate-dnxhr-parse     \
             fate-dnxhr-prefix1   \
             fate-dnxhr-prefix2   \
             fate-dnxhr-prefix3   \
             fate-dnxhr-prefix4   \
             fate-dnxhr-prefix5

FATE_DNXHD_SCALE := fate-dnxhd-mbaff     \
                    fate-dnxhr-444       \
                    fate-dnxhr-12bit     \

FATE_DNXHD-$(call FRAMECRC, MOV, DNXHD) += $(FATE_DNXHD)
FATE_DNXHD-$(call FRAMECRC, MOV, DNXHD, SCALE_FILTER) += $(FATE_DNXHD_SCALE)
FATE_SAMPLES_FFMPEG += $(FATE_DNXHD-yes)
fate-dnxhd: $(FATE_DNXHD-yes) $(FATE_VCODEC_DNXHD)

fate-dnxhd-mbaff: CMD = framecrc -flags +bitexact -idct simple -i $(TARGET_SAMPLES)/dnxhd/dnxhd100_cid1260.mov -pix_fmt yuv422p10le -vf scale
fate-dnxhr-444:   CMD = framecrc -flags +bitexact -idct simple -i $(TARGET_SAMPLES)/dnxhd/dnxhr444_cid1270.mov -pix_fmt yuv444p10le -vf scale
fate-dnxhr-12bit: CMD = framecrc -flags +bitexact -idct simple -i $(TARGET_SAMPLES)/dnxhd/dnxhr_cid1271_12bit.mov -pix_fmt yuv422p12le -vf scale
fate-dnxhr-parse: CMD = framecrc -flags +bitexact -idct simple -i $(TARGET_SAMPLES)/dnxhd/dnxhr_cid1274.dnxhr -pix_fmt yuv422p
fate-dnxhr-prefix1: CMD = framecrc -flags +bitexact -idct simple -i $(TARGET_SAMPLES)/dnxhd/prefix-256x1536.dnxhr -pix_fmt yuv422p
fate-dnxhr-prefix2: CMD = framecrc -flags +bitexact -idct simple -i $(TARGET_SAMPLES)/dnxhd/prefix-256x1716.dnxhr -pix_fmt yuv422p
fate-dnxhr-prefix3: CMD = framecrc -flags +bitexact -idct simple -i $(TARGET_SAMPLES)/dnxhd/prefix-256x2048.dnxhr -pix_fmt yuv422p
fate-dnxhr-prefix4: CMD = framecrc -flags +bitexact -idct simple -i $(TARGET_SAMPLES)/dnxhd/prefix-256x2160.dnxhr -pix_fmt yuv422p
fate-dnxhr-prefix5: CMD = framecrc -flags +bitexact -idct simple -i $(TARGET_SAMPLES)/dnxhd/prefix-256x3212.dnxhr -pix_fmt yuv422p
