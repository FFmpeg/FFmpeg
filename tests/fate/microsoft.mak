FATE_MICROSOFT-$(call DEMDEC, AVI, MSMPEG4V1) += fate-msmpeg4v1
fate-msmpeg4v1: CMD = framecrc -flags +bitexact -idct simple -i $(TARGET_SAMPLES)/msmpeg4v1/mpg4.avi -an

FATE_SAMPLES_AVCONV-$(call DEMDEC, ASF, MSS1) += fate-mss1-pal
fate-mss1-pal: CMD = framecrc -i $(TARGET_SAMPLES)/mss1/screen_codec.wmv -an

FATE_MSS2 += fate-mss2-pal
fate-mss2-pal: CMD = framecrc -i $(TARGET_SAMPLES)/mss2/rlepal.wmv

FATE_MSS2 += fate-mss2-pals
fate-mss2-pals: CMD = framecrc -i $(TARGET_SAMPLES)/mss2/rlepals.wmv

FATE_MSS2 += fate-mss2-rgb555
fate-mss2-rgb555: CMD = framecrc -i $(TARGET_SAMPLES)/mss2/rle555.wmv -pix_fmt rgb555le -vf scale

FATE_MSS2 += fate-mss2-rgb555s
fate-mss2-rgb555s: CMD = framecrc -i $(TARGET_SAMPLES)/mss2/rle555s.wmv -pix_fmt rgb555le -vf scale

FATE_MSS2 += fate-mss2-wmv
fate-mss2-wmv: CMD = framecrc -i $(TARGET_SAMPLES)/mss2/msscreencodec.wmv -an -frames 100

FATE_SAMPLES_AVCONV-$(call DEMDEC, ASF, MSS2) += $(FATE_MSS2)
fate-mss2: $(FATE_MSS2)

FATE_MTS2 += fate-mts2-xesc
fate-mts2-xesc: CMD = framecrc -i $(TARGET_SAMPLES)/mts2/sample.xesc -pix_fmt yuv444p

FATE_SAMPLES_AVCONV-$(call DEMDEC, ASF, MTS2) += $(FATE_MTS2)
fate-mts2: $(FATE_MTS2)

FATE_MSVIDEO1 += fate-msvideo1-8bit
fate-msvideo1-8bit: CMD = framecrc -i $(TARGET_SAMPLES)/cram/skating.avi -t 1 -pix_fmt rgb24 -vf scale

FATE_MSVIDEO1 += fate-msvideo1-16bit
fate-msvideo1-16bit: CMD = framecrc -i $(TARGET_SAMPLES)/cram/clock-cram16.avi -pix_fmt rgb24 -vf scale

FATE_MICROSOFT-$(call DEMDEC, AVI, MSVIDEO1) += $(FATE_MSVIDEO1)
fate-msvideo1: $(FATE_MSVIDEO1)

FATE_SAMPLES_AVCONV-$(call DEMDEC, ASF, MTS2) += fate-mts2
fate-mts2: CMD = framecrc -i $(TARGET_SAMPLES)/mts2/ScreenCapture.xesc

FATE_WMV3_DRM += fate-wmv3-drm-dec
# discard last packet to avoid fails due to overread of VC-1 decoder
fate-wmv3-drm-dec: CMD = framecrc -cryptokey 137381538c84c068111902a59c5cf6c340247c39 -i $(TARGET_SAMPLES)/wmv8/wmv_drm.wmv -an -frames:v 129

FATE_WMV3_DRM += fate-wmv3-drm-nodec
fate-wmv3-drm-nodec: CMD = framecrc -cryptokey 137381538c84c068111902a59c5cf6c340247c39 -i $(TARGET_SAMPLES)/wmv8/wmv_drm.wmv -c:a copy -c:v copy

FATE_SAMPLES_AVCONV-$(call DEMDEC, ASF, WMV3) += $(FATE_WMV3_DRM)
fate-wmv3-drm: $(FATE_WMV3_DRM)

FATE_MICROSOFT-$(call DEMDEC, ASF, WMV2) += fate-wmv8-x8intra
fate-wmv8-x8intra: CMD = framecrc -flags +bitexact -i $(TARGET_SAMPLES)/wmv8/wmv8_x8intra.wmv -an

FATE_VC1-$(CONFIG_VC1_DEMUXER) += fate-vc1_sa00040
fate-vc1_sa00040: CMD = framecrc -i $(TARGET_SAMPLES)/vc1/SA00040.vc1

FATE_VC1-$(CONFIG_VC1_DEMUXER) += fate-vc1_sa00050
fate-vc1_sa00050: CMD = framecrc -i $(TARGET_SAMPLES)/vc1/SA00050.vc1

FATE_VC1-$(CONFIG_VC1_DEMUXER) += fate-vc1_sa10091
fate-vc1_sa10091: CMD = framecrc -i $(TARGET_SAMPLES)/vc1/SA10091.vc1

FATE_VC1-$(CONFIG_VC1_DEMUXER) += fate-vc1_sa10143
fate-vc1_sa10143: CMD = framecrc -i $(TARGET_SAMPLES)/vc1/SA10143.vc1

FATE_VC1-$(CONFIG_VC1_DEMUXER) += fate-vc1_sa20021
fate-vc1_sa20021: CMD = framecrc -i $(TARGET_SAMPLES)/vc1/SA20021.vc1

FATE_VC1-$(CONFIG_VC1_DEMUXER) += fate-vc1_ilaced_twomv
fate-vc1_ilaced_twomv: CMD = framecrc -flags +bitexact -i $(TARGET_SAMPLES)/vc1/ilaced_twomv.vc1

FATE_VC1-$(CONFIG_VC1T_DEMUXER) += fate-vc1test_smm0005
fate-vc1test_smm0005: CMD = framecrc -i $(TARGET_SAMPLES)/vc1/SMM0005.rcv

FATE_VC1-$(CONFIG_VC1T_DEMUXER) += fate-vc1test_smm0015
fate-vc1test_smm0015: CMD = framecrc -i $(TARGET_SAMPLES)/vc1/SMM0015.rcv

FATE_VC1-$(CONFIG_MOV_DEMUXER) += fate-vc1-ism
fate-vc1-ism: CMD = framecrc -i $(TARGET_SAMPLES)/isom/vc1-wmapro.ism -an

FATE_MICROSOFT-$(CONFIG_VC1_DECODER) += $(FATE_VC1-yes)
fate-vc1: $(FATE_VC1-yes)

FATE_MICROSOFT-$(CONFIG_ASF_DEMUXER) += fate-asf-repldata
fate-asf-repldata: CMD = framecrc -i $(TARGET_SAMPLES)/asf/bug821-2.asf -c copy

FATE_MICROSOFT += $(FATE_MICROSOFT-yes)

FATE_SAMPLES_FFMPEG += $(FATE_MICROSOFT)
fate-microsoft: $(FATE_MICROSOFT)
