FILTERDEMDEC        = $(call ALLYES, $(1)_FILTER $(2)_DEMUXER $(3)_DECODER)
FILTERDEMDECMUX     = $(call ALLYES, $(1)_FILTER $(2)_DEMUXER $(3)_DECODER $(4)_MUXER)
FILTERDEMDECENCMUX  = $(call ALLYES, $(1)_FILTER $(2)_DEMUXER $(3)_DECODER $(4)_ENCODER $(5)_MUXER)

FATE_AMIX += fate-filter-amix-simple
fate-filter-amix-simple: CMD = ffmpeg -filter_complex amix -i $(SRC) -ss 3 -i $(SRC1) -f f32le -
fate-filter-amix-simple: REF = $(SAMPLES)/filter/amix_simple.pcm

FATE_AMIX += fate-filter-amix-first
fate-filter-amix-first: CMD = ffmpeg -filter_complex amix=duration=first -ss 4 -i $(SRC) -i $(SRC1) -f f32le -
fate-filter-amix-first: REF = $(SAMPLES)/filter/amix_first.pcm

FATE_AMIX += fate-filter-amix-transition
fate-filter-amix-transition: tests/data/asynth-44100-2-3.wav
fate-filter-amix-transition: SRC2 = $(TARGET_PATH)/tests/data/asynth-44100-2-3.wav
fate-filter-amix-transition: CMD = ffmpeg -filter_complex amix=inputs=3:dropout_transition=0.5 -i $(SRC) -ss 2 -i $(SRC1) -ss 4 -i $(SRC2) -f f32le -
fate-filter-amix-transition: REF = $(SAMPLES)/filter/amix_transition.pcm

$(FATE_AMIX): tests/data/asynth-44100-2.wav tests/data/asynth-44100-2-2.wav
$(FATE_AMIX): SRC  = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
$(FATE_AMIX): SRC1 = $(TARGET_PATH)/tests/data/asynth-44100-2-2.wav
$(FATE_AMIX): CMP  = oneoff
$(FATE_AMIX): CMP_UNIT = f32

FATE_FILTER-$(call FILTERDEMDECENCMUX, AMIX, WAV, PCM_S16LE, PCM_F32LE, PCM_F32LE) += $(FATE_AMIX)

FATE_FILTER-$(call FILTERDEMDECMUX, ASYNCTS, FLV, NELLYMOSER, PCM_S16LE) += fate-filter-asyncts
fate-filter-asyncts: SRC = $(SAMPLES)/nellymoser/nellymoser-discont.flv
fate-filter-asyncts: CMD = pcm -analyzeduration 10000000 -i $(SRC) -af asyncts
fate-filter-asyncts: CMP = oneoff
fate-filter-asyncts: REF = $(SAMPLES)/nellymoser/nellymoser-discont-async-v2.pcm

FATE_FILTER-$(CONFIG_ARESAMPLE_FILTER) += fate-filter-aresample
fate-filter-aresample: SRC = $(SAMPLES)/nellymoser/nellymoser-discont.flv
fate-filter-aresample: CMD = pcm -i $(SRC) -af aresample=min_comp=0.001:min_hard_comp=0.1:first_pts=0
fate-filter-aresample: CMP = oneoff
fate-filter-aresample: REF = $(SAMPLES)/nellymoser/nellymoser-discont.pcm

FATE_FILTER_VSYNTH-$(CONFIG_BOXBLUR_FILTER) += fate-filter-boxblur
fate-filter-boxblur: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf boxblur=2:1

FATE_FILTER-$(call FILTERDEMDECENCMUX, CHANNELMAP, WAV, PCM_S16LE, PCM_S16LE, WAV) += fate-filter-channelmap
fate-filter-channelmap: SRC = $(TARGET_PATH)/tests/data/asynth-44100-6.wav
fate-filter-channelmap: tests/data/asynth-44100-6.wav
fate-filter-channelmap: CMD = md5 -i $(SRC) -filter_complex_script $(SRC_PATH)/tests/filtergraphs/channelmap -f wav -flags +bitexact
fate-filter-channelmap: CMP = oneline
fate-filter-channelmap: REF = 06168d06085e2c0603e4e118ba4cade2

FATE_FILTER-$(call FILTERDEMDECENCMUX, CHANNELSPLIT, WAV, PCM_S16LE, PCM_S16LE, PCM_S16LE) += fate-filter-channelsplit
fate-filter-channelsplit: SRC = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
fate-filter-channelsplit: tests/data/asynth-44100-2.wav
fate-filter-channelsplit: CMD = md5 -i $(SRC) -filter_complex channelsplit -f s16le
fate-filter-channelsplit: CMP = oneline
fate-filter-channelsplit: REF = d92988d0fe2dd92236763f47b07ab597

FATE_FILTER-$(call ALLYES, PERMS_FILTER DELOGO_FILTER RM_DEMUXER RV30_DECODER) += fate-filter-delogo
fate-filter-delogo: CMD = framecrc -i $(SAMPLES)/real/rv30.rm -vf perms=random,delogo=show=0:x=290:y=25:w=26:h=16 -an

FATE_FILTER_VSYNTH-$(CONFIG_DRAWBOX_FILTER) += fate-filter-drawbox
fate-filter-drawbox: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf drawbox=10:20:200:60:red@0.5

FATE_FILTER_VSYNTH-$(CONFIG_FADE_FILTER) += fate-filter-fade
fate-filter-fade: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf fade=in:0:25,fade=out:25:25

FATE_FILTER_VSYNTH-$(CONFIG_GRADFUN_FILTER) += fate-filter-gradfun
fate-filter-gradfun: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf gradfun

FATE_FILTER_VSYNTH-$(CONFIG_HQDN3D_FILTER) += fate-filter-hqdn3d
fate-filter-hqdn3d: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf hqdn3d

#FATE_FILTER-$(call FILTERDEMDECENCMUX, JOIN, WAV, PCM_S16LE, PCM_S16LE, PCM_S16LE) += fate-filter-join
fate-filter-join: SRC1 = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
fate-filter-join: SRC2 = $(TARGET_PATH)/tests/data/asynth-44100-3.wav
fate-filter-join: tests/data/asynth-44100-2.wav tests/data/asynth-44100-3.wav
fate-filter-join: CMD = md5 -i $(SRC1) -i $(SRC2) -filter_complex join=channel_layout=5 -f s16le
fate-filter-join: CMP = oneline
fate-filter-join: REF = 38fa1b18b0c46d77df6f17bfc4f078dd

FATE_FILTER_VSYNTH-$(CONFIG_NEGATE_FILTER) += fate-filter-negate
fate-filter-negate: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf negate

FATE_FILTER_VSYNTH-$(CONFIG_OVERLAY_FILTER) += fate-filter-overlay
fate-filter-overlay: CMD = framecrc -c:v pgmyuv -i $(SRC) -c:v pgmyuv -i $(SRC) -filter_complex_script $(SRC_PATH)/tests/filtergraphs/overlay

FATE_FILTER_VSYNTH-$(call ALLYES, SETPTS_FILTER  SETTB_FILTER) += fate-filter-setpts
fate-filter-setpts: CMD = framecrc -c:v pgmyuv -i $(SRC) -filter_script $(SRC_PATH)/tests/filtergraphs/setpts

FATE_FILTER_VSYNTH-$(CONFIG_TRANSPOSE_FILTER) += fate-filter-transpose
fate-filter-transpose: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf transpose

FATE_FILTER_VSYNTH-$(CONFIG_UNSHARP_FILTER) += fate-filter-unsharp
fate-filter-unsharp: CMD = framecrc -c:v pgmyuv -i $(SRC) -vf unsharp

FATE_FILTER-$(call ALLYES, WAV_DEMUXER PCM_S16LE_DECODER PCM_S16LE_ENCODER PCM_S16LE_MUXER APERMS_FILTER VOLUME_FILTER) += fate-filter-volume
fate-filter-volume: SRC = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
fate-filter-volume: tests/data/asynth-44100-2.wav
fate-filter-volume: CMD = md5 -i $(SRC) -af aperms=random,volume=precision=fixed:volume=0.5 -f s16le
fate-filter-volume: CMP = oneline
fate-filter-volume: REF = 4d6ba75ef3e32d305d066b9bc771d6f4

FATE_YADIF += fate-filter-yadif-mode0
fate-filter-yadif-mode0: CMD = framecrc -flags bitexact -idct simple -i $(SAMPLES)/mpeg2/mpeg2_field_encoding.ts -vframes 30 -vf yadif=0

FATE_YADIF += fate-filter-yadif-mode1
fate-filter-yadif-mode1: CMD = framecrc -flags bitexact -idct simple -i $(SAMPLES)/mpeg2/mpeg2_field_encoding.ts -vframes 59 -vf yadif=1

FATE_FILTER-$(call FILTERDEMDEC, YADIF, MPEGTS, MPEG2VIDEO) += $(FATE_YADIF)

FATE_FILTER-$(call ALLYES, SMJPEG_DEMUXER MJPEG_DECODER PERMS_FILTER HQDN3D_FILTER) += fate-filter-hqdn3d-sample
fate-filter-hqdn3d-sample: CMD = framecrc -idct simple -i $(SAMPLES)/smjpeg/scenwin.mjpg -vf perms=random,hqdn3d -an

FATE_FILTER-$(call ALLYES, UTVIDEO_DECODER AVI_DEMUXER PERMS_FILTER CURVES_FILTER) += fate-filter-curves
fate-filter-curves: CMD = framecrc -i $(SAMPLES)/utvideo/utvideo_rgb_median.avi -vf perms=random,curves=vintage

FATE_FILTER-$(call ALLYES, VMD_DEMUXER VMDVIDEO_DECODER FORMAT_FILTER PERMS_FILTER GRADFUN_FILTER) += fate-filter-gradfun-sample
fate-filter-gradfun-sample: CMD = framecrc -i $(SAMPLES)/vmd/12.vmd -filter_script $(SRC_PATH)/tests/filtergraphs/gradfun -an -frames:v 20

FATE_FILTER-$(call ALLYES, TESTSRC_FILTER SINE_FILTER CONCAT_FILTER) += fate-filter-concat
fate-filter-concat: CMD = framecrc -filter_complex_script $(SRC_PATH)/tests/filtergraphs/concat

$(FATE_FILTER_VSYNTH-yes): tests/vsynth1/00.pgm
$(FATE_FILTER_VSYNTH-yes): SRC = $(TARGET_PATH)/tests/vsynth1/%02d.pgm
FATE_AVCONV-$(call DEMDEC, IMAGE2, PGMYUV) += $(FATE_FILTER_VSYNTH-yes)

FATE_SAMPLES_AVCONV += $(FATE_FILTER-yes)

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

fate-filter: $(FATE_FILTER-yes) $(FATE_FILTER_VSYNTH-yes) $(FATE_METADATA_FILTER-yes)
