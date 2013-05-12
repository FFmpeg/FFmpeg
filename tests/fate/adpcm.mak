FATE_ADPCM-$(call DEMDEC, FOURXM, ADPCM_4XM) += fate-adpcm-4xm
fate-adpcm-4xm: CMD = framecrc -i $(SAMPLES)/4xm/dracula.4xm -vn -map 0:6

FATE_ADPCM-$(call DEMDEC, AST, ADPCM_AFC) += fate-adpcm-afc
fate-adpcm-afc: CMD = framecrc -i $(SAMPLES)/ast/demo11_02_partial.ast

FATE_ADPCM-$(call DEMDEC, WAV, ADPCM_CT) += fate-adpcm-creative
fate-adpcm-creative: CMD = md5 -i $(SAMPLES)/creative/intro-partial.wav -f s16le

FATE_ADPCM-$(call DEMDEC, VOC, ADPCM_SBPRO_2) += fate-adpcm-creative-8-2bit
fate-adpcm-creative-8-2bit: CMD = md5 -i $(SAMPLES)/creative/BBC_2BIT.VOC -f s16le

FATE_ADPCM-$(call DEMDEC, VOC, ADPCM_SBPRO_3) += fate-adpcm-creative-8-2.6bit
fate-adpcm-creative-8-2.6bit: CMD = md5 -i $(SAMPLES)/creative/BBC_3BIT.VOC -f s16le

FATE_ADPCM-$(call DEMDEC, VOC, ADPCM_SBPRO_4) += fate-adpcm-creative-8-4bit
fate-adpcm-creative-8-4bit: CMD = md5 -i $(SAMPLES)/creative/BBC_4BIT.VOC -f s16le

FATE_ADPCM-$(call DEMDEC, EA, ADPCM_EA) += fate-adpcm-ea-1
fate-adpcm-ea-1: CMD = framecrc -i $(SAMPLES)/ea-wve/networkBackbone-partial.wve -frames:a 26 -vn

FATE_ADPCM-$(call DEMDEC, EA, ADPCM_EA) += fate-adpcm-ea-2
fate-adpcm-ea-2: CMD = framecrc -i $(SAMPLES)/ea-dct/NFS2Esprit-partial.dct -vn

FATE_ADPCM-$(call DEMDEC, XA, ADPCM_EA_MAXIS_XA) += fate-adpcm-ea-maxis-xa
fate-adpcm-ea-maxis-xa: CMD = framecrc -i $(SAMPLES)/maxis-xa/SC2KBUG.XA -frames:a 30

FATE_ADPCM-$(call DEMDEC, EA, ADPCM_EA_R1) += fate-adpcm-ea-r1
fate-adpcm-ea-r1: CMD = framecrc -i $(SAMPLES)/ea-mad/NFS6LogoE.mad -vn

FATE_ADPCM-$(call DEMDEC, EA, ADPCM_EA_R2) += fate-adpcm-ea-r2
fate-adpcm-ea-r2: CMD = crc -i $(SAMPLES)/ea-mpc/THX_logo.mpc -vn

FATE_ADPCM-$(call DEMDEC, EA, ADPCM_EA_R3) += fate-adpcm-ea-r3
fate-adpcm-ea-r3: CMD = crc -i $(SAMPLES)/ea-vp6/THX_logo.vp6 -vn

FATE_ADPCM-$(call DEMDEC, AVI, ADPCM_IMA_AMV) += fate-adpcm-ima-amv
fate-adpcm-ima-amv: CMD = framecrc -i $(SAMPLES)/amv/MTV_high_res_320x240_sample_Penguin_Joke_MTV_from_WMV.amv -t 10 -vn

FATE_ADPCM-$(call DEMDEC, APC, ADPCM_IMA_APC) += fate-adpcm-ima-apc
fate-adpcm-ima-apc: CMD = md5 -i $(SAMPLES)/cryo-apc/cine007.APC -f s16le

FATE_ADPCM-$(call DEMDEC, AVI, ADPCM_IMA_DK3) += fate-adpcm-ima-dk3
fate-adpcm-ima-dk3: CMD = md5 -i $(SAMPLES)/duck/sop-audio-only.avi -f s16le

FATE_ADPCM-$(call DEMDEC, AVI, ADPCM_IMA_DK4) += fate-adpcm-ima-dk4
fate-adpcm-ima-dk4: CMD = md5 -i $(SAMPLES)/duck/salsa-audio-only.avi -f s16le

FATE_ADPCM-$(call DEMDEC, EA, ADPCM_IMA_EA_EACS) += fate-adpcm-ima-ea-eacs
fate-adpcm-ima-ea-eacs: CMD = framecrc -i $(SAMPLES)/ea-tgv/INTRO8K-partial.TGV -vn

FATE_ADPCM-$(call DEMDEC, EA, ADPCM_IMA_EA_SEAD) += fate-adpcm-ima-ea-sead
fate-adpcm-ima-ea-sead: CMD = framecrc -i $(SAMPLES)/ea-tgv/INTEL_S.TGV -vn

FATE_ADPCM-$(call DEMDEC, ISS, ADPCM_IMA_ISS) += fate-adpcm-ima-iss
fate-adpcm-ima-iss: CMD = md5 -i $(SAMPLES)/funcom-iss/0004010100.iss -f s16le

FATE_ADPCM-$(call DEMDEC, WAV, ADPCM_IMA_OKI) += fate-adpcm-ima-oki
fate-adpcm-ima-oki: CMD = md5 -i $(SAMPLES)/oki/test.wav -f s16le

FATE_ADPCM-$(call DEMDEC, SMJPEG, ADPCM_IMA_SMJPEG) += fate-adpcm-ima-smjpeg
fate-adpcm-ima-smjpeg: CMD = framecrc -i $(SAMPLES)/smjpeg/scenwin.mjpg -vn

FATE_ADPCM-$(call DEMDEC, MOV, ADPCM_IMA_WAV) += fate-adpcm-ima_wav-stereo
fate-adpcm-ima_wav-stereo: CMD = md5 -i $(SAMPLES)/qt-surge-suite/surge-2-16-L-ms11.mov -f s16le

FATE_ADPCM-$(call DEMDEC, WSVQA, ADPCM_IMA_WS) += fate-adpcm-ima-ws
fate-adpcm-ima-ws: CMD = framecrc -i $(SAMPLES)/vqa/cc-demo1-partial.vqa -vn

FATE_ADPCM-$(call DEMDEC, DXA, ADPCM_MS) += fate-adpcm-ms-mono
fate-adpcm-ms-mono: CMD = framecrc -i $(SAMPLES)/dxa/meetsquid.dxa -t 2 -vn

FATE_ADPCM-$(call DEMDEC, MOV, ADPCM_MS) += fate-adpcm_ms-stereo
fate-adpcm_ms-stereo: CMD = md5 -i $(SAMPLES)/qt-surge-suite/surge-2-16-L-ms02.mov -f s16le

FATE_ADPCM-$(call DEMDEC, THP, ADPCM_THP) += fate-adpcm-thp
fate-adpcm-thp: CMD = framecrc -i $(SAMPLES)/thp/pikmin2-opening1-partial.thp -vn

FATE_ADPCM-$(call DEMDEC, STR, ADPCM_XA) += fate-adpcm-xa
fate-adpcm-xa: CMD = framecrc -i $(SAMPLES)/psx-str/abc000_cut.str -vn

FATE_SAMPLES_AVCONV += $(FATE_ADPCM-yes)
fate-adpcm: $(FATE_ADPCM-yes)
