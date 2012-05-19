FATE_SAMPLES_AVCONV += fate-msmpeg4v1
fate-msmpeg4v1: CMD = framecrc -flags +bitexact -dct fastint -idct simple -i $(SAMPLES)/msmpeg4v1/mpg4.avi -an

FATE_MSVIDEO1 += fate-msvideo1-16bit
fate-msvideo1-16bit: CMD = framecrc -i $(SAMPLES)/cram/clock-cram16.avi -pix_fmt rgb24

FATE_MSVIDEO1 += fate-msvideo1-8bit
fate-msvideo1-8bit: CMD = framecrc -i $(SAMPLES)/cram/skating.avi -t 1 -pix_fmt rgb24

FATE_SAMPLES_AVCONV += $(FATE_MSVIDEO1)
fate-msvideo1: $(FATE_MSVIDEO1)

FATE_WMV8_DRM += fate-wmv8-drm
# discard last packet to avoid fails due to overread of VC-1 decoder
fate-wmv8-drm: CMD = framecrc -cryptokey 137381538c84c068111902a59c5cf6c340247c39 -i $(SAMPLES)/wmv8/wmv_drm.wmv -an -vframes 162

FATE_WMV8_DRM += fate-wmv8-drm-nodec
fate-wmv8-drm-nodec: CMD = framecrc -cryptokey 137381538c84c068111902a59c5cf6c340247c39 -i $(SAMPLES)/wmv8/wmv_drm.wmv -acodec copy -vcodec copy

FATE_SAMPLES_AVCONV += $(FATE_WMV8_DRM)
fate-wmv8_drm: $(FATE_WMV8_DRM)

FATE_VC1 += fate-vc1_sa00040
fate-vc1_sa00040: CMD = framecrc -i $(SAMPLES)/vc1/SA00040.vc1

FATE_VC1 += fate-vc1_sa00050
fate-vc1_sa00050: CMD = framecrc -i $(SAMPLES)/vc1/SA00050.vc1

FATE_VC1 += fate-vc1_sa10091
fate-vc1_sa10091: CMD = framecrc -i $(SAMPLES)/vc1/SA10091.vc1

FATE_VC1 += fate-vc1_sa20021
fate-vc1_sa20021: CMD = framecrc -i $(SAMPLES)/vc1/SA20021.vc1

FATE_VC1 += fate-vc1-ism
fate-vc1-ism: CMD = framecrc -i $(SAMPLES)/isom/vc1-wmapro.ism -an

FATE_SAMPLES_AVCONV += $(FATE_VC1)
fate-vc1: $(FATE_VC1)
