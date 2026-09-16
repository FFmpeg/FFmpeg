FATE_MP3 += fate-mp3-float-conf-compl
fate-mp3-float-conf-compl: CMD = ffmpeg -auto_conversion_filters -c:a mp3float -i $(TARGET_SAMPLES)/mp3-conformance/compl.bit -f f32le -
fate-mp3-float-conf-compl: REF = $(SAMPLES)/mp3-conformance/compl.f32

FATE_MP3 += fate-mp3-float-conf-he_32khz
fate-mp3-float-conf-he_32khz: CMD = ffmpeg -auto_conversion_filters -c:a mp3float -i $(TARGET_SAMPLES)/mp3-conformance/he_32khz.bit -af atrim=end_sample=171648 -f f32le -
fate-mp3-float-conf-he_32khz: REF = $(SAMPLES)/mp3-conformance/he_32khz.f32

FATE_MP3 += fate-mp3-float-conf-he_44khz
fate-mp3-float-conf-he_44khz: CMD = ffmpeg -auto_conversion_filters -c:a mp3float -i $(TARGET_SAMPLES)/mp3-conformance/he_44khz.bit -af atrim=end_sample=471168 -f f32le -
fate-mp3-float-conf-he_44khz: REF = $(SAMPLES)/mp3-conformance/he_44khz.f32

FATE_MP3 += fate-mp3-float-conf-he_48khz
fate-mp3-float-conf-he_48khz: CMD = ffmpeg -auto_conversion_filters -c:a mp3float -i $(TARGET_SAMPLES)/mp3-conformance/he_48khz.bit -af atrim=end_sample=171648 -f f32le -
fate-mp3-float-conf-he_48khz: REF = $(SAMPLES)/mp3-conformance/he_48khz.f32

FATE_MP3 += fate-mp3-float-conf-hecommon
fate-mp3-float-conf-hecommon: CMD = ffmpeg -auto_conversion_filters -c:a mp3float -i $(TARGET_SAMPLES)/mp3-conformance/hecommon.bit -af atrim=end_sample=33408 -f f32le -
fate-mp3-float-conf-hecommon: REF = $(SAMPLES)/mp3-conformance/hecommon.f32

FATE_MP3 += fate-mp3-float-conf-si
fate-mp3-float-conf-si: CMD = ffmpeg -auto_conversion_filters -c:a mp3float -i $(TARGET_SAMPLES)/mp3-conformance/si.bit -af atrim=end_sample=134784 -f f32le -
fate-mp3-float-conf-si: REF = $(SAMPLES)/mp3-conformance/si.f32

FATE_MP3 += fate-mp3-float-conf-si_block
fate-mp3-float-conf-si_block: CMD = ffmpeg -auto_conversion_filters -c:a mp3float -i $(TARGET_SAMPLES)/mp3-conformance/si_block.bit -af atrim=end_sample=72576 -f f32le -
fate-mp3-float-conf-si_block: REF = $(SAMPLES)/mp3-conformance/si_block.f32

FATE_MP3 += fate-mp3-float-extra_overread
fate-mp3-float-extra_overread: CMD = ffmpeg -auto_conversion_filters -c:a mp3float -i $(TARGET_SAMPLES)/mpegaudio/extra_overread.mp3 -f f32le -
fate-mp3-float-extra_overread: REF = $(SAMPLES)/mpegaudio/extra_overread.f32

$(FATE_MP3): CMP = oneoff
$(FATE_MP3): CMP_UNIT = f32
$(FATE_MP3): FUZZ = 18

fate-mp3-float-extra_overread: FUZZ = 23

FATE_MP3-$(call DEMDEC, MP3, MP3FLOAT, ARESAMPLE_FILTER PIPE_PROTOCOL) += $(FATE_MP3)

FATE_SAMPLES_AVCONV += $(FATE_MP3-yes)
fate-mp3: $(FATE_MP3-yes)

MPA_HEADER_TESTBIN = libavcodec/tests/mpegaudiodecheader$(EXESUF)

FATE_MPA_HEADER-$(CONFIG_MPEGAUDIOHEADER) += fate-mpegaudiodecheader-hecommon
fate-mpegaudiodecheader-hecommon: $(MPA_HEADER_TESTBIN)
fate-mpegaudiodecheader-hecommon: CMD = run "$(MPA_HEADER_TESTBIN)" "$(TARGET_SAMPLES)/mp3-conformance/hecommon.bit"

FATE_MPA_HEADER-$(CONFIG_MPEGAUDIOHEADER) += fate-mpegaudiodecheader-bitrates
fate-mpegaudiodecheader-bitrates: $(MPA_HEADER_TESTBIN)
fate-mpegaudiodecheader-bitrates: CMD = run "$(MPA_HEADER_TESTBIN)" "$(TARGET_SAMPLES)/mp3-conformance/he_32khz.bit"

FATE_MPA_HEADER-$(CONFIG_MPEGAUDIOHEADER) += fate-mpegaudiodecheader-mpeg25
fate-mpegaudiodecheader-mpeg25: $(MPA_HEADER_TESTBIN)
fate-mpegaudiodecheader-mpeg25: CMD = run "$(MPA_HEADER_TESTBIN)" "$(TARGET_SAMPLES)/id3v2/lang_xxx.mp3"

FATE_MPA_HEADER-$(CONFIG_MPEGAUDIOHEADER) += fate-mpegaudiodecheader-joint-stereo
fate-mpegaudiodecheader-joint-stereo: $(MPA_HEADER_TESTBIN)
fate-mpegaudiodecheader-joint-stereo: CMD = run "$(MPA_HEADER_TESTBIN)" "$(TARGET_SAMPLES)/mp3-conformance/sin1k0db.bit"

FATE_MPA_HEADER-$(CONFIG_MPEGAUDIOHEADER) += fate-mpegaudiodecheader-xing
fate-mpegaudiodecheader-xing: $(MPA_HEADER_TESTBIN)
fate-mpegaudiodecheader-xing: CMD = run "$(MPA_HEADER_TESTBIN)" "$(TARGET_SAMPLES)/exif/embedded_small.mp3"

FATE_MPA_HEADER-$(call ALLYES, MPEGAUDIOHEADER MP3_DEMUXER MP3FLOAT_DECODER ARESAMPLE_FILTER MP2_ENCODER MP2_MUXER FILE_PROTOCOL) += fate-mpegaudiodecheader-mp2
fate-mpegaudiodecheader-mp2: $(MPA_HEADER_TESTBIN)
fate-mpegaudiodecheader-mp2: CMD = run_with_temp "$(FFMPEG) -nostdin -hide_banner -loglevel error -i $(TARGET_SAMPLES)/mp3-conformance/compl.bit -ar 24000 -c:a mp2 -fflags +bitexact -f mp2 -y" "$(MPA_HEADER_TESTBIN)" mp2

FATE_MPA_HEADER-$(call REMUX, MP3, MPEGAUDIOHEADER) += fate-mpegaudiodecheader-mp3-remux
fate-mpegaudiodecheader-mp3-remux: $(MPA_HEADER_TESTBIN)
fate-mpegaudiodecheader-mp3-remux: CMD = run_with_temp "$(FFMPEG) -nostdin -hide_banner -loglevel error -i $(TARGET_SAMPLES)/exif/embedded_small.mp3 -map 0:a -c copy -fflags +bitexact -f mp3 -y" "$(MPA_HEADER_TESTBIN)" mp3

FATE_MPA_HEADER_LAME-$(call ENCDEC, LIBMP3LAME PCM_S16LE, MP3 WAV, MPEGAUDIOHEADER PAN_FILTER ARESAMPLE_FILTER) += fate-mpegaudiodecheader-libmp3lame
fate-mpegaudiodecheader-libmp3lame: tests/data/asynth-44100-2.wav $(MPA_HEADER_TESTBIN)
fate-mpegaudiodecheader-libmp3lame: CMD = run_with_temp "$(FFMPEG) -nostdin -hide_banner -loglevel error -i $(TARGET_PATH)/tests/data/asynth-44100-2.wav -af pan=stereo|c0=c0|c1=c0 -c:a libmp3lame -b:a 128k -fflags +bitexact -f mp3 -y" "$(MPA_HEADER_TESTBIN)" mp3

FATE_SAMPLES_FFMPEG += $(FATE_MPA_HEADER-yes)
FATE_FFMPEG += $(FATE_MPA_HEADER_LAME-yes)
fate-mpegaudiodecheader: $(FATE_MPA_HEADER-yes) $(FATE_MPA_HEADER_LAME-yes)
