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

FATE_ADPCM += fate-adpcm-ea-mad-ea-r1
fate-adpcm-ea-mad-ea-r1: CMD = framecrc -i $(SAMPLES)/ea-mad/NFS6LogoE.mad

FATE_ADPCM += fate-adpcm-ea-tqi
fate-adpcm-ea-tqi: CMD = framecrc -i $(SAMPLES)/ea-wve/networkBackbone-partial.wve -frames:v 26

FATE_ADPCM += fate-adpcm-ima-dk3
fate-adpcm-ima-dk3: CMD = md5 -i $(SAMPLES)/duck/sop-audio-only.avi -f s16le

FATE_ADPCM += fate-adpcm-ima-dk4
fate-adpcm-ima-dk4: CMD = md5 -i $(SAMPLES)/duck/salsa-audio-only.avi -f s16le

FATE_ADPCM += fate-adpcm-ima_wav-stereo
fate-adpcm-ima_wav-stereo: CMD = md5 -i $(SAMPLES)/qt-surge-suite/surge-2-16-L-ms11.mov -f s16le

FATE_ADPCM += fate-adpcm-psx-str-v3
fate-adpcm-psx-str-v3: CMD = framecrc -i $(SAMPLES)/psx-str/abc000_cut.str -vn

FATE_ADPCM += fate-adpcm-thp
fate-adpcm-thp: CMD = framecrc -idct simple -i $(SAMPLES)/thp/pikmin2-opening1-partial.thp

FATE_ADPCM += fate-adpcm_ms-stereo
fate-adpcm_ms-stereo: CMD = md5 -i $(SAMPLES)/qt-surge-suite/surge-2-16-L-ms02.mov -f s16le

FATE_TESTS += $(FATE_ADPCM)
fate-adpcm: $(FATE_ADPCM)
