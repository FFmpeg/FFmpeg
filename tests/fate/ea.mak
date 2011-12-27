FATE_TESTS += fate-ea-cdata
fate-ea-cdata: CMD = md5 -i $(SAMPLES)/ea-cdata/166b084d.46410f77.0009b440.24be960c.cdata -f s16le

FATE_TESTS += fate-ea-cmv
fate-ea-cmv: CMD = framecrc -i $(SAMPLES)/ea-cmv/TITLE.CMV -vsync 0 -pix_fmt rgb24

FATE_TESTS += fate-ea-dct
fate-ea-dct: CMD = framecrc -idct simple -i $(SAMPLES)/ea-dct/NFS2Esprit-partial.dct

FATE_TESTS += fate-ea-tgq
fate-ea-tgq: CMD = framecrc -i $(SAMPLES)/ea-tgq/v27.tgq -an

FATE_TESTS += fate-ea-tgv-ima-ea-eacs
fate-ea-tgv-ima-ea-eacs: CMD = framecrc -i $(SAMPLES)/ea-tgv/INTRO8K-partial.TGV -pix_fmt rgb24

FATE_TESTS += fate-ea-tgv-ima-ea-sead
fate-ea-tgv-ima-ea-sead: CMD = framecrc -i $(SAMPLES)/ea-tgv/INTEL_S.TGV -pix_fmt rgb24
