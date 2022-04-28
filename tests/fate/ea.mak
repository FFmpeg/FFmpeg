FATE_SAMPLES_EA-$(call ENCDEC, PCM_S16LE ADPCM_EA_XAS, PCM_S16LE EA_CDATA, ARESAMPLE_FILTER) += fate-ea-cdata
fate-ea-cdata: CMD = md5 -i $(TARGET_SAMPLES)/ea-cdata/166b084d.46410f77.0009b440.24be960c.cdata -f s16le -af aresample

FATE_SAMPLES_EA-$(call FRAMECRC, EA, EACMV, SCALE_FILTER) += fate-ea-cmv
fate-ea-cmv: CMD = framecrc -i $(TARGET_SAMPLES)/ea-cmv/TITLE.CMV -pix_fmt rgb24 -vf scale

FATE_SAMPLES_EA-$(call FRAMECRC, EA, EAMAD) += fate-ea-mad
fate-ea-mad: CMD = framecrc -i $(TARGET_SAMPLES)/ea-mad/NFS6LogoE.mad -an

FATE_SAMPLES_EA-$(call FRAMECRC, EA, EATGQ) += fate-ea-tgq
fate-ea-tgq: CMD = framecrc -i $(TARGET_SAMPLES)/ea-tgq/v27.tgq -an

FATE_EA_TGV-$(call FRAMECRC, EA, EATGV, SCALE_FILTER) += fate-ea-tgv-1 fate-ea-tgv-2
fate-ea-tgv-1: CMD = framecrc -i $(TARGET_SAMPLES)/ea-tgv/INTRO8K-partial.TGV -pix_fmt rgb24 -an -vf scale
fate-ea-tgv-2: CMD = framecrc -i $(TARGET_SAMPLES)/ea-tgv/INTEL_S.TGV -pix_fmt rgb24 -an -vf scale

FATE_SAMPLES_EA-yes += $(FATE_EA_TGV-yes)
fate-ea-tgv: $(FATE_EA_TGV-yes)

FATE_SAMPLES_EA-$(call FRAMECRC, EA, EATQI) += fate-ea-tqi
fate-ea-tqi: CMD = framecrc -i $(TARGET_SAMPLES)/ea-wve/networkBackbone-partial.wve -frames:v 26 -an

FATE_SAMPLES_FFMPEG += $(FATE_SAMPLES_EA-yes)
fate-ea: $(FATE_SAMPLES_EA-yes)
