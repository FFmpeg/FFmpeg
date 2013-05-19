FATE_FILTER-$(call FILTERDEMDEC, DELOGO, RM, RV30) += fate-filter-delogo
fate-filter-delogo: CMD = framecrc -i $(TARGET_SAMPLES)/real/rv30.rm -vf delogo=show=0:x=290:y=25:w=26:h=16 -an

FATE_YADIF += fate-filter-yadif-mode0
fate-filter-yadif-mode0: CMD = framecrc -flags bitexact -idct simple -i $(TARGET_SAMPLES)/mpeg2/mpeg2_field_encoding.ts -vf yadif=0

FATE_YADIF += fate-filter-yadif-mode1
fate-filter-yadif-mode1: CMD = framecrc -flags bitexact -idct simple -i $(TARGET_SAMPLES)/mpeg2/mpeg2_field_encoding.ts -vf yadif=1

FATE_FILTER-$(call FILTERDEMDEC, YADIF, MPEGTS, MPEG2VIDEO) += $(FATE_YADIF)

FATE_SAMPLES_AVCONV += $(FATE_FILTER-yes)


FATE_FILTER_VSYNTH-$(CONFIG_BOXBLUR_FILTER) += fate-filter-boxblur
fate-filter-boxblur: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf boxblur=2:1

FATE_FILTER_VSYNTH-$(CONFIG_DRAWBOX_FILTER) += fate-filter-drawbox
fate-filter-drawbox: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf drawbox=10:20:200:60:red@0.5

FATE_FILTER_VSYNTH-$(CONFIG_FADE_FILTER) += fate-filter-fade
fate-filter-fade: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf fade=in:0:25,fade=out:25:25

FATE_FILTER_VSYNTH-$(CONFIG_GRADFUN_FILTER) += fate-filter-gradfun
fate-filter-gradfun: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf gradfun

FATE_FILTER_VSYNTH-$(CONFIG_HQDN3D_FILTER) += fate-filter-hqdn3d
fate-filter-hqdn3d: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf hqdn3d

FATE_FILTER_VSYNTH-$(CONFIG_INTERLACE_FILTER) += fate-filter-interlace
fate-filter-interlace: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf interlace

FATE_FILTER_VSYNTH-$(CONFIG_NEGATE_FILTER) += fate-filter-negate
fate-filter-negate: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf negate

FATE_FILTER_VSYNTH-$(CONFIG_OVERLAY_FILTER) += fate-filter-overlay
fate-filter-overlay: tests/data/filtergraphs/overlay
fate-filter-overlay: CMD = framecrc -c:v pgmyuv -i $(SRC) -c:v pgmyuv -i $(SRC) -filter_complex_script $(TARGET_PATH)/tests/data/filtergraphs/overlay

FATE_FILTER_VSYNTH-$(call ALLYES, SETPTS_FILTER  SETTB_FILTER) += fate-filter-setpts
fate-filter-setpts: tests/data/filtergraphs/setpts
fate-filter-setpts: CMD = framecrc -c:v pgmyuv -i $(SRC) -filter_script $(TARGET_PATH)/tests/data/filtergraphs/setpts

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
fate-filter-unsharp: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf unsharp


FATE_FILTER_VSYNTH-$(CONFIG_CROP_FILTER) += fate-filter-crop
fate-filter-crop: CMD = video_filter "crop=iw-100:ih-100:100:100"

FATE_FILTER_VSYNTH-$(call ALLYES, CROP_FILTER SCALE_FILTER) += fate-filter-crop_scale
fate-filter-crop_scale: CMD = video_filter "crop=iw-100:ih-100:100:100,scale=400:-1"

FATE_FILTER_VSYNTH-$(call ALLYES, CROP_FILTER SCALE_FILTER VFLIP_FILTER) += fate-filter-crop_scale_vflip
fate-filter-crop_scale_vflip: CMD = video_filter "null,null,crop=iw-200:ih-200:200:200,crop=iw-20:ih-20:20:20,scale=200:200,scale=250:250,vflip,vflip,null,scale=200:200,crop=iw-100:ih-100:100:100,vflip,scale=200:200,null,vflip,crop=iw-100:ih-100:100:100,null"

FATE_FILTER_VSYNTH-$(call ALLYES, CROP_FILTER VFLIP_FILTER) += fate-filter-crop_vflip
fate-filter-crop_vflip: CMD = video_filter "crop=iw-100:ih-100:100:100,vflip"

FATE_FILTER_VSYNTH-$(CONFIG_NULL_FILTER) += fate-filter-null
fate-filter-null: CMD = video_filter "null"

FATE_FILTER_VSYNTH-$(CONFIG_SCALE_FILTER) += fate-filter-scale200
fate-filter-scale200: CMD = video_filter "scale=200:200"

FATE_FILTER_VSYNTH-$(CONFIG_SCALE_FILTER) += fate-filter-scale500
fate-filter-scale500: CMD = video_filter "scale=500:500"

FATE_FILTER_VSYNTH-$(CONFIG_VFLIP_FILTER) += fate-filter-vflip
fate-filter-vflip: CMD = video_filter "vflip"

FATE_FILTER_VSYNTH-$(call ALLYES, CROP_FILTER VFLIP_FILTER) += fate-filter-vflip_crop
fate-filter-vflip_crop: CMD = video_filter "vflip,crop=iw-100:ih-100:100:100"

FATE_FILTER_VSYNTH-$(CONFIG_VFLIP_FILTER) += fate-filter-vflip_vflip
fate-filter-vflip_vflip: CMD = video_filter "vflip,vflip"


FATE_FILTER_VSYNTH-$(CONFIG_FORMAT_FILTER) += fate-filter-pixdesc
fate-filter-pixdesc: CMD = pixdesc


FATE_FILTER_PIXFMTS += fate-filter-pixfmts-copy
fate-filter-pixfmts-copy:  CMD = pixfmts

FATE_FILTER_PIXFMTS += fate-filter-pixfmts-crop
fate-filter-pixfmts-crop:  CMD = pixfmts "100:100:100:100"

FATE_FILTER_PIXFMTS += fate-filter-pixfmts-hflip
fate-filter-pixfmts-hflip: CMD = pixfmts

FATE_FILTER_PIXFMTS += fate-filter-pixfmts-null
fate-filter-pixfmts-null:  CMD = pixfmts

FATE_FILTER_PIXFMTS += fate-filter-pixfmts-pad
fate-filter-pixfmts-pad:   CMD = pixfmts "500:400:20:20"

FATE_FILTER_PIXFMTS += fate-filter-pixfmts-scale
fate-filter-pixfmts-scale: CMD = pixfmts "200:100"

FATE_FILTER_PIXFMTS += fate-filter-pixfmts-vflip
fate-filter-pixfmts-vflip: CMD = pixfmts

$(FATE_FILTER_PIXFMTS): libavfilter/filtfmts-test$(EXESUF)
FATE_FILTER_VSYNTH-$(CONFIG_FORMAT_FILTER) += $(FATE_FILTER_PIXFMTS)


$(FATE_FILTER_VSYNTH-yes): $(VREF)
$(FATE_FILTER_VSYNTH-yes): SRC = $(TARGET_PATH)/tests/vsynth1/%02d.pgm

FATE_AVCONV-$(call DEMDEC, IMAGE2, PGMYUV) += $(FATE_FILTER_VSYNTH-yes)

fate-vfilter: $(FATE_FILTER-yes) $(FATE_FILTER_VSYNTH-yes)

fate-filter: fate-afilter fate-vfilter
