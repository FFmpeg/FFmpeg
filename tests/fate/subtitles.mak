FATE_SUBTITLES_ASS-$(call ALLYES, AQTITLE_DEMUXER TEXT_DECODER ICONV) += fate-sub-aqtitle
fate-sub-aqtitle: CMD = fmtstdout ass -sub_charenc windows-1250 -i $(TARGET_SAMPLES)/sub/AQTitle_capability_tester.aqt

FATE_SUBTITLES_ASS-$(call ALLYES, AVDEVICE LAVFI_INDEV CCAPTION_DECODER MOVIE_FILTER MPEGTS_DEMUXER) += fate-sub-cc
fate-sub-cc: CMD = fmtstdout ass -f lavfi -i "movie=$(TARGET_SAMPLES)/sub/Closedcaption_rollup.m2v[out0+subcc]"

FATE_SUBTITLES_ASS-$(call ALLYES, AVDEVICE LAVFI_INDEV CCAPTION_DECODER MOVIE_FILTER MPEGTS_DEMUXER) += fate-sub-cc-realtime
fate-sub-cc-realtime: CMD = fmtstdout ass -real_time 1 -f lavfi -i "movie=$(TARGET_SAMPLES)/sub/Closedcaption_rollup.m2v[out0+subcc]"

FATE_SUBTITLES_ASS-$(call DEMDEC, ASS, ASS) += fate-sub-ass-to-ass-transcode
fate-sub-ass-to-ass-transcode: CMD = fmtstdout ass -i $(TARGET_SAMPLES)/sub/1ededcbd7b.ass

FATE_SUBTITLES_ASS-$(CONFIG_ASS_DEMUXER) += fate-sub-ssa-to-ass-remux
fate-sub-ssa-to-ass-remux: CMD = fmtstdout ass -i $(TARGET_SAMPLES)/sub/a9-misc.ssa -c copy

FATE_SUBTITLES-$(call ALLYES, ASS_DEMUXER, MATROSKA_MUXER) += fate-binsub-mksenc
fate-binsub-mksenc: CMD = md5 -i $(TARGET_SAMPLES)/sub/1ededcbd7b.ass -c copy -f matroska -flags +bitexact -fflags +bitexact

FATE_SUBTITLES_ASS-$(call DEMDEC, JACOSUB, JACOSUB) += fate-sub-jacosub
fate-sub-jacosub: CMD = fmtstdout ass -i $(TARGET_SAMPLES)/sub/JACOsub_capability_tester.jss

FATE_SUBTITLES_ASS-$(call DEMDEC, MICRODVD, MICRODVD) += fate-sub-microdvd
fate-sub-microdvd: CMD = fmtstdout ass -i $(TARGET_SAMPLES)/sub/MicroDVD_capability_tester.sub

FATE_SUBTITLES-$(call ALLYES, MICRODVD_DEMUXER MICRODVD_MUXER) += fate-sub-microdvd-remux
fate-sub-microdvd-remux: CMD = fmtstdout microdvd -i $(TARGET_SAMPLES)/sub/MicroDVD_capability_tester.sub -c:s copy

FATE_SUBTITLES_ASS-$(call DEMDEC, MOV, MOVTEXT) += fate-sub-movtext
fate-sub-movtext: CMD = fmtstdout ass -i $(TARGET_SAMPLES)/sub/MovText_capability_tester.mp4

FATE_SUBTITLES-$(call ENCDEC, MOVTEXT, MOV) += fate-binsub-movtextenc
fate-binsub-movtextenc: CMD = md5 -i $(TARGET_SAMPLES)/sub/MovText_capability_tester.mp4 -map 0 -scodec mov_text -f mp4 -flags +bitexact -fflags +bitexact -movflags frag_keyframe+empty_moov

FATE_SUBTITLES_ASS-$(call DEMDEC, MPL2, MPL2) += fate-sub-mpl2
fate-sub-mpl2: CMD = fmtstdout ass -i $(TARGET_SAMPLES)/sub/MPL2_capability_tester.txt

FATE_SUBTITLES_ASS-$(call DEMDEC, MPSUB, TEXT) += fate-sub-mpsub
fate-sub-mpsub: CMD = fmtstdout ass -i $(TARGET_SAMPLES)/sub/MPSub_capability_tester.sub

FATE_SUBTITLES_ASS-$(call DEMDEC, MPSUB, TEXT) += fate-sub-mpsub-frames
fate-sub-mpsub-frames: CMD = fmtstdout ass -i $(TARGET_SAMPLES)/sub/MPSub_capability_tester_frames.sub

FATE_SUBTITLES_ASS-$(call DEMDEC, PJS, PJS) += fate-sub-pjs
fate-sub-pjs: CMD = fmtstdout ass -i $(TARGET_SAMPLES)/sub/PJS_capability_tester.pjs

FATE_SUBTITLES_ASS-$(call DEMDEC, REALTEXT, REALTEXT) += fate-sub-realtext
fate-sub-realtext: CMD = fmtstdout ass -i $(TARGET_SAMPLES)/sub/RealText_capability_tester.rt

FATE_SUBTITLES_ASS-$(call DEMDEC, SAMI, SAMI) += fate-sub-sami
fate-sub-sami: CMD = fmtstdout ass -i $(TARGET_SAMPLES)/sub/SAMI_capability_tester.smi

FATE_SUBTITLES_ASS-$(call DEMDEC, SAMI, SAMI) += fate-sub-sami2
fate-sub-sami2: CMD = fmtstdout ass -i $(TARGET_SAMPLES)/sub/SAMI_multilang_tweak_tester.smi

FATE_SUBTITLES_ASS-$(call DEMDEC, SRT, SUBRIP) += fate-sub-srt
fate-sub-srt: CMD = fmtstdout ass -i $(TARGET_SAMPLES)/sub/SubRip_capability_tester.srt

FATE_SUBTITLES-$(call ALLYES, SRT_DEMUXER SUBRIP_DECODER SRT_MUXER) += fate-sub-srt-rrn-remux
fate-sub-srt-rrn-remux: CMD = fmtstdout srt -i $(TARGET_SAMPLES)/sub/ticket5032-rrn.srt -c:s copy

FATE_SUBTITLES-$(call ALLYES, SRT_DEMUXER SUBRIP_DECODER SRT_MUXER) += fate-sub-srt-madness-timeshift
fate-sub-srt-madness-timeshift: CMD = fmtstdout srt -itsoffset 3.14 -i $(TARGET_SAMPLES)/sub/madness.srt -c:s copy

FATE_SUBTITLES-$(call ALLYES, SRT_DEMUXER SUBRIP_DECODER SRT_MUXER) += fate-sub-srt-empty-events
fate-sub-srt-empty-events: CMD = fmtstdout srt -i $(TARGET_SAMPLES)/sub/empty-events-2167.srt -c:s copy

FATE_SUBTITLES_ASS-$(call DEMDEC, STL, STL) += fate-sub-stl
fate-sub-stl: CMD = fmtstdout ass -i $(TARGET_SAMPLES)/sub/STL_capability_tester.stl

FATE_SUBTITLES-$(call ALLYES, MOV_DEMUXER MOVTEXT_DECODER SUBRIP_ENCODER SRT_MUXER) += fate-sub-subripenc
fate-sub-subripenc: CMD = fmtstdout srt -i $(TARGET_SAMPLES)/sub/MovText_capability_tester.mp4 -scodec subrip

FATE_SUBTITLES_ASS-$(call ALLYES, SUBVIEWER1_DEMUXER SUBVIEWER1_DECODER ICONV) += fate-sub-subviewer1
fate-sub-subviewer1: CMD = fmtstdout ass -sub_charenc windows-1250 -i $(TARGET_SAMPLES)/sub/SubViewer1_capability_tester.sub

FATE_SUBTITLES_ASS-$(call DEMDEC, SUBVIEWER, SUBVIEWER) += fate-sub-subviewer
fate-sub-subviewer: CMD = fmtstdout ass -i $(TARGET_SAMPLES)/sub/SubViewer_capability_tester.sub

FATE_SUBTITLES_ASS-$(call DEMDEC, VPLAYER, VPLAYER) += fate-sub-vplayer
fate-sub-vplayer: CMD = fmtstdout ass -i $(TARGET_SAMPLES)/sub/VPlayer_capability_tester.txt

FATE_SUBTITLES_ASS-$(call DEMDEC, WEBVTT, WEBVTT) += fate-sub-webvtt
fate-sub-webvtt: CMD = fmtstdout ass -i $(TARGET_SAMPLES)/sub/WebVTT_capability_tester.vtt

FATE_SUBTITLES_ASS-$(call DEMDEC, WEBVTT, WEBVTT) += fate-sub-webvtt2
fate-sub-webvtt2: CMD = fmtstdout ass -i $(TARGET_SAMPLES)/sub/WebVTT_extended_tester.vtt

FATE_SUBTITLES-$(call ALLYES, SRT_DEMUXER SUBRIP_DECODER WEBVTT_ENCODER WEBVTT_MUXER) += fate-sub-webvttenc
fate-sub-webvttenc: CMD = fmtstdout webvtt -i $(TARGET_SAMPLES)/sub/SubRip_capability_tester.srt

FATE_SUBTITLES-$(call ALLYES, SRT_DEMUXER SUBRIP_DECODER TEXT_ENCODER SRT_MUXER) += fate-sub-textenc
fate-sub-textenc: CMD = fmtstdout srt -i $(TARGET_SAMPLES)/sub/SubRip_capability_tester.srt -c:s text

FATE_SUBTITLES_ASS-$(call ALLYES, MICRODVD_DEMUXER MICRODVD_DECODER ICONV) += fate-sub-charenc
fate-sub-charenc: CMD = fmtstdout ass -sub_charenc cp1251 -i $(TARGET_SAMPLES)/sub/cp1251-subtitles.sub

FATE_SUBTITLES-$(call DEMDEC, SCC, CCAPTION) += fate-sub-scc
fate-sub-scc: CMD = fmtstdout ass -ss 57 -i $(TARGET_SAMPLES)/sub/witch.scc

FATE_SUBTITLES-$(call ENCMUX, ASS, ASS) += $(FATE_SUBTITLES_ASS-yes)
FATE_SUBTITLES += $(FATE_SUBTITLES-yes)

fate-sub-%: CMP = rawdiff

FATE_SAMPLES_FFMPEG += $(FATE_SUBTITLES)
fate-subtitles: $(FATE_SUBTITLES)
