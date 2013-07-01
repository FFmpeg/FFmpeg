FATE_SUBTITLES_ASS-$(call ALLYES, AQTITLE_DEMUXER TEXT_DECODER ICONV) += fate-sub-aqtitle
fate-sub-aqtitle: CMD = md5 -sub_charenc windows-1250 -i $(TARGET_SAMPLES)/sub/AQTitle_capability_tester.aqt -f ass

FATE_SUBTITLES_ASS-$(call DEMDEC, JACOSUB, JACOSUB) += fate-sub-jacosub
fate-sub-jacosub: CMD = md5 -i $(TARGET_SAMPLES)/sub/JACOsub_capability_tester.jss -f ass

FATE_SUBTITLES_ASS-$(call DEMDEC, MICRODVD, MICRODVD) += fate-sub-microdvd
fate-sub-microdvd: CMD = md5 -i $(TARGET_SAMPLES)/sub/MicroDVD_capability_tester.sub -f ass

FATE_SUBTITLES-$(call ALLYES, MICRODVD_DEMUXER MICRODVD_MUXER) += fate-sub-microdvd-remux
fate-sub-microdvd-remux: CMD = md5 -i $(TARGET_SAMPLES)/sub/MicroDVD_capability_tester.sub -c:s copy -f microdvd

FATE_SUBTITLES_ASS-$(call DEMDEC, MOV, MOVTEXT) += fate-sub-movtext
fate-sub-movtext: CMD = md5 -i $(TARGET_SAMPLES)/sub/MovText_capability_tester.mp4 -f ass

FATE_SUBTITLES-$(call ENCDEC, MOVTEXT, MOV) += fate-sub-movtextenc
fate-sub-movtextenc: CMD = md5 -i $(TARGET_SAMPLES)/sub/MovText_capability_tester.mp4 -map 0 -scodec mov_text -f mp4 -flags +bitexact -movflags frag_keyframe+empty_moov

FATE_SUBTITLES_ASS-$(call DEMDEC, MPL2, MPL2) += fate-sub-mpl2
fate-sub-mpl2: CMD = md5 -i $(TARGET_SAMPLES)/sub/MPL2_capability_tester.txt -f ass

FATE_SUBTITLES_ASS-$(call DEMDEC, MPSUB, TEXT) += fate-sub-mpsub
fate-sub-mpsub: CMD = md5 -i $(TARGET_SAMPLES)/sub/MPSub_capability_tester.sub -f ass

FATE_SUBTITLES_ASS-$(call DEMDEC, MPSUB, TEXT) += fate-sub-mpsub-frames
fate-sub-mpsub-frames: CMD = md5 -i $(TARGET_SAMPLES)/sub/MPSub_capability_tester_frames.sub -f ass

FATE_SUBTITLES_ASS-$(call DEMDEC, PJS, PJS) += fate-sub-pjs
fate-sub-pjs: CMD = md5 -i $(TARGET_SAMPLES)/sub/PJS_capability_tester.pjs -f ass

FATE_SUBTITLES_ASS-$(call DEMDEC, REALTEXT, REALTEXT) += fate-sub-realtext
fate-sub-realtext: CMD = md5 -i $(TARGET_SAMPLES)/sub/RealText_capability_tester.rt -f ass

FATE_SUBTITLES_ASS-$(call DEMDEC, SAMI, SAMI) += fate-sub-sami
fate-sub-sami: CMD = md5 -i $(TARGET_SAMPLES)/sub/SAMI_capability_tester.smi -f ass

FATE_SUBTITLES_ASS-$(call DEMDEC, SRT, SUBRIP) += fate-sub-srt
fate-sub-srt: CMD = md5 -i $(TARGET_SAMPLES)/sub/SubRip_capability_tester.srt -f ass

FATE_SUBTITLES-$(call ALLYES, MOV_DEMUXER MOVTEXT_DECODER SUBRIP_ENCODER) += fate-sub-subripenc
fate-sub-subripenc: CMD = md5 -i $(TARGET_SAMPLES)/sub/MovText_capability_tester.mp4 -scodec subrip -f srt

FATE_SUBTITLES_ASS-$(call ALLYES, SUBVIEWER1_DEMUXER SUBVIEWER1_DECODER ICONV) += fate-sub-subviewer1
fate-sub-subviewer1: CMD = md5 -sub_charenc windows-1250 -i $(TARGET_SAMPLES)/sub/SubViewer1_capability_tester.sub -f ass

FATE_SUBTITLES_ASS-$(call DEMDEC, SUBVIEWER, SUBVIEWER) += fate-sub-subviewer
fate-sub-subviewer: CMD = md5 -i $(TARGET_SAMPLES)/sub/SubViewer_capability_tester.sub -f ass

FATE_SUBTITLES_ASS-$(call DEMDEC, VPLAYER, VPLAYER) += fate-sub-vplayer
fate-sub-vplayer: CMD = md5 -i $(TARGET_SAMPLES)/sub/VPlayer_capability_tester.txt -f ass

FATE_SUBTITLES_ASS-$(call DEMDEC, WEBVTT, WEBVTT) += fate-sub-webvtt
fate-sub-webvtt: CMD = md5 -i $(TARGET_SAMPLES)/sub/WebVTT_capability_tester.vtt -f ass

FATE_SUBTITLES_ASS-$(call ALLYES, MICRODVD_DEMUXER MICRODVD_DECODER ICONV) += fate-sub-charenc
fate-sub-charenc: CMD = md5 -sub_charenc cp1251 -i $(TARGET_SAMPLES)/sub/cp1251-subtitles.sub -f ass

FATE_SUBTITLES-$(call ENCMUX, ASS, ASS) += $(FATE_SUBTITLES_ASS-yes)
FATE_SUBTITLES += $(FATE_SUBTITLES-yes)

FATE_SAMPLES_FFMPEG += $(FATE_SUBTITLES)
fate-subtitles: $(FATE_SUBTITLES)
