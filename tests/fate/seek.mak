# files from fate-acodec

FATE_SEEK_ACODEC += adpcm-ima_qt  adpcm-ima_qt-trellis  \
                    adpcm-ima_wav adpcm-ima_wav-trellis \
                    adpcm-ms      adpcm-ms-trellis      \
                    adpcm-swf     adpcm-swf-trellis     \
                    adpcm-yamaha  adpcm-yamaha-trellis  \
                    alac flac mp2                       \
                    pcm-alaw  pcm-mulaw pcm-s8 pcm-u8   \
                    pcm-s16be pcm-s16le pcm-s24be       \
                    pcm-s24le pcm-s32be pcm-s32le       \
                    pcm-f32be pcm-f32le pcm-f64be       \
                    pcm-f64le                           \

fate-seek-acodec-adpcm-ima_qt:  SRC = fate/acodec-adpcm-ima_qt.aiff
fate-seek-acodec-adpcm-ima_wav: SRC = fate/acodec-adpcm-ima_wav.wav
fate-seek-acodec-adpcm-ms:      SRC = fate/acodec-adpcm-ms.wav
fate-seek-acodec-adpcm-swf:     SRC = fate/acodec-adpcm-swf.flv
fate-seek-acodec-adpcm-yamaha:  SRC = fate/acodec-adpcm-yamaha.wav
fate-seek-acodec-adpcm-ima_qt-trellis:  SRC = fate/acodec-adpcm-ima_qt-trellis.aiff
fate-seek-acodec-adpcm-ima_wav-trellis: SRC = fate/acodec-adpcm-ima_wav-trellis.wav
fate-seek-acodec-adpcm-ms-trellis:      SRC = fate/acodec-adpcm-ms-trellis.wav
fate-seek-acodec-adpcm-swf-trellis:     SRC = fate/acodec-adpcm-swf-trellis.flv
fate-seek-acodec-adpcm-yamaha-trellis:  SRC = fate/acodec-adpcm-yamaha-trellis.wav
fate-seek-acodec-alac:          SRC = fate/acodec-alac.mov
fate-seek-acodec-flac:          SRC = fate/acodec-flac.flac
fate-seek-acodec-mp2:           SRC = fate/acodec-mp2.mp2
fate-seek-acodec-pcm-alaw:      SRC = fate/acodec-pcm-alaw.wav
fate-seek-acodec-pcm-f32be:     SRC = fate/acodec-pcm-f32be.au
fate-seek-acodec-pcm-f32le:     SRC = fate/acodec-pcm-f32le.wav
fate-seek-acodec-pcm-f64be:     SRC = fate/acodec-pcm-f64be.au
fate-seek-acodec-pcm-f64le:     SRC = fate/acodec-pcm-f64le.wav
fate-seek-acodec-pcm-mulaw:     SRC = fate/acodec-pcm-mulaw.wav
fate-seek-acodec-pcm-s16be:     SRC = fate/acodec-pcm-s16be.mov
fate-seek-acodec-pcm-s16le:     SRC = fate/acodec-pcm-s16le.wav
fate-seek-acodec-pcm-s24be:     SRC = fate/acodec-pcm-s24be.mov
fate-seek-acodec-pcm-s24le:     SRC = fate/acodec-pcm-s24le.wav
fate-seek-acodec-pcm-s32be:     SRC = fate/acodec-pcm-s32be.mov
fate-seek-acodec-pcm-s32le:     SRC = fate/acodec-pcm-s32le.wav
fate-seek-acodec-pcm-s8:        SRC = fate/acodec-pcm-s8.mov
fate-seek-acodec-pcm-u8:        SRC = fate/acodec-pcm-u8.wav

FATE_SEEK_ACODEC := $(FATE_SEEK_ACODEC:%=fate-seek-acodec-%)
# The following disables every fate-seek-* test whose
# corresponding fate-* test has unmet requirements (or is disabled).
FATE_SEEK_ACODEC := $(filter $(subst fate-,fate-seek-,$(FATE_ACODEC)), $(FATE_SEEK_ACODEC))
FATE_SEEK += $(FATE_SEEK_ACODEC)

# files from fate-vsynth_lena

FATE_SEEK_VSYNTH_LENA += asv1 asv2                      \
                         dnxhd-720p dnxhd-720p-rd       \
                         dnxhd-1080i dnxhd-4k-hr-lb     \
                         dv dv-411 dv-50                \
                         ffv1                           \
                         flashsv                        \
                         flv                            \
                         h261 h263 h263p                \
                         huffyuv                        \
                         jpegls ljpeg mjpeg             \
                         mpeg1 mpeg1b                   \
                         mpeg2-422    mpeg2-idct-int    \
                         mpeg2-ilace  mpeg2-ivlc-qprd   \
                         mpeg2-thread mpeg2-thread-ivlc \
                         mpeg4 $(FATE_MPEG4_AVI)        \
                         msmpeg4 msmpeg4v2              \
                         rgb                            \
                         roqvideo                       \
                         rv10 rv20                      \
                         snow snow-ll                   \
                         svq1                           \
                         wmv1 wmv2                      \
                         yuv                            \

fate-seek-vsynth_lena-asv1:              SRC = fate/vsynth_lena-asv1.avi
fate-seek-vsynth_lena-asv2:              SRC = fate/vsynth_lena-asv2.avi
fate-seek-vsynth_lena-dnxhd-1080i:       SRC = fate/vsynth_lena-dnxhd-1080i.mov
fate-seek-vsynth_lena-dnxhd-720p:        SRC = fate/vsynth_lena-dnxhd-720p.dnxhd
fate-seek-vsynth_lena-dnxhd-720p-rd:     SRC = fate/vsynth_lena-dnxhd-720p.dnxhd
fate-seek-vsynth_lena-dnxhd-4k-hr-lb:    SRC = fate/vsynth_lena-dnxhd-4k-hr-lb.dnxhd
fate-seek-vsynth_lena-dv:                SRC = fate/vsynth_lena-dv.dv
fate-seek-vsynth_lena-dv-411:            SRC = fate/vsynth_lena-dv-411.dv
fate-seek-vsynth_lena-dv-50:             SRC = fate/vsynth_lena-dv-50.dv
fate-seek-vsynth_lena-ffv1:              SRC = fate/vsynth_lena-ffv1.avi
fate-seek-vsynth_lena-flashsv:           SRC = fate/vsynth_lena-flashsv.flv
fate-seek-vsynth_lena-flv:               SRC = fate/vsynth_lena-flv.flv
fate-seek-vsynth_lena-h261:              SRC = fate/vsynth_lena-h261.avi
fate-seek-vsynth_lena-h263:              SRC = fate/vsynth_lena-h263.avi
fate-seek-vsynth_lena-h263p:             SRC = fate/vsynth_lena-h263p.avi
fate-seek-vsynth_lena-huffyuv:           SRC = fate/vsynth_lena-huffyuv.avi
fate-seek-vsynth_lena-jpegls:            SRC = fate/vsynth_lena-jpegls.avi
fate-seek-vsynth_lena-ljpeg:             SRC = fate/vsynth_lena-ljpeg.avi
fate-seek-vsynth_lena-mjpeg:             SRC = fate/vsynth_lena-mjpeg.avi
fate-seek-vsynth_lena-mpeg1:             SRC = fate/vsynth_lena-mpeg1.mpeg1video
fate-seek-vsynth_lena-mpeg1b:            SRC = fate/vsynth_lena-mpeg1b.mpeg1video
fate-seek-vsynth_lena-mpeg2-422:         SRC = fate/vsynth_lena-mpeg2-422.mpeg2video
fate-seek-vsynth_lena-mpeg2-idct-int:    SRC = fate/vsynth_lena-mpeg2-idct-int.mpeg2video
fate-seek-vsynth_lena-mpeg2-ilace:       SRC = fate/vsynth_lena-mpeg2-ilace.mpeg2video
fate-seek-vsynth_lena-mpeg2-ivlc-qprd:   SRC = fate/vsynth_lena-mpeg2-ivlc-qprd.mpeg2video
fate-seek-vsynth_lena-mpeg2-thread:      SRC = fate/vsynth_lena-mpeg2-thread.mpeg2video
fate-seek-vsynth_lena-mpeg2-thread-ivlc: SRC = fate/vsynth_lena-mpeg2-thread-ivlc.mpeg2video
fate-seek-vsynth_lena-mpeg4:             SRC = fate/vsynth_lena-mpeg4.mp4
fate-seek-vsynth_lena-mpeg4-adap:        SRC = fate/vsynth_lena-mpeg4-adap.avi
fate-seek-vsynth_lena-mpeg4-adv:         SRC = fate/vsynth_lena-mpeg4-adv.avi
fate-seek-vsynth_lena-mpeg4-error:       SRC = fate/vsynth_lena-mpeg4-error.avi
fate-seek-vsynth_lena-mpeg4-nr:          SRC = fate/vsynth_lena-mpeg4-nr.avi
fate-seek-vsynth_lena-mpeg4-nsse:        SRC = fate/vsynth_lena-mpeg4-nsse.avi
fate-seek-vsynth_lena-mpeg4-qpel:        SRC = fate/vsynth_lena-mpeg4-qpel.avi
fate-seek-vsynth_lena-mpeg4-qprd:        SRC = fate/vsynth_lena-mpeg4-qprd.avi
fate-seek-vsynth_lena-mpeg4-rc:          SRC = fate/vsynth_lena-mpeg4-rc.avi
fate-seek-vsynth_lena-mpeg4-thread:      SRC = fate/vsynth_lena-mpeg4-thread.avi
fate-seek-vsynth_lena-msmpeg4:           SRC = fate/vsynth_lena-msmpeg4.avi
fate-seek-vsynth_lena-msmpeg4v2:         SRC = fate/vsynth_lena-msmpeg4v2.avi
fate-seek-vsynth_lena-rgb:               SRC = fate/vsynth_lena-rgb.avi
fate-seek-vsynth_lena-roqvideo:          SRC = fate/vsynth_lena-roqvideo.roq
fate-seek-vsynth_lena-rv10:              SRC = fate/vsynth_lena-rv10.rm
fate-seek-vsynth_lena-rv20:              SRC = fate/vsynth_lena-rv20.rm
fate-seek-vsynth_lena-snow:              SRC = fate/vsynth_lena-snow.avi
fate-seek-vsynth_lena-snow-ll:           SRC = fate/vsynth_lena-snow-ll.avi
fate-seek-vsynth_lena-svq1:              SRC = fate/vsynth_lena-svq1.mov
fate-seek-vsynth_lena-wmv1:              SRC = fate/vsynth_lena-wmv1.avi
fate-seek-vsynth_lena-wmv2:              SRC = fate/vsynth_lena-wmv2.avi
fate-seek-vsynth_lena-yuv:               SRC = fate/vsynth_lena-yuv.avi

FATE_SEEK_VSYNTH_LENA := $(FATE_SEEK_VSYNTH_LENA:%=fate-seek-vsynth_lena-%)
FATE_SEEK_VSYNTH_LENA := $(filter $(subst fate-,fate-seek-,$(FATE_VSYNTH_LENA)), $(FATE_SEEK_VSYNTH_LENA))
FATE_SAMPLES_SEEK += $(FATE_SEEK_VSYNTH_LENA)

# files from fate-lavf-audio

FATE_SEEK_LAVF_AUDIO += aiff al au mmf ogg ul voc wav

FATE_SEEK_LAVF_AUDIO := $(FATE_SEEK_LAVF_AUDIO:%=fate-seek-lavf-%)
FATE_SEEK_LAVF_AUDIO := $(filter $(subst fate-,fate-seek-,$(FATE_LAVF_AUDIO)), $(FATE_SEEK_LAVF_AUDIO))
FATE_SEEK += $(FATE_SEEK_LAVF_AUDIO)

# files from fate-lavf-container

FATE_SEEK_LAVF_CONTAINER += asf avi dv flv gxf mkv mov mpg    \
                            mxf mxf_d10 mxf_dv25 mxf_dvcpro50 \
                            mxf_opatom mxf_opatom_audio       \
                            nut swf ts wtv
# rm is special: fate-lavf-rm does not read the created file
# and therefore does not require the corresponding demuxer
# to be present, so we have to explicitly check for this here.
FATE_SEEK_LAVF_CONTAINER-$(CONFIG_RM_DEMUXER) += rm
FATE_SEEK_LAVF_CONTAINER += $(FATE_SEEK_LAVF_CONTAINER-yes)

FATE_SEEK_LAVF_CONTAINER := $(FATE_SEEK_LAVF_CONTAINER:%=fate-seek-lavf-%)
FATE_SEEK_LAVF_CONTAINER := $(filter $(subst fate-,fate-seek-,$(FATE_LAVF_CONTAINER)), $(FATE_SEEK_LAVF_CONTAINER))
FATE_SEEK += $(FATE_SEEK_LAVF_CONTAINER)

# files from fate-lavf-video

FATE_SEEK_LAVF_VIDEO += gif y4m

FATE_SEEK_LAVF_VIDEO := $(FATE_SEEK_LAVF_VIDEO:%=fate-seek-lavf-%)
FATE_SEEK_LAVF_VIDEO := $(filter $(subst fate-,fate-seek-,$(FATE_LAVF_VIDEO)), $(FATE_SEEK_LAVF_VIDEO))
FATE_SEEK += $(FATE_SEEK_LAVF_VIDEO)

$(FATE_SEEK_LAVF_AUDIO) $(FATE_SEEK_LAVF_CONTAINER) $(FATE_SEEK_LAVF_VIDEO): SRC = lavf/lavf.$(@:fate-seek-lavf-%=%)

# files from fate-lavf-image

FATE_SEEK_LAVF_IMAGE += bmp jpg pcx pgm ppm sgi tga tiff

FATE_SEEK_LAVF_IMAGE := $(FATE_SEEK_LAVF_IMAGE:%=fate-seek-lavf-%)
FATE_SEEK_LAVF_IMAGE := $(filter $(subst fate-,fate-seek-,$(FATE_LAVF_IMAGES)), $(FATE_SEEK_LAVF_IMAGE))
$(FATE_SEEK_LAVF_IMAGE): SRC = images/$(@:fate-seek-lavf-%=%)/%02d.$(@:fate-seek-lavf-%=%)
FATE_SEEK += $(FATE_SEEK_LAVF_IMAGE)

#files from fate-lavf-image2pipe

FATE_SEEK_LAVF_IMAGE2PIPE += pbmpipe pgmpipe ppmpipe

FATE_SEEK_LAVF_IMAGE2PIPE := $(FATE_SEEK_LAVF_IMAGE2PIPE:%=fate-seek-lavf-%)
FATE_SEEK_LAVF_IMAGE2PIPE := $(filter $(subst fate-,fate-seek-,$(FATE_LAVF_IMAGE2PIPE)), $(FATE_SEEK_LAVF_IMAGE2PIPE))
$(FATE_SEEK_LAVF_IMAGE2PIPE): SRC = lavf/$(@:fate-seek-lavf-%pipe=%)pipe.$(@:fate-seek-lavf-%pipe=%)
FATE_SEEK += $(FATE_SEEK_LAVF_IMAGE2PIPE)

# extra files

FATE_SEEK_EXTRA-$(CONFIG_MP3_DEMUXER)   += fate-seek-extra-mp3
FATE_SEEK_EXTRA-$(call ALLYES, CACHE_PROTOCOL PIPE_PROTOCOL MP3_DEMUXER) += fate-seek-cache-pipe
FATE_SEEK_EXTRA-$(CONFIG_MATROSKA_DEMUXER) += fate-seek-mkv-codec-delay
FATE_SEEK_EXTRA-$(CONFIG_MOV_DEMUXER) += fate-seek-extra-mp4
FATE_SEEK_EXTRA-$(CONFIG_MOV_DEMUXER) += fate-seek-empty-edit-mp4
FATE_SEEK_EXTRA-$(CONFIG_MOV_DEMUXER) += fate-seek-test-iibbibb-mp4
FATE_SEEK_EXTRA-$(CONFIG_MOV_DEMUXER) += fate-seek-test-iibbibb-neg-ctts-mp4

fate-seek-extra-mp3:  CMD = run libavformat/tests/seek$(EXESUF) $(TARGET_SAMPLES)/gapless/gapless.mp3 -fastseek 1
fate-seek-extra-mp4:  CMD = run libavformat/tests/seek$(EXESUF) $(TARGET_SAMPLES)/mov/buck480p30_na.mp4 -duration 180 -frames 4
fate-seek-empty-edit-mp4:  CMD = run libavformat/tests/seek$(EXESUF) $(TARGET_SAMPLES)/mov/empty_edit_5s.mp4 -duration 15 -frames 4
fate-seek-test-iibbibb-mp4:  CMD = run libavformat/tests/seek$(EXESUF) $(TARGET_SAMPLES)/mov/test_iibbibb.mp4 -duration 13 -frames 4
fate-seek-test-iibbibb-neg-ctts-mp4:  CMD = run libavformat/tests/seek$(EXESUF) $(TARGET_SAMPLES)/mov/test_iibbibb_neg_ctts.mp4 -duration 13 -frames 4
fate-seek-cache-pipe: CMD = cat $(SAMPLES)/gapless/gapless.mp3 | run libavformat/tests/seek$(EXESUF) cache:pipe:0 -read_ahead_limit -1
fate-seek-mkv-codec-delay:   CMD = run libavformat/tests/seek$(EXESUF) $(TARGET_SAMPLES)/mkv/codec_delay_opus.mkv

FATE_SEEK_EXTRA += $(FATE_SEEK_EXTRA-yes)


$(FATE_SEEK) $(FATE_SAMPLES_SEEK) $(FATE_SEEK_EXTRA): libavformat/tests/seek$(EXESUF)
$(FATE_SEEK) $(FATE_SAMPLES_SEEK): CMD = run libavformat/tests/seek$(EXESUF) $(TARGET_PATH)/tests/data/$(SRC)
$(FATE_SEEK) $(FATE_SAMPLES_SEEK): fate-seek-%: fate-%
$(subst fate-seek-,fate-,$(FATE_SAMPLES_SEEK) $(FATE_SEEK)): KEEP_FILES ?= 1
fate-seek-%: REF = $(SRC_PATH)/tests/ref/seek/$(@:fate-seek-%=%)

FATE_AVCONV += $(FATE_SEEK)
FATE_SAMPLES_AVCONV += $(FATE_SAMPLES_SEEK) $(FATE_SEEK_EXTRA)
fate-seek:     $(FATE_SEEK) $(FATE_SAMPLES_SEEK) $(FATE_SEEK_EXTRA)
