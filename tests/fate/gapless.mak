FATE_GAPLESS-$(CONFIG_MP3_DEMUXER) += fate-gapless-mp3
fate-gapless-mp3: CMD = gapless $(TARGET_SAMPLES)/gapless/gapless.mp3 "-c:a mp3"

FATE_GAPLESSINFO_PROBE-$(CONFIG_MP3_DEMUXER) += fate-gapless-mp3-side-data
fate-gapless-mp3-side-data: CMD = ffprobe_demux $(TARGET_SAMPLES)/gapless/gapless.mp3

FATE_GAPLESS-$(CONFIG_MP3_DEMUXER) += fate-audiomatch-square-mp3
fate-audiomatch-square-mp3: CMD = audio_match $(TARGET_SAMPLES)/audiomatch/square3.mp3 $(SAMPLES)/audiomatch/square3.wav

FATE_GAPLESS-$(CONFIG_MOV_DEMUXER) += fate-audiomatch-square-aac
FATE_GAPLESS-$(CONFIG_MOV_DEMUXER) += fate-audiomatch-afconvert-16000-mono-lc-adts    fate-audiomatch-afconvert-16000-mono-lc-m4a
FATE_GAPLESS-$(CONFIG_MOV_DEMUXER) += fate-audiomatch-afconvert-44100-mono-lc-adts    fate-audiomatch-afconvert-44100-mono-lc-m4a
FATE_GAPLESS-$(CONFIG_MOV_DEMUXER) += fate-audiomatch-afconvert-16000-mono-he-adts    fate-audiomatch-afconvert-16000-mono-he-m4a
FATE_GAPLESS-$(CONFIG_MOV_DEMUXER) += fate-audiomatch-afconvert-44100-mono-he-adts    fate-audiomatch-afconvert-44100-mono-he-m4a
FATE_GAPLESS-$(CONFIG_MOV_DEMUXER) += fate-audiomatch-afconvert-16000-stereo-he-adts  fate-audiomatch-afconvert-16000-stereo-he-m4a
FATE_GAPLESS-$(CONFIG_MOV_DEMUXER) += fate-audiomatch-afconvert-44100-stereo-he-adts  fate-audiomatch-afconvert-44100-stereo-he-m4a
FATE_GAPLESS-$(CONFIG_MOV_DEMUXER) += fate-audiomatch-afconvert-16000-stereo-he2-adts fate-audiomatch-afconvert-16000-stereo-he2-m4a
FATE_GAPLESS-$(CONFIG_MOV_DEMUXER) += fate-audiomatch-afconvert-44100-stereo-he2-adts fate-audiomatch-afconvert-44100-stereo-he2-m4a
FATE_GAPLESS-$(CONFIG_MOV_DEMUXER) += fate-audiomatch-afconvert-16000-stereo-lc-adts  fate-audiomatch-afconvert-16000-stereo-lc-m4a
FATE_GAPLESS-$(CONFIG_MOV_DEMUXER) += fate-audiomatch-afconvert-44100-stereo-lc-adts  fate-audiomatch-afconvert-44100-stereo-lc-m4a

FATE_GAPLESS-$(CONFIG_MOV_DEMUXER) += fate-audiomatch-faac-16000-mono-lc-adts    fate-audiomatch-faac-16000-mono-lc-m4a
FATE_GAPLESS-$(CONFIG_MOV_DEMUXER) += fate-audiomatch-faac-44100-mono-lc-adts    fate-audiomatch-faac-44100-mono-lc-m4a
FATE_GAPLESS-$(CONFIG_MOV_DEMUXER) += fate-audiomatch-faac-16000-stereo-lc-adts  fate-audiomatch-faac-16000-stereo-lc-m4a
FATE_GAPLESS-$(CONFIG_MOV_DEMUXER) += fate-audiomatch-faac-44100-stereo-lc-adts  fate-audiomatch-faac-44100-stereo-lc-m4a

FATE_GAPLESS-$(CONFIG_MOV_DEMUXER) += fate-audiomatch-dolby-44100-mono-lc-mp4
FATE_GAPLESS-$(CONFIG_MOV_DEMUXER) += fate-audiomatch-dolby-44100-mono-he-mp4
FATE_GAPLESS-$(CONFIG_MOV_DEMUXER) += fate-audiomatch-dolby-44100-stereo-he-mp4
FATE_GAPLESS-$(CONFIG_MOV_DEMUXER) += fate-audiomatch-dolby-44100-stereo-he2-mp4
FATE_GAPLESS-$(CONFIG_MOV_DEMUXER) += fate-audiomatch-dolby-44100-stereo-lc-mp4

FATE_GAPLESS-$(CONFIG_MOV_DEMUXER) += fate-audiomatch-nero-16000-mono-lc-m4a
FATE_GAPLESS-$(CONFIG_MOV_DEMUXER) += fate-audiomatch-nero-44100-mono-lc-m4a
FATE_GAPLESS-$(CONFIG_MOV_DEMUXER) += fate-audiomatch-nero-16000-mono-he-m4a
FATE_GAPLESS-$(CONFIG_MOV_DEMUXER) += fate-audiomatch-nero-44100-mono-he-m4a
FATE_GAPLESS-$(CONFIG_MOV_DEMUXER) += fate-audiomatch-nero-16000-stereo-he-m4a
FATE_GAPLESS-$(CONFIG_MOV_DEMUXER) += fate-audiomatch-nero-44100-stereo-he-m4a
FATE_GAPLESS-$(CONFIG_MOV_DEMUXER) += fate-audiomatch-nero-16000-stereo-he2-m4a
FATE_GAPLESS-$(CONFIG_MOV_DEMUXER) += fate-audiomatch-nero-44100-stereo-he2-m4a
FATE_GAPLESS-$(CONFIG_MOV_DEMUXER) += fate-audiomatch-nero-16000-stereo-lc-m4a
FATE_GAPLESS-$(CONFIG_MOV_DEMUXER) += fate-audiomatch-nero-44100-stereo-lc-m4a

FATE_GAPLESS-$(CONFIG_MOV_DEMUXER) += fate-audiomatch-quicktime7-44100-stereo-lc-mp4 fate-audiomatch-quicktimeX-44100-stereo-lc-m4a

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

fate-audiomatch-quicktime7-44100-stereo-lc-mp4: CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_quicktime7_44100_stereo_aac_lc.mp4  $(SAMPLES)/audiomatch/tones_44100_stereo.wav
fate-audiomatch-quicktimeX-44100-stereo-lc-m4a: CMD = audio_match $(TARGET_SAMPLES)/audiomatch/tones_quicktimeX_44100_stereo_aac_lc.m4a  $(SAMPLES)/audiomatch/tones_44100_stereo.wav


FATE_GAPLESS = $(FATE_GAPLESS-yes)

FATE_GAPLESSINFO_PROBE-$(call DEMDEC, MOV, AAC) += fate-gaplessinfo-itunes1
fate-gaplessinfo-itunes1: ffprobe$(PROGSSUF)$(EXESUF)
fate-gaplessinfo-itunes1: CMD = probegaplessinfo $(TARGET_SAMPLES)/cover_art/Owner-iTunes_9.0.3.15.m4a

FATE_GAPLESSINFO_PROBE-$(call DEMDEC, MOV, AAC) += fate-gaplessinfo-itunes2
fate-gaplessinfo-itunes2: ffprobe$(PROGSSUF)$(EXESUF)
fate-gaplessinfo-itunes2: CMD = probegaplessinfo $(TARGET_SAMPLES)/gapless/102400samples_qt-lc-aac.m4a

FATE_GAPLESSENC_PROBE-$(call ENCDEC, AAC, MOV) += fate-gaplessenc-itunes-to-ipod-aac
fate-gaplessenc-itunes-to-ipod-aac: ffprobe$(PROGSSUF)$(EXESUF)
fate-gaplessenc-itunes-to-ipod-aac: CMD = gaplessenc $(TARGET_SAMPLES)/gapless/102400samples_qt-lc-aac.m4a ipod aac

FATE_GAPLESSENC_PROBE-$(call ENCDEC, AAC, MOV) += fate-gaplessenc-pcm-to-mov-aac
fate-gaplessenc-pcm-to-mov-aac: $(AREF)
fate-gaplessenc-pcm-to-mov-aac: ffprobe$(PROGSSUF)$(EXESUF)
fate-gaplessenc-pcm-to-mov-aac: CMD = gaplessenc $(AREF) mov aac

FATE_GAPLESSINFO-$(CONFIG_FFPROBE) = $(FATE_GAPLESSINFO_PROBE-yes)
FATE_GAPLESSINFO = $(FATE_GAPLESSINFO-yes)

FATE_GAPLESSENC-$(CONFIG_FFPROBE) = $(FATE_GAPLESSENC_PROBE-yes)
FATE_GAPLESSENC = $(FATE_GAPLESSENC-yes)

FATE_SAMPLES_AVCONV += $(FATE_GAPLESS)
FATE_SAMPLES_FFMPEG_FFPROBE += $(FATE_GAPLESSENC)
FATE_SAMPLES_FFPROBE += $(FATE_GAPLESSINFO)

fate-gapless: $(FATE_GAPLESS) $(FATE_GAPLESSINFO) $(FATE_GAPLESSENC)
