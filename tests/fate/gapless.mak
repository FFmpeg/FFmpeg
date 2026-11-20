FATE_GAPLESS-$(call FRAMECRC, MP3, MP3, ARESAMPLE_FILTER WAV_MUXER MD5_PROTOCOL) += fate-gapless-mp3
fate-gapless-mp3: CMD = gapless $(TARGET_SAMPLES)/gapless/gapless.mp3 "-c:a mp3"

FATE_GAPLESSINFO_PROBE-$(call ALLYES, MP3_DEMUXER MP3FLOAT_DECODER FILE_PROTOCOL) += fate-gapless-mp3-side-data
fate-gapless-mp3-side-data: CMD = ffprobe_demux $(TARGET_SAMPLES)/gapless/gapless.mp3

# gapless.mp3 with a 32 KiB APE tag inserted before its ID3v1 tag, at byte
# 250136: the tags exceed 1/16 of the Info tag byte count. That is not a
# concatenated file: the duration still comes from the Info tag's frame count,
# and the Info tag CRC, which the tail scan must not break, still validates
# (encoder tag).
tests/data/gapless-trailing-ape.mp3: TAG = GEN
tests/data/gapless-trailing-ape.mp3: $(SAMPLES)/gapless/gapless.mp3 | tests/data
	$(M){ dd if=$< bs=250136 count=1 && \
	  printf '\000\200\000\000\000\000\000\000Comment\000%032768d' 0 && \
	  printf 'APETAGEX\320\007\000\000\060\200\000\000\001\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000' && \
	  dd if=$< bs=1 skip=250136; } 2>/dev/null > $@

FATE_GAPLESSINFO_PROBE-$(call ALLYES, MP3_DEMUXER MP3FLOAT_DECODER FILE_PROTOCOL) += fate-gapless-mp3-trailing-ape
fate-gapless-mp3-trailing-ape: tests/data/gapless-trailing-ape.mp3
fate-gapless-mp3-trailing-ape: CMD = probe -show_entries stream=start_time,duration_ts,duration:stream_tags=encoder:format=duration $(TARGET_PATH)/tests/data/gapless-trailing-ape.mp3

# The trailing padding of this sample spans two packets. The duration of the
# remuxed file has to stay the one fate-gapless-mp3-side-data reports.
FATE_GAPLESSENC_PROBE-$(call ALLYES, MP3_DEMUXER MP3_MUXER NULL_MUXER) += fate-gapless-mp3-remux
fate-gapless-mp3-remux: CMD = transcode mp3 $(TARGET_SAMPLES)/gapless/gapless.mp3 mp3 "-c copy" "-c copy" "-of compact -show_entries stream=start_pts,duration_ts" "" "" "" null

FATE_GAPLESS-$(call FRAMECRC, MP3, MP3, ARESAMPLE_FILTER WAV_MUXER MD5_PROTOCOL) += fate-gapless-mp3-itunes
fate-gapless-mp3-itunes: CMD = gapless $(TARGET_SAMPLES)/gapless/gapless-itunes.mp3 "-c:a mp3"

FATE_GAPLESSINFO_PROBE-$(call ALLYES, MP3_DEMUXER FILE_PROTOCOL) += fate-gapless-mp3-itunes-side-data
fate-gapless-mp3-itunes-side-data: CMD = ffprobe_demux $(TARGET_SAMPLES)/gapless/gapless-itunes.mp3 -show_entries packet=pts:packet_side_data:stream=codec_name,start_pts,start_time,duration_ts,duration:format=start_time,duration

# The iTunSMPB priming and padding of this sample have to survive a remux,
# which stores them in the LAME tag.
FATE_GAPLESSENC_PROBE-$(call ALLYES, MP3_DEMUXER MP3_MUXER NULL_MUXER) += fate-gapless-mp3-itunes-remux
fate-gapless-mp3-itunes-remux: CMD = transcode mp3 $(TARGET_SAMPLES)/gapless/gapless-itunes.mp3 mp3 "-c copy" "-c copy" "-of compact -show_entries stream=start_pts,duration_ts" "" "" "" null

# Only the start is checked, as the encoded data and the end of the stream
# depend on the shine version.
FATE_GAPLESSENC_LOCAL-$(call ALLYES, FFMPEG FFPROBE FILE_PROTOCOL LIBSHINE_ENCODER MP3_MUXER MP3_DEMUXER WAV_DEMUXER PCM_S16LE_DECODER ARESAMPLE_FILTER) += fate-gaplessenc-pcm-to-mp3-libshine
fate-gaplessenc-pcm-to-mp3-libshine: tests/data/asynth-44100-2.wav
fate-gaplessenc-pcm-to-mp3-libshine: CMD = enc_external tests/data/asynth-44100-2.wav mp3 "-af aresample -c:a libshine" "-of compact -show_entries stream=start_pts"

FATE_GAPLESS-$(call DEMDEC, MP3, MP3, ARESAMPLE_FILTER WAV_MUXER) += fate-audiomatch-square-mp3
fate-audiomatch-square-mp3: CMD = audio_match $(TARGET_SAMPLES)/audiomatch/square3.mp3 $(SAMPLES)/audiomatch/square3.wav

FATE_GAPLESS-$(call TRANSCODE, PCM_S16LE AAC, WAV MOV, ARESAMPLE_FILTER) += fate-audiomatch-square-aac
FATE_GAPLESS-$(call TRANSCODE, PCM_S16LE AAC, WAV MOV, AAC_DEMUXER ARESAMPLE_FILTER) += fate-audiomatch-afconvert-16000-mono-lc-adts    fate-audiomatch-afconvert-16000-mono-lc-m4a
FATE_GAPLESS-$(call TRANSCODE, PCM_S16LE AAC, WAV MOV, AAC_DEMUXER ARESAMPLE_FILTER) += fate-audiomatch-afconvert-44100-mono-lc-adts    fate-audiomatch-afconvert-44100-mono-lc-m4a
FATE_GAPLESS-$(call TRANSCODE, PCM_S16LE AAC, WAV MOV, AAC_DEMUXER ARESAMPLE_FILTER) += fate-audiomatch-afconvert-16000-mono-he-adts    fate-audiomatch-afconvert-16000-mono-he-m4a
FATE_GAPLESS-$(call TRANSCODE, PCM_S16LE AAC, WAV MOV, AAC_DEMUXER ARESAMPLE_FILTER) += fate-audiomatch-afconvert-44100-mono-he-adts    fate-audiomatch-afconvert-44100-mono-he-m4a
FATE_GAPLESS-$(call TRANSCODE, PCM_S16LE AAC, WAV MOV, AAC_DEMUXER ARESAMPLE_FILTER) += fate-audiomatch-afconvert-16000-stereo-he-adts  fate-audiomatch-afconvert-16000-stereo-he-m4a
FATE_GAPLESS-$(call TRANSCODE, PCM_S16LE AAC, WAV MOV, AAC_DEMUXER ARESAMPLE_FILTER) += fate-audiomatch-afconvert-44100-stereo-he-adts  fate-audiomatch-afconvert-44100-stereo-he-m4a
FATE_GAPLESS-$(call TRANSCODE, PCM_S16LE AAC, WAV MOV, AAC_DEMUXER ARESAMPLE_FILTER) += fate-audiomatch-afconvert-16000-stereo-he2-adts fate-audiomatch-afconvert-16000-stereo-he2-m4a
FATE_GAPLESS-$(call TRANSCODE, PCM_S16LE AAC, WAV MOV, AAC_DEMUXER ARESAMPLE_FILTER) += fate-audiomatch-afconvert-44100-stereo-he2-adts fate-audiomatch-afconvert-44100-stereo-he2-m4a
FATE_GAPLESS-$(call TRANSCODE, PCM_S16LE AAC, WAV MOV, AAC_DEMUXER ARESAMPLE_FILTER) += fate-audiomatch-afconvert-16000-stereo-lc-adts  fate-audiomatch-afconvert-16000-stereo-lc-m4a
FATE_GAPLESS-$(call TRANSCODE, PCM_S16LE AAC, WAV MOV, AAC_DEMUXER ARESAMPLE_FILTER) += fate-audiomatch-afconvert-44100-stereo-lc-adts  fate-audiomatch-afconvert-44100-stereo-lc-m4a

FATE_GAPLESS-$(call TRANSCODE, PCM_S16LE AAC, WAV MOV, AAC_DEMUXER ARESAMPLE_FILTER) += fate-audiomatch-faac-16000-mono-lc-adts    fate-audiomatch-faac-16000-mono-lc-m4a
FATE_GAPLESS-$(call TRANSCODE, PCM_S16LE AAC, WAV MOV, AAC_DEMUXER ARESAMPLE_FILTER) += fate-audiomatch-faac-44100-mono-lc-adts    fate-audiomatch-faac-44100-mono-lc-m4a
FATE_GAPLESS-$(call TRANSCODE, PCM_S16LE AAC, WAV MOV, AAC_DEMUXER ARESAMPLE_FILTER) += fate-audiomatch-faac-16000-stereo-lc-adts  fate-audiomatch-faac-16000-stereo-lc-m4a
FATE_GAPLESS-$(call TRANSCODE, PCM_S16LE AAC, WAV MOV, AAC_DEMUXER ARESAMPLE_FILTER) += fate-audiomatch-faac-44100-stereo-lc-adts  fate-audiomatch-faac-44100-stereo-lc-m4a

FATE_GAPLESS-$(call TRANSCODE, PCM_S16LE AAC, WAV MOV, ARESAMPLE_FILTER) += fate-audiomatch-dolby-44100-mono-lc-mp4
FATE_GAPLESS-$(call TRANSCODE, PCM_S16LE AAC, WAV MOV, ARESAMPLE_FILTER) += fate-audiomatch-dolby-44100-mono-he-mp4
FATE_GAPLESS-$(call TRANSCODE, PCM_S16LE AAC, WAV MOV, ARESAMPLE_FILTER) += fate-audiomatch-dolby-44100-stereo-he-mp4
FATE_GAPLESS-$(call TRANSCODE, PCM_S16LE AAC, WAV MOV, ARESAMPLE_FILTER) += fate-audiomatch-dolby-44100-stereo-he2-mp4
FATE_GAPLESS-$(call TRANSCODE, PCM_S16LE AAC, WAV MOV, ARESAMPLE_FILTER) += fate-audiomatch-dolby-44100-stereo-lc-mp4

FATE_GAPLESS-$(call TRANSCODE, PCM_S16LE AAC, WAV MOV, ARESAMPLE_FILTER) += fate-audiomatch-fdkaac-44100-stereo-he-m4a
FATE_GAPLESS-$(call TRANSCODE, PCM_S16LE AAC, WAV MOV, ARESAMPLE_FILTER) += fate-audiomatch-fdkaac-44100-stereo-he2-m4a
FATE_GAPLESS-$(call TRANSCODE, PCM_S16LE AAC, WAV MOV, ARESAMPLE_FILTER) += fate-audiomatch-fdkaac-44100-stereo-lc-m4a

FATE_GAPLESS-$(call TRANSCODE, PCM_S16LE AAC, WAV MOV, ARESAMPLE_FILTER) += fate-audiomatch-nero-16000-mono-lc-m4a
FATE_GAPLESS-$(call TRANSCODE, PCM_S16LE AAC, WAV MOV, ARESAMPLE_FILTER) += fate-audiomatch-nero-44100-mono-lc-m4a
FATE_GAPLESS-$(call TRANSCODE, PCM_S16LE AAC, WAV MOV, ARESAMPLE_FILTER) += fate-audiomatch-nero-16000-mono-he-m4a
FATE_GAPLESS-$(call TRANSCODE, PCM_S16LE AAC, WAV MOV, ARESAMPLE_FILTER) += fate-audiomatch-nero-44100-mono-he-m4a
FATE_GAPLESS-$(call TRANSCODE, PCM_S16LE AAC, WAV MOV, ARESAMPLE_FILTER) += fate-audiomatch-nero-16000-stereo-he-m4a
FATE_GAPLESS-$(call TRANSCODE, PCM_S16LE AAC, WAV MOV, ARESAMPLE_FILTER) += fate-audiomatch-nero-44100-stereo-he-m4a
FATE_GAPLESS-$(call TRANSCODE, PCM_S16LE AAC, WAV MOV, ARESAMPLE_FILTER) += fate-audiomatch-nero-16000-stereo-he2-m4a
FATE_GAPLESS-$(call TRANSCODE, PCM_S16LE AAC, WAV MOV, ARESAMPLE_FILTER) += fate-audiomatch-nero-44100-stereo-he2-m4a
FATE_GAPLESS-$(call TRANSCODE, PCM_S16LE AAC, WAV MOV, ARESAMPLE_FILTER) += fate-audiomatch-nero-16000-stereo-lc-m4a
FATE_GAPLESS-$(call TRANSCODE, PCM_S16LE AAC, WAV MOV, ARESAMPLE_FILTER) += fate-audiomatch-nero-44100-stereo-lc-m4a

FATE_GAPLESS-$(call TRANSCODE, PCM_S16LE OPUS, WAV OGG, ARESAMPLE_FILTER) += fate-audiomatch-opus-48000-stereo-opus
FATE_GAPLESS-$(call TRANSCODE, PCM_S16LE OPUS, WAV MATROSKA, ARESAMPLE_FILTER) += fate-audiomatch-opus-48000-stereo-mka

FATE_GAPLESS-$(call TRANSCODE, PCM_S16LE AAC, WAV MOV, ARESAMPLE_FILTER) += fate-audiomatch-quicktime7-44100-stereo-lc-mp4 fate-audiomatch-quicktimeX-44100-stereo-lc-m4a

fate-audiomatch-square-aac: CMD = audio_match $(TARGET_SAMPLES)/audiomatch/square3.m4a $(SAMPLES)/audiomatch/square3.wav

fate-audiomatch-afconvert-16000-mono-lc-adts: CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_afconvert_16000_mono_aac_lc.adts  $(SAMPLES)/audiomatch/tones_16000_mono.wav
fate-audiomatch-afconvert-16000-mono-lc-m4a:  CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_afconvert_16000_mono_aac_lc.m4a   $(SAMPLES)/audiomatch/tones_16000_mono.wav
fate-audiomatch-afconvert-16000-mono-he-adts: CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_afconvert_16000_mono_aac_he.adts  $(SAMPLES)/audiomatch/tones_16000_mono.wav "-ac 1 -ar 16000"
fate-audiomatch-afconvert-16000-mono-he-m4a:  CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_afconvert_16000_mono_aac_he.m4a   $(SAMPLES)/audiomatch/tones_16000_mono.wav "-ac 1 -ar 16000"
fate-audiomatch-afconvert-16000-stereo-lc-adts: CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_afconvert_16000_stereo_aac_lc.adts  $(SAMPLES)/audiomatch/tones_16000_stereo.wav
fate-audiomatch-afconvert-16000-stereo-lc-m4a:  CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_afconvert_16000_stereo_aac_lc.m4a   $(SAMPLES)/audiomatch/tones_16000_stereo.wav
fate-audiomatch-afconvert-16000-stereo-he-adts: CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_afconvert_16000_stereo_aac_he.adts  $(SAMPLES)/audiomatch/tones_16000_stereo.wav "-ar 16000"
fate-audiomatch-afconvert-16000-stereo-he-m4a:  CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_afconvert_16000_stereo_aac_he.m4a   $(SAMPLES)/audiomatch/tones_16000_stereo.wav "-ar 16000"
fate-audiomatch-afconvert-16000-stereo-he2-adts:CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_afconvert_16000_stereo_aac_he2.adts $(SAMPLES)/audiomatch/tones_16000_stereo.wav "-ar 16000"
fate-audiomatch-afconvert-16000-stereo-he2-m4a: CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_afconvert_16000_stereo_aac_he2.m4a  $(SAMPLES)/audiomatch/tones_16000_stereo.wav "-ar 16000"
fate-audiomatch-afconvert-44100-mono-lc-adts: CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_afconvert_44100_mono_aac_lc.adts  $(SAMPLES)/audiomatch/tones_44100_mono.wav
fate-audiomatch-afconvert-44100-mono-lc-m4a:  CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_afconvert_44100_mono_aac_lc.m4a   $(SAMPLES)/audiomatch/tones_44100_mono.wav
fate-audiomatch-afconvert-44100-mono-he-adts: CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_afconvert_44100_mono_aac_he.adts  $(SAMPLES)/audiomatch/tones_44100_mono.wav "-ac 1"
fate-audiomatch-afconvert-44100-mono-he-m4a:  CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_afconvert_44100_mono_aac_he.m4a   $(SAMPLES)/audiomatch/tones_44100_mono.wav "-ac 1"
fate-audiomatch-afconvert-44100-stereo-lc-adts: CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_afconvert_44100_stereo_aac_lc.adts  $(SAMPLES)/audiomatch/tones_44100_stereo.wav
fate-audiomatch-afconvert-44100-stereo-lc-m4a:  CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_afconvert_44100_stereo_aac_lc.m4a   $(SAMPLES)/audiomatch/tones_44100_stereo.wav
fate-audiomatch-afconvert-44100-stereo-he-adts: CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_afconvert_44100_stereo_aac_he.adts  $(SAMPLES)/audiomatch/tones_44100_stereo.wav
fate-audiomatch-afconvert-44100-stereo-he-m4a:  CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_afconvert_44100_stereo_aac_he.m4a   $(SAMPLES)/audiomatch/tones_44100_stereo.wav
fate-audiomatch-afconvert-44100-stereo-he2-adts:CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_afconvert_44100_stereo_aac_he2.adts $(SAMPLES)/audiomatch/tones_44100_stereo.wav
fate-audiomatch-afconvert-44100-stereo-he2-m4a: CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_afconvert_44100_stereo_aac_he2.m4a  $(SAMPLES)/audiomatch/tones_44100_stereo.wav

fate-audiomatch-dolby-44100-mono-lc-mp4:  CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_dolby_44100_mono_aac_lc.mp4   $(SAMPLES)/audiomatch/tones_44100_mono.wav
fate-audiomatch-dolby-44100-mono-he-mp4:  CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_dolby_44100_mono_aac_he.mp4   $(SAMPLES)/audiomatch/tones_44100_mono.wav "-ac 1"
fate-audiomatch-dolby-44100-stereo-lc-mp4:  CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_dolby_44100_stereo_aac_lc.mp4   $(SAMPLES)/audiomatch/tones_44100_stereo.wav
fate-audiomatch-dolby-44100-stereo-he-mp4:  CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_dolby_44100_stereo_aac_he.mp4   $(SAMPLES)/audiomatch/tones_44100_stereo.wav
fate-audiomatch-dolby-44100-stereo-he2-mp4: CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_dolby_44100_stereo_aac_he2.mp4  $(SAMPLES)/audiomatch/tones_44100_stereo.wav

fate-audiomatch-faac-16000-mono-lc-adts: CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_faac_16000_mono_aac_lc.adts  $(SAMPLES)/audiomatch/tones_16000_mono.wav
fate-audiomatch-faac-16000-mono-lc-m4a:  CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_faac_16000_mono_aac_lc.m4a   $(SAMPLES)/audiomatch/tones_16000_mono.wav
fate-audiomatch-faac-16000-stereo-lc-adts: CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_faac_16000_stereo_aac_lc.adts  $(SAMPLES)/audiomatch/tones_16000_stereo.wav
fate-audiomatch-faac-16000-stereo-lc-m4a:  CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_faac_16000_stereo_aac_lc.m4a   $(SAMPLES)/audiomatch/tones_16000_stereo.wav
fate-audiomatch-faac-44100-mono-lc-adts: CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_faac_44100_mono_aac_lc.adts  $(SAMPLES)/audiomatch/tones_44100_mono.wav
fate-audiomatch-faac-44100-mono-lc-m4a:  CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_faac_44100_mono_aac_lc.m4a   $(SAMPLES)/audiomatch/tones_44100_mono.wav
fate-audiomatch-faac-44100-stereo-lc-adts: CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_faac_44100_stereo_aac_lc.adts  $(SAMPLES)/audiomatch/tones_44100_stereo.wav
fate-audiomatch-faac-44100-stereo-lc-m4a:  CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_faac_44100_stereo_aac_lc.m4a   $(SAMPLES)/audiomatch/tones_44100_stereo.wav

fate-audiomatch-fdkaac-44100-stereo-lc-m4a:  CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_fdkaac_44100_stereo_aac_lc.m4a   $(SAMPLES)/audiomatch/tones_44100_stereo.wav
fate-audiomatch-fdkaac-44100-stereo-he-m4a:  CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_fdkaac_44100_stereo_aac_he.m4a   $(SAMPLES)/audiomatch/tones_44100_stereo.wav
fate-audiomatch-fdkaac-44100-stereo-he2-m4a: CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_fdkaac_44100_stereo_aac_he2.m4a  $(SAMPLES)/audiomatch/tones_44100_stereo.wav

fate-audiomatch-nero-16000-mono-lc-m4a:  CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_nero_16000_mono_aac_lc.m4a   $(SAMPLES)/audiomatch/tones_16000_mono.wav
fate-audiomatch-nero-16000-mono-he-m4a:  CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_nero_16000_mono_aac_he.m4a   $(SAMPLES)/audiomatch/tones_16000_mono.wav
fate-audiomatch-nero-16000-stereo-lc-m4a:  CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_nero_16000_stereo_aac_lc.m4a   $(SAMPLES)/audiomatch/tones_16000_stereo.wav
fate-audiomatch-nero-16000-stereo-he-m4a:  CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_nero_16000_stereo_aac_he.m4a   $(SAMPLES)/audiomatch/tones_16000_stereo.wav
fate-audiomatch-nero-16000-stereo-he2-m4a: CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_nero_16000_stereo_aac_he2.m4a  $(SAMPLES)/audiomatch/tones_16000_stereo.wav
fate-audiomatch-nero-44100-mono-lc-m4a:  CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_nero_44100_mono_aac_lc.m4a   $(SAMPLES)/audiomatch/tones_44100_mono.wav
fate-audiomatch-nero-44100-mono-he-m4a:  CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_nero_44100_mono_aac_he.m4a   $(SAMPLES)/audiomatch/tones_44100_mono.wav
fate-audiomatch-nero-44100-stereo-lc-m4a:  CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_nero_44100_stereo_aac_lc.m4a   $(SAMPLES)/audiomatch/tones_44100_stereo.wav
fate-audiomatch-nero-44100-stereo-he-m4a:  CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_nero_44100_stereo_aac_he.m4a   $(SAMPLES)/audiomatch/tones_44100_stereo.wav
fate-audiomatch-nero-44100-stereo-he2-m4a: CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_nero_44100_stereo_aac_he2.m4a  $(SAMPLES)/audiomatch/tones_44100_stereo.wav

fate-audiomatch-opus-48000-stereo-opus: CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_opus_48000_stereo.opus  $(SAMPLES)/audiomatch/tones_44100_stereo.wav "-ar 44100"
fate-audiomatch-opus-48000-stereo-mka:  CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_opus_48000_stereo.mka   $(SAMPLES)/audiomatch/tones_44100_stereo.wav "-ar 44100"

fate-audiomatch-quicktime7-44100-stereo-lc-mp4: CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_quicktime7_44100_stereo_aac_lc.mp4  $(SAMPLES)/audiomatch/tones_44100_stereo.wav
fate-audiomatch-quicktimeX-44100-stereo-lc-m4a: CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_quicktimeX_44100_stereo_aac_lc.m4a  $(SAMPLES)/audiomatch/tones_44100_stereo.wav


FATE_GAPLESS = $(FATE_GAPLESS-yes)

FATE_GAPLESSINFO_PROBE-$(call DEMDEC, MOV, AAC) += fate-gaplessinfo-itunes1
fate-gaplessinfo-itunes1: CMD = probegaplessinfo $(TARGET_SAMPLES)/cover_art/Owner-iTunes_9.0.3.15.m4a

FATE_GAPLESSINFO_PROBE-$(call DEMDEC, MOV, AAC) += fate-gaplessinfo-itunes2
fate-gaplessinfo-itunes2: CMD = probegaplessinfo $(TARGET_SAMPLES)/gapless/102400samples_qt-lc-aac.m4a

FATE_GAPLESSENC_PROBE-$(call ENCDEC, AAC, IPOD MOV, ARESAMPLE_FILTER) += fate-gaplessenc-itunes-to-ipod-aac
fate-gaplessenc-itunes-to-ipod-aac: CMD = gaplessenc $(TARGET_SAMPLES)/gapless/102400samples_qt-lc-aac.m4a ipod aac

FATE_GAPLESSENC_PROBE-$(call ENCDEC, AAC, MOV, PCM_S16LE_DEMUXER PCM_S16LE_DECODER ARESAMPLE_FILTER) += fate-gaplessenc-pcm-to-mov-aac
fate-gaplessenc-pcm-to-mov-aac: $(AREF)
fate-gaplessenc-pcm-to-mov-aac: CMD = gaplessenc $(AREF) mov aac

FATE_GAPLESSINFO-$(CONFIG_FFPROBE) = $(FATE_GAPLESSINFO_PROBE-yes)
FATE_GAPLESSINFO = $(FATE_GAPLESSINFO-yes)

FATE_GAPLESSENC-$(CONFIG_FFPROBE) = $(FATE_GAPLESSENC_PROBE-yes)
FATE_GAPLESSENC = $(FATE_GAPLESSENC-yes)

FATE_SAMPLES_AVCONV += $(FATE_GAPLESS)
FATE_SAMPLES_FFMPEG_FFPROBE += $(FATE_GAPLESSENC)
FATE_SAMPLES_FFPROBE += $(FATE_GAPLESSINFO)
FATE_FFMPEG_FFPROBE += $(FATE_GAPLESSENC_LOCAL-yes)

fate-gapless: $(FATE_GAPLESS) $(FATE_GAPLESSINFO) $(FATE_GAPLESSENC) $(FATE_GAPLESSENC_LOCAL-yes)
