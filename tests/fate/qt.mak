FATE_QT-$(call FRAMECRC, MOV, EIGHTBPS, SCALE_FILTER ARESAMPLE_FILTER) += fate-8bps
fate-8bps: CMD = framecrc -i $(TARGET_SAMPLES)/8bps/full9iron-partial.mov -pix_fmt rgb24 -vf scale -af aresample

FATE_QT-$(call ENCDEC, PCM_S16LE QDM2, PCM_S16LE MOV, PIPE_PROTOCOL) += fate-qdm2
fate-qdm2: CMD = pcm -i $(TARGET_SAMPLES)/qt-surge-suite/surge-2-16-B-QDM2.mov
fate-qdm2: CMP = oneoff
fate-qdm2: REF = $(SAMPLES)/qt-surge-suite/surge-2-16-B-QDM2.pcm
fate-qdm2: FUZZ = 2

FATE_QT-$(call ENCDEC, PCM_S16LE PCM_ALAW, MOV PCM_S16LE) += fate-qt-alaw-mono
fate-qt-alaw-mono: CMD = md5 -i $(TARGET_SAMPLES)/qt-surge-suite/surge-1-16-B-alaw.mov -f s16le

FATE_QT-$(call ENCDEC, PCM_S16LE PCM_ALAW, MOV PCM_S16LE) += fate-qt-alaw-stereo
fate-qt-alaw-stereo: CMD = md5 -i $(TARGET_SAMPLES)/qt-surge-suite/surge-2-16-B-alaw.mov -f s16le

FATE_QT-$(call ENCDEC, PCM_S16LE ADPCM_IMA_QT, PCM_S16LE MOV, ARESAMPLE_FILTER) += fate-qt-ima4-mono
fate-qt-ima4-mono: CMD = md5 -i $(TARGET_SAMPLES)/qt-surge-suite/surge-1-16-B-ima4.mov -f s16le -af aresample

FATE_QT-$(call ENCDEC, PCM_S16LE ADPCM_IMA_QT, PCM_S16LE MOV, ARESAMPLE_FILTER) += fate-qt-ima4-stereo
fate-qt-ima4-stereo: CMD = md5 -i $(TARGET_SAMPLES)/qt-surge-suite/surge-2-16-B-ima4.mov -f s16le -af aresample

FATE_QT-$(call ENCDEC, PCM_S16LE MACE3, PCM_S16LE MOV, ARESAMPLE_FILTER) += fate-qt-mac3-mono
fate-qt-mac3-mono: CMD = md5 -i $(TARGET_SAMPLES)/qt-surge-suite/surge-1-8-MAC3.mov -f s16le -af aresample

FATE_QT-$(call ENCDEC, PCM_S16LE MACE3, PCM_S16LE MOV, ARESAMPLE_FILTER) += fate-qt-mac3-stereo
fate-qt-mac3-stereo: CMD = md5 -i $(TARGET_SAMPLES)/qt-surge-suite/surge-2-8-MAC3.mov -f s16le -af aresample

FATE_QT-$(call ENCDEC, PCM_S16LE MACE6, PCM_S16LE MOV, ARESAMPLE_FILTER) += fate-qt-mac6-mono
fate-qt-mac6-mono: CMD = md5 -i $(TARGET_SAMPLES)/qt-surge-suite/surge-1-8-MAC6.mov -f s16le -af aresample

FATE_QT-$(call ENCDEC, PCM_S16LE MACE6, PCM_S16LE MOV, ARESAMPLE_FILTER) += fate-qt-mac6-stereo
fate-qt-mac6-stereo: CMD = md5 -i $(TARGET_SAMPLES)/qt-surge-suite/surge-2-8-MAC6.mov -f s16le -af aresample

FATE_QT-$(call ENCDEC, PCM_S16LE PCM_MULAW, PCM_S16LE MOV) += fate-qt-ulaw-mono
fate-qt-ulaw-mono: CMD = md5 -i $(TARGET_SAMPLES)/qt-surge-suite/surge-1-16-B-ulaw.mov -f s16le

FATE_QT-$(call ENCDEC, PCM_S16LE PCM_MULAW, PCM_S16LE MOV) += fate-qt-ulaw-stereo
fate-qt-ulaw-stereo: CMD = md5 -i $(TARGET_SAMPLES)/qt-surge-suite/surge-2-16-B-ulaw.mov -f s16le

FATE_QT-$(call FRAMECRC, MOV, QDRAW, SCALE_FILTER) += fate-quickdraw
fate-quickdraw: CMD = framecrc -i $(TARGET_SAMPLES)/quickdraw/Airplane.mov -pix_fmt rgb24 -vf scale

FATE_QT-$(call FRAMECRC, MOV, RPZA, SCALE_FILTER) += fate-rpza
fate-rpza: CMD = framecrc -i $(TARGET_SAMPLES)/rpza/rpza2.mov -t 2 -pix_fmt rgb24 -vf scale

FATE_QT-$(call FRAMECRC, MOV, SVQ1) += fate-svq1
fate-svq1: CMD = framecrc -i $(TARGET_SAMPLES)/svq1/marymary-shackles.mov -an -t 10

FATE_QT-$(call FRAMECRC, MOV, SVQ1) += fate-svq1-headerswap
fate-svq1-headerswap: CMD = framecrc -i $(TARGET_SAMPLES)/svq1/ct_ending_cut.mov -frames 4

FATE_SVQ3 += fate-svq3-1
fate-svq3-1: CMD = framecrc -i $(TARGET_SAMPLES)/svq3/Vertical400kbit.sorenson3.mov -t 6 -an

#FATE_SVQ3 += fate-svq3-2
#FIXME: first frame changes depending on --enable-memory-poisoning being used to configure or not
fate-svq3-2: CMD = framecrc -flags +bitexact -ignore_editlist 1 -i $(TARGET_SAMPLES)/svq3/svq3_decoding_regression.mov -an

FATE_SVQ3 += fate-svq3-watermark
fate-svq3-watermark: CMD = framecrc -flags +bitexact -i $(TARGET_SAMPLES)/svq3/svq3_watermark.mov -fps_mode passthrough

FATE_QT-$(call FRAMECRC, MOV, SVQ3, ZLIB) += $(FATE_SVQ3)
fate-svq3: $(FATE_SVQ3)

FATE_QT += $(FATE_QT-yes)

FATE_SAMPLES_FFMPEG += $(FATE_QT)
fate-qt: $(FATE_QT)
