FATE_FILTER-$(call ALLYES, PERMS_FILTER DELOGO_FILTER RM_DEMUXER RV30_DECODER) += fate-filter-delogo
fate-filter-delogo: CMD = framecrc -i $(SAMPLES)/real/rv30.rm -vf perms=random,delogo=show=0:x=290:y=25:w=26:h=16 -an

FATE_YADIF += fate-filter-yadif-mode0
fate-filter-yadif-mode0: CMD = framecrc -flags bitexact -idct simple -i $(SAMPLES)/mpeg2/mpeg2_field_encoding.ts -vframes 30 -vf yadif=0

FATE_YADIF += fate-filter-yadif-mode1
fate-filter-yadif-mode1: CMD = framecrc -flags bitexact -idct simple -i $(SAMPLES)/mpeg2/mpeg2_field_encoding.ts -vframes 59 -vf yadif=1

FATE_FILTER-$(call FILTERDEMDEC, YADIF, MPEGTS, MPEG2VIDEO) += $(FATE_YADIF)

FATE_SAMPLES_AVCONV += $(FATE_FILTER-yes)


FATE_FILTER-$(call ALLYES, AVDEVICE LIFE_FILTER) += fate-filter-lavd-life
fate-filter-lavd-life: CMD = framecrc -f lavfi -i life=s=40x40:r=5:seed=42:mold=64:ratio=0.1:death_color=red:life_color=green -t 2

FATE_FILTER-$(call ALLYES, AVDEVICE TESTSRC_FILTER) += fate-filter-lavd-testsrc
fate-filter-lavd-testsrc: CMD = framecrc -f lavfi -i testsrc=r=7:n=2:d=10

FATE_FILTER-$(call ALLYES, AVDEVICE TESTSRC_FILTER FORMAT_FILTER CONCAT_FILTER SCALE_FILTER) += fate-filter-lavd-scalenorm
fate-filter-lavd-scalenorm: CMD = framecrc -f lavfi -graph_file $(SRC_PATH)/tests/filtergraphs/scalenorm -i dummy


FATE_FILTER_VSYNTH-$(CONFIG_BOXBLUR_FILTER) += fate-filter-boxblur
fate-filter-boxblur: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf boxblur=2:1

FATE_FILTER_VSYNTH-$(CONFIG_DRAWBOX_FILTER) += fate-filter-drawbox
fate-filter-drawbox: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf drawbox=224:24:88:72:red@0.5

FATE_FILTER_VSYNTH-$(CONFIG_FADE_FILTER) += fate-filter-fade
fate-filter-fade: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf fade=in:5:15,fade=out:30:15

FATE_FILTER_VSYNTH-$(CONFIG_GRADFUN_FILTER) += fate-filter-gradfun
fate-filter-gradfun: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf gradfun

FATE_FILTER_VSYNTH-$(CONFIG_HQDN3D_FILTER) += fate-filter-hqdn3d
fate-filter-hqdn3d: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf hqdn3d

FATE_FILTER_VSYNTH-$(CONFIG_INTERLACE_FILTER) += fate-filter-interlace
fate-filter-interlace: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf interlace

FATE_FILTER_VSYNTH-$(call ALLYES, NEGATE_FILTER PERMS_FILTER) += fate-filter-negate
fate-filter-negate: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf perms=random,negate

FATE_FILTER_VSYNTH-$(CONFIG_HISTOGRAM_FILTER) += fate-filter-histogram-levels
fate-filter-histogram-levels: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf histogram -flags +bitexact -sws_flags +accurate_rnd+bitexact

FATE_FILTER_VSYNTH-$(CONFIG_HISTOGRAM_FILTER) += fate-filter-histogram-waveform
fate-filter-histogram-waveform: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf histogram=mode=waveform -flags +bitexact -sws_flags +accurate_rnd+bitexact

FATE_FILTER_VSYNTH-$(CONFIG_OVERLAY_FILTER) += fate-filter-overlay
fate-filter-overlay: CMD = framecrc -c:v pgmyuv -i $(SRC) -c:v pgmyuv -i $(SRC) -filter_complex_script $(SRC_PATH)/tests/filtergraphs/overlay

FATE_FILTER_VSYNTH-$(call ALLYES, SPLIT_FILTER SCALE_FILTER PAD_FILTER OVERLAY_FILTER) += fate-filter-overlay_rgb
fate-filter-overlay_rgb: CMD = framecrc -c:v pgmyuv -i $(SRC) -filter_complex_script $(SRC_PATH)/tests/filtergraphs/overlay_rgb

FATE_FILTER_VSYNTH-$(call ALLYES, SPLIT_FILTER SCALE_FILTER PAD_FILTER OVERLAY_FILTER) += fate-filter-overlay_yuv420
fate-filter-overlay_yuv420: CMD = framecrc -c:v pgmyuv -i $(SRC) -filter_complex_script $(SRC_PATH)/tests/filtergraphs/overlay_yuv420

FATE_FILTER_VSYNTH-$(call ALLYES, SPLIT_FILTER SCALE_FILTER PAD_FILTER OVERLAY_FILTER) += fate-filter-overlay_yuv444
fate-filter-overlay_yuv444: CMD = framecrc -c:v pgmyuv -i $(SRC) -filter_complex_script $(SRC_PATH)/tests/filtergraphs/overlay_yuv444

FATE_FILTER_VSYNTH-$(CONFIG_SEPARATEFIELDS_FILTER) += fate-filter-separatefields
fate-filter-separatefields: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf separatefields

FATE_FILTER_VSYNTH-$(call ALLYES, SETPTS_FILTER  SETTB_FILTER) += fate-filter-setpts
fate-filter-setpts: CMD = framecrc -c:v pgmyuv -i $(SRC) -filter_script $(SRC_PATH)/tests/filtergraphs/setpts

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

FATE_FILTER-$(call ALLYES, SMJPEG_DEMUXER MJPEG_DECODER PERMS_FILTER HQDN3D_FILTER) += fate-filter-hqdn3d-sample
fate-filter-hqdn3d-sample: CMD = framecrc -idct simple -i $(SAMPLES)/smjpeg/scenwin.mjpg -vf perms=random,hqdn3d -an

FATE_FILTER-$(call ALLYES, UTVIDEO_DECODER AVI_DEMUXER PERMS_FILTER CURVES_FILTER) += fate-filter-curves
fate-filter-curves: CMD = framecrc -i $(SAMPLES)/utvideo/utvideo_rgb_median.avi -vf perms=random,curves=vintage

FATE_FILTER-$(call ALLYES, VMD_DEMUXER VMDVIDEO_DECODER FORMAT_FILTER PERMS_FILTER GRADFUN_FILTER) += fate-filter-gradfun-sample
fate-filter-gradfun-sample: CMD = framecrc -i $(SAMPLES)/vmd/12.vmd -filter_script $(SRC_PATH)/tests/filtergraphs/gradfun -an -frames:v 20

FATE_FILTER-$(call ALLYES, TESTSRC_FILTER SINE_FILTER CONCAT_FILTER) += fate-filter-concat
fate-filter-concat: CMD = framecrc -filter_complex_script $(SRC_PATH)/tests/filtergraphs/concat

FATE_FILTER_VSYNTH-$(call ALLYES, FORMAT_FILTER SPLIT_FILTER ALPHAEXTRACT_FILTER ALPHAMERGE_FILTER) += fate-filter-alphaextract_alphamerge_rgb
fate-filter-alphaextract_alphamerge_rgb: CMD = framecrc -c:v pgmyuv -i $(SRC) -filter_complex_script $(SRC_PATH)/tests/filtergraphs/alphamerge_alphaextract_rgb

FATE_FILTER_VSYNTH-$(call ALLYES, FORMAT_FILTER SPLIT_FILTER ALPHAEXTRACT_FILTER ALPHAMERGE_FILTER) += fate-filter-alphaextract_alphamerge_yuv
fate-filter-alphaextract_alphamerge_yuv: CMD = framecrc -c:v pgmyuv -i $(SRC) -filter_complex_script $(SRC_PATH)/tests/filtergraphs/alphamerge_alphaextract_yuv

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

FATE_FILTER_VSYNTH-$(CONFIG_COLORMATRIX_FILTER) += fate-filter-colormatrix1
fate-filter-colormatrix1: CMD = video_filter "colormatrix=bt601:smpte240m,colormatrix=smpte240m:fcc,colormatrix=fcc:bt601,colormatrix=bt601:fcc,colormatrix=fcc:smpte240m,colormatrix=smpte240m:bt709"

FATE_FILTER_VSYNTH-$(CONFIG_COLORMATRIX_FILTER) += fate-filter-colormatrix2
fate-filter-colormatrix2: CMD = video_filter "colormatrix=bt709:fcc,colormatrix=fcc:bt709,colormatrix=bt709:bt601,colormatrix=bt601:bt709,colormatrix=bt709:smpte240m,colormatrix=smpte240m:bt601"

FATE_FILTER_VSYNTH-$(call ALLYES, CROP_FILTER VFLIP_FILTER) += fate-filter-vflip_crop
fate-filter-vflip_crop: CMD = video_filter "vflip,crop=iw-100:ih-100:100:100"

FATE_FILTER_VSYNTH-$(CONFIG_VFLIP_FILTER) += fate-filter-vflip_vflip
fate-filter-vflip_vflip: CMD = video_filter "vflip,vflip"

FATE_FILTER_VSYNTH-$(call ALLYES, FORMAT_FILTER PERMS_FILTER EDGEDETECT_FILTER) += fate-filter-edgedetect
fate-filter-edgedetect: CMD = video_filter "format=gray,perms=random,edgedetect"

FATE_FILTER_VSYNTH-$(call ALLYES, PERMS_FILTER HUE_FILTER) += fate-filter-hue
fate-filter-hue: CMD = video_filter "perms=random,hue=s=sin(2*PI*t)+1"

FATE_FILTER_VSYNTH-$(CONFIG_IDET_FILTER) += fate-filter-idet
fate-filter-idet: CMD = video_filter "idet"

FATE_FILTER_VSYNTH-$(CONFIG_PAD_FILTER) += fate-filter-pad
fate-filter-pad: CMD = video_filter "pad=iw*1.5:ih*1.5:iw*0.3:ih*0.2"

FATE_FILTER_VSYNTH-$(CONFIG_PP_FILTER) += fate-filter-pp
fate-filter-pp: CMD = video_filter "pp=be/hb/vb/tn/l5/al"

FATE_FILTER_VSYNTH-$(CONFIG_PP_FILTER) += fate-filter-pp2
fate-filter-pp2: CMD = video_filter "pp=be/fq|16/h1/v1/lb"

FATE_FILTER_VSYNTH-$(CONFIG_PP_FILTER) += fate-filter-pp3
fate-filter-pp3: CMD = video_filter "pp=be/fq|8/ha|128|7/va/li"

FATE_FILTER_VSYNTH-$(CONFIG_PP_FILTER) += fate-filter-pp4
fate-filter-pp4: CMD = video_filter "pp=be/ci"

FATE_FILTER_VSYNTH-$(CONFIG_PP_FILTER) += fate-filter-pp5
fate-filter-pp5: CMD = video_filter "pp=md"

FATE_FILTER_VSYNTH-$(CONFIG_PP_FILTER) += fate-filter-pp6
fate-filter-pp6: CMD = video_filter "pp=be/fd"

FATE_FILTER_VSYNTH-$(CONFIG_SELECT_FILTER) += fate-filter-select
fate-filter-select: CMD = video_filter "select=not(eq(mod(n\,2)\,0)+eq(mod(n\,3)\,0))"

FATE_FILTER_VSYNTH-$(CONFIG_SETDAR_FILTER) += fate-filter-setdar
fate-filter-setdar: CMD = video_filter "setdar=dar=16/9"

FATE_FILTER_VSYNTH-$(CONFIG_SETSAR_FILTER) += fate-filter-setsar
fate-filter-setsar: CMD = video_filter "setsar=sar=16/11"

FATE_FILTER_VSYNTH-$(CONFIG_THUMBNAIL_FILTER) += fate-filter-thumbnail
fate-filter-thumbnail: CMD = video_filter "thumbnail=10"

FATE_FILTER_VSYNTH-$(CONFIG_TILE_FILTER) += fate-filter-tile
fate-filter-tile: CMD = video_filter "tile=3x3:nb_frames=5:padding=7:margin=2"


FATE_FILTER_VSYNTH-$(CONFIG_FORMAT_FILTER) += fate-filter-pixdesc
fate-filter-pixdesc: CMD = pixdesc


FATE_FILTER_PIXFMTS-$(CONFIG_COPY_FILTER) += fate-filter-pixfmts-copy
fate-filter-pixfmts-copy:  CMD = pixfmts

FATE_FILTER_PIXFMTS-$(CONFIG_CROP_FILTER) += fate-filter-pixfmts-crop
fate-filter-pixfmts-crop:  CMD = pixfmts "100:100:100:100"

FATE_FILTER_PIXFMTS-$(CONFIG_FIELD_FILTER) += fate-filter-pixfmts-field
fate-filter-pixfmts-field: CMD = pixfmts "bottom"

FATE_FILTER_PIXFMTS-$(CONFIG_HFLIP_FILTER) += fate-filter-pixfmts-hflip
fate-filter-pixfmts-hflip: CMD = pixfmts

#FATE_FILTER_PIXFMTS-$(CONFIG_HISTEQ_FILTER) += fate-filter-pixfmts-histeq
#fate-filter-pixfmts-histeq: CMD = pixfmts "antibanding=strong"

FATE_FILTER_PIXFMTS-$(CONFIG_IL_FILTER) += fate-filter-pixfmts-il
fate-filter-pixfmts-il:    CMD = pixfmts "luma_mode=d:chroma_mode=d:alpha_mode=d"

FATE_FILTER_PIXFMTS-$(CONFIG_KERNDEINT_FILTER) += fate-filter-pixfmts-kerndeint
fate-filter-pixfmts-kerndeint: CMD = pixfmts "" "tinterlace=interleave_top,"

FATE_FILTER_PIXFMTS-$(CONFIG_NULL_FILTER) += fate-filter-pixfmts-null
fate-filter-pixfmts-null:  CMD = pixfmts

FATE_FILTER_PIXFMTS-$(CONFIG_PAD_FILTER) += fate-filter-pixfmts-pad
fate-filter-pixfmts-pad:   CMD = pixfmts "500:400:20:20"

FATE_FILTER_PIXFMTS-$(CONFIG_SCALE_FILTER) += fate-filter-pixfmts-scale
fate-filter-pixfmts-scale: CMD = pixfmts "200:100"

FATE_FILTER_PIXFMTS-$(CONFIG_SUPER2XSAI_FILTER) += fate-filter-pixfmts-super2xsai
fate-filter-pixfmts-super2xsai: CMD = pixfmts

FATE_FILTER_PIXFMTS-$(CONFIG_SWAPUV_FILTER) += fate-filter-pixfmts-swapuv
fate-filter-pixfmts-swapuv: CMD = pixfmts

FATE_FILTER_PIXFMTS-$(CONFIG_TINTERLACE_FILTER) += fate-filter-pixfmts-tinterlace_merge
fate-filter-pixfmts-tinterlace_merge: CMD = pixfmts "merge"

FATE_FILTER_PIXFMTS-$(CONFIG_TINTERLACE_FILTER) += fate-filter-pixfmts-tinterlace_pad
fate-filter-pixfmts-tinterlace_pad: CMD = pixfmts "pad"

FATE_FILTER_PIXFMTS-$(CONFIG_VFLIP_FILTER) += fate-filter-pixfmts-vflip
fate-filter-pixfmts-vflip: CMD = pixfmts

$(FATE_FILTER_PIXFMTS-yes): libavfilter/filtfmts-test$(EXESUF)
FATE_FILTER_VSYNTH-$(CONFIG_FORMAT_FILTER) += $(FATE_FILTER_PIXFMTS-yes)

fate-filter-pixfmts: $(FATE_FILTER_PIXFMTS-yes)

$(FATE_FILTER_VSYNTH-yes): $(VREF)
$(FATE_FILTER_VSYNTH-yes): SRC = $(TARGET_PATH)/tests/vsynth1/%02d.pgm

FATE_AVCONV-$(call DEMDEC, IMAGE2, PGMYUV) += $(FATE_FILTER_VSYNTH-yes)

#
# Metadata tests
#
FILTER_METADATA_COMMAND = ffprobe$(EXESUF) -of compact=p=0 -show_entries frame=pkt_pts:frame_tags -bitexact -f lavfi

SCENEDETECT_DEPS = FFPROBE LAVFI_INDEV MOVIE_FILTER SELECT_FILTER SCALE_FILTER \
                   AVCODEC AVDEVICE MOV_DEMUXER SVQ3_DECODER ZLIB
FATE_METADATA_FILTER-$(call ALLYES, $(SCENEDETECT_DEPS)) += fate-filter-metadata-scenedetect
fate-filter-metadata-scenedetect: SRC = $(SAMPLES)/svq3/Vertical400kbit.sorenson3.mov
fate-filter-metadata-scenedetect: CMD = run $(FILTER_METADATA_COMMAND) "sws_flags=+accurate_rnd+bitexact;movie='$(SRC)',select=gt(scene\,.4)"

SILENCEDETECT_DEPS = FFPROBE AVDEVICE LAVFI_INDEV AMOVIE_FILTER AMR_DEMUXER AMRWB_DECODER SILENCEDETECT_FILTER
FATE_METADATA_FILTER-$(call ALLYES, $(SILENCEDETECT_DEPS)) += fate-filter-metadata-silencedetect
fate-filter-metadata-silencedetect: SRC = $(SAMPLES)/amrwb/seed-12k65.awb
fate-filter-metadata-silencedetect: CMD = run $(FILTER_METADATA_COMMAND) "amovie='$(SRC)',silencedetect=d=-20dB"

EBUR128_METADATA_DEPS = FFPROBE AVDEVICE LAVFI_INDEV AMOVIE_FILTER FLAC_DEMUXER FLAC_DECODER EBUR128_FILTER
FATE_METADATA_FILTER-$(call ALLYES, $(EBUR128_METADATA_DEPS)) += fate-filter-metadata-ebur128
fate-filter-metadata-ebur128: SRC = $(SAMPLES)/filter/seq-3341-7_seq-3342-5-24bit.flac
fate-filter-metadata-ebur128: CMD = run $(FILTER_METADATA_COMMAND) "amovie='$(SRC)',ebur128=metadata=1"

FATE_SAMPLES_FFPROBE += $(FATE_METADATA_FILTER-yes)

fate-vfilter: $(FATE_FILTER-yes) $(FATE_FILTER_VSYNTH-yes)

fate-filter: fate-afilter fate-vfilter $(FATE_METADATA_FILTER-yes)
