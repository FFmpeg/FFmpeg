FATE_FILTER_SAMPLES-$(call ALLYES, SMJPEG_DEMUXER MJPEG_DECODER PERMS_FILTER OWDENOISE_FILTER) += fate-filter-owdenoise-sample
fate-filter-owdenoise-sample: CMD = ffmpeg -idct simple -i $(TARGET_SAMPLES)/smjpeg/scenwin.mjpg -vf "trim=duration=0.5,perms=random,owdenoise=10:20:20:enable=not(between(t\,0.2\,1.2))" -an -f rawvideo -
fate-filter-owdenoise-sample: REF = $(SAMPLES)/filter-reference/owdenoise-scenwin.raw
fate-filter-owdenoise-sample: CMP_TARGET = 1
fate-filter-owdenoise-sample: FUZZ = 3539
fate-filter-owdenoise-sample: CMP = oneoff

FATE_FILTER_SAMPLES-$(call ALLYES, PERMS_FILTER DELOGO_FILTER RM_DEMUXER RV30_DECODER) += fate-filter-delogo
fate-filter-delogo: CMD = framecrc -i $(TARGET_SAMPLES)/real/rv30.rm -vf perms=random,delogo=show=0:x=290:y=25:w=26:h=16 -an

FATE_YADIF += fate-filter-yadif-mode0
fate-filter-yadif-mode0: CMD = framecrc -flags bitexact -idct simple -i $(TARGET_SAMPLES)/mpeg2/mpeg2_field_encoding.ts -frames:v 30 -vf yadif=0

FATE_YADIF += fate-filter-yadif-mode1
fate-filter-yadif-mode1: CMD = framecrc -flags bitexact -idct simple -i $(TARGET_SAMPLES)/mpeg2/mpeg2_field_encoding.ts -frames:v 59 -vf yadif=1

FATE_YADIF += fate-filter-yadif10
fate-filter-yadif10: CMD = framecrc -flags bitexact -idct simple -i $(TARGET_SAMPLES)/mpeg2/mpeg2_field_encoding.ts -flags bitexact -pix_fmt yuv420p10le -frames:v 30 -vf yadif=0

FATE_YADIF += fate-filter-yadif16
fate-filter-yadif16: CMD = framecrc -flags bitexact -idct simple -i $(TARGET_SAMPLES)/mpeg2/mpeg2_field_encoding.ts -flags bitexact -pix_fmt yuv420p16le -frames:v 30 -vf yadif=0

FATE_FILTER_SAMPLES-$(call FILTERDEMDEC, YADIF, MPEGTS, MPEG2VIDEO) += $(FATE_YADIF)

FATE_W3FDIF += fate-filter-w3fdif-simple
fate-filter-w3fdif-simple: CMD = framecrc -flags bitexact -idct simple -i $(TARGET_SAMPLES)/mpeg2/mpeg2_field_encoding.ts -frames:v 30 -vf w3fdif=0

FATE_W3FDIF += fate-filter-w3fdif-complex
fate-filter-w3fdif-complex: CMD = framecrc -flags bitexact -idct simple -i $(TARGET_SAMPLES)/mpeg2/mpeg2_field_encoding.ts -frames:v 30 -vf w3fdif=1

FATE_FILTER_SAMPLES-$(call FILTERDEMDEC, W3FDIF, MPEGTS, MPEG2VIDEO) += $(FATE_W3FDIF)

FATE_MCDEINT += fate-filter-mcdeint-fast
fate-filter-mcdeint-fast: CMD = framecrc -flags bitexact -idct simple -i $(TARGET_SAMPLES)/mpeg2/mpeg2_field_encoding.ts -frames:v 30 -vf mcdeint=fast

FATE_MCDEINT += fate-filter-mcdeint-medium
fate-filter-mcdeint-medium: CMD = framecrc -flags bitexact -idct simple -i $(TARGET_SAMPLES)/mpeg2/mpeg2_field_encoding.ts -frames:v 30 -vf mcdeint=mode=medium

FATE_FILTER_SAMPLES-$(call ALLYES, MCDEINT_FILTER, MPEGTS_DEMUXER, MPEG2VIDEO_DECODER SNOW_ENCODER) += $(FATE_MCDEINT)

FATE_FILTER_SAMPLES-$(call ALLYES, CODECVIEW_FILTER RM_DEMUXER RV40_DECODER) += fate-filter-codecview-mvs
fate-filter-codecview-mvs: CMD = framecrc -flags2 +export_mvs -i $(TARGET_SAMPLES)/real/spygames-2MB.rmvb -vf codecview=mv=pf+bf+bb -frames:v 60 -an

FATE_FILTER_SAMPLES-$(call ALLYES, SHOWPALETTE_FILTER FLIC_DEMUXER FLIC_DECODER) += fate-filter-showpalette
fate-filter-showpalette: CMD = framecrc -i $(TARGET_SAMPLES)/fli/fli-engines.fli -vf showpalette=3 -pix_fmt bgra

FATE_FILTER_PALETTEGEN += fate-filter-palettegen-1
fate-filter-palettegen-1: CMD = framecrc -i $(TARGET_SAMPLES)/filter/anim.mkv -vf palettegen -pix_fmt bgra

FATE_FILTER_PALETTEGEN += fate-filter-palettegen-2
fate-filter-palettegen-2: CMD = framecrc -i $(TARGET_SAMPLES)/filter/anim.mkv -vf palettegen=max_colors=128:reserve_transparent=0:stats_mode=diff -pix_fmt bgra

fate-filter-palettegen: $(FATE_FILTER_PALETTEGEN)
FATE_FILTER_SAMPLES-$(call ALLYES, PALETTEGEN_FILTER MATROSKA_DEMUXER H264_DECODER) += $(FATE_FILTER_PALETTEGEN)

FATE_FILTER_PALETTEUSE += fate-filter-paletteuse-nodither
fate-filter-paletteuse-nodither: CMD = framecrc -i $(TARGET_SAMPLES)/filter/anim.mkv -i $(TARGET_SAMPLES)/filter/anim-palette.png -lavfi paletteuse=none -pix_fmt bgra

FATE_FILTER_PALETTEUSE += fate-filter-paletteuse-bayer
fate-filter-paletteuse-bayer: CMD = framecrc -i $(TARGET_SAMPLES)/filter/anim.mkv -i $(TARGET_SAMPLES)/filter/anim-palette.png -lavfi paletteuse=bayer -pix_fmt bgra

FATE_FILTER_PALETTEUSE += fate-filter-paletteuse-sierra2_4a
fate-filter-paletteuse-sierra2_4a: CMD = framecrc -i $(TARGET_SAMPLES)/filter/anim.mkv -i $(TARGET_SAMPLES)/filter/anim-palette.png -lavfi paletteuse=sierra2_4a:diff_mode=rectangle -pix_fmt bgra

fate-filter-paletteuse: $(FATE_FILTER_PALETTEUSE)
FATE_FILTER_SAMPLES-$(call ALLYES, PALETTEUSE_FILTER MATROSKA_DEMUXER H264_DECODER IMAGE2_DEMUXER PNG_DECODER) += $(FATE_FILTER_PALETTEUSE)

FATE_FILTER-$(call ALLYES, AVDEVICE LIFE_FILTER) += fate-filter-lavd-life
fate-filter-lavd-life: CMD = framecrc -f lavfi -i life=s=40x40:r=5:seed=42:mold=64:ratio=0.1:death_color=red:life_color=green -t 2

FATE_FILTER-$(call ALLYES, AVDEVICE TESTSRC_FILTER) += fate-filter-lavd-testsrc
fate-filter-lavd-testsrc: CMD = framecrc -f lavfi -i testsrc=r=7:n=2:d=10

FATE_FILTER-$(call ALLYES, TESTSRC2_FILTER) += fate-filter-testsrc2-yuv420p
fate-filter-testsrc2-yuv420p: CMD = framecrc -lavfi testsrc2=r=7:d=10 -pix_fmt yuv420p

FATE_FILTER-$(call ALLYES, TESTSRC2_FILTER) += fate-filter-testsrc2-yuv444p
fate-filter-testsrc2-yuv444p: CMD = framecrc -lavfi testsrc2=r=7:d=10 -pix_fmt yuv444p

FATE_FILTER-$(call ALLYES, TESTSRC2_FILTER) += fate-filter-testsrc2-rgb24
fate-filter-testsrc2-rgb24: CMD = framecrc -lavfi testsrc2=r=7:d=10 -pix_fmt rgb24

FATE_FILTER-$(call ALLYES, LAVFI_INDEV TESTSRC2_FILTER) += fate-filter-testsrc2-rgba
fate-filter-testsrc2-rgba: CMD = framecrc -lavfi testsrc2=r=7:d=10 -pix_fmt rgba

FATE_FILTER-$(call ALLYES, LAVFI_INDEV ALLRGB_FILTER) += fate-filter-allrgb
fate-filter-allrgb: CMD = framecrc -lavfi allrgb=rate=5:duration=1 -pix_fmt rgb24

FATE_FILTER-$(call ALLYES, LAVFI_INDEV ALLYUV_FILTER) += fate-filter-allyuv
fate-filter-allyuv: CMD = framecrc -lavfi allyuv=rate=5:duration=1 -pix_fmt yuv444p

FATE_FILTER-$(call ALLYES, LAVFI_INDEV PAL75BARS_FILTER) += fate-filter-pal75bars
fate-filter-pal75bars: CMD = framecrc -lavfi pal75bars=rate=5:duration=1 -pix_fmt yuv420p

FATE_FILTER-$(call ALLYES, LAVFI_INDEV PAL100BARS_FILTER) += fate-filter-pal100bars
fate-filter-pal100bars: CMD = framecrc -lavfi pal100bars=rate=5:duration=1 -pix_fmt yuv420p

FATE_FILTER-$(call ALLYES, LAVFI_INDEV RGBTESTSRC_FILTER) += fate-filter-rgbtestsrc
fate-filter-rgbtestsrc: CMD = framecrc -lavfi rgbtestsrc=rate=5:duration=1 -pix_fmt rgb24

FATE_FILTER-$(call ALLYES, LAVFI_INDEV SMPTEBARS_FILTER) += fate-filter-smptebars
fate-filter-smptebars: CMD = framecrc -lavfi smptebars=rate=5:duration=1 -pix_fmt yuv420p

FATE_FILTER-$(call ALLYES, LAVFI_INDEV SMPTEHDBARS_FILTER) += fate-filter-smptehdbars
fate-filter-smptehdbars: CMD = framecrc -lavfi smptehdbars=rate=5:duration=1 -pix_fmt yuv444p

FATE_FILTER-$(call ALLYES, LAVFI_INDEV YUVTESTSRC_FILTER) += fate-filter-yuvtestsrc-yuv444p
fate-filter-yuvtestsrc-yuv444p: CMD = framecrc -lavfi yuvtestsrc=rate=5:duration=1 -pix_fmt yuv444p

FATE_FILTER-$(call ALLYES, LAVFI_INDEV YUVTESTSRC_FILTER) += fate-filter-yuvtestsrc-yuv444p12
fate-filter-yuvtestsrc-yuv444p12: CMD = framecrc -lavfi yuvtestsrc=rate=5:duration=1,format=yuv444p12 -pix_fmt yuv444p12le

FATE_FILTER-$(call ALLYES, AVDEVICE TESTSRC_FILTER FORMAT_FILTER CONCAT_FILTER SCALE_FILTER) += fate-filter-lavd-scalenorm
fate-filter-lavd-scalenorm: tests/data/filtergraphs/scalenorm
fate-filter-lavd-scalenorm: CMD = framecrc -f lavfi -graph_file $(TARGET_PATH)/tests/data/filtergraphs/scalenorm -i dummy

FATE_FILTER-$(call ALLYES, FRAMERATE_FILTER TESTSRC2_FILTER) += fate-filter-framerate-up fate-filter-framerate-down
fate-filter-framerate-up: CMD = framecrc -lavfi testsrc2=r=2:d=10,framerate=fps=10 -t 1
fate-filter-framerate-down: CMD = framecrc -lavfi testsrc2=r=2:d=10,framerate=fps=1 -t 1

FATE_FILTER-$(call ALLYES, FRAMERATE_FILTER TESTSRC2_FILTER FORMAT_FILTER) += fate-filter-framerate-12bit-up fate-filter-framerate-12bit-down
fate-filter-framerate-12bit-up: CMD = framecrc -lavfi testsrc2=r=50:d=1,format=pix_fmts=yuv422p12le,framerate=fps=60 -t 1 -pix_fmt yuv422p12le
fate-filter-framerate-12bit-down: CMD = framecrc -lavfi testsrc2=r=60:d=1,format=pix_fmts=yuv422p12le,framerate=fps=50 -t 1 -pix_fmt yuv422p12le

FATE_FILTER_VSYNTH-$(CONFIG_BOXBLUR_FILTER) += fate-filter-boxblur
fate-filter-boxblur: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf boxblur=2:1

FATE_FILTER_VSYNTH-$(call ALLYES, COLORCHANNELMIXER_FILTER FORMAT_FILTER PERMS_FILTER) += fate-filter-colorchannelmixer
fate-filter-colorchannelmixer: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf format=rgb24,perms=random,colorchannelmixer=.31415927:.4:.31415927:0:.27182818:.8:.27182818:0:.2:.6:.2:0 -flags +bitexact -sws_flags +accurate_rnd+bitexact

FATE_FILTER_VSYNTH-$(CONFIG_DRAWBOX_FILTER) += fate-filter-drawbox
fate-filter-drawbox: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf drawbox=224:24:88:72:red@0.5

FATE_FILTER_VSYNTH-$(CONFIG_FADE_FILTER) += fate-filter-fade
fate-filter-fade: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf fade=in:5:15,fade=out:30:15

FATE_FILTER_VSYNTH-$(call ALLYES, INTERLACE_FILTER FIELDORDER_FILTER) += fate-filter-fieldorder
fate-filter-fieldorder: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf interlace=tff,fieldorder=bff -sws_flags +accurate_rnd+bitexact

define FATE_FPFILTER_SUITE
FATE_FILTER_FRAMEPACK += fate-filter-framepack-$(1)
fate-filter-framepack-$(1): CMD = framecrc -c:v pgmyuv -i $(TARGET_PATH)/tests/vsynth1/%02d.pgm -c:v pgmyuv -i $(TARGET_PATH)/tests/vsynth1/%02d.pgm -filter_complex framepack=$(1) -frames 15
endef

FPMODES = columns frameseq lines sbs tab
$(foreach MODE,$(FPMODES),$(eval $(call FATE_FPFILTER_SUITE,$(MODE))))
FATE_FILTER_VSYNTH-$(CONFIG_FRAMEPACK_FILTER) += $(FATE_FILTER_FRAMEPACK)
fate-filter-framepack: $(FATE_FILTER_FRAMEPACK)

FATE_FILTER_VSYNTH-$(CONFIG_GRADFUN_FILTER) += fate-filter-gradfun
fate-filter-gradfun: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf gradfun

FATE_FILTER_VSYNTH-$(CONFIG_HQDN3D_FILTER) += fate-filter-hqdn3d
fate-filter-hqdn3d: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf hqdn3d

FATE_FILTER_VSYNTH-$(CONFIG_INTERLACE_FILTER) += fate-filter-interlace
fate-filter-interlace: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf interlace

FATE_FILTER_VSYNTH-$(CONFIG_INTERLACE_FILTER) += fate-filter-interlace-complex
fate-filter-interlace-complex: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf interlace=lowpass=complex

FATE_FILTER_VSYNTH-$(call ALLYES, NEGATE_FILTER PERMS_FILTER) += fate-filter-negate
fate-filter-negate: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf perms=random,negate

FATE_FILTER_VSYNTH-$(CONFIG_HISTOGRAM_FILTER) += fate-filter-histogram-levels
fate-filter-histogram-levels: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf histogram -flags +bitexact -sws_flags +accurate_rnd+bitexact

FATE_FILTER_VSYNTH-$(CONFIG_WAVEFORM_FILTER) += fate-filter-waveform_column
fate-filter-waveform_column: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf waveform -flags +bitexact -sws_flags +accurate_rnd+bitexact

FATE_FILTER_VSYNTH-$(CONFIG_WAVEFORM_FILTER) += fate-filter-waveform_row
fate-filter-waveform_row: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf waveform=m=row -flags +bitexact -sws_flags +accurate_rnd+bitexact

FATE_FILTER_VSYNTH-$(CONFIG_WAVEFORM_FILTER) += fate-filter-waveform_envelope
fate-filter-waveform_envelope: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf waveform=e=3 -flags +bitexact -sws_flags +accurate_rnd+bitexact

FATE_FILTER_VSYNTH-$(CONFIG_WAVEFORM_FILTER) += fate-filter-waveform_uv
fate-filter-waveform_uv: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf waveform=c=6 -flags +bitexact -sws_flags +accurate_rnd+bitexact

FATE_FILTER_VSYNTH-$(CONFIG_VECTORSCOPE_FILTER) += fate-filter-vectorscope_gray
fate-filter-vectorscope_gray: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf vectorscope=gray -sws_flags +accurate_rnd+bitexact -frames:v 3

FATE_FILTER_VSYNTH-$(CONFIG_VECTORSCOPE_FILTER) += fate-filter-vectorscope_color
fate-filter-vectorscope_color: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf vectorscope=color -sws_flags +accurate_rnd+bitexact -frames:v 3

FATE_FILTER_VSYNTH-$(CONFIG_VECTORSCOPE_FILTER) += fate-filter-vectorscope_color2
fate-filter-vectorscope_color2: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf vectorscope=color2 -sws_flags +accurate_rnd+bitexact -frames:v 3

FATE_FILTER_VSYNTH-$(CONFIG_VECTORSCOPE_FILTER) += fate-filter-vectorscope_color3
fate-filter-vectorscope_color3: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf vectorscope=color3 -sws_flags +accurate_rnd+bitexact -frames:v 3

FATE_FILTER_VSYNTH-$(CONFIG_VECTORSCOPE_FILTER) += fate-filter-vectorscope_color4
fate-filter-vectorscope_color4: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf vectorscope=color4 -sws_flags +accurate_rnd+bitexact -frames:v 3

FATE_FILTER_VSYNTH-$(CONFIG_VECTORSCOPE_FILTER) += fate-filter-vectorscope_xy
fate-filter-vectorscope_xy: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf vectorscope=x=0:y=1 -sws_flags +accurate_rnd+bitexact -frames:v 3

FATE_FILTER_VSYNTH-$(CONFIG_MERGEPLANES_FILTER) += fate-filter-mergeplanes
fate-filter-mergeplanes: tests/data/filtergraphs/mergeplanes
fate-filter-mergeplanes: CMD = framecrc -c:v pgmyuv -i $(SRC) -c:v pgmyuv -i $(SRC) -filter_complex_script $(TARGET_PATH)/tests/data/filtergraphs/mergeplanes

FATE_FILTER_VSYNTH-$(CONFIG_HSTACK_FILTER) += fate-filter-hstack
fate-filter-hstack: tests/data/filtergraphs/hstack
fate-filter-hstack: CMD = framecrc -c:v pgmyuv -i $(SRC) -c:v pgmyuv -i $(SRC) -filter_complex_script $(TARGET_PATH)/tests/data/filtergraphs/hstack

FATE_FILTER_VSYNTH-$(CONFIG_VSTACK_FILTER) += fate-filter-vstack
fate-filter-vstack: tests/data/filtergraphs/vstack
fate-filter-vstack: CMD = framecrc -c:v pgmyuv -i $(SRC) -c:v pgmyuv -i $(SRC) -filter_complex_script $(TARGET_PATH)/tests/data/filtergraphs/vstack

FATE_FILTER_VSYNTH-$(CONFIG_OVERLAY_FILTER) += fate-filter-overlay
fate-filter-overlay: tests/data/filtergraphs/overlay
fate-filter-overlay: CMD = framecrc -c:v pgmyuv -i $(SRC) -c:v pgmyuv -i $(SRC) -filter_complex_script $(TARGET_PATH)/tests/data/filtergraphs/overlay

FATE_FILTER_VSYNTH-$(call ALLYES, SPLIT_FILTER SCALE_FILTER PAD_FILTER OVERLAY_FILTER) += fate-filter-overlay_rgb
fate-filter-overlay_rgb: tests/data/filtergraphs/overlay_rgb
fate-filter-overlay_rgb: CMD = framecrc -c:v pgmyuv -i $(SRC) -filter_complex_script $(TARGET_PATH)/tests/data/filtergraphs/overlay_rgb

FATE_FILTER_VSYNTH-$(call ALLYES, SPLIT_FILTER SCALE_FILTER PAD_FILTER OVERLAY_FILTER) += fate-filter-overlay_yuv420
fate-filter-overlay_yuv420: tests/data/filtergraphs/overlay_yuv420
fate-filter-overlay_yuv420: CMD = framecrc -c:v pgmyuv -i $(SRC) -filter_complex_script $(TARGET_PATH)/tests/data/filtergraphs/overlay_yuv420

FATE_FILTER_VSYNTH-$(call ALLYES, SPLIT_FILTER SCALE_FILTER PAD_FILTER OVERLAY_FILTER) += fate-filter-overlay_nv12
fate-filter-overlay_nv12: tests/data/filtergraphs/overlay_nv12
fate-filter-overlay_nv12: CMD = framecrc -c:v pgmyuv -i $(SRC) -filter_complex_script $(TARGET_PATH)/tests/data/filtergraphs/overlay_nv12
fate-filter-overlay_nv12: REF = $(SRC_PATH)/tests/ref/fate/filter-overlay_yuv420

FATE_FILTER_VSYNTH-$(call ALLYES, SPLIT_FILTER SCALE_FILTER PAD_FILTER OVERLAY_FILTER) += fate-filter-overlay_nv21
fate-filter-overlay_nv21: tests/data/filtergraphs/overlay_nv21
fate-filter-overlay_nv21: CMD = framecrc -c:v pgmyuv -i $(SRC) -filter_complex_script $(TARGET_PATH)/tests/data/filtergraphs/overlay_nv21
fate-filter-overlay_nv21: REF = $(SRC_PATH)/tests/ref/fate/filter-overlay_yuv420

FATE_FILTER_VSYNTH-$(call ALLYES, SPLIT_FILTER SCALE_FILTER PAD_FILTER OVERLAY_FILTER) += fate-filter-overlay_yuv422
fate-filter-overlay_yuv422: tests/data/filtergraphs/overlay_yuv422
fate-filter-overlay_yuv422: CMD = framecrc -c:v pgmyuv -i $(SRC) -filter_complex_script $(TARGET_PATH)/tests/data/filtergraphs/overlay_yuv422

FATE_FILTER_VSYNTH-$(call ALLYES, SPLIT_FILTER SCALE_FILTER PAD_FILTER OVERLAY_FILTER) += fate-filter-overlay_yuv444
fate-filter-overlay_yuv444: tests/data/filtergraphs/overlay_yuv444
fate-filter-overlay_yuv444: CMD = framecrc -c:v pgmyuv -i $(SRC) -filter_complex_script $(TARGET_PATH)/tests/data/filtergraphs/overlay_yuv444

FATE_FILTER_OVERLAY_ALPHA += fate-filter-overlay_yuv420_yuva420  fate-filter-overlay_yuv422_yuva422  fate-filter-overlay_yuv444_yuva444  fate-filter-overlay_rgb_rgba  fate-filter-overlay_gbrp_gbrap
FATE_FILTER_OVERLAY_ALPHA += fate-filter-overlay_yuva420_yuva420 fate-filter-overlay_yuva422_yuva422 fate-filter-overlay_yuva444_yuva444 fate-filter-overlay_rgba_rgba fate-filter-overlay_gbrap_gbrap
$(FATE_FILTER_OVERLAY_ALPHA): SRC = $(TARGET_SAMPLES)/png1/lena-rgba.png
$(FATE_FILTER_OVERLAY_ALPHA): CMD = framecrc -i $(SRC) -sws_flags +accurate_rnd+bitexact -vf $(FILTER) -frames:v 1

fate-filter-overlay_yuv420_yuva420:  FILTER = "format=yuva420p[over];color=black:128x128,format=yuv420p[main];[main][over]overlay=format=yuv420"
fate-filter-overlay_yuv422_yuva422:  FILTER = "format=yuva422p[over];color=black:128x128,format=yuv422p[main];[main][over]overlay=format=yuv422"
fate-filter-overlay_yuv444_yuva444:  FILTER = "format=yuva444p[over];color=black:128x128,format=yuv444p[main];[main][over]overlay=format=yuv444"
fate-filter-overlay_rgb_rgba:        FILTER = "format=rgba[over];color=black:128x128,format=rgb24[main];[main][over]overlay=format=rgb"
fate-filter-overlay_gbrp_gbrap:      FILTER = "format=gbrap[over];color=black:128x128,format=gbrp[main];[main][over]overlay=format=gbrp"

fate-filter-overlay_yuva420_yuva420: FILTER = "format=yuva420p[over];color=black:128x128,format=yuva420p[main];[main][over]overlay=format=yuv420"
fate-filter-overlay_yuva422_yuva422: FILTER = "format=yuva422p[over];color=black:128x128,format=yuva422p[main];[main][over]overlay=format=yuv422"
fate-filter-overlay_yuva444_yuva444: FILTER = "format=yuva444p[over];color=black:128x128,format=yuva444p[main];[main][over]overlay=format=yuv444"
fate-filter-overlay_rgba_rgba:       FILTER = "format=rgba[over];color=black:128x128,format=rgba[main];[main][over]overlay=format=rgb"
fate-filter-overlay_gbrap_gbrap:     FILTER = "format=gbrap[over];color=black:128x128,format=gbrap[main];[main][over]overlay=format=gbrp"

FATE_FILTER_SAMPLES-$(call ALLYES, PNG_DECODER APNG_DEMUXER FORMAT_FILTER COLOR_FILTER OVERLAY_FILTER) += $(FATE_FILTER_OVERLAY_ALPHA)

FATE_FILTER_VSYNTH-$(CONFIG_PHASE_FILTER) += fate-filter-phase
fate-filter-phase: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf phase

FATE_REMOVEGRAIN += fate-filter-removegrain-mode-00
fate-filter-removegrain-mode-00: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 1 -vf removegrain=0:0:0

FATE_REMOVEGRAIN += fate-filter-removegrain-mode-01
fate-filter-removegrain-mode-01: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 1 -vf removegrain=1:1:1

FATE_REMOVEGRAIN += fate-filter-removegrain-mode-02
fate-filter-removegrain-mode-02: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 1 -vf removegrain=2:2:2

FATE_REMOVEGRAIN += fate-filter-removegrain-mode-03
fate-filter-removegrain-mode-03: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 1 -vf removegrain=3:3:3

FATE_REMOVEGRAIN += fate-filter-removegrain-mode-04
fate-filter-removegrain-mode-04: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 1 -vf removegrain=4:4:4

FATE_REMOVEGRAIN += fate-filter-removegrain-mode-05
fate-filter-removegrain-mode-05: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 1 -vf removegrain=5:5:5

FATE_REMOVEGRAIN += fate-filter-removegrain-mode-06
fate-filter-removegrain-mode-06: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 1 -vf removegrain=6:6:6

FATE_REMOVEGRAIN += fate-filter-removegrain-mode-07
fate-filter-removegrain-mode-07: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 1 -vf removegrain=7:7:7

FATE_REMOVEGRAIN += fate-filter-removegrain-mode-08
fate-filter-removegrain-mode-08: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 1 -vf removegrain=8:8:8

FATE_REMOVEGRAIN += fate-filter-removegrain-mode-09
fate-filter-removegrain-mode-09: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 1 -vf removegrain=9:9:9

FATE_REMOVEGRAIN += fate-filter-removegrain-mode-10
fate-filter-removegrain-mode-10: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 1 -vf removegrain=10:10:10

FATE_REMOVEGRAIN += fate-filter-removegrain-mode-11
fate-filter-removegrain-mode-11: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 1 -vf removegrain=11:11:11

FATE_REMOVEGRAIN += fate-filter-removegrain-mode-12
fate-filter-removegrain-mode-12: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 1 -vf removegrain=12:12:12

FATE_REMOVEGRAIN += fate-filter-removegrain-mode-13
fate-filter-removegrain-mode-13: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 1 -vf removegrain=13:13:13

FATE_REMOVEGRAIN += fate-filter-removegrain-mode-14
fate-filter-removegrain-mode-14: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 1 -vf removegrain=14:14:14

FATE_REMOVEGRAIN += fate-filter-removegrain-mode-15
fate-filter-removegrain-mode-15: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 1 -vf removegrain=15:15:15

FATE_REMOVEGRAIN += fate-filter-removegrain-mode-16
fate-filter-removegrain-mode-16: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 1 -vf removegrain=16:16:16

FATE_REMOVEGRAIN += fate-filter-removegrain-mode-17
fate-filter-removegrain-mode-17: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 1 -vf removegrain=17:17:17

FATE_REMOVEGRAIN += fate-filter-removegrain-mode-18
fate-filter-removegrain-mode-18: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 1 -vf removegrain=18:18:18

FATE_REMOVEGRAIN += fate-filter-removegrain-mode-19
fate-filter-removegrain-mode-19: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 1 -vf removegrain=19:19:19

FATE_REMOVEGRAIN += fate-filter-removegrain-mode-20
fate-filter-removegrain-mode-20: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 1 -vf removegrain=20:20:20

FATE_REMOVEGRAIN += fate-filter-removegrain-mode-21
fate-filter-removegrain-mode-21: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 1 -vf removegrain=21:21:21

FATE_REMOVEGRAIN += fate-filter-removegrain-mode-22
fate-filter-removegrain-mode-22: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 1 -vf removegrain=22:22:22

FATE_REMOVEGRAIN += fate-filter-removegrain-mode-23
fate-filter-removegrain-mode-23: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 1 -vf removegrain=23:23:23

FATE_REMOVEGRAIN += fate-filter-removegrain-mode-24
fate-filter-removegrain-mode-24: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 1 -vf removegrain=24:24:24

fate-filter-removegrain: $(FATE_REMOVEGRAIN)
FATE_FILTER_VSYNTH-$(CONFIG_REMOVEGRAIN_FILTER) += $(FATE_REMOVEGRAIN)

FATE_FILTER_VSYNTH-$(CONFIG_SEPARATEFIELDS_FILTER) += fate-filter-separatefields
fate-filter-separatefields: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf separatefields

FATE_FILTER_VSYNTH-$(CONFIG_WEAVE_FILTER) += fate-filter-weave
fate-filter-weave: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf weave=bottom

FATE_FILTER_VSYNTH-$(CONFIG_SELECT_FILTER) += fate-filter-select-alternate
fate-filter-select-alternate: tests/data/filtergraphs/select-alternate
fate-filter-select-alternate: CMD = framecrc -c:v pgmyuv -i $(SRC) -filter_script $(TARGET_PATH)/tests/data/filtergraphs/select-alternate

FATE_FILTER_VSYNTH-$(call ALLYES, SETPTS_FILTER  SETTB_FILTER) += fate-filter-setpts
fate-filter-setpts: tests/data/filtergraphs/setpts
fate-filter-setpts: CMD = framecrc -c:v pgmyuv -i $(SRC) -filter_script $(TARGET_PATH)/tests/data/filtergraphs/setpts

FATE_SHUFFLEFRAMES += fate-filter-shuffleframes
fate-filter-shuffleframes: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf shuffleframes="2|1|0"

FATE_FILTER_VSYNTH-$(CONFIG_SHUFFLEFRAMES_FILTER) += $(FATE_SHUFFLEFRAMES)

FATE_SHUFFLEPLANES += fate-filter-shuffleplanes-dup-luma
fate-filter-shuffleplanes-dup-luma: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf format=yuva444p,shuffleplanes=0:0:0:0

FATE_SHUFFLEPLANES += fate-filter-shuffleplanes-swapuv
fate-filter-shuffleplanes-swapuv: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf shuffleplanes=0:2:1:0

FATE_FILTER_VSYNTH-$(CONFIG_SHUFFLEPLANES_FILTER) += $(FATE_SHUFFLEPLANES)

FATE_SWAPRECT += fate-filter-swaprect
fate-filter-swaprect: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf swaprect

FATE_FILTER_VSYNTH-$(CONFIG_SWAPRECT_FILTER) += $(FATE_SWAPRECT)

FATE_FILTER_VSYNTH-$(CONFIG_TBLEND_FILTER) += fate-filter-tblend
fate-filter-tblend: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf tblend=all_mode=difference128

FATE_FILTER_VSYNTH-$(CONFIG_TELECINE_FILTER) += fate-filter-telecine
fate-filter-telecine: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf telecine

FATE_FILTER_VSYNTH-$(CONFIG_TRANSPOSE_FILTER) += fate-filter-transpose
fate-filter-transpose: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf transpose

FATE_TRIM += fate-filter-trim-duration
fate-filter-trim-duration: CMD = framecrc -i $(SRC) -vf trim=start=0.4:duration=0.05

FATE_TRIM += fate-filter-trim-frame
fate-filter-trim-frame: CMD = framecrc -i $(SRC) -vf trim=start_frame=3:end_frame=10

FATE_TRIM += fate-filter-trim-mixed
fate-filter-trim-mixed: CMD = framecrc -i $(SRC) -vf trim=start=0.2:end=0.4:start_frame=1:end_frame=3

FATE_TRIM += fate-filter-trim-time
fate-filter-trim-time: CMD = framecrc -i $(SRC) -vf trim=0:0.09

FATE_FILTER_VSYNTH-$(CONFIG_TRIM_FILTER) += $(FATE_TRIM)

FATE_FILTER_VSYNTH-$(CONFIG_UNSHARP_FILTER) += fate-filter-unsharp
fate-filter-unsharp: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf unsharp=11:11:-1.5:11:11:-1.5

FATE_FILTER_SAMPLES-$(call ALLYES, SMJPEG_DEMUXER MJPEG_DECODER PERMS_FILTER HQDN3D_FILTER) += fate-filter-hqdn3d-sample
fate-filter-hqdn3d-sample: tests/data/filtergraphs/hqdn3d
fate-filter-hqdn3d-sample: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/smjpeg/scenwin.mjpg -filter_complex_script $(TARGET_PATH)/tests/data/filtergraphs/hqdn3d -an

FATE_FILTER_SAMPLES-$(call ALLYES, MATROSKA_DEMUXER OVERLAY_FILTER H264_DECODER DVDSUB_DECODER) += fate-filter-overlay-dvdsub-2397
fate-filter-overlay-dvdsub-2397: tests/data/filtergraphs/overlay-dvdsub-2397
fate-filter-overlay-dvdsub-2397: CMD = framecrc -flags bitexact -i $(TARGET_SAMPLES)/filter/242_4.mkv -filter_complex_script $(TARGET_PATH)/tests/data/filtergraphs/overlay-dvdsub-2397 -c:a copy

FATE_FILTER_HQX-$(call ALLYES, IMAGE2_DEMUXER PNG_DECODER HQX_FILTER) = fate-filter-hq2x fate-filter-hq3x fate-filter-hq4x
FATE_FILTER_SAMPLES-yes += $(FATE_FILTER_HQX-yes)
fate-filter-hq2x: CMD = framecrc -i $(TARGET_SAMPLES)/filter/pixelart%d.png -vf hqx=2 -pix_fmt bgra
fate-filter-hq3x: CMD = framecrc -i $(TARGET_SAMPLES)/filter/pixelart%d.png -vf hqx=3 -pix_fmt bgra
fate-filter-hq4x: CMD = framecrc -i $(TARGET_SAMPLES)/filter/pixelart%d.png -vf hqx=4 -pix_fmt bgra
fate-filter-hqx: $(FATE_FILTER_HQX-yes)

FATE_FILTER_XBR-$(call ALLYES, IMAGE2_DEMUXER PNG_DECODER XBR_FILTER) = fate-filter-2xbr fate-filter-3xbr fate-filter-4xbr
FATE_FILTER_SAMPLES-yes += $(FATE_FILTER_XBR-yes)
fate-filter-2xbr: CMD = framecrc -i $(TARGET_SAMPLES)/filter/pixelart%d.png -vf xbr=2 -pix_fmt bgra
fate-filter-3xbr: CMD = framecrc -i $(TARGET_SAMPLES)/filter/pixelart%d.png -vf xbr=3 -pix_fmt bgra
fate-filter-4xbr: CMD = framecrc -i $(TARGET_SAMPLES)/filter/pixelart%d.png -vf xbr=4 -pix_fmt bgra
fate-filter-xbr: $(FATE_FILTER_XBR-yes)

FATE_FILTER_SAMPLES-$(call ALLYES, UTVIDEO_DECODER AVI_DEMUXER PERMS_FILTER CURVES_FILTER) += fate-filter-curves
fate-filter-curves: CMD = framecrc -i $(TARGET_SAMPLES)/utvideo/utvideo_rgb_median.avi -vf perms=random,curves=vintage

FATE_FILTER_SAMPLES-$(call ALLYES, VMD_DEMUXER VMDVIDEO_DECODER FORMAT_FILTER PERMS_FILTER GRADFUN_FILTER) += fate-filter-gradfun-sample
fate-filter-gradfun-sample: tests/data/filtergraphs/gradfun
fate-filter-gradfun-sample: CMD = framecrc -i $(TARGET_SAMPLES)/vmd/12.vmd -filter_script $(TARGET_PATH)/tests/data/filtergraphs/gradfun -an -frames:v 20

FATE_FILTER-$(call ALLYES, TESTSRC_FILTER SINE_FILTER CONCAT_FILTER) += fate-filter-concat fate-filter-concat-vfr
fate-filter-concat: tests/data/filtergraphs/concat
fate-filter-concat: CMD = framecrc -filter_complex_script $(TARGET_PATH)/tests/data/filtergraphs/concat
fate-filter-concat-vfr: tests/data/filtergraphs/concat-vfr
fate-filter-concat-vfr: CMD = framecrc -filter_complex_script $(TARGET_PATH)/tests/data/filtergraphs/concat-vfr

FATE_FILTER-$(call ALLYES, TESTSRC2_FILTER FPS_FILTER MPDECIMATE_FILTER) += fate-filter-mpdecimate
fate-filter-mpdecimate: CMD = framecrc -lavfi testsrc2=r=2:d=10,fps=3,mpdecimate -r 3 -pix_fmt yuv420p

FATE_FILTER-$(call ALLYES, FPS_FILTER TESTSRC2_FILTER) += fate-filter-fps-up fate-filter-fps-up-round-down fate-filter-fps-up-round-up fate-filter-fps-down fate-filter-fps-down-round-down fate-filter-fps-down-round-up fate-filter-fps-down-eof-pass fate-filter-fps-start-drop fate-filter-fps-start-fill
fate-filter-fps-up: CMD = framecrc -lavfi testsrc2=r=3:d=2,fps=7
fate-filter-fps-up-round-down: CMD = framecrc -lavfi testsrc2=r=3:d=2,fps=7:round=down
fate-filter-fps-up-round-up: CMD = framecrc -lavfi testsrc2=r=3:d=2,fps=7:round=up
fate-filter-fps-down: CMD = framecrc -lavfi testsrc2=r=7:d=3.5,fps=3
fate-filter-fps-down-round-down: CMD = framecrc -lavfi testsrc2=r=7:d=3.5,fps=3:round=down
fate-filter-fps-down-round-up: CMD = framecrc -lavfi testsrc2=r=7:d=3.5,fps=3:round=up
fate-filter-fps-down-eof-pass: CMD = framecrc -lavfi testsrc2=r=7:d=3.5,fps=3:eof_action=pass
fate-filter-fps-start-drop: CMD = framecrc -lavfi testsrc2=r=7:d=3.5,fps=3:start_time=1.5
fate-filter-fps-start-fill: CMD = framecrc -lavfi testsrc2=r=7:d=1.5,setpts=PTS+14,fps=3:start_time=1.5

FATE_FILTER_SAMPLES-$(call ALLYES, MOV_DEMUXER FPS_FILTER QTRLE_DECODER) += fate-filter-fps-cfr fate-filter-fps fate-filter-fps-r
fate-filter-fps-cfr: CMD = framecrc -i $(TARGET_SAMPLES)/qtrle/apple-animation-variable-fps-bug.mov -r 30 -vsync cfr -pix_fmt yuv420p
fate-filter-fps-r:   CMD = framecrc -i $(TARGET_SAMPLES)/qtrle/apple-animation-variable-fps-bug.mov -r 30 -vf fps -pix_fmt yuv420p
fate-filter-fps:     CMD = framecrc -i $(TARGET_SAMPLES)/qtrle/apple-animation-variable-fps-bug.mov -vf fps=30 -pix_fmt yuv420p

FATE_FILTER_VSYNTH-$(call ALLYES, FORMAT_FILTER SPLIT_FILTER ALPHAEXTRACT_FILTER ALPHAMERGE_FILTER) += fate-filter-alphaextract_alphamerge_rgb
fate-filter-alphaextract_alphamerge_rgb: tests/data/filtergraphs/alphamerge_alphaextract_rgb
fate-filter-alphaextract_alphamerge_rgb: CMD = framecrc -c:v pgmyuv -i $(SRC) -filter_complex_script $(TARGET_PATH)/tests/data/filtergraphs/alphamerge_alphaextract_rgb

FATE_FILTER_VSYNTH-$(call ALLYES, FORMAT_FILTER SPLIT_FILTER ALPHAEXTRACT_FILTER ALPHAMERGE_FILTER) += fate-filter-alphaextract_alphamerge_yuv
fate-filter-alphaextract_alphamerge_yuv: tests/data/filtergraphs/alphamerge_alphaextract_yuv
fate-filter-alphaextract_alphamerge_yuv: CMD = framecrc -c:v pgmyuv -i $(SRC) -filter_complex_script $(TARGET_PATH)/tests/data/filtergraphs/alphamerge_alphaextract_yuv

FATE_FILTER_VSYNTH-$(CONFIG_CROP_FILTER) += fate-filter-crop
fate-filter-crop: CMD = video_filter "crop=iw-100:ih-100:100:100"

FATE_FILTER_VSYNTH-$(call ALLYES, CROP_FILTER SCALE_FILTER) += fate-filter-crop_scale
fate-filter-crop_scale: CMD = video_filter "crop=iw-100:ih-100:100:100,scale=w=400:h=-1"

FATE_FILTER_VSYNTH-$(call ALLYES, CROP_FILTER SCALE_FILTER VFLIP_FILTER) += fate-filter-crop_scale_vflip
fate-filter-crop_scale_vflip: CMD = video_filter "null,null,crop=iw-200:ih-200:200:200,crop=iw-20:ih-20:20:20,scale=w=200:h=200,scale=w=250:h=250,vflip,vflip,null,scale=w=200:h=200,crop=iw-100:ih-100:100:100,vflip,scale=w=200:h=200,null,vflip,crop=iw-100:ih-100:100:100,null"

FATE_FILTER_VSYNTH-$(call ALLYES, CROP_FILTER VFLIP_FILTER) += fate-filter-crop_vflip
fate-filter-crop_vflip: CMD = video_filter "crop=iw-100:ih-100:100:100,vflip"

FATE_FILTER_VSYNTH-$(CONFIG_NULL_FILTER) += fate-filter-null
fate-filter-null: CMD = video_filter "null"

FATE_FILTER_VSYNTH-$(CONFIG_SCALE_FILTER) += fate-filter-scale200
fate-filter-scale200: CMD = video_filter "scale=w=200:h=200"

FATE_FILTER_VSYNTH-$(CONFIG_SCALE_FILTER) += fate-filter-scale500
fate-filter-scale500: CMD = video_filter "scale=w=500:h=500"

FATE_FILTER_VSYNTH-$(CONFIG_SCALE2REF_FILTER) += fate-filter-scale2ref_keep_aspect
fate-filter-scale2ref_keep_aspect: tests/data/filtergraphs/scale2ref_keep_aspect
fate-filter-scale2ref_keep_aspect: CMD = framemd5 -frames:v 5 -filter_complex_script $(TARGET_PATH)/tests/data/filtergraphs/scale2ref_keep_aspect -map "[main]"

FATE_FILTER_VSYNTH-$(CONFIG_SCALE_FILTER) += fate-filter-scalechroma
fate-filter-scalechroma: tests/data/vsynth1.yuv
fate-filter-scalechroma: CMD = framecrc -flags bitexact -s 352x288 -pix_fmt yuv444p -i $(TARGET_PATH)/tests/data/vsynth1.yuv -pix_fmt yuv420p -sws_flags +bitexact -vf scale=out_v_chr_pos=33:out_h_chr_pos=151

FATE_FILTER_VSYNTH-$(CONFIG_VFLIP_FILTER) += fate-filter-vflip
fate-filter-vflip: CMD = video_filter "vflip"

FATE_FILTER_VSYNTH-$(CONFIG_COLORLEVELS_FILTER) += fate-filter-colorlevels
fate-filter-colorlevels: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf format=rgb24,colorlevels -flags +bitexact -sws_flags +accurate_rnd+bitexact

FATE_FILTER_VSYNTH-$(CONFIG_COLORLEVELS_FILTER) += fate-filter-colorlevels-16
fate-filter-colorlevels-16: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf format=rgb48,colorlevels -pix_fmt rgb48le -flags +bitexact -sws_flags +accurate_rnd+bitexact

FATE_FILTER_VSYNTH-$(CONFIG_COLORMATRIX_FILTER) += fate-filter-colormatrix1
fate-filter-colormatrix1: CMD = video_filter "colormatrix=bt601:smpte240m,colormatrix=smpte240m:fcc,colormatrix=fcc:bt601,colormatrix=bt601:fcc,colormatrix=fcc:smpte240m,colormatrix=smpte240m:bt709"

FATE_FILTER_VSYNTH-$(CONFIG_COLORMATRIX_FILTER) += fate-filter-colormatrix2
fate-filter-colormatrix2: CMD = video_filter "colormatrix=bt709:fcc,colormatrix=fcc:bt709,colormatrix=bt709:bt601,colormatrix=bt601:bt709,colormatrix=bt709:smpte240m,colormatrix=smpte240m:bt601"

FATE_FILTER_VSYNTH-$(call ALLYES, CROP_FILTER VFLIP_FILTER) += fate-filter-vflip_crop
fate-filter-vflip_crop: CMD = video_filter "vflip,crop=iw-100:ih-100:100:100"

FATE_FILTER_VSYNTH-$(CONFIG_VFLIP_FILTER) += fate-filter-vflip_vflip
fate-filter-vflip_vflip: CMD = video_filter "vflip,vflip"

FATE_FILTER_VSYNTH-$(call ALLYES, FORMAT_FILTER PERMS_FILTER EDGEDETECT_FILTER) += fate-filter-edgedetect
fate-filter-edgedetect: CMD = video_filter "format=gray,perms=random,edgedetect" -frames:v 20

FATE_FILTER_VSYNTH-$(call ALLYES, FORMAT_FILTER PERMS_FILTER EDGEDETECT_FILTER) += fate-filter-edgedetect-colormix
fate-filter-edgedetect-colormix: CMD = video_filter "format=gbrp,perms=random,edgedetect=mode=colormix" -frames:v 20

FATE_FILTER_VSYNTH-$(call ALLYES, PERMS_FILTER HUE_FILTER) += fate-filter-hue1
fate-filter-hue1: CMD = video_filter "perms=random,hue=s=sin(2*PI*t)+1" -frames:v 20

FATE_FILTER_VSYNTH-$(call ALLYES, PERMS_FILTER HUE_FILTER) += fate-filter-hue2
fate-filter-hue2: CMD = video_filter "perms=random,hue=h=18*n" -frames:v 20

FATE_FILTER_VSYNTH-$(call ALLYES, PERMS_FILTER HUE_FILTER) += fate-filter-hue3
fate-filter-hue3: CMD = video_filter "perms=random,hue=b=n-10" -frames:v 20

FATE_FILTER_VSYNTH-$(call ALLYES, FORMAT_FILTER PERMS_FILTER HUE_FILTER) += fate-filter-hue4
fate-filter-hue4: CMD = video_filter "format=yuv422p10,perms=random,hue=h=18*n:s=n/10" -frames:v 20 -pix_fmt yuv422p10le

FATE_FILTER_VSYNTH-$(CONFIG_IDET_FILTER) += fate-filter-idet
fate-filter-idet: CMD = framecrc -flags bitexact -idct simple -i $(SRC) -vf idet -frames:v 25 -flags +bitexact

FATE_FILTER_VSYNTH-$(CONFIG_PAD_FILTER) += fate-filter-pad
fate-filter-pad: CMD = video_filter "pad=iw*1.5:ih*1.5:iw*0.3:ih*0.2"

FATE_FILTER_PP = fate-filter-pp fate-filter-pp1 fate-filter-pp2 fate-filter-pp3 fate-filter-pp4 fate-filter-pp5 fate-filter-pp6
FATE_FILTER_VSYNTH-$(CONFIG_PP_FILTER) += $(FATE_FILTER_PP)
$(FATE_FILTER_PP): fate-vsynth1-mpeg4-qprd

fate-filter-pp:  CMD = framecrc -flags bitexact -idct simple -i $(TARGET_PATH)/tests/data/fate/vsynth1-mpeg4-qprd.avi -frames:v 5 -flags +bitexact -vf "pp=be/hb/vb/tn/l5/al"
fate-filter-pp1: CMD = video_filter "pp=fq|4/be/hb/vb/tn/l5/al"
fate-filter-pp2: CMD = video_filter "qp=x+y,pp=be/h1/v1/lb"
fate-filter-pp3: CMD = video_filter "qp=x+y,pp=be/ha|128|7/va/li"
fate-filter-pp4: CMD = video_filter "pp=be/ci"
fate-filter-pp5: CMD = video_filter "pp=md"
fate-filter-pp6: CMD = video_filter "pp=be/fd"

FATE_FILTER_VSYNTH-$(call ALLYES, QP_FILTER PP_FILTER) += fate-filter-qp
fate-filter-qp: CMD = video_filter "qp=17,pp=be/hb/vb/tn/l5/al"

FATE_FILTER_VSYNTH-$(CONFIG_SELECT_FILTER) += fate-filter-select
fate-filter-select: CMD = framecrc -flags bitexact -idct simple -i $(SRC) -vf "select=not(eq(mod(n\,2)\,0)+eq(mod(n\,3)\,0))" -frames:v 25 -flags +bitexact

FATE_FILTER_VSYNTH-$(CONFIG_SETDAR_FILTER) += fate-filter-setdar
fate-filter-setdar: CMD = video_filter "setdar=dar=16/9"

FATE_FILTER_VSYNTH-$(CONFIG_SETSAR_FILTER) += fate-filter-setsar
fate-filter-setsar: CMD = video_filter "setsar=sar=16/11"

FATE_STEREO3D += fate-filter-stereo3d-al-sbsl
fate-filter-stereo3d-al-sbsl: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 5 -flags +bitexact -sws_flags +accurate_rnd+bitexact -vf stereo3d=al:sbsl

FATE_STEREO3D += fate-filter-stereo3d-ar-abl
fate-filter-stereo3d-ar-abl: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 5 -flags +bitexact -sws_flags +accurate_rnd+bitexact -vf stereo3d=ar:abl

FATE_STEREO3D += fate-filter-stereo3d-abr-mr
fate-filter-stereo3d-abr-mr: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 5 -flags +bitexact -sws_flags +accurate_rnd+bitexact -vf stereo3d=abr:mr

FATE_STEREO3D += fate-filter-stereo3d-abr-ml
fate-filter-stereo3d-abr-ml: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 5 -flags +bitexact -sws_flags +accurate_rnd+bitexact -vf stereo3d=abr:ml

FATE_STEREO3D  += fate-filter-stereo3d-sbsl-abl
fate-filter-stereo3d-sbsl-abl: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 5 -flags +bitexact -sws_flags +accurate_rnd+bitexact -vf stereo3d=sbsl:abl

FATE_STEREO3D += fate-filter-stereo3d-sbsl-abr
fate-filter-stereo3d-sbsl-abr: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 5 -flags +bitexact -sws_flags +accurate_rnd+bitexact -vf stereo3d=sbsl:abr

FATE_STEREO3D += fate-filter-stereo3d-sbsl-al
fate-filter-stereo3d-sbsl-al: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 5 -flags +bitexact -sws_flags +accurate_rnd+bitexact -vf stereo3d=sbsl:al

FATE_STEREO3D += fate-filter-stereo3d-sbsl-sbsr
fate-filter-stereo3d-sbsl-sbsr: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 5 -flags +bitexact -sws_flags +accurate_rnd+bitexact -vf stereo3d=sbsl:sbsr

FATE_STEREO3D += fate-filter-stereo3d-sbsl-agmc
fate-filter-stereo3d-sbsl-agmc: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 5 -flags +bitexact -sws_flags +accurate_rnd+bitexact -vf stereo3d=sbsl:agmc

FATE_STEREO3D += fate-filter-stereo3d-sbsl-agmd
fate-filter-stereo3d-sbsl-agmd: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 5 -flags +bitexact -sws_flags +accurate_rnd+bitexact -vf stereo3d=sbsl:agmd

FATE_STEREO3D += fate-filter-stereo3d-sbsl-agmg
fate-filter-stereo3d-sbsl-agmg: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 5 -flags +bitexact -sws_flags +accurate_rnd+bitexact -vf stereo3d=sbsl:agmg

FATE_STEREO3D += fate-filter-stereo3d-sbsl-agmh
fate-filter-stereo3d-sbsl-agmh: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 5 -flags +bitexact -sws_flags +accurate_rnd+bitexact -vf stereo3d=sbsl:agmh

FATE_STEREO3D += fate-filter-stereo3d-sbsl-arbg
fate-filter-stereo3d-sbsl-arbg: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 5 -flags +bitexact -sws_flags +accurate_rnd+bitexact -vf stereo3d=sbsl:arbg

FATE_STEREO3D += fate-filter-stereo3d-sbsl-arcc
fate-filter-stereo3d-sbsl-arcc: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 5 -flags +bitexact -sws_flags +accurate_rnd+bitexact -vf stereo3d=sbsl:arcc

FATE_STEREO3D += fate-filter-stereo3d-sbsl-arcd
fate-filter-stereo3d-sbsl-arcd: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 5 -flags +bitexact -sws_flags +accurate_rnd+bitexact -vf stereo3d=sbsl:arcd

FATE_STEREO3D += fate-filter-stereo3d-sbsl-arcg
fate-filter-stereo3d-sbsl-arcg: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 5 -flags +bitexact -sws_flags +accurate_rnd+bitexact -vf stereo3d=sbsl:arcg

FATE_STEREO3D += fate-filter-stereo3d-sbsl-arch
fate-filter-stereo3d-sbsl-arch: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 5 -flags +bitexact -sws_flags +accurate_rnd+bitexact -vf stereo3d=sbsl:arch

FATE_STEREO3D += fate-filter-stereo3d-sbsl-argg
fate-filter-stereo3d-sbsl-argg: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 5 -flags +bitexact -sws_flags +accurate_rnd+bitexact -vf stereo3d=sbsl:argg

FATE_STEREO3D += fate-filter-stereo3d-sbsl-aybc
fate-filter-stereo3d-sbsl-aybc: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 5 -flags +bitexact -sws_flags +accurate_rnd+bitexact -vf stereo3d=sbsl:aybc

FATE_STEREO3D += fate-filter-stereo3d-sbsl-aybd
fate-filter-stereo3d-sbsl-aybd: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 5 -flags +bitexact -sws_flags +accurate_rnd+bitexact -vf stereo3d=sbsl:aybd

FATE_STEREO3D += fate-filter-stereo3d-sbsl-aybg
fate-filter-stereo3d-sbsl-aybg: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 5 -flags +bitexact -sws_flags +accurate_rnd+bitexact -vf stereo3d=sbsl:aybg

FATE_STEREO3D += fate-filter-stereo3d-sbsl-aybh
fate-filter-stereo3d-sbsl-aybh: CMD = framecrc -c:v pgmyuv -i $(SRC) -frames:v 5 -flags +bitexact -sws_flags +accurate_rnd+bitexact -vf stereo3d=sbsl:aybh

fate-filter-stereo3d: $(FATE_STEREO3D)
FATE_FILTER_VSYNTH-$(CONFIG_STEREO3D_FILTER) += $(FATE_STEREO3D)

FATE_FILTER_VSYNTH-$(CONFIG_THUMBNAIL_FILTER) += fate-filter-thumbnail
fate-filter-thumbnail: CMD = video_filter "thumbnail=10"

FATE_FILTER_VSYNTH-$(CONFIG_TILE_FILTER) += fate-filter-tile
fate-filter-tile: CMD = video_filter "tile=3x3:nb_frames=5:padding=7:margin=2"


tests/pixfmts.mak: TAG = GEN
tests/pixfmts.mak: ffmpeg$(PROGSSUF)$(EXESUF) | tests
	$(M)printf "PIXFMTS = " > $@
	$(Q)$(TARGET_EXEC) $(TARGET_PATH)/$< -pix_fmts list 2> /dev/null | awk 'NR > 8 && /^IO/ { printf $$2 " " }' >> $@
	$(Q)printf "\n" >> $@

RUNNING_PIXFMTS_TESTS := $(filter check fate fate-list fate-filter fate-vfilter fate-filter-pixdesc%,$(MAKECMDGOALS))

ifneq (,$(RUNNING_PIXFMTS_TESTS))
-include tests/pixfmts.mak
endif

define PIXDESC_TEST
FATE_FILTER_PIXDESC-$(CONFIG_FORMAT_FILTER) += fate-filter-pixdesc-$(1)
fate-filter-pixdesc-$(1): CMD = video_filter "format=$(1),pixdesctest" -pix_fmt $(1)
endef

$(foreach fmt, $(PIXFMTS), $(eval $(call PIXDESC_TEST,$(fmt))))

fate-filter-pixdesc: $(FATE_FILTER_PIXDESC-yes)
FATE_FILTER_VSYNTH-yes += $(FATE_FILTER_PIXDESC-yes)


FATE_FILTER_PIXFMTS-$(CONFIG_COPY_FILTER) += fate-filter-pixfmts-copy
fate-filter-pixfmts-copy:  CMD = pixfmts

FATE_FILTER_PIXFMTS-$(CONFIG_CROP_FILTER) += fate-filter-pixfmts-crop
fate-filter-pixfmts-crop:  CMD = pixfmts "100:100:100:100"

FATE_FILTER_PIXFMTS-$(CONFIG_FIELD_FILTER) += fate-filter-pixfmts-field
fate-filter-pixfmts-field: CMD = pixfmts "bottom"

FATE_FILTER_PIXFMTS-$(call ALLYES, TELECINE_FILTER FIELDMATCH_FILTER) += fate-filter-pixfmts-fieldmatch
fate-filter-pixfmts-fieldmatch: CMD = pixfmts "" "telecine," 25

FATE_FILTER_PIXFMTS-$(CONFIG_FIELDORDER_FILTER) += fate-filter-pixfmts-fieldorder
fate-filter-pixfmts-fieldorder: CMD = pixfmts "tff" "setfield=bff,"

FATE_FILTER_PIXFMTS-$(CONFIG_HFLIP_FILTER) += fate-filter-pixfmts-hflip
fate-filter-pixfmts-hflip: CMD = pixfmts

#FATE_FILTER_PIXFMTS-$(CONFIG_HISTEQ_FILTER) += fate-filter-pixfmts-histeq
#fate-filter-pixfmts-histeq: CMD = pixfmts "antibanding=strong"

FATE_FILTER_PIXFMTS-$(CONFIG_IL_FILTER) += fate-filter-pixfmts-il
fate-filter-pixfmts-il:    CMD = pixfmts "luma_mode=d:chroma_mode=d:alpha_mode=d"

FATE_FILTER_PIXFMTS-$(CONFIG_KERNDEINT_FILTER) += fate-filter-pixfmts-kerndeint
fate-filter-pixfmts-kerndeint: CMD = pixfmts "" "tinterlace=interleave_top,"

FATE_FILTER_PIXFMTS-$(CONFIG_LUT_FILTER) += fate-filter-pixfmts-lut
fate-filter-pixfmts-lut: CMD = pixfmts "c0=2*val:c1=2*val:c2=val/2:c3=negval+40"

FATE_FILTER_PIXFMTS-$(CONFIG_NULL_FILTER) += fate-filter-pixfmts-null
fate-filter-pixfmts-null:  CMD = pixfmts

FATE_FILTER_PIXFMTS-$(CONFIG_PAD_FILTER) += fate-filter-pixfmts-pad
fate-filter-pixfmts-pad:   CMD = pixfmts "500:400:20:20"

FATE_FILTER_PIXFMTS-$(call ALLYES, TELECINE_FILTER PULLUP_FILTER) += fate-filter-pixfmts-pullup
fate-filter-pixfmts-pullup: CMD = pixfmts "" "telecine," 25

FATE_FILTER_PIXFMTS-$(CONFIG_ROTATE_FILTER) += fate-filter-pixfmts-rotate
fate-filter-pixfmts-rotate: CMD = pixfmts "2*PI*n/50"

FATE_FILTER_PIXFMTS-$(CONFIG_SCALE_FILTER) += fate-filter-pixfmts-scale
fate-filter-pixfmts-scale: CMD = pixfmts "200:100"

FATE_FILTER_PIXFMTS-$(CONFIG_SUPER2XSAI_FILTER) += fate-filter-pixfmts-super2xsai
fate-filter-pixfmts-super2xsai: CMD = pixfmts

FATE_FILTER_PIXFMTS-$(CONFIG_SWAPUV_FILTER) += fate-filter-pixfmts-swapuv
fate-filter-pixfmts-swapuv: CMD = pixfmts

FATE_FILTER_PIXFMTS-$(CONFIG_TINTERLACE_FILTER) += fate-filter-pixfmts-tinterlace_cvlpf
fate-filter-pixfmts-tinterlace_cvlpf: CMD = pixfmts "interleave_top:cvlpf"

FATE_FILTER_PIXFMTS-$(CONFIG_TINTERLACE_FILTER) += fate-filter-pixfmts-tinterlace_merge
fate-filter-pixfmts-tinterlace_merge: CMD = pixfmts "merge"

FATE_FILTER_PIXFMTS-$(CONFIG_TINTERLACE_FILTER) += fate-filter-pixfmts-tinterlace_pad
fate-filter-pixfmts-tinterlace_pad: CMD = pixfmts "pad"

FATE_FILTER_PIXFMTS-$(CONFIG_TINTERLACE_FILTER) += fate-filter-pixfmts-tinterlace_vlpf
fate-filter-pixfmts-tinterlace_vlpf: CMD = pixfmts "interleave_top:vlpf"

FATE_FILTER_PIXFMTS-$(CONFIG_TRANSPOSE_FILTER) += fate-filter-pixfmts-transpose
fate-filter-pixfmts-transpose: CMD = pixfmts "dir=cclock_flip"

FATE_FILTER_PIXFMTS-$(CONFIG_VFLIP_FILTER) += fate-filter-pixfmts-vflip
fate-filter-pixfmts-vflip: CMD = pixfmts

$(FATE_FILTER_PIXFMTS-yes): libavfilter/tests/filtfmts$(EXESUF)
FATE_FILTER_VSYNTH-$(CONFIG_FORMAT_FILTER) += $(FATE_FILTER_PIXFMTS-yes)

fate-filter-pixfmts: $(FATE_FILTER_PIXFMTS-yes)

$(FATE_FILTER_VSYNTH-yes): $(VREF)
$(FATE_FILTER_VSYNTH-yes): SRC = $(TARGET_PATH)/tests/vsynth1/%02d.pgm

FATE_AVCONV-$(call DEMDEC, IMAGE2, PGMYUV) += $(FATE_FILTER_VSYNTH-yes)

#
# Metadata tests
#
FILTER_METADATA_COMMAND = ffprobe$(PROGSSUF)$(EXESUF) -of compact=p=0 -show_entries frame=pkt_pts:frame_tags -bitexact -f lavfi

SCENEDETECT_DEPS = FFPROBE LAVFI_INDEV MOVIE_FILTER SELECT_FILTER SCALE_FILTER \
                   AVCODEC AVDEVICE MOV_DEMUXER SVQ3_DECODER ZLIB
FATE_METADATA_FILTER-$(call ALLYES, $(SCENEDETECT_DEPS)) += fate-filter-metadata-scenedetect
fate-filter-metadata-scenedetect: SRC = $(TARGET_SAMPLES)/svq3/Vertical400kbit.sorenson3.mov
fate-filter-metadata-scenedetect: CMD = run $(FILTER_METADATA_COMMAND) "sws_flags=+accurate_rnd+bitexact;movie='$(SRC)',select=gt(scene\,.25)"

CROPDETECT_DEPS = FFPROBE LAVFI_INDEV MOVIE_FILTER CROPDETECT_FILTER SCALE_FILTER \
                  AVCODEC AVDEVICE MOV_DEMUXER H264_DECODER
FATE_METADATA_FILTER-$(call ALLYES, $(CROPDETECT_DEPS)) += fate-filter-metadata-cropdetect
fate-filter-metadata-cropdetect: SRC = $(TARGET_SAMPLES)/filter/cropdetect.mp4
fate-filter-metadata-cropdetect: CMD = run $(FILTER_METADATA_COMMAND) "sws_flags=+accurate_rnd+bitexact;movie='$(SRC)',cropdetect=max_outliers=3"

FREEZEDETECT_DEPS = FFPROBE AVDEVICE LAVFI_INDEV MPTESTSRC_FILTER SCALE_FILTER FREEZEDETECT_FILTER
FATE_METADATA_FILTER-$(call ALLYES, $(FREEZEDETECT_DEPS)) += fate-filter-metadata-freezedetect
fate-filter-metadata-freezedetect: CMD = run $(FILTER_METADATA_COMMAND) "sws_flags=+accurate_rnd+bitexact;mptestsrc=r=25:d=10:m=51,freezedetect"

SILENCEDETECT_DEPS = FFPROBE AVDEVICE LAVFI_INDEV AMOVIE_FILTER TTA_DEMUXER TTA_DECODER SILENCEDETECT_FILTER
FATE_METADATA_FILTER-$(call ALLYES, $(SILENCEDETECT_DEPS)) += fate-filter-metadata-silencedetect
fate-filter-metadata-silencedetect: SRC = $(TARGET_SAMPLES)/lossless-audio/inside.tta
fate-filter-metadata-silencedetect: CMD = run $(FILTER_METADATA_COMMAND) "amovie='$(SRC)',silencedetect=n=-33.5dB:d=0.2"

EBUR128_METADATA_DEPS = FFPROBE AVDEVICE LAVFI_INDEV AMOVIE_FILTER FLAC_DEMUXER FLAC_DECODER EBUR128_FILTER
FATE_METADATA_FILTER-$(call ALLYES, $(EBUR128_METADATA_DEPS)) += fate-filter-metadata-ebur128
fate-filter-metadata-ebur128: SRC = $(TARGET_SAMPLES)/filter/seq-3341-7_seq-3342-5-24bit.flac
fate-filter-metadata-ebur128: CMD = run $(FILTER_METADATA_COMMAND) "amovie='$(SRC)',ebur128=metadata=1"

READVITC_METADATA_DEPS = FFPROBE LAVFI_INDEV MOVIE_FILTER AVCODEC AVDEVICE \
                         AVI_DEMUXER FFVHUFF_DECODER READVITC_FILTER
FATE_METADATA_FILTER-$(call ALLYES, $(READVITC_METADATA_DEPS)) += fate-filter-metadata-readvitc-def
fate-filter-metadata-readvitc-def: SRC = $(TARGET_SAMPLES)/filter/sample-vitc.avi
fate-filter-metadata-readvitc-def: CMD = run $(FILTER_METADATA_COMMAND) "movie='$(SRC)',readvitc"

FATE_METADATA_FILTER-$(call ALLYES, $(READVITC_METADATA_DEPS)) += fate-filter-metadata-readvitc-thr
fate-filter-metadata-readvitc-thr: SRC = $(TARGET_SAMPLES)/filter/sample-vitc.avi
fate-filter-metadata-readvitc-thr: CMD = run $(FILTER_METADATA_COMMAND) "movie='$(SRC)',readvitc=thr_b=0.3:thr_w=0.5"

AVF_PHASE_METER_DEPS = FFPROBE AVDEVICE LAVFI_INDEV AMOVIE_FILTER FLAC_DEMUXER FLAC_DECODER SINE_FILTER APHASEMETER_FILTER
FATE_METADATA_FILTER-$(call ALLYES, $(AVF_PHASE_METER_DEPS)) += fate-filter-metadata-avf-aphase-meter-mono
fate-filter-metadata-avf-aphase-meter-mono: CMD = run $(FILTER_METADATA_COMMAND) sine="frequency=1000:sample_rate=48000:duration=1,aphasemeter=video=0"

FATE_METADATA_FILTER-$(call ALLYES, $(AVF_PHASE_METER_DEPS)) += fate-filter-metadata-avf-aphase-meter-out-of-phase
fate-filter-metadata-avf-aphase-meter-out-of-phase: SRC = $(TARGET_SAMPLES)/filter/out-of-phase-1000hz.flac
fate-filter-metadata-avf-aphase-meter-out-of-phase: CMD = run $(FILTER_METADATA_COMMAND) "amovie='$(SRC)',aphasemeter=video=0"

tests/data/file4560-override2rotate0.mov: TAG = GEN
tests/data/file4560-override2rotate0.mov: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< \
	-i $(TARGET_SAMPLES)/filter/sample-in-issue-505.mov -c copy -flags +bitexact -metadata:s:v:0 rotate=0 $(TARGET_PATH)/$@ -y 2>/dev/null

FATE_FILTER_SAMPLES-$(call ALLYES, MOV_DEMUXER H264_DECODER AAC_FIXED_DECODER PCM_S16LE_ENCODER MOV_MUXER) += fate-filter-meta-4560-rotate0
fate-filter-meta-4560-rotate0: tests/data/file4560-override2rotate0.mov
fate-filter-meta-4560-rotate0: CMD = framecrc -flags +bitexact -c:a aac_fixed -i $(TARGET_PATH)/tests/data/file4560-override2rotate0.mov

REFCMP_DEPS = FFMPEG LAVFI_INDEV TESTSRC2_FILTER AVGBLUR_FILTER METADATA_FILTER

FATE_FILTER_SAMPLES-$(call ALLYES, $(REFCMP_DEPS) PSNR_FILTER) += fate-filter-refcmp-psnr-rgb
fate-filter-refcmp-psnr-rgb: CMD = refcmp_metadata psnr rgb24 0.002

FATE_FILTER_SAMPLES-$(call ALLYES, $(REFCMP_DEPS) PSNR_FILTER) += fate-filter-refcmp-psnr-yuv
fate-filter-refcmp-psnr-yuv: CMD = refcmp_metadata psnr yuv422p 0.0015

FATE_FILTER_SAMPLES-$(call ALLYES, $(REFCMP_DEPS) SSIM_FILTER) += fate-filter-refcmp-ssim-rgb
fate-filter-refcmp-ssim-rgb: CMD = refcmp_metadata ssim rgb24 0.015

FATE_FILTER_SAMPLES-$(call ALLYES, $(REFCMP_DEPS) SSIM_FILTER) += fate-filter-refcmp-ssim-yuv
fate-filter-refcmp-ssim-yuv: CMD = refcmp_metadata ssim yuv422p 0.015

FATE_SAMPLES_FFPROBE += $(FATE_METADATA_FILTER-yes)
FATE_SAMPLES_FFMPEG += $(FATE_FILTER_SAMPLES-yes)
FATE_FFMPEG += $(FATE_FILTER-yes)

fate-vfilter: $(FATE_FILTER-yes) $(FATE_FILTER_SAMPLES-yes) $(FATE_FILTER_VSYNTH-yes)

fate-filter: fate-afilter fate-vfilter $(FATE_METADATA_FILTER-yes)
