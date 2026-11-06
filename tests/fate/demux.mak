FATE_SAMPLES_DEMUX-$(call FRAMECRC, AVI, FRAPS) += fate-avio-direct
fate-avio-direct: CMD = framecrc -avioflags direct -i $(TARGET_SAMPLES)/fraps/fraps-v5-bouncing-balls-partial.avi -avioflags direct

FATE_SAMPLES_DEMUX-$(call FRAMECRC, AAC, AAC, AAC_PARSER CRC_MUXER) += fate-adts-demux fate-adts-id3v1-demux fate-adts-id3v2-demux fate-adts-id3v2-two-tags-demux
fate-adts-demux: CMD = crc -i $(TARGET_SAMPLES)/aac/ct_faac-adts.aac -c:a copy
fate-adts-id3v1-demux: CMD = framecrc -f aac -i $(TARGET_SAMPLES)/aac/id3v1.aac -c:a copy
fate-adts-id3v2-demux: CMD = framecrc -f aac -i $(TARGET_SAMPLES)/aac/id3v2.aac -c:a copy
fate-adts-id3v2-two-tags-demux: CMD = framecrc -i $(TARGET_SAMPLES)/aac/id3v2_two_tags.aac -c:a copy

FATE_FFPROBE_DEMUX-$(call DEMDEC, AAC, AAC) += fate-adts-noparse-demux
fate-adts-noparse-demux: CMD = ffprobe_demux $(TARGET_SAMPLES)/aac/id3v1.aac -f aac -fflags +noparse

FATE_SAMPLES_DEMUX-$(call FRAMECRC, AA,, SIPR_PARSER) += fate-aa-demux
fate-aa-demux: CMD = framecrc -i $(TARGET_SAMPLES)/aa/bush.aa -c:a copy

FATE_SAMPLES_DEMUX-$(call CRC, AEA) += fate-aea-demux
fate-aea-demux: CMD = crc -i $(TARGET_SAMPLES)/aea/chirp.aea -c:a copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, AV1, AV1, EXTRACT_EXTRADATA_BSF) += fate-av1-annexb-demux
fate-av1-annexb-demux: CMD = framecrc -c:v av1 -i $(TARGET_SAMPLES)/av1/annexb.obu -c:v copy

FATE_SAMPLES_DEMUX-$(call CRC, AST) += fate-ast
fate-ast: CMD = crc -i $(TARGET_SAMPLES)/ast/demo11_02_partial.ast -c copy

FATE_SAMPLES_DEMUX-$(call CRC, BINK) += fate-bink-demux
fate-bink-demux: CMD = crc -i $(TARGET_SAMPLES)/bink/Snd0a7d9b58.dee -vn -c:a copy

FATE_SAMPLES_DEMUX-$(call CRC, BFSTM) += fate-bfstm fate-bcstm
fate-bfstm: CMD = crc -i $(TARGET_SAMPLES)/bfstm/spl-forest-day.bfstm -c:a copy
fate-bcstm: CMD = crc -i $(TARGET_SAMPLES)/bfstm/loz-mm-mikau.bcstm -c:a copy

FATE_SAMPLES_DEMUX-$(call CRC, BRSTM) += fate-brstm
fate-brstm: CMD = crc -i $(TARGET_SAMPLES)/brstm/lozswd_partial.brstm -c:a copy

FATE_FFPROBE_DEMUX-$(call DEMDEC, CAVSVIDEO, CAVS, CAVSVIDEO_PARSER EXTRACT_EXTRADATA_BSF) += fate-cavs-demux
fate-cavs-demux: CMD = ffprobe_demux $(TARGET_SAMPLES)/cavs/bunny.mp4

FATE_SAMPLES_DEMUX-$(call FRAMECRC, CDXL) += fate-cdxl-demux
fate-cdxl-demux: CMD = framecrc -i $(TARGET_SAMPLES)/cdxl/mirage.cdxl -c:v copy -c:a copy

FATE_SAMPLES_DEMUX-$(call CRC, CINE) += fate-cine-demux
fate-cine-demux: CMD = crc -i $(TARGET_SAMPLES)/cine/bayer_gbrg8.cine -c copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, DAUD) += fate-d-cinema-demux
fate-d-cinema-demux: CMD = framecrc -i $(TARGET_SAMPLES)/d-cinema/THX_Science_FLT_1920-partial.302 -c:a copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, EA, VP6) += fate-d-eavp6-demux
fate-d-eavp6-demux: CMD = framecrc -i $(TARGET_SAMPLES)/ea-vp6/SmallRing.vp6 -map 0 -c:v copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, FITS GIF, FITS GIF, FITS_MUXER FITS_ENCODER GIF_PARSER SCALE_FILTER) += fate-fits-demux
fate-fits-demux: tests/data/fits-multi.fits
fate-fits-demux: CMD = framecrc -i $(TARGET_PATH)/tests/data/fits-multi.fits -c:v copy

FATE_FFPROBE_DEMUX-$(call DEMDEC, FLV, H264 AAC, H264_PARSER) += fate-flv-demux
fate-flv-demux: CMD = ffprobe_demux $(TARGET_SAMPLES)/flv/Enigma_Principles_of_Lust-part.flv

FATE_SAMPLES_DEMUX-$(call FRAMECRC, GIF,, GIF_PARSER) += fate-gif-demux
fate-gif-demux: CMD = framecrc -i $(TARGET_SAMPLES)/gif/Newtons_cradle_animation_book_2.gif -c:v copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, HXVS, HEVC, EXTRACT_EXTRADATA_BSF) += fate-hxvs-demux
fate-hxvs-demux: CMD = framecrc -i $(TARGET_SAMPLES)/hxvs/A231130_171422_171436.bin -c copy -t 3

FATE_SAMPLES_DEMUX-$(call FRAMECRC, IFF) += fate-iff-demux
fate-iff-demux: CMD = framecrc -i $(TARGET_SAMPLES)/iff-anim/Hammer2.sndanim -c:v copy -c:a copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, IV8,, MPEG4VIDEO_PARSER EXTRACT_EXTRADATA_BSF) += fate-iv8-demux
fate-iv8-demux: CMD = framecrc -i $(TARGET_SAMPLES)/iv8/zzz-partial.mpg -c:v copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, JV) += fate-jv-demux
fate-jv-demux: CMD = framecrc -i $(TARGET_SAMPLES)/jv/intro.jv -c:v copy -c:a copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, LMLM4,, MPEG4VIDEO_PARSER MPEGAUDIO_PARSER EXTRACT_EXTRADATA_BSF) += fate-lmlm4-demux
fate-lmlm4-demux: CMD = framecrc -i $(TARGET_SAMPLES)/lmlm4/LMLM4_CIFat30fps.divx -t 3 -c:a copy -c:v copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, XA) += fate-maxis-xa
fate-maxis-xa: CMD = framecrc -i $(TARGET_SAMPLES)/maxis-xa/SC2KBUG.XA -frames:a 30 -c:a copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, MATROSKA, H264) += fate-mkv
fate-mkv: CMD = framecrc -i $(TARGET_SAMPLES)/mkv/test7_cut.mkv -c copy

#No dts errors or duplicate DTS should be in this
FATE_SAMPLES_DEMUX-$(call FRAMECRC, MATROSKA,, H264_PARSER) += fate-mkv-1242
fate-mkv-1242: CMD = framecrc -i $(TARGET_SAMPLES)/mkv/1242-small.mkv -c copy -frames:v 11

FATE_SAMPLES_DEMUX-$(call CRC, MLV) += fate-mlv-demux
fate-mlv-demux: CMD = crc -i $(TARGET_SAMPLES)/mlv/M19-0333-cut.MLV -c copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, MOV) += fate-mov-mp3-demux
fate-mov-mp3-demux: CMD = framecrc -i $(TARGET_SAMPLES)/mpegaudio/packed_maindata.mp3.mp4 -c copy

FATE_FFPROBE_DEMUX-$(call PARSERDEMDEC, OPUS, MPEGTS, OPUS) += fate-ts-opus-demux
fate-ts-opus-demux: CMD = ffprobe_demux $(TARGET_SAMPLES)/opus/test-8-7.1.opus-small.ts

FATE_FFPROBE_DEMUX-$(call PARSERDEMDEC, H264, MPEGTS, H264, EXTRACT_EXTRADATA_BSF) += fate-ts-small-demux
fate-ts-small-demux: CMD = ffprobe_demux $(TARGET_SAMPLES)/mpegts/h264small.ts

FATE_SAMPLES_DEMUX-$(call FRAMECRC, MTV,, MPEGAUDIO_PARSER) += fate-mtv
fate-mtv: CMD = framecrc -i $(TARGET_SAMPLES)/mtv/comedian_auto-partial.mtv -c copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, MXF,, MPEG4VIDEO_PARSER) += fate-mxf-demux
fate-mxf-demux: CMD = framecrc -i $(TARGET_SAMPLES)/mxf/C0023S01.mxf -c:a copy -c:v copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, NC,, MPEG4VIDEO_PARSER EXTRACT_EXTRADATA_BSF) += fate-nc-demux
fate-nc-demux: CMD = framecrc -i $(TARGET_SAMPLES)/nc-camera/nc-sample-partial -c:v copy

FATE_SAMPLES_DEMUX-$(call CRC, NISTSPHERE) += fate-nistsphere-demux
fate-nistsphere-demux: CMD = crc -i $(TARGET_SAMPLES)/nistsphere/nist-ulaw.nist -c:a copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, NSV,, VP3_PARSER MPEGAUDIO_PARSER) += fate-nsv-demux
fate-nsv-demux: CMD = framecrc -i $(TARGET_SAMPLES)/nsv/witchblade-51kbps.nsv -t 6 -c:v copy -c:a copy

FATE_FFPROBE_DEMUX-$(call DEMDEC, OGG, OPUS) += fate-oggopus-demux
fate-oggopus-demux: CMD = ffprobe_demux $(TARGET_SAMPLES)/ogg/intro-partial.opus

FATE_SAMPLES_DEMUX-$(call FRAMECRC, OGG, THEORA) += fate-oggtheora-demux
fate-oggtheora-demux: CMD = framecrc -i $(TARGET_SAMPLES)/ogg/empty_theora_packets.ogv -c:v copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, OGG,, VP8_PARSER) += fate-oggvp8-demux
fate-oggvp8-demux: CMD = framecrc -i $(TARGET_SAMPLES)/ogg/videotest.ogv -c:v copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, PAF) += fate-paf-demux
fate-paf-demux: CMD = framecrc -i $(TARGET_SAMPLES)/paf/hod1-partial.paf -c:v copy -c:a copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, PMP) += fate-pmp-demux
fate-pmp-demux: CMD = framecrc -i $(TARGET_SAMPLES)/pmp/demo.pmp -vn -c:a copy

FATE_SAMPLES_DEMUX-$(call CRC, RSD) += fate-rsd-demux
fate-rsd-demux: CMD = crc -i $(TARGET_SAMPLES)/rsd/hum01_partial.rsd -c:a copy

FATE_SAMPLES_DEMUX-$(call CRC, REDSPARK) += fate-redspark-demux
fate-redspark-demux: CMD = crc -i $(TARGET_SAMPLES)/redspark/jingle04_partial.rsd -c:a copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, STR) += fate-psx-str-demux
fate-psx-str-demux: CMD = framecrc -i $(TARGET_SAMPLES)/psx-str/descent-partial.str -c copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, PVA, MPEG2VIDEO, MPEGAUDIO_PARSER) += fate-pva-demux
fate-pva-demux: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/pva/PVA_test-partial.pva -t 0.6 -c:a copy

FATE_SAMPLES_DEMUX-$(call CRC, QCP) += fate-qcp-demux
fate-qcp-demux: CMD = crc -i $(TARGET_SAMPLES)/qcp/0036580847.QCP -c:a copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, RAWVIDEO, RAWVIDEO) += fate-rawvideo-rgb-demux
fate-rawvideo-rgb-demux: CMD = framecrc -f rawvideo -pixel_format rgb24 -video_size 160x500 -stride 512 -i $(TARGET_SAMPLES)/hevc/two_first_slice.mp4

FATE_SAMPLES_DEMUX-$(call FRAMECRC, RAWVIDEO, RAWVIDEO) += fate-rawvideo-yuv-demux
fate-rawvideo-yuv-demux: CMD = framecrc -f rawvideo -pixel_format yuv420p -video_size 250x500 -stride 256,128,128 -i $(TARGET_SAMPLES)/hevc/two_first_slice.mp4

FATE_SAMPLES_DEMUX-$(call FRAMECRC, R3D, JPEG2000 PCM_S32BE) += fate-redcode-demux
fate-redcode-demux: CMD = framecrc -i $(TARGET_SAMPLES)/r3d/4MB-sample.r3d -c:v copy -c:a copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, S337M,, DOLBY_E_PARSER) += fate-s337m-demux
fate-s337m-demux: CMD = framecrc -i $(TARGET_SAMPLES)/dolby_e/16-11 -c copy -ss 2 -t 1

FATE_SAMPLES_DEMUX-$(call FRAMECRC, SIFF) += fate-siff-demux
fate-siff-demux: CMD = framecrc -i $(TARGET_SAMPLES)/SIFF/INTRO_B.VB -c copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, SMJPEG, MJPEG ADPCM_IMA_SMJPEG) += fate-smjpeg-demux
fate-smjpeg-demux: CMD = framecrc -i $(TARGET_SAMPLES)/smjpeg/scenwin.mjpg -c copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, WAV SPDIF,, AC3_PARSER) += fate-wav-ac3
fate-wav-ac3: CMD = framecrc -i $(TARGET_SAMPLES)/ac3/diatonis_invisible_order_anfos_ac3-small.wav -c copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, WSAUD) += fate-westwood-aud
fate-westwood-aud: CMD = framecrc -i $(TARGET_SAMPLES)/westwood-aud/excellent.aud -c copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, WTV, MJPEG, MPEGVIDEO_PARSER MPEGAUDIO_PARSER EXTRACT_EXTRADATA_BSF) += fate-wtv-demux
fate-wtv-demux: CMD = framecrc -i $(TARGET_SAMPLES)/wtv/law-and-order-partial.wtv -c:v copy -c:a copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, XMV) += fate-xmv-demux
fate-xmv-demux: CMD = framecrc -i $(TARGET_SAMPLES)/xmv/logos1p.fmv -c:v copy -c:a copy

FATE_SAMPLES_DEMUX-$(call CRC, XWMA) += fate-xwma-demux
fate-xwma-demux: CMD = crc -i $(TARGET_SAMPLES)/xwma/ergon.xwma -c:a copy

FATE_FFPROBE_DEMUX-$(call PARSERDEMDEC, MPEGVIDEO, MPEGTS, MPEG2VIDEO AC3, EXTRACT_EXTRADATA_BSF) += fate-ts-demux
fate-ts-demux: CMD = ffprobe_demux $(TARGET_SAMPLES)/ac3/mp3ac325-4864-small.ts

FATE_FFPROBE_DEMUX-$(CONFIG_MPEGTS_DEMUXER) += fate-ts-timed-id3-demux
fate-ts-timed-id3-demux: CMD = ffprobe_demux $(TARGET_SAMPLES)/mpegts/id3.ts

tests/data/id3.ts: TAG = GEN
tests/data/id3.ts: $(SAMPLES)/mpegts/id3.ts | tests/data
	$(Q)cp $< $@

tests/data/id3.m3u8: tests/data/id3.ts | tests/data
	$(Q)printf '#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXTINF:2,\nid3.ts\n#EXT-X-ENDLIST\n' > $@

FATE_FFPROBE_DEMUX-$(call DEMDEC, MPEGTS HLS) += fate-ts-timed-id3-hls-demux
fate-ts-timed-id3-hls-demux: tests/data/id3.m3u8
fate-ts-timed-id3-hls-demux: CMD = ffprobe_demux $(TARGET_PATH)/tests/data/id3.m3u8

tests/data/hls-webvtt.m3u8: TAG = GEN
tests/data/hls-webvtt.m3u8: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
        -f lavfi -i "aevalsrc=0:d=6" -c:a mp2fixed -flags +bitexact -muxdelay 0 -muxpreload 0 \
        -f hls -hls_time 2 -hls_list_size 0 \
        -hls_segment_filename $(TARGET_PATH)/tests/data/hls-webvtt-a%d.ts \
        $(TARGET_PATH)/tests/data/hls-webvtt-a.m3u8 2>/dev/null
	$(Q)printf '#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXTINF:2,\nhls-webvtt-s0.vtt\n#EXTINF:2,\nhls-webvtt-s1.vtt\n#EXTINF:2,\nhls-webvtt-s2.vtt\n#EXT-X-ENDLIST\n' > tests/data/hls-webvtt-s.m3u8
	$(Q)printf 'WEBVTT\n\n00:00.500 --> 00:01.500\nfirst\n' > tests/data/hls-webvtt-s0.vtt
	$(Q)printf 'WEBVTT\n\n00:02.500 --> 00:03.500\nsecond\n' > tests/data/hls-webvtt-s1.vtt
	$(Q)printf 'WEBVTT\n\n00:04.500 --> 00:05.500\nthird\n' > tests/data/hls-webvtt-s2.vtt
	$(Q)printf '#EXTM3U\n#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID="s",NAME="en",URI="hls-webvtt-s.m3u8"\n#EXT-X-STREAM-INF:BANDWIDTH=64000,SUBTITLES="s"\nhls-webvtt-a.m3u8\n' > $@

FATE_FFMPEG-$(call FILTERDEMDECENCMUX, AEVALSRC ARESAMPLE, HLS MPEGTS WEBVTT, MP2 PCM_F64LE, MP2FIXED, HLS MPEGTS NULL WEBVTT, LAVFI_INDEV MPEGAUDIO_PARSER PIPE_PROTOCOL) += fate-hls-webvtt-demux
fate-hls-webvtt-demux: tests/data/hls-webvtt.m3u8
fate-hls-webvtt-demux: CLEANFILES = tests/data/hls-webvtt.m3u8 tests/data/hls-webvtt-a.m3u8 tests/data/hls-webvtt-s.m3u8 tests/data/hls-webvtt-a[0-9]*.ts tests/data/hls-webvtt-s[0-9]*.vtt
fate-hls-webvtt-demux: CMD = fmtstdout webvtt -copyts -i $(TARGET_PATH)/tests/data/hls-webvtt.m3u8 -map 0:a -c copy -f null - -map 0:s -c copy

define HLS_WEBVTT_MAP_DATA
tests/data/$(1).m3u8: TAG = GEN
tests/data/$(1).m3u8: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$$(M)$$(TARGET_EXEC) $$(TARGET_PATH)/$$< -nostdin \
        -f lavfi -i "aevalsrc=0:d=6" -c:a mp2fixed -flags +bitexact -muxdelay 0 -muxpreload 0 -output_ts_offset 10 \
        -f hls -hls_time 2 -hls_list_size 0 \
        -hls_segment_filename $$(TARGET_PATH)/tests/data/$(1)-a%d.ts \
        $$(TARGET_PATH)/tests/data/$(1)-a.m3u8 2>/dev/null
	$$(Q)printf '#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXTINF:2,\n$(1)-s0.vtt\n#EXTINF:2,\n$(1)-s1.vtt\n#EXTINF:2,\n$(1)-s2.vtt\n#EXT-X-ENDLIST\n' > tests/data/$(1)-s.m3u8
	$$(Q)printf 'WEBVTT\nX-TIMESTAMP-MAP=MPEGTS:900000,LOCAL:00:00:00.000\n\n00:00.500 --> 00:01.500\nfirst\n\n00:01.000 --> 00:04.500 line:1\nlong\n' > tests/data/$(1)-s0.vtt
	$$(Q)printf 'WEBVTT\nX-TIMESTAMP-MAP=MPEGTS:900000,LOCAL:00:00:00.000\n\n00:01.000 --> 00:04.500 line:1\nlong\n\n00:03.000 --> 00:03.900 align:start\nsecond\n' > tests/data/$(1)-s1.vtt
	$$(Q)printf 'WEBVTT\nX-TIMESTAMP-MAP=MPEGTS:900000,LOCAL:00:00:00.000\n\n00:01.000 --> 00:04.500 line:1\nlong\n\n00:04.500 --> 00:05.500\nthird\n' > tests/data/$(1)-s2.vtt
	$$(Q)printf '#EXTM3U\n#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID="s",NAME="en",URI="$(1)-s.m3u8"\n#EXT-X-STREAM-INF:BANDWIDTH=64000,SUBTITLES="s"\n$(1)-a.m3u8\n' > $$@

fate-$(1)-demux: tests/data/$(1).m3u8
fate-$(1)-demux: CLEANFILES = tests/data/$(1).m3u8 tests/data/$(1)-a.m3u8 tests/data/$(1)-s.m3u8 tests/data/$(1)-a[0-9]*.ts tests/data/$(1)-s[0-9]*.vtt
endef

$(eval $(call HLS_WEBVTT_MAP_DATA,hls-webvtt-map))
$(eval $(call HLS_WEBVTT_MAP_DATA,hls-webvtt-seek))

FATE_HLS_WEBVTT_MAP-$(call FILTERDEMDECENCMUX, AEVALSRC ARESAMPLE, HLS MPEGTS WEBVTT, MP2 PCM_F64LE, MP2FIXED, HLS MPEGTS NULL WEBVTT, LAVFI_INDEV MPEGAUDIO_PARSER PIPE_PROTOCOL) += fate-hls-webvtt-map-demux fate-hls-webvtt-seek-demux
FATE_FFMPEG += $(FATE_HLS_WEBVTT_MAP-yes)
fate-hls-webvtt-map-demux: CMD = fmtstdout webvtt -copyts -i $(TARGET_PATH)/tests/data/hls-webvtt-map.m3u8 -map 0:a -c copy -f null - -map 0:s -c copy
fate-hls-webvtt-seek-demux: CMD = fmtstdout webvtt -copyts -ss 4 -i $(TARGET_PATH)/tests/data/hls-webvtt-seek.m3u8 -map 0:s -c copy

tests/data/hls-webvtt-discont.m3u8: TAG = GEN
tests/data/hls-webvtt-discont.m3u8: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
        -f lavfi -i "aevalsrc=0:d=2" -c:a mp2fixed -flags +bitexact -muxdelay 0 -muxpreload 0 -output_ts_offset 10 \
        -f mpegts -y $(TARGET_PATH)/tests/data/hls-webvtt-discont-a0.ts 2>/dev/null
	$(Q)cp tests/data/hls-webvtt-discont-a0.ts tests/data/hls-webvtt-discont-a1.ts
	$(Q)printf '#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXTINF:2,\nhls-webvtt-discont-a0.ts\n#EXT-X-DISCONTINUITY\n#EXTINF:2,\nhls-webvtt-discont-a1.ts\n#EXT-X-ENDLIST\n' > tests/data/hls-webvtt-discont-a.m3u8
	$(Q)printf 'WEBVTT\nX-TIMESTAMP-MAP=MPEGTS:900000,LOCAL:00:00:00.000\n\n00:00.500 --> 00:01.500\nhello\n' > tests/data/hls-webvtt-discont-s0.vtt
	$(Q)cp tests/data/hls-webvtt-discont-s0.vtt tests/data/hls-webvtt-discont-s1.vtt
	$(Q)printf '#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXTINF:2,\nhls-webvtt-discont-s0.vtt\n#EXT-X-DISCONTINUITY\n#EXTINF:2,\nhls-webvtt-discont-s1.vtt\n#EXT-X-ENDLIST\n' > tests/data/hls-webvtt-discont-s.m3u8
	$(Q)printf '#EXTM3U\n#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID="s",NAME="en",URI="hls-webvtt-discont-s.m3u8"\n#EXT-X-STREAM-INF:BANDWIDTH=64000,SUBTITLES="s"\nhls-webvtt-discont-a.m3u8\n' > $@

FATE_HLS_WEBVTT_DISCONT-$(call FILTERDEMDECENCMUX, AEVALSRC ARESAMPLE, HLS MPEGTS WEBVTT, MP2 PCM_F64LE, MP2FIXED, MPEGTS NULL WEBVTT, LAVFI_INDEV MPEGAUDIO_PARSER PIPE_PROTOCOL) += fate-hls-webvtt-discont-demux
FATE_FFMPEG += $(FATE_HLS_WEBVTT_DISCONT-yes)
fate-hls-webvtt-discont-demux: tests/data/hls-webvtt-discont.m3u8
fate-hls-webvtt-discont-demux: CLEANFILES = tests/data/hls-webvtt-discont.m3u8 tests/data/hls-webvtt-discont-a.m3u8 tests/data/hls-webvtt-discont-s.m3u8 tests/data/hls-webvtt-discont-a[0-9]*.ts tests/data/hls-webvtt-discont-s[0-9]*.vtt
fate-hls-webvtt-discont-demux: CMD = fmtstdout webvtt -copyts -i $(TARGET_PATH)/tests/data/hls-webvtt-discont.m3u8 -map 0:a -c copy -f null - -map 0:s -c copy

tests/data/hls-webvtt-wrap.m3u8: TAG = GEN
tests/data/hls-webvtt-wrap.m3u8: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
        -f lavfi -i "aevalsrc=0:d=6" -c:a mp2fixed -flags +bitexact -muxdelay 0 -muxpreload 0 -output_ts_offset 95442 \
        -f hls -hls_time 2 -hls_list_size 0 \
        -hls_segment_filename $(TARGET_PATH)/tests/data/hls-webvtt-wrap-a%d.ts \
        $(TARGET_PATH)/tests/data/hls-webvtt-wrap-a.m3u8 2>/dev/null
	$(Q)printf '#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXTINF:2,\nhls-webvtt-wrap-s0.vtt\n#EXTINF:2,\nhls-webvtt-wrap-s1.vtt\n#EXTINF:2,\nhls-webvtt-wrap-s2.vtt\n#EXT-X-ENDLIST\n' > tests/data/hls-webvtt-wrap-s.m3u8
	$(Q)printf 'WEBVTT\nX-TIMESTAMP-MAP=MPEGTS:8589780020,LOCAL:00:00:00.000\n\n00:00.500 --> 00:01.500\nbefore\n\n00:01.500 --> 00:02.500\nacross\n' > tests/data/hls-webvtt-wrap-s0.vtt
	$(Q)printf 'WEBVTT\nX-TIMESTAMP-MAP=MPEGTS:25428,LOCAL:00:00:02.000\n\n00:01.500 --> 00:02.500\nacross\n\n00:03.000 --> 00:03.900\nafter\n' > tests/data/hls-webvtt-wrap-s1.vtt
	$(Q)printf 'WEBVTT\nX-TIMESTAMP-MAP=MPEGTS:205428,LOCAL:00:00:04.000\n\n00:04.500 --> 00:05.500\nlast\n' > tests/data/hls-webvtt-wrap-s2.vtt
	$(Q)printf '#EXTM3U\n#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID="s",NAME="en",URI="hls-webvtt-wrap-s.m3u8"\n#EXT-X-STREAM-INF:BANDWIDTH=64000,SUBTITLES="s"\nhls-webvtt-wrap-a.m3u8\n' > $@

FATE_HLS_WEBVTT_WRAP-$(call FILTERDEMDECENCMUX, AEVALSRC ARESAMPLE, HLS MPEGTS WEBVTT, MP2 PCM_F64LE, MP2FIXED, HLS MPEGTS NULL FRAMECRC, LAVFI_INDEV MPEGAUDIO_PARSER PIPE_PROTOCOL) += fate-hls-webvtt-wrap-demux
FATE_FFMPEG += $(FATE_HLS_WEBVTT_WRAP-yes)
fate-hls-webvtt-wrap-demux: tests/data/hls-webvtt-wrap.m3u8
fate-hls-webvtt-wrap-demux: CLEANFILES = tests/data/hls-webvtt-wrap.m3u8 tests/data/hls-webvtt-wrap-a.m3u8 tests/data/hls-webvtt-wrap-s.m3u8 tests/data/hls-webvtt-wrap-a[0-9]*.ts tests/data/hls-webvtt-wrap-s[0-9]*.vtt
fate-hls-webvtt-wrap-demux: CMD = framecrc -copyts -i $(TARGET_PATH)/tests/data/hls-webvtt-wrap.m3u8 -map 0:a -c copy -f null - -map 0:s -c copy

tests/data/hls-self-variant.m3u8: TAG = GEN
tests/data/hls-self-variant.m3u8: | tests/data
	$(Q)printf '#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=64000\nhls-self-variant.m3u8\n#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID="s",NAME="en",URI="hls-self-variant-missing.m3u8"\n#EXTINF:2,\nhls-self-variant.vtt\n' > $@

FATE_FFPROBE-$(call DEMDEC, HLS) += fate-hls-self-variant-demux
fate-hls-self-variant-demux: tests/data/hls-self-variant.m3u8
fate-hls-self-variant-demux: CLEANFILES = tests/data/hls-self-variant.m3u8
fate-hls-self-variant-demux: CMD = ffprobe_demux $(TARGET_PATH)/tests/data/hls-self-variant.m3u8

tests/data/hls-shared-renditions.m3u8: TAG = GEN
tests/data/hls-shared-renditions.m3u8: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
        -f lavfi -i "aevalsrc=0:s=16000:d=2" -c:a mp2fixed -flags +bitexact -muxdelay 0 -muxpreload 0 \
        -f hls -hls_time 2 -hls_list_size 0 \
        -hls_segment_filename $(TARGET_PATH)/tests/data/hls-shared-renditions-a%d.ts \
        $(TARGET_PATH)/tests/data/hls-shared-renditions-a.m3u8 2>/dev/null
	$(Q)printf '#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXTINF:2,\nhls-shared-renditions-s0.vtt\n#EXT-X-ENDLIST\n' > tests/data/hls-shared-renditions-s.m3u8
	$(Q)printf 'WEBVTT\n\n00:00.500 --> 00:01.500\nfirst\n' > tests/data/hls-shared-renditions-s0.vtt
	$(Q)printf '#EXTM3U\n#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID="g",NAME="German",LANGUAGE="de",URI="hls-shared-renditions-s.m3u8"\n#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID="g",NAME="English",LANGUAGE="en",DEFAULT=YES\n#EXT-X-STREAM-INF:BANDWIDTH=64000,AUDIO="g",SUBTITLES="g"\nhls-shared-renditions-a.m3u8\n#EXT-X-STREAM-INF:BANDWIDTH=128000,AUDIO="g",SUBTITLES="g"\nhls-shared-renditions-a.m3u8\n' > $@

FATE_HLS_SHARED_RENDITIONS-$(call FILTERDEMDECENCMUX, AEVALSRC ARESAMPLE, HLS MPEGTS WEBVTT, MP3FLOAT PCM_F64LE, MP2FIXED, HLS MPEGTS, LAVFI_INDEV MPEGAUDIO_PARSER) += fate-hls-shared-renditions-demux
FATE_FFMPEG_FFPROBE += $(FATE_HLS_SHARED_RENDITIONS-yes)
fate-hls-shared-renditions-demux: tests/data/hls-shared-renditions.m3u8
fate-hls-shared-renditions-demux: CLEANFILES = tests/data/hls-shared-renditions*.m3u8 tests/data/hls-shared-renditions-*.ts tests/data/hls-shared-renditions-*.vtt
fate-hls-shared-renditions-demux: CMD = ffprobe_demux $(TARGET_PATH)/tests/data/hls-shared-renditions.m3u8 -show_programs

tests/data/hls-start-master.m3u8: TAG = GEN
tests/data/hls-start-master.m3u8: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
        -f lavfi -i "aevalsrc=0:s=16000:d=0.576" -c:a mp2fixed -flags +bitexact -muxdelay 0 -muxpreload 0 \
        -f hls -hls_time 0.144 -hls_list_size 0 -hls_flags omit_endlist \
        -hls_segment_filename $(TARGET_PATH)/tests/data/hls-start-%d.ts \
        $(TARGET_PATH)/tests/data/hls-start-rendition.m3u8 2>/dev/null
	$(Q)printf '#EXTM3U\n#EXT-X-START:TIME-OFFSET=0.5\n#EXT-X-TARGETDURATION:0\n#EXTINF:0.144,\nhls-start-0.ts\n#EXTINF:0.144,\nhls-start-1.ts\n#EXTINF:0.144,\nhls-start-2.ts\n#EXTINF:0.144,\nhls-start-3.ts\n' > tests/data/hls-start-media.m3u8
	$(Q)printf '#EXTM3U\n#EXT-X-START:TIME-OFFSET=0.5\n#EXT-X-ENDLIST\n#EXT-X-TARGETDURATION:0\n#EXTINF:0.144,\nhls-start-0.ts\n#EXTINF:0.144,\nhls-start-1.ts\n#EXTINF:0.144,\nhls-start-2.ts\n#EXTINF:0.144,\nhls-start-3.ts\n' > tests/data/hls-start-endlist.m3u8
	$(Q)printf '#EXTM3U\n#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID="a",NAME="a",URI="hls-start-rendition.m3u8"\n#EXT-X-STREAM-INF:BANDWIDTH=64000,AUDIO="a"\nhls-start-media.m3u8\n' > tests/data/hls-start-variant.m3u8
	$(Q)printf '#EXTM3U\n#EXT-X-START:PRECISE=NO,TIME-OFFSET=0.1\n#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID="a",NAME="a",URI="hls-start-rendition.m3u8"\n#EXT-X-STREAM-INF:BANDWIDTH=64000,AUDIO="a"\nhls-start-media.m3u8\n' > $@

FATE_HLS_START = fate-hls-start-master-demux  \
                 fate-hls-start-variant-demux \
                 fate-hls-start-media-demux   \
                 fate-hls-start-endlist-demux

FATE_HLS_START-$(call FILTERDEMDECENCMUX, AEVALSRC ARESAMPLE, HLS MPEGTS, MP2 MP3FLOAT PCM_F64LE, MP2FIXED, HLS MPEGTS, LAVFI_INDEV MPEGAUDIO_PARSER) += $(FATE_HLS_START)
FATE_FFMPEG_FFPROBE += $(FATE_HLS_START-yes)
$(FATE_HLS_START): tests/data/hls-start-master.m3u8
fate-hls-start-master-demux: CMD = ffprobe_demux $(TARGET_PATH)/tests/data/hls-start-master.m3u8 -prefer_x_start 1 -max_reload 1
fate-hls-start-variant-demux: CMD = ffprobe_demux $(TARGET_PATH)/tests/data/hls-start-variant.m3u8 -prefer_x_start 1 -max_reload 1
fate-hls-start-media-demux: CMD = ffprobe_demux $(TARGET_PATH)/tests/data/hls-start-media.m3u8 -prefer_x_start 1 -max_reload 1
fate-hls-start-endlist-demux: CMD = ffprobe_demux $(TARGET_PATH)/tests/data/hls-start-endlist.m3u8 -prefer_x_start 1 -max_reload 1

FATE_SAMPLES_DEMUX-$(call PARSERDEM, JPEGXS, IMAGE_JPEGXS_PIPE, CONCAT_PROTOCOL) += fate-jxs-concat-demux
fate-jxs-concat-demux: CMD = framecrc "-i concat:$(TARGET_SAMPLES)/jxs/lena.jxs|$(TARGET_SAMPLES)/jxs/lena.jxs -c:v copy"

# 21-byte crafted VPK: header parses (nb_channels=80) but adpcm_psx open
# fails. After find_stream_info the layout must stay 80, not 0.
# ffprobe cannot be used: it aborts on the failed decoder open before
# printing -show_entries.
FATE_FFMPEG-$(call ALLYES, VPK_DEMUXER ADPCM_PSX_DECODER PIPE_PROTOCOL NULL_MUXER) += fate-demux-vpk-div0
fate-demux-vpk-div0: CMD = printf "\040\113\120\126\126\120\000\370\004\000\073\003\141\071\126\062\066\066\060\070\120" | ffmpeg -i pipe: -map 0 -c copy -f null -
fate-demux-vpk-div0: CMP = grep
fate-demux-vpk-div0: REF = 80 channels

FATE_SAMPLES_DEMUX += $(FATE_SAMPLES_DEMUX-yes)
FATE_SAMPLES_FFMPEG += $(FATE_SAMPLES_DEMUX)
FATE_FFPROBE_DEMUX   += $(FATE_FFPROBE_DEMUX-yes)
FATE_SAMPLES_FFPROBE += $(FATE_FFPROBE_DEMUX)
fate-demux: $(FATE_SAMPLES_DEMUX) $(FATE_FFPROBE_DEMUX)
