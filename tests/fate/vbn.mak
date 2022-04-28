fate-vbn-%: VBN_FILE = tests/data/$(subst fate-vbn-,,$(@)).vbn
fate-vbn-%: PIX_FMT = rgba
fate-vbn-raw-rgb24: PIX_FMT = rgb24
fate-vbn-%: SRC = $(TARGET_SAMPLES)/png1/lena-$(PIX_FMT).png
fate-vbn-%: ENC_OPTS = -c:v vbn -format $(word 3,$(subst -, ,$(@)))
fate-vbn-%: CMD = refcmp_metadata_transcode "$(SRC)" "$(ENC_OPTS)" image2 vbn psnr $(PIX_FMT)

FATE_VBN += fate-vbn-dxt1
FATE_VBN += fate-vbn-dxt5
FATE_VBN += fate-vbn-raw-rgba
FATE_VBN += fate-vbn-raw-rgb24

FATE_VBN-$(call ENCDEC2, VBN, WRAPPED_AVFRAME PNG, IMAGE2,        \
                         PSNR_FILTER METADATA_FILTER SCALE_FILTER \
                         NULL_MUXER PIPE_PROTOCOL) += $(FATE_VBN)

FATE_SAMPLES_FFMPEG += $(FATE_VBN-yes)
fate-vbn: $(FATE_VBN-yes)
