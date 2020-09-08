#lossless
FATE_CANOPUS_CLLC += fate-canopus-cllc-argb
fate-canopus-cllc-argb: CMD = framecrc -i $(TARGET_SAMPLES)/cllc/sample-cllc-argb.avi

FATE_CANOPUS_CLLC += fate-canopus-cllc-rgb
fate-canopus-cllc-rgb: CMD = framecrc -i $(TARGET_SAMPLES)/cllc/sample-cllc-rgb.avi

FATE_CANOPUS_CLLC += fate-canopus-cllc-yuy2-noblock
fate-canopus-cllc-yuy2-noblock: CMD = framecrc -i $(TARGET_SAMPLES)/cllc/sample-cllc-yuy2-noblock.avi

FATE_SAMPLES_FFMPEG-$(call DEMDEC, AVI, CLLC) += $(FATE_CANOPUS_CLLC)
fate-canopus-cllc: $(FATE_CANOPUS_CLLC)

#lossy
FATE_CANOPUS_HQ_HQA += fate-canopus-hq_hqa-hq
fate-canopus-hq_hqa-hq: CMD = framecrc -i $(TARGET_SAMPLES)/canopus/hq.avi

FATE_CANOPUS_HQ_HQA += fate-canopus-hq_hqa-hqa
fate-canopus-hq_hqa-hqa: CMD = framecrc -i $(TARGET_SAMPLES)/canopus/hqa.avi

FATE_CANOPUS_HQ_HQA += fate-canopus-hq_hqa-inter
fate-canopus-hq_hqa-inter: CMD = framecrc -i $(TARGET_SAMPLES)/canopus/hq25i.avi

FATE_SAMPLES_FFMPEG-$(call DEMDEC, AVI, HQ_HQA) += $(FATE_CANOPUS_HQ_HQA)
fate-canopus-hq_hqa: $(FATE_CANOPUS_HQ_HQA)

FATE_CANOPUS_HQX += fate-canopus-hqx422
fate-canopus-hqx422: CMD = framecrc -i $(TARGET_SAMPLES)/canopus/hqx422.avi -pix_fmt yuv422p16be -an -vf scale

FATE_CANOPUS_HQX += fate-canopus-hqx422a
fate-canopus-hqx422a: CMD = framecrc -i $(TARGET_SAMPLES)/canopus/hqx422a.avi -pix_fmt yuv422p16be -an -vf scale

FATE_SAMPLES_FFMPEG-$(call DEMDEC, AVI, HQX) += $(FATE_CANOPUS_HQX)
fate-canopus-hqx: $(FATE_CANOPUS_HQX)
