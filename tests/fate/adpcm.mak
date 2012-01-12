FATE_TESTS += fate-adpcm-ea-r2
fate-adpcm-ea-r2: CMD = crc -i $(SAMPLES)/ea-mpc/THX_logo.mpc -vn

FATE_TESTS += fate-adpcm-ea-r3
fate-adpcm-ea-r3: CMD = crc -i $(SAMPLES)/ea-vp6/THX_logo.vp6 -vn

FATE_TESTS += fate-creative-adpcm
fate-creative-adpcm: CMD = md5 -i $(SAMPLES)/creative/intro-partial.wav -f s16le

FATE_TESTS += fate-creative-adpcm-8-2bit
fate-creative-adpcm-8-2bit: CMD = md5 -i $(SAMPLES)/creative/BBC_2BIT.VOC -f s16le

FATE_TESTS += fate-creative-adpcm-8-2.6bit
fate-creative-adpcm-8-2.6bit: CMD = md5 -i $(SAMPLES)/creative/BBC_3BIT.VOC -f s16le

FATE_TESTS += fate-creative-adpcm-8-4bit
fate-creative-adpcm-8-4bit: CMD = md5 -i $(SAMPLES)/creative/BBC_4BIT.VOC -f s16le

FATE_TESTS += fate-ea-mad-adpcm-ea-r1
fate-ea-mad-adpcm-ea-r1: CMD = framecrc -i $(SAMPLES)/ea-mad/NFS6LogoE.mad

FATE_TESTS += fate-ea-tqi-adpcm
fate-ea-tqi-adpcm: CMD = framecrc -i $(SAMPLES)/ea-wve/networkBackbone-partial.wve -frames:v 26

FATE_TESTS += fate-psx-str-v3-adpcm_xa
fate-psx-str-v3-adpcm_xa: CMD = framecrc -i $(SAMPLES)/psx-str/abc000_cut.str -vn

FATE_TESTS += fate-qt-msadpcm-stereo
fate-qt-msadpcm-stereo: CMD = md5 -i $(SAMPLES)/qt-surge-suite/surge-2-16-L-ms02.mov -f s16le

FATE_TESTS += fate-qt-msimaadpcm-stereo
fate-qt-msimaadpcm-stereo: CMD = md5 -i $(SAMPLES)/qt-surge-suite/surge-2-16-L-ms11.mov -f s16le

FATE_TESTS += fate-thp-mjpeg-adpcm
fate-thp-mjpeg-adpcm: CMD = framecrc -idct simple -i $(SAMPLES)/thp/pikmin2-opening1-partial.thp
