FATE_ADPCM-$(call DEMDEC, FOURXM, ADPCM_4XM) += fate-adpcm-4xm
fate-adpcm-4xm: CMD = framecrc -i $(TARGET_SAMPLES)/4xm/dracula.4xm -vn -map 0:6 -af aresample

FATE_ADPCM-$(call DEMDEC, AST, ADPCM_AFC) += fate-adpcm-afc
fate-adpcm-afc: CMD = framecrc -i $(TARGET_SAMPLES)/ast/demo11_02_partial.ast -af aresample

FATE_ADPCM-$(call DEMDEC, WAV, ADPCM_CT) += fate-adpcm-creative
fate-adpcm-creative: CMD = md5 -i $(TARGET_SAMPLES)/creative/intro-partial.wav -f s16le

FATE_ADPCM-$(call DEMDEC, VOC, ADPCM_SBPRO_2) += fate-adpcm-creative-8-2bit
fate-adpcm-creative-8-2bit: CMD = md5 -i $(TARGET_SAMPLES)/creative/BBC_2BIT.VOC -f s16le

FATE_ADPCM-$(call DEMDEC, VOC, ADPCM_SBPRO_3) += fate-adpcm-creative-8-2.6bit
fate-adpcm-creative-8-2.6bit: CMD = md5 -i $(TARGET_SAMPLES)/creative/BBC_3BIT.VOC -f s16le

FATE_ADPCM-$(call DEMDEC, VOC, ADPCM_SBPRO_4) += fate-adpcm-creative-8-4bit
fate-adpcm-creative-8-4bit: CMD = md5 -i $(TARGET_SAMPLES)/creative/BBC_4BIT.VOC -f s16le

FATE_ADPCM-$(call DEMDEC, ADP, ADPCM_DTK) += fate-adpcm-dtk
fate-adpcm-dtk: CMD = framecrc -i $(TARGET_SAMPLES)/adp/shakespr_partial.adp -f s16le -af aresample

FATE_ADPCM-$(call DEMDEC, EA, ADPCM_EA) += fate-adpcm-ea-1
fate-adpcm-ea-1: CMD = framecrc -i $(TARGET_SAMPLES)/ea-wve/networkBackbone-partial.wve -frames:a 26 -vn

FATE_ADPCM-$(call DEMDEC, EA, ADPCM_EA) += fate-adpcm-ea-2
fate-adpcm-ea-2: CMD = framecrc -i $(TARGET_SAMPLES)/ea-dct/NFS2Esprit-partial.dct -vn

FATE_ADPCM-$(call DEMDEC, XA, ADPCM_EA_MAXIS_XA) += fate-adpcm-ea-maxis-xa
fate-adpcm-ea-maxis-xa: CMD = framecrc -i $(TARGET_SAMPLES)/maxis-xa/SC2KBUG.XA -frames:a 30

FATE_ADPCM-$(call DEMDEC, EA, ADPCM_EA_R1) += fate-adpcm-ea-r1
fate-adpcm-ea-r1: CMD = framecrc -i $(TARGET_SAMPLES)/ea-mad/NFS6LogoE.mad -vn -af aresample

FATE_ADPCM-$(call DEMDEC, EA, ADPCM_EA_R2) += fate-adpcm-ea-r2
fate-adpcm-ea-r2: CMD = crc -i $(TARGET_SAMPLES)/ea-mpc/THX_logo.mpc -vn -af aresample

FATE_ADPCM-$(call DEMDEC, EA, ADPCM_EA_R3) += fate-adpcm-ea-r3
fate-adpcm-ea-r3: CMD = crc -i $(TARGET_SAMPLES)/ea-vp6/THX_logo.vp6 -vn -af aresample

FATE_ADPCM-$(call DEMDEC, AVI, ADPCM_IMA_AMV) += fate-adpcm-ima-amv
fate-adpcm-ima-amv: CMD = framecrc -i $(TARGET_SAMPLES)/amv/MTV_high_res_320x240_sample_Penguin_Joke_MTV_from_WMV.amv -t 10 -vn

FATE_ADPCM-$(call DEMDEC, APC, ADPCM_IMA_APC) += fate-adpcm-ima-apc
fate-adpcm-ima-apc: CMD = md5 -i $(TARGET_SAMPLES)/cryo-apc/cine007.APC -f s16le

FATE_ADPCM-$(call DEMDEC, AVI, ADPCM_IMA_DK3) += fate-adpcm-ima-dk3
fate-adpcm-ima-dk3: CMD = md5 -i $(TARGET_SAMPLES)/duck/sop-audio-only.avi -f s16le

FATE_ADPCM-$(call DEMDEC, AVI, ADPCM_IMA_DK4) += fate-adpcm-ima-dk4
fate-adpcm-ima-dk4: CMD = md5 -i $(TARGET_SAMPLES)/duck/salsa-audio-only.avi -f s16le

FATE_ADPCM-$(call DEMDEC, EA, ADPCM_IMA_EA_EACS) += fate-adpcm-ima-ea-eacs
fate-adpcm-ima-ea-eacs: CMD = framecrc -i $(TARGET_SAMPLES)/ea-tgv/INTRO8K-partial.TGV -vn

FATE_ADPCM-$(call DEMDEC, EA, ADPCM_IMA_EA_SEAD) += fate-adpcm-ima-ea-sead
fate-adpcm-ima-ea-sead: CMD = framecrc -i $(TARGET_SAMPLES)/ea-tgv/INTEL_S.TGV -vn

FATE_ADPCM-$(call DEMDEC, ISS, ADPCM_IMA_ISS) += fate-adpcm-ima-iss
fate-adpcm-ima-iss: CMD = md5 -i $(TARGET_SAMPLES)/funcom-iss/0004010100.iss -f s16le

FATE_ADPCM-$(call DEMDEC, WAV, ADPCM_IMA_OKI) += fate-adpcm-ima-oki
fate-adpcm-ima-oki: CMD = md5 -i $(TARGET_SAMPLES)/oki/test.wav -f s16le

FATE_ADPCM-$(call DEMDEC, RSD, ADPCM_IMA_RAD) += fate-adpcm-ima-rad
fate-adpcm-ima-rad: CMD = md5 -i $(TARGET_SAMPLES)/rsd/hit_run_partial.rsd -f s16le

FATE_ADPCM-$(call DEMDEC, SMJPEG, ADPCM_IMA_SMJPEG) += fate-adpcm-ima-smjpeg
fate-adpcm-ima-smjpeg: CMD = framecrc -i $(TARGET_SAMPLES)/smjpeg/scenwin.mjpg -vn

FATE_ADPCM-$(call DEMDEC, MOV, ADPCM_IMA_WAV) += fate-adpcm-ima_wav-stereo
fate-adpcm-ima_wav-stereo: CMD = md5 -i $(TARGET_SAMPLES)/qt-surge-suite/surge-2-16-L-ms11.mov -f s16le -af aresample

FATE_ADPCM-$(call DEMDEC, WSVQA, ADPCM_IMA_WS) += fate-adpcm-ima-ws
fate-adpcm-ima-ws: CMD = framecrc -i $(TARGET_SAMPLES)/vqa/cc-demo1-partial.vqa -vn

FATE_ADPCM-$(call DEMDEC, DXA, ADPCM_MS) += fate-adpcm-ms-mono
fate-adpcm-ms-mono: CMD = framecrc -i $(TARGET_SAMPLES)/dxa/meetsquid.dxa -t 2 -vn

FATE_ADPCM-$(call DEMDEC, MOV, ADPCM_MS) += fate-adpcm_ms-stereo
fate-adpcm_ms-stereo: CMD = md5 -i $(TARGET_SAMPLES)/qt-surge-suite/surge-2-16-L-ms02.mov -f s16le

FATE_ADPCM-$(call DEMDEC, THP, ADPCM_THP) += fate-adpcm-thp
fate-adpcm-thp: CMD = framecrc -i $(TARGET_SAMPLES)/thp/pikmin2-opening1-partial.thp -vn -af aresample

FATE_ADPCM-$(call DEMDEC, SMUSH, ADPCM_VIMA) += fate-adpcm-vima
fate-adpcm-vima: CMD = framecrc -i $(TARGET_SAMPLES)/smush/ronin_part.znm -vn

FATE_ADPCM-$(call DEMDEC, STR, ADPCM_XA) += fate-adpcm-xa
fate-adpcm-xa: CMD = framecrc -i $(TARGET_SAMPLES)/psx-str/abc000_cut.str -vn -af aresample

FATE_ADPCM-$(call DEMDEC, ARGO_ASF, ADPCM_ARGO) += fate-adpcm-argo-mono
fate-adpcm-argo-mono: CMD = md5 -i $(TARGET_SAMPLES)/argo-asf/PWIN22M.ASF -f s16le -af aresample

FATE_ADPCM-$(call DEMDEC, ARGO_ASF, ADPCM_ARGO) += fate-adpcm-argo-stereo
fate-adpcm-argo-stereo: CMD = md5 -i $(TARGET_SAMPLES)/argo-asf/CBK2_cut.asf -f s16le -af aresample

FATE_ADPCM-$(call DEMDEC, KVAG, ADPCM_IMA_SSI) += fate-adpcm-ima-ssi-mono
fate-adpcm-ima-ssi-mono: CMD = md5 -i $(TARGET_SAMPLES)/kvag/mull1_cut.vag -f s16le

FATE_ADPCM-$(call DEMDEC, KVAG, ADPCM_IMA_SSI) += fate-adpcm-ima-ssi-stereo
fate-adpcm-ima-ssi-stereo: CMD = md5 -i $(TARGET_SAMPLES)/kvag/credits_cut.vag -f s16le

FATE_ADPCM-$(call DEMDEC, APM, ADPCM_IMA_APM) += fate-adpcm-ima-apm-mono
fate-adpcm-ima-apm-mono: CMD = md5 -i $(TARGET_SAMPLES)/apm/outro1.apm -f s16le

FATE_ADPCM-$(call DEMDEC, APM, ADPCM_IMA_APM) += fate-adpcm-ima-apm-stereo
fate-adpcm-ima-apm-stereo: CMD = md5 -i $(TARGET_SAMPLES)/apm/AS01.apm -f s16le

FATE_ADPCM-$(call DEMDEC, ALP, ADPCM_IMA_ALP) += fate-adpcm-ima-alp-mono
fate-adpcm-ima-alp-mono: CMD = md5 -i $(TARGET_SAMPLES)/alp/AD_P11.PCM -f s16le

FATE_ADPCM-$(call DEMDEC, ALP, ADPCM_IMA_ALP) += fate-adpcm-ima-alp-stereo
fate-adpcm-ima-alp-stereo: CMD = md5 -i $(TARGET_SAMPLES)/alp/theme-cut.tun -f s16le

FATE_ADPCM-$(call DEMDEC, PP_BNK, ADPCM_IMA_CUNNING) += fate-adpcm-ima-cunning-single
fate-adpcm-ima-cunning-single: CMD = md5 -y -i $(TARGET_SAMPLES)/pp_bnk/GD-cut.5c -f s16le -af aresample

FATE_ADPCM-$(call DEMDEC, PP_BNK, ADPCM_IMA_CUNNING) += fate-adpcm-ima-cunning-track0
fate-adpcm-ima-cunning-track0: CMD = md5 -y -i $(TARGET_SAMPLES)/pp_bnk/VIDEOMOD-cut.11c -map 0:a:0 -f s16le -af aresample

FATE_ADPCM-$(call DEMDEC, PP_BNK, ADPCM_IMA_CUNNING) += fate-adpcm-ima-cunning-track1
fate-adpcm-ima-cunning-track1: CMD = md5 -y -i $(TARGET_SAMPLES)/pp_bnk/VIDEOMOD-cut.11c -map 0:a:1 -f s16le -af aresample

FATE_ADPCM-$(call DEMDEC, PP_BNK, ADPCM_IMA_CUNNING) += fate-adpcm-ima-cunning-trunc-t1
fate-adpcm-ima-cunning-trunc-t1: CMD = md5 -y -i $(TARGET_SAMPLES)/pp_bnk/VIDEOMOD-trunc-t1.11c -map 0:a:0 -f s16le -af aresample

FATE_ADPCM-$(call DEMDEC, PP_BNK, ADPCM_IMA_CUNNING) += fate-adpcm-ima-cunning-trunc-t2-track0
fate-adpcm-ima-cunning-trunc-t2-track0: CMD = md5 -y -i $(TARGET_SAMPLES)/pp_bnk/VIDEOMOD-trunc-t2.11c -map 0:a:0 -f s16le -af aresample

FATE_ADPCM-$(call DEMDEC, PP_BNK, ADPCM_IMA_CUNNING) += fate-adpcm-ima-cunning-trunc-t2-track1
fate-adpcm-ima-cunning-trunc-t2-track1: CMD = md5 -y -i $(TARGET_SAMPLES)/pp_bnk/VIDEOMOD-trunc-t2.11c -map 0:a:1 -f s16le -af aresample

FATE_ADPCM-$(call DEMDEC, PP_BNK, ADPCM_IMA_CUNNING) += fate-adpcm-ima-cunning-trunc-t2a-track0
fate-adpcm-ima-cunning-trunc-t2a-track0: CMD = md5 -y -i $(TARGET_SAMPLES)/pp_bnk/VIDEOMOD-trunc-t2a.11c -map 0:a:0 -f s16le -af aresample

FATE_ADPCM-$(call DEMDEC, PP_BNK, ADPCM_IMA_CUNNING) += fate-adpcm-ima-cunning-trunc-t2a-track1
fate-adpcm-ima-cunning-trunc-t2a-track1: CMD = md5 -y -i $(TARGET_SAMPLES)/pp_bnk/VIDEOMOD-trunc-t2a.11c -map 0:a:1 -f s16le -af aresample

FATE_ADPCM-$(call DEMDEC, PP_BNK, ADPCM_IMA_CUNNING) += fate-adpcm-ima-cunning-trunc-h2
fate-adpcm-ima-cunning-trunc-h2: CMD = md5 -y -i $(TARGET_SAMPLES)/pp_bnk/VIDEOMOD-trunc-h2.11c -map 0:a:0 -f s16le -af aresample

FATE_ADPCM-$(call DEMDEC, PP_BNK, ADPCM_IMA_CUNNING) += fate-adpcm-ima-cunning-stereo
fate-adpcm-ima-cunning-stereo: CMD = md5 -y -i $(TARGET_SAMPLES)/pp_bnk/MOGODON2-cut.44c -f s16le -af aresample

FATE_SAMPLES_AVCONV += $(FATE_ADPCM-yes)
fate-adpcm: $(FATE_ADPCM-yes)
