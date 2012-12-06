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

FATE_FILTER-$(CONFIG_AMIX_FILTER) += $(FATE_AMIX)

FATE_FILTER-$(CONFIG_ASYNCTS_FILTER) += fate-filter-asyncts
fate-filter-asyncts: SRC = $(SAMPLES)/nellymoser/nellymoser-discont.flv
fate-filter-asyncts: CMD = pcm -analyzeduration 10000000 -i $(SRC) -af asyncts
fate-filter-asyncts: CMP = oneoff
fate-filter-asyncts: REF = $(SAMPLES)/nellymoser/nellymoser-discont-async-v2.pcm

FATE_FILTER-$(CONFIG_ARESAMPLE_FILTER) += fate-filter-aresample
fate-filter-aresample: SRC = $(SAMPLES)/nellymoser/nellymoser-discont.flv
fate-filter-aresample: CMD = pcm -i $(SRC) -af aresample=min_comp=0.001:min_hard_comp=0.1
fate-filter-aresample: CMP = oneoff
fate-filter-aresample: REF = $(SAMPLES)/nellymoser/nellymoser-discont.pcm

fate-filter-delogo: CMD = framecrc -i $(SAMPLES)/real/rv30.rm -vf delogo=show=0:x=290:y=25:w=26:h=16 -an

FATE_FILTER-$(CONFIG_DELOGO_FILTER) += fate-filter-delogo

FATE_YADIF += fate-filter-yadif-mode0
fate-filter-yadif-mode0: CMD = framecrc -flags bitexact -idct simple -i $(SAMPLES)/mpeg2/mpeg2_field_encoding.ts -vf yadif=0

FATE_YADIF += fate-filter-yadif-mode1
fate-filter-yadif-mode1: CMD = framecrc -flags bitexact -idct simple -i $(SAMPLES)/mpeg2/mpeg2_field_encoding.ts -vf yadif=1

FATE_FILTER-$(CONFIG_YADIF_FILTER) += $(FATE_YADIF)

FATE_HQDN3D += fate-filter-hqdn3d
fate-filter-hqdn3d: CMD = framecrc -idct simple -i $(SAMPLES)/smjpeg/scenwin.mjpg -vf hqdn3d -an
FATE_FILTER-$(call ALLYES, SMJPEG_DEMUXER MJPEG_DECODER HQDN3D_FILTER) += $(FATE_HQDN3D)

FATE_GRADFUN += fate-filter-gradfun
fate-filter-gradfun: CMD = framecrc -i $(SAMPLES)/vmd/12.vmd -vf "sws_flags=+accurate_rnd+bitexact;gradfun=10:8" -an -frames:v 20
FATE_FILTER-$(call ALLYES, VMD_DEMUXER VMDVIDEO_DECODER GRADFUN_FILTER) += $(FATE_GRADFUN)

FATE_SAMPLES_AVCONV += $(FATE_FILTER-yes)

#
# Metadata tests
#
FILTER_METADATA_COMMAND = ffprobe$(EXESUF) -show_frames -of compact=nk=1:p=0 -bitexact -f lavfi

SCENEDETECT_DEPS = FFPROBE LAVFI_INDEV MOVIE_FILTER SELECT_FILTER SCALE_FILTER \
                   AVCODEC MOV_DEMUXER SVQ3_DECODER ZLIB
FATE_METADATA_FILTER-$(call ALLYES, $(SCENEDETECT_DEPS)) += fate-filter-metadata-scenedetect
fate-filter-metadata-scenedetect: SRC = $(SAMPLES)/svq3/Vertical400kbit.sorenson3.mov
fate-filter-metadata-scenedetect: CMD = run $(FILTER_METADATA_COMMAND) "sws_flags=+accurate_rnd+bitexact;movie='$(SRC)',select=gt(scene\,.4)"

SILENCEDETECT_DEPS = FFPROBE LAVFI_INDEV AMOVIE_FILTER AMR_DEMUXER AMRWB_DECODER
FATE_METADATA_FILTER-$(call ALLYES, $(SILENCEDETECT_DEPS)) += fate-filter-metadata-silencedetect
fate-filter-metadata-silencedetect: SRC = $(SAMPLES)/amrwb/seed-12k65.awb
fate-filter-metadata-silencedetect: CMD = run $(FILTER_METADATA_COMMAND) "amovie='$(SRC)',silencedetect=d=-20dB"

FATE_SAMPLES_FFPROBE += $(FATE_METADATA_FILTER-yes)

fate-filter: $(FATE_FILTER-yes) $(FATE_METADATA_FILTER-yes)
