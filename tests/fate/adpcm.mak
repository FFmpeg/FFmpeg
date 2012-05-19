FATE_ADPCM += fate-adpcm-ea-r2
fate-adpcm-ea-r2: CMD = crc -i $(SAMPLES)/ea-mpc/THX_logo.mpc -vn

FATE_ADPCM += fate-adpcm-ea-r3
fate-adpcm-ea-r3: CMD = crc -i $(SAMPLES)/ea-vp6/THX_logo.vp6 -vn

FATE_ADPCM += fate-adpcm-creative
fate-adpcm-creative: CMD = md5 -i $(SAMPLES)/creative/intro-partial.wav -f s16le

FATE_ADPCM += fate-adpcm-creative-8-2bit
fate-adpcm-creative-8-2bit: CMD = md5 -i $(SAMPLES)/creative/BBC_2BIT.VOC -f s16le

FATE_ADPCM += fate-adpcm-creative-8-2.6bit
fate-adpcm-creative-8-2.6bit: CMD = md5 -i $(SAMPLES)/creative/BBC_3BIT.VOC -f s16le

FATE_ADPCM += fate-adpcm-creative-8-4bit
fate-adpcm-creative-8-4bit: CMD = md5 -i $(SAMPLES)/creative/BBC_4BIT.VOC -f s16le

FATE_ADPCM += fate-adpcm-ea-1
fate-adpcm-ea-1: CMD = framecrc -i $(SAMPLES)/ea-wve/networkBackbone-partial.wve -frames:a 26 -vn

FATE_ADPCM += fate-adpcm-ea-2
fate-adpcm-ea-2: CMD = framecrc -i $(SAMPLES)/ea-dct/NFS2Esprit-partial.dct -vn

FATE_ADPCM += fate-adpcm-ea-maxis-xa
fate-adpcm-ea-maxis-xa: CMD = framecrc -i $(SAMPLES)/maxis-xa/SC2KBUG.XA -frames:a 30

FATE_ADPCM += fate-adpcm-ea-r1
fate-adpcm-ea-r1: CMD = framecrc -i $(SAMPLES)/ea-mad/NFS6LogoE.mad -vn

FATE_ADPCM += fate-adpcm-ima-amv
fate-adpcm-ima-amv: CMD = framecrc -i $(SAMPLES)/amv/MTV_high_res_320x240_sample_Penguin_Joke_MTV_from_WMV.amv -t 10 -vn

FATE_ADPCM += fate-adpcm-ima-apc
fate-adpcm-ima-apc: CMD = md5 -i $(SAMPLES)/cryo-apc/cine007.APC -f s16le

FATE_ADPCM += fate-adpcm-ima-dk3
fate-adpcm-ima-dk3: CMD = md5 -i $(SAMPLES)/duck/sop-audio-only.avi -f s16le

FATE_ADPCM += fate-adpcm-ima-dk4
fate-adpcm-ima-dk4: CMD = md5 -i $(SAMPLES)/duck/salsa-audio-only.avi -f s16le

FATE_ADPCM += fate-adpcm-ima-ea-eacs
fate-adpcm-ima-ea-eacs: CMD = framecrc -i $(SAMPLES)/ea-tgv/INTRO8K-partial.TGV -vn

FATE_ADPCM += fate-adpcm-ima-ea-sead
fate-adpcm-ima-ea-sead: CMD = framecrc -i $(SAMPLES)/ea-tgv/INTEL_S.TGV -vn

FATE_ADPCM += fate-adpcm-ima-iss
fate-adpcm-ima-iss: CMD = md5 -i $(SAMPLES)/funcom-iss/0004010100.iss -f s16le

FATE_ADPCM += fate-adpcm-ima-smjpeg
fate-adpcm-ima-smjpeg: CMD = framecrc -i $(SAMPLES)/smjpeg/scenwin.mjpg -vn

FATE_ADPCM += fate-adpcm-ima_wav-stereo
fate-adpcm-ima_wav-stereo: CMD = md5 -i $(SAMPLES)/qt-surge-suite/surge-2-16-L-ms11.mov -f s16le

FATE_ADPCM += fate-adpcm-ima-ws
fate-adpcm-ima-ws: CMD = framecrc -i $(SAMPLES)/vqa/cc-demo1-partial.vqa -vn

FATE_ADPCM += fate-adpcm-ms-mono
fate-adpcm-ms-mono: CMD = framecrc -i $(SAMPLES)/dxa/meetsquid.dxa -t 2 -vn

FATE_ADPCM += fate-adpcm-thp
fate-adpcm-thp: CMD = framecrc -i $(SAMPLES)/thp/pikmin2-opening1-partial.thp -vn

FATE_ADPCM += fate-adpcm-xa
fate-adpcm-xa: CMD = framecrc -i $(SAMPLES)/psx-str/abc000_cut.str -vn

FATE_ADPCM += fate-adpcm_ms-stereo
fate-adpcm_ms-stereo: CMD = md5 -i $(SAMPLES)/qt-surge-suite/surge-2-16-L-ms02.mov -f s16le

FATE_SAMPLES_AVCONV += $(FATE_ADPCM)
fate-adpcm: $(FATE_ADPCM)
