FATE_GAPLESS-$(CONFIG_MP3_DEMUXER) += fate-gapless-mp3
fate-gapless-mp3: CMD = gapless $(TARGET_SAMPLES)/gapless/gapless.mp3

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
FATE_SAMPLES_AVCONV += $(FATE_GAPLESSINFO)
FATE_SAMPLES_AVCONV += $(FATE_GAPLESSENC)

fate-gapless: $(FATE_GAPLESS) $(FATE_GAPLESSINFO) $(FATE_GAPLESSENC)
