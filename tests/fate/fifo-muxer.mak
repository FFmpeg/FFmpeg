fate-fifo-muxer-h264: CMD = ffmpeg -i $(TARGET_SAMPLES)/mkv/1242-small.mkv -frames:v 11\
                            -c:v copy -c:a copy -map v:0 -map a:0 -flags +bitexact\
                            -fflags +bitexact -f fifo -fifo_format framecrc -
fate-fifo-muxer-h264: REF = $(SRC_PATH)/tests/ref/fate/mkv-1242
FATE_SAMPLES_FIFO_MUXER-$(call ALLYES, FIFO_MUXER, MATROSKA_DEMUXER, H264_DECODER) += fate-fifo-muxer-h264

fate-fifo-muxer-wav: CMD = ffmpeg -i $(TARGET_SAMPLES)/audio-reference/chorusnoise_2ch_44kHz_s16.wav\
                           -c:a copy -map a:0 -flags +bitexact\
                           -fflags +bitexact -f fifo -fifo_format wav md5:
fate-fifo-muxer-wav: CMP = oneline
fate-fifo-muxer-wav: REF = 4dda5dcc7ecdc2218b0739a152ada802
FATE_SAMPLES_FIFO_MUXER-$(call ALLYES, FIFO_MUXER, WAV_DEMUXER) += fate-fifo-muxer-wav

fate-fifo-muxer-tst: libavformat/tests/fifo_muxer$(EXESUF)
fate-fifo-muxer-tst: CMD = run libavformat/tests/fifo_muxer$(EXESUF)
FATE_FIFO_MUXER-$(call ALLYES, FIFO_MUXER NETWORK) += fate-fifo-muxer-tst

FATE_SAMPLES_FFMPEG += $(FATE_SAMPLES_FIFO_MUXER-yes)
FATE_FFMPEG += $(FATE_FIFO_MUXER-yes)
fate-fifo-muxer: $(FATE_FIFO_MUXER-yes) $(FATE_SAMPLES_FIFO_MUXER-yes)
