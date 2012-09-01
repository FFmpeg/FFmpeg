FATE_SUBTITLES += fate-sub-jacosub
fate-sub-jacosub: CMD = md5 -i $(SAMPLES)/sub/JACOsub_capability_tester.jss -f ass

FATE_SUBTITLES += fate-sub-microdvd
fate-sub-microdvd: CMD = md5 -i $(SAMPLES)/sub/MicroDVD_capability_tester.sub -f ass

FATE_SUBTITLES += fate-sub-movtext
fate-sub-movtext: CMD = md5 -i $(SAMPLES)/sub/MovText_capability_tester.mp4 -f ass

FATE_SUBTITLES += fate-sub-movtextenc
fate-sub-movtextenc: CMD = md5 -i $(SAMPLES)/sub/MovText_capability_tester.mp4 -map 0 -scodec mov_text -f mp4 -flags +bitexact -movflags frag_keyframe+empty_moov

FATE_SUBTITLES += fate-sub-realtext
fate-sub-realtext: CMD = md5 -i $(SAMPLES)/sub/RealText_capability_tester.rt -f ass

FATE_SUBTITLES += fate-sub-sami
fate-sub-sami: CMD = md5 -i $(SAMPLES)/sub/SAMI_capability_tester.smi -f ass

FATE_SUBTITLES += fate-sub-srt
fate-sub-srt: CMD = md5 -i $(SAMPLES)/sub/SubRip_capability_tester.srt -f ass

FATE_SUBTITLES += fate-sub-subripenc
fate-sub-subripenc: CMD = md5 -i $(SAMPLES)/sub/MovText_capability_tester.mp4 -scodec subrip -f srt

FATE_SUBTITLES += fate-sub-subviewer
fate-sub-subviewer: CMD = md5 -i $(SAMPLES)/sub/SubViewer_capability_tester.sub -f ass

FATE_SUBTITLES += fate-sub-webvtt
fate-sub-webvtt: CMD = md5 -i $(SAMPLES)/sub/WebVTT_capability_tester.vtt -f ass

FATE_SAMPLES_FFMPEG += $(FATE_SUBTITLES)
fate-subtitles: $(FATE_SUBTITLES)
