FATE_WAV_SAMPLES-$(CONFIG_WAV_DEMUXER) += fate-wav-bad-avg-byterate
fate-wav-bad-avg-byterate: libavformat/tests/seek$(EXESUF)
fate-wav-bad-avg-byterate: CMD = run libavformat/tests/seek$(EXESUF) $(TARGET_SAMPLES)/wav/wrong-avg-byterate.wav -seekback 500 -stream_id 0

FATE_WAV_ENCINFO-$(CONFIG_ADPCM_MS_ENCODER) += fate-wav-enc-adpcm-ms-encinfo
fate-wav-enc-adpcm-ms-encinfo: libavcodec/tests/encinfo$(EXESUF)
fate-wav-enc-adpcm-ms-encinfo: CMD = run libavcodec/tests/encinfo$(EXESUF) adpcm_ms

FATE_WAV_ENCINFO-$(CONFIG_ADPCM_IMA_WAV_ENCODER) += fate-wav-enc-adpcm-ima-wav-encinfo
fate-wav-enc-adpcm-ima-wav-encinfo: libavcodec/tests/encinfo$(EXESUF)
fate-wav-enc-adpcm-ima-wav-encinfo: CMD = run libavcodec/tests/encinfo$(EXESUF) adpcm_ima_wav

FATE_WAV_BITRATE-$(call ALLYES, ADPCM_MS_ENCODER PCM_S16LE_DECODER WAV_MUXER WAV_DEMUXER FILE_PROTOCOL) += fate-wav-enc-adpcm-ms-bitrate
fate-wav-enc-adpcm-ms-bitrate: tests/data/asynth-44100-2.wav
fate-wav-enc-adpcm-ms-bitrate: CMD = run_with_temp \
    "$(FFMPEG) -nostdin -hide_banner -loglevel quiet \
    -i $(TARGET_PATH)/tests/data/asynth-44100-2.wav -c:a adpcm_ms -y" \
    "ffprobe$(PROGSSUF)$(EXESUF) -v 0 -bitexact \
    -show_entries stream=codec_name,bit_rate:format=duration -print_format default=nk=1" \
    wav

FATE_WAV_BITRATE-$(call ALLYES, ADPCM_IMA_WAV_ENCODER PCM_S16LE_DECODER WAV_MUXER WAV_DEMUXER FILE_PROTOCOL) += fate-wav-enc-adpcm-ima-wav-bitrate
fate-wav-enc-adpcm-ima-wav-bitrate: tests/data/asynth-44100-2.wav
fate-wav-enc-adpcm-ima-wav-bitrate: CMD = run_with_temp \
    "$(FFMPEG) -nostdin -hide_banner -loglevel quiet \
    -i $(TARGET_PATH)/tests/data/asynth-44100-2.wav -c:a adpcm_ima_wav -y" \
    "ffprobe$(PROGSSUF)$(EXESUF) -v 0 -bitexact \
    -show_entries stream=codec_name,bit_rate:format=duration -print_format default=nk=1" \
    wav

WAV_BITRATE_PROBE = ffprobe$(PROGSSUF)$(EXESUF) -v 0 -bitexact \
    -show_entries stream=codec_name,bit_rate -print_format default=nk=1

# nAvgBytesPerSec wrong (too low), block_align correct:  bit_rate must be corrected.
# nAvgBytesPerSec correct, block_align too small:        bit_rate must be preserved.
# nAvgBytesPerSec correct, block_align too large:        bit_rate must be preserved.

FATE_WAV_BITRATE-$(call ALLYES, WAV_MUXER WAV_DEMUXER FILE_PROTOCOL) += fate-wav-pcm-s16le-wrong-avg-byterate
fate-wav-pcm-s16le-wrong-avg-byterate: tests/data/asynth-44100-2.wav
fate-wav-pcm-s16le-wrong-avg-byterate: CMD = run_with_patched_temp \
    "$(FFMPEG) -nostdin -loglevel quiet \
    -i $(TARGET_PATH)/tests/data/asynth-44100-2.wav -c:a copy -fflags +bitexact -y" \
    "$(WAV_BITRATE_PROBE)" wav 28 "\0210\0130\0001\0000"

FATE_WAV_BITRATE-$(call ALLYES, WAV_MUXER WAV_DEMUXER FILE_PROTOCOL) += fate-wav-pcm-s16le-large-blockalign
fate-wav-pcm-s16le-large-blockalign: tests/data/asynth-44100-2.wav
fate-wav-pcm-s16le-large-blockalign: CMD = run_with_patched_temp \
    "$(FFMPEG) -nostdin -loglevel quiet \
    -i $(TARGET_PATH)/tests/data/asynth-44100-2.wav -c:a copy -fflags +bitexact -y" \
    "$(WAV_BITRATE_PROBE)" wav 32 "\0006\0000"

FATE_WAV_BITRATE-$(call ALLYES, WAV_MUXER WAV_DEMUXER FILE_PROTOCOL) += fate-wav-pcm-s16le-bad-blockalign
fate-wav-pcm-s16le-bad-blockalign: tests/data/asynth-44100-2.wav
fate-wav-pcm-s16le-bad-blockalign: CMD = run_with_patched_temp \
    "$(FFMPEG) -nostdin -loglevel quiet \
    -i $(TARGET_PATH)/tests/data/asynth-44100-2.wav -c:a copy -fflags +bitexact -y" \
    "$(WAV_BITRATE_PROBE)" wav 32 "\0002\0000"

FATE_WAV_BITRATE-$(call ALLYES, PCM_S16LE_DECODER PCM_U8_ENCODER WAV_MUXER WAV_DEMUXER FILE_PROTOCOL) += fate-wav-pcm-u8-wrong-avg-byterate
fate-wav-pcm-u8-wrong-avg-byterate: tests/data/asynth-44100-2.wav
fate-wav-pcm-u8-wrong-avg-byterate: CMD = run_with_patched_temp \
    "$(FFMPEG) -nostdin -loglevel quiet \
    -i $(TARGET_PATH)/tests/data/asynth-44100-2.wav -c:a pcm_u8 -fflags +bitexact -y" \
    "$(WAV_BITRATE_PROBE)" wav 28 "\0104\0254\0000\0000"

FATE_WAV_BITRATE-$(call ALLYES, PCM_S16LE_DECODER PCM_U8_ENCODER WAV_MUXER WAV_DEMUXER FILE_PROTOCOL) += fate-wav-pcm-u8-bad-blockalign
fate-wav-pcm-u8-bad-blockalign: tests/data/asynth-44100-2.wav
fate-wav-pcm-u8-bad-blockalign: CMD = run_with_patched_temp \
    "$(FFMPEG) -nostdin -loglevel quiet \
    -i $(TARGET_PATH)/tests/data/asynth-44100-2.wav -c:a pcm_u8 -fflags +bitexact -y" \
    "$(WAV_BITRATE_PROBE)" wav 32 "\0001\0000"

FATE_WAV_BITRATE-$(call ALLYES, PCM_S16LE_DECODER PCM_S24LE_ENCODER WAV_MUXER WAV_DEMUXER FILE_PROTOCOL) += fate-wav-pcm-s24le-wrong-avg-byterate
fate-wav-pcm-s24le-wrong-avg-byterate: tests/data/asynth-44100-2.wav
fate-wav-pcm-s24le-wrong-avg-byterate: CMD = run_with_patched_temp \
    "$(FFMPEG) -nostdin -loglevel quiet \
    -i $(TARGET_PATH)/tests/data/asynth-44100-2.wav -c:a pcm_s24le -fflags +bitexact -y" \
    "$(WAV_BITRATE_PROBE)" wav 28 "\0020\0261\0002\0000"

FATE_WAV_BITRATE-$(call ALLYES, PCM_S16LE_DECODER PCM_S24LE_ENCODER WAV_MUXER WAV_DEMUXER FILE_PROTOCOL) += fate-wav-pcm-s24le-bad-blockalign
fate-wav-pcm-s24le-bad-blockalign: tests/data/asynth-44100-2.wav
fate-wav-pcm-s24le-bad-blockalign: CMD = run_with_patched_temp \
    "$(FFMPEG) -nostdin -loglevel quiet \
    -i $(TARGET_PATH)/tests/data/asynth-44100-2.wav -c:a pcm_s24le -fflags +bitexact -y" \
    "$(WAV_BITRATE_PROBE)" wav 32 "\0002\0000"

FATE_WAV_BITRATE-$(call ALLYES, PCM_S16LE_DECODER PCM_ALAW_ENCODER WAV_MUXER WAV_DEMUXER FILE_PROTOCOL) += fate-wav-pcm-alaw-wrong-avg-byterate
fate-wav-pcm-alaw-wrong-avg-byterate: tests/data/asynth-44100-2.wav
fate-wav-pcm-alaw-wrong-avg-byterate: CMD = run_with_patched_temp \
    "$(FFMPEG) -nostdin -loglevel quiet \
    -i $(TARGET_PATH)/tests/data/asynth-44100-2.wav -c:a pcm_alaw -fflags +bitexact -y" \
    "$(WAV_BITRATE_PROBE)" wav 28 "\0104\0254\0000\0000"

FATE_WAV_BITRATE-$(call ALLYES, PCM_S16LE_DECODER PCM_ALAW_ENCODER WAV_MUXER WAV_DEMUXER FILE_PROTOCOL) += fate-wav-pcm-alaw-bad-blockalign
fate-wav-pcm-alaw-bad-blockalign: tests/data/asynth-44100-2.wav
fate-wav-pcm-alaw-bad-blockalign: CMD = run_with_patched_temp \
    "$(FFMPEG) -nostdin -loglevel quiet \
    -i $(TARGET_PATH)/tests/data/asynth-44100-2.wav -c:a pcm_alaw -fflags +bitexact -y" \
    "$(WAV_BITRATE_PROBE)" wav 32 "\0001\0000"

FATE_WAV_BITRATE-$(call ALLYES, PCM_S16LE_DECODER PCM_MULAW_ENCODER WAV_MUXER WAV_DEMUXER FILE_PROTOCOL) += fate-wav-pcm-mulaw-wrong-avg-byterate
fate-wav-pcm-mulaw-wrong-avg-byterate: tests/data/asynth-44100-2.wav
fate-wav-pcm-mulaw-wrong-avg-byterate: CMD = run_with_patched_temp \
    "$(FFMPEG) -nostdin -loglevel quiet \
    -i $(TARGET_PATH)/tests/data/asynth-44100-2.wav -c:a pcm_mulaw -fflags +bitexact -y" \
    "$(WAV_BITRATE_PROBE)" wav 28 "\0104\0254\0000\0000"

FATE_WAV_BITRATE-$(call ALLYES, PCM_S16LE_DECODER PCM_MULAW_ENCODER WAV_MUXER WAV_DEMUXER FILE_PROTOCOL) += fate-wav-pcm-mulaw-bad-blockalign
fate-wav-pcm-mulaw-bad-blockalign: tests/data/asynth-44100-2.wav
fate-wav-pcm-mulaw-bad-blockalign: CMD = run_with_patched_temp \
    "$(FFMPEG) -nostdin -loglevel quiet \
    -i $(TARGET_PATH)/tests/data/asynth-44100-2.wav -c:a pcm_mulaw -fflags +bitexact -y" \
    "$(WAV_BITRATE_PROBE)" wav 32 "\0001\0000"

FATE_EXTERN         += $(FATE_WAV_SAMPLES-yes)
FATE-yes            += $(FATE_WAV_ENCINFO-yes)
FATE_FFMPEG_FFPROBE += $(FATE_WAV_BITRATE-yes)

fate-wav: $(FATE_WAV_SAMPLES-yes) $(FATE_WAV_ENCINFO-yes) $(FATE_WAV_BITRATE-yes)
