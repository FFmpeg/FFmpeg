FATE_IAMF-$(call TRANSCODE, FLAC, IAMF, WAV_DEMUXER PCM_S16LE_DECODER) += fate-iamf-stereo
fate-iamf-stereo: tests/data/asynth-44100-2.wav tests/data/streamgroups/audio_element-stereo tests/data/streamgroups/mix_presentation-stereo
fate-iamf-stereo: SRC = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
fate-iamf-stereo: CMD = transcode wav $(SRC) iamf " \
  -/stream_group $(TARGET_PATH)/tests/data/streamgroups/audio_element-stereo \
  -/stream_group $(TARGET_PATH)/tests/data/streamgroups/mix_presentation-stereo \
  -streamid 0:0 -c:a flac -t 1" "-c:a copy -map 0" \
  "-show_entries stream_group=index,id,nb_streams,type:stream_group_components:stream_group_stream=index,id:stream_group_stream_disposition"

FATE_IAMF-$(call TRANSCODE, FLAC, IAMF, WAV_DEMUXER PCM_S16LE_DECODER ARESAMPLE_FILTER) += fate-iamf-5_1_4
fate-iamf-5_1_4: tests/data/asynth-44100-10.wav tests/data/filtergraphs/iamf_5_1_4 tests/data/streamgroups/audio_element-5_1_4 tests/data/streamgroups/mix_presentation-5_1_4
fate-iamf-5_1_4: SRC = $(TARGET_PATH)/tests/data/asynth-44100-10.wav
fate-iamf-5_1_4: CMD = transcode wav $(SRC) iamf "-auto_conversion_filters \
  -/filter_complex $(TARGET_PATH)/tests/data/filtergraphs/iamf_5_1_4 \
  -/stream_group $(TARGET_PATH)/tests/data/streamgroups/audio_element-5_1_4 \
  -/stream_group $(TARGET_PATH)/tests/data/streamgroups/mix_presentation-5_1_4 \
  -streamid 0:0 -streamid 1:1 -streamid 2:2 -streamid 3:3 -streamid 4:4 -streamid 5:5 -map [FRONT] -map [BACK] -map [CENTER] -map [LFE] -map [TOP_FRONT] -map [TOP_BACK] -c:a flac -t 1" "-c:a copy -map 0" \
  "-show_entries stream_group=index,id,nb_streams,type:stream_group_components:stream_group_stream=index,id:stream_group_stream_disposition"

FATE_IAMF-$(call TRANSCODE, FLAC, IAMF, WAV_DEMUXER PCM_S16LE_DECODER ARESAMPLE_FILTER) += fate-iamf-7_1_4
fate-iamf-7_1_4: tests/data/asynth-44100-12.wav tests/data/filtergraphs/iamf_7_1_4 tests/data/streamgroups/audio_element-7_1_4 tests/data/streamgroups/mix_presentation-7_1_4
fate-iamf-7_1_4: SRC = $(TARGET_PATH)/tests/data/asynth-44100-12.wav
fate-iamf-7_1_4: CMD = transcode wav $(SRC) iamf "-auto_conversion_filters \
  -/filter_complex $(TARGET_PATH)/tests/data/filtergraphs/iamf_7_1_4 \
  -/stream_group $(TARGET_PATH)/tests/data/streamgroups/audio_element-7_1_4 \
  -/stream_group $(TARGET_PATH)/tests/data/streamgroups/mix_presentation-7_1_4 \
  -streamid 0:0 -streamid 1:1 -streamid 2:2 -streamid 3:3 -streamid 4:4 -streamid 5:5 -streamid 6:6 -map [FRONT] -map [BACK] -map [CENTER] -map [LFE] -map [SIDE] -map [TOP_FRONT] -map [TOP_BACK] -c:a flac -t 1" "-c:a copy -map 0" \
  "-show_entries stream_group=index,id,nb_streams,type:stream_group_components:stream_group_stream=index,id:stream_group_stream_disposition"

FATE_IAMF-$(call TRANSCODE, FLAC, IAMF, WAV_DEMUXER PCM_S16LE_DECODER ARESAMPLE_FILTER) += fate-iamf-9_1_6
fate-iamf-9_1_6: tests/data/asynth-44100-12.wav tests/data/filtergraphs/iamf_9_1_6 tests/data/streamgroups/audio_element-9_1_6 tests/data/streamgroups/audio_element-9_1_6-stereo tests/data/streamgroups/mix_presentation-9_1_6
fate-iamf-9_1_6: SRC = $(TARGET_PATH)/tests/data/asynth-44100-12.wav
fate-iamf-9_1_6: CMD = transcode wav $(SRC) iamf "-auto_conversion_filters \
  -/filter_complex $(TARGET_PATH)/tests/data/filtergraphs/iamf_9_1_6 \
  -/stream_group $(TARGET_PATH)/tests/data/streamgroups/audio_element-9_1_6 \
  -/stream_group $(TARGET_PATH)/tests/data/streamgroups/audio_element-9_1_6-stereo \
  -/stream_group $(TARGET_PATH)/tests/data/streamgroups/mix_presentation-9_1_6 \
  -streamid 0:0 -streamid 1:1 -streamid 2:2 -streamid 3:3 -streamid 4:4 -streamid 5:5 -streamid 6:6 -streamid 7:7 -streamid 8:8 -streamid 9:9 -map [FRONT] -map [BACK] -map [CENTER] -map [LFE] -map [FRONT_CENTER] -map [SIDE] -map [TOP_FRONT] -map [TOP_BACK] -map [TOP_SIDE] -map [STEREO] -c:a flac -t 1" "-c:a copy -map 0" \
  "-show_entries stream_group=index,id,nb_streams,type:stream_group_components:stream_group_stream=index,id:stream_group_stream_disposition"

FATE_IAMF-$(call TRANSCODE, FLAC, IAMF, WAV_DEMUXER PCM_S16LE_DECODER ARESAMPLE_FILTER) += fate-iamf-ambisonic_1
fate-iamf-ambisonic_1: tests/data/asynth-44100-4.wav tests/data/filtergraphs/iamf_ambisonic_1 tests/data/streamgroups/audio_element-ambisonic_1 tests/data/streamgroups/mix_presentation-ambisonic_1
fate-iamf-ambisonic_1: SRC = $(TARGET_PATH)/tests/data/asynth-44100-4.wav
fate-iamf-ambisonic_1: CMD = transcode wav $(SRC) iamf "-auto_conversion_filters \
  -/filter_complex $(TARGET_PATH)/tests/data/filtergraphs/iamf_ambisonic_1 \
  -/stream_group $(TARGET_PATH)/tests/data/streamgroups/audio_element-ambisonic_1 \
  -/stream_group $(TARGET_PATH)/tests/data/streamgroups/mix_presentation-ambisonic_1 \
  -streamid 0:0 -streamid 1:1 -streamid 2:2 -streamid 3:3 -map [MONO0] -map [MONO1] -map [MONO2] -map [MONO3] -c:a flac -t 1" "-c:a copy -map 0" \
  "-show_entries stream_group=index,id,nb_streams,type:stream_group_components:stream_group_stream=index,id:stream_group_stream_disposition"

FATE_IAMF_SAMPLES-$(call FRAMECRC, IAMF, OPUS) += fate-iamf-stereo-demux
fate-iamf-stereo-demux: CMD = stream_demux iamf $(TARGET_SAMPLES)/iamf/test_000076.iamf "" \
  "-c:a copy -frames:a 0 -map 0:g:\#42" \
  "-show_entries stream_group=index,id,nb_streams,type:stream_group_components:stream_group_stream=index,id:stream_group_stream_disposition"

FATE_IAMF_SAMPLES-$(call FRAMECRC, IAMF, OPUS) += fate-iamf-5_1-demux
fate-iamf-5_1-demux: CMD = stream_demux iamf $(TARGET_SAMPLES)/iamf/test_000059.iamf "" \
  "-c:a copy -frames:a 0 -map 0:g:\#42" \
  "-show_entries stream_group=index,id,nb_streams,type:stream_group_components:stream_group_stream=index,id:stream_group_stream_disposition"

FATE_IAMF_SAMPLES-$(call REMUX, IAMF, OPUS_DECODER) += fate-iamf-5_1-copy
fate-iamf-5_1-copy: CMD = stream_remux iamf $(TARGET_SAMPLES)/iamf/test_000059.iamf "" iamf \
  "-map 0 -stream_group map=0=0:st=0:st=1:st=2:st=3 -stream_group map=0=1:stg=0 -streamid 0:0 -streamid 1:1 -streamid 2:2 -streamid 3:3" "" "-c:a copy -frames:a 0 -map 0:g:i:42" \
  "-show_entries stream_group=index,id,nb_streams,type:stream_group_components:stream_group_stream=index,id:stream_group_stream_disposition"

FATE_IAMF += $(FATE_IAMF-yes)
FATE_IAMF_SAMPLES += $(FATE_IAMF_SAMPLES-yes)

FATE_FFMPEG_FFPROBE += $(FATE_IAMF)
FATE_SAMPLES_FFMPEG_FFPROBE += $(FATE_IAMF_SAMPLES)

fate-iamf: $(FATE_IAMF) $(FATE_IAMF_SAMPLES)
