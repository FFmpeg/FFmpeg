FATE_MATROSKA-$(call ALLYES, MATROSKA_DEMUXER ZLIB) += fate-matroska-prores-zlib
fate-matroska-prores-zlib: CMD = framecrc -i $(TARGET_SAMPLES)/mkv/prores_zlib.mkv -c:v copy

# This tests that the matroska demuxer correctly adds the icpf header atom
# upon demuxing; it also tests bz2 decompression and unknown-length cluster.
FATE_MATROSKA-$(call ALLYES, MATROSKA_DEMUXER BZLIB) += fate-matroska-prores-header-insertion-bz2
fate-matroska-prores-header-insertion-bz2: CMD = framecrc -i $(TARGET_SAMPLES)/mkv/prores_bz2.mkv -map 0 -c copy

# This tests that the matroska demuxer supports modifying the colorspace
# properties in remuxing (-c:v copy)
# It also tests automatic insertion of the vp9_superframe bitstream filter
FATE_MATROSKA-$(call DEMMUX, MATROSKA, MATROSKA) += fate-matroska-remux
fate-matroska-remux: CMD = md5pipe -i $(TARGET_SAMPLES)/vp9-test-vectors/vp90-2-2pass-akiyo.webm -color_trc 4 -c:v copy -fflags +bitexact -strict -2 -f matroska
fate-matroska-remux: CMP = oneline
fate-matroska-remux: REF = b9c7b650349972c9dce42ab79b472917

FATE_MATROSKA-$(call ALLYES, MATROSKA_DEMUXER VORBIS_PARSER) += fate-matroska-xiph-lacing
fate-matroska-xiph-lacing: CMD = framecrc -i $(TARGET_SAMPLES)/mkv/xiph_lacing.mka -c:a copy

# This tests that the Matroska demuxer correctly demuxes WavPack
# without CodecPrivate; it also tests zlib compressed WavPack.
FATE_MATROSKA-$(call ALLYES, MATROSKA_DEMUXER ZLIB) += fate-matroska-wavpack-missing-codecprivate
fate-matroska-wavpack-missing-codecprivate: CMD = framecrc -i $(TARGET_SAMPLES)/mkv/wavpack_missing_codecprivate.mka -c copy

# This tests that the matroska demuxer supports decompressing
# zlib compressed tracks (both the CodecPrivate as well as the actual frames).
FATE_MATROSKA-$(call ALLYES, MATROSKA_DEMUXER ZLIB) += fate-matroska-zlib-decompression
fate-matroska-zlib-decompression: CMD = framecrc -i $(TARGET_SAMPLES)/mkv/subtitle_zlib.mks -c:s copy

# This tests that the matroska demuxer can decompress lzo compressed tracks.
FATE_MATROSKA-$(CONFIG_MATROSKA_DEMUXER) += fate-matroska-lzo-decompression
fate-matroska-lzo-decompression: CMD = framecrc -i $(TARGET_SAMPLES)/mkv/lzo.mka -c copy

# This tests that the ALAC extradata is correctly transformed upon remuxing.
# It also tests setting the AV_DISPOSITION_COMMENT disposition as well as
# writing creation_time metadata.
FATE_MATROSKA_FFMPEG_FFPROBE-$(call REMUX, MATROSKA, MOV_DEMUXER) += fate-matroska-alac-remux
fate-matroska-alac-remux: CMD = transcode mov $(TARGET_SAMPLES)/lossless-audio/inside.m4a matroska "-map 0:a -c copy -metadata creation_time=2009-01-25T16:08:26.000000Z -disposition +comment" "-c copy" "-show_entries format_tags:stream_disposition"

# This tests that the matroska demuxer correctly propagates
# the channel layout contained in vorbis comments in the CodecPrivate
# of flac tracks. It also tests header removal compression.
FATE_MATROSKA-$(call ALLYES, MATROSKA_DEMUXER FLAC_PARSER) += fate-matroska-flac-channel-mapping
fate-matroska-flac-channel-mapping: CMD = framecrc -i $(TARGET_SAMPLES)/mkv/flac_channel_layouts.mka -map 0 -c:a copy

# This tests that the Matroska muxer writes the channel layout
# of FLAC tracks as a Vorbis comment in the CodecPrivate if necessary
# and that FLAC extradata is correctly updated when a packet
# with sidedata containing new extradata is encountered.
# Furthermore it tests everything the matroska-flac-channel-mapping test
# tests and it also tests the FLAC decoder and encoder, in particular
# the latter's ability to send updated extradata.
FATE_MATROSKA-$(call ALLYES, FLAC_DECODER FLAC_ENCODER FLAC_PARSER \
                MATROSKA_DEMUXER MATROSKA_MUXER) += fate-matroska-flac-extradata-update
fate-matroska-flac-extradata-update: CMD = transcode matroska $(TARGET_SAMPLES)/mkv/flac_channel_layouts.mka \
                                           matroska "-map 0 -map 0:0 -c flac -frames:a:2 8" "-map 0 -c copy"

# This tests that the Matroska/WebM muxer writes the AV1 CodecPrivate
# via extradata obtained from packet side data. It also tests that
# the aspect ratio is only written with pixels as DisplayUnit for WebM.
FATE_MATROSKA-$(call REMUX, WEBM MATROSKA, IVF_DEMUXER AV1_PARSER EXTRACT_EXTRADATA_BSF) += fate-webm-av1-extradata-update
fate-webm-av1-extradata-update: CMD = transcode ivf $(TARGET_SAMPLES)/av1/decode_model.ivf webm "-c copy -bsf extract_extradata -sar 3:1" "-c copy" "" "" "-nofind_stream_info" "-nofind_stream_info"

# This test tests demuxing Vorbis and chapters from ogg and muxing it in and
# demuxing it from Matroska/WebM. It furthermore tests the WebM muxer, in
# particular its DASH mode. Finally, it tests writing the Cues at the front.
FATE_MATROSKA_FFMPEG_FFPROBE-$(call ALLYES, MATROSKA_DEMUXER OGG_DEMUXER  \
                                 VORBIS_DECODER VORBIS_PARSER WEBM_MUXER) \
                               += fate-webm-dash-chapters
fate-webm-dash-chapters: CMD = transcode ogg $(TARGET_SAMPLES)/vorbis/vorbis_chapter_extension_demo.ogg webm "-c copy -cluster_time_limit 1500 -dash 1 -dash_track_number 124 -reserve_index_space 400" "-c copy -t 0.5" -show_chapters

# The input file has a Block whose payload has a size of zero before reversing
# header removal compression; it furthermore uses chained SeekHeads and has
# level 1-elements after the Cluster. This is tested on the demuxer's side.
# For the muxer this tests that it can correctly write huge TrackNumbers and
# that it can expand the Cues element's length field by one byte if necessary.
# It furthermore tests correct propagation of the description tag.
FATE_MATROSKA_FFMPEG_FFPROBE-$(call DEMMUX, MATROSKA, MATROSKA) \
                               += fate-matroska-zero-length-block
fate-matroska-zero-length-block: CMD = transcode matroska $(TARGET_SAMPLES)/mkv/zero_length_block.mks matroska "-c:s copy -dash 1 -dash_track_number 2000000000 -reserve_index_space 62 -metadata_header_padding 1 -default_mode infer_no_subs" "-c:s copy" "-show_entries stream_tags=description"

# This mainly tests the Matroska muxer's ability to shift the data
# to create enough free space to write the Cues at the front.
# The metadata_header_padding has been chosen so that three attempts
# to write the Cues are necessary.
# It also tests writing PCM audio in both endiannesses and putting
# Cues with the same timestamp in the same CuePoint as well as
# omitting CRC-32 elements when writing Matroska.
FATE_MATROSKA-$(call TRANSCODE, PCM_S24BE PCM_S24LE, MATROSKA, WAV_DEMUXER) \
                += fate-matroska-move-cues-to-front
fate-matroska-move-cues-to-front: CMD = transcode wav $(TARGET_SAMPLES)/audio-reference/divertimenti_2ch_96kHz_s24.wav matroska "-map 0 -map 0 -c:a:0 pcm_s24be -c:a:1 copy -cluster_time_limit 5 -cues_to_front yes -metadata_header_padding 7840 -write_crc32 0" "-map 0 -c copy -t 0.1"

# This test covers the case in which a displaymatrix is not a rotation
# and is therefore ignored by the muxer, i.e. the ffprobe output of
# side data should be empty.
FATE_MATROSKA_FFMPEG_FFPROBE-$(call REMUX, MATROSKA, MOV_DEMUXER H264_PARSER H264_DECODER) \
                               += fate-matroska-non-rotation-displaymatrix
fate-matroska-non-rotation-displaymatrix: CMD = transcode mov $(TARGET_SAMPLES)/mov/displaymatrix.mov matroska \
    "-c copy -frames:v 5" \
    "-c copy" \
    "-show_entries stream_side_data_list"

# This tests DOVI (reading from MP4 and Matroska and writing to Matroska)
# as well as writing the Cues at the front (by shifting data) if
# the initially reserved amount of space turns out to be insufficient.
FATE_MATROSKA_FFMPEG_FFPROBE-$(call REMUX, MATROSKA, MOV_DEMUXER HEVC_DECODER) \
                               += fate-matroska-dovi-write-config7
fate-matroska-dovi-write-config7: CMD = transcode mov $(TARGET_SAMPLES)/mov/dovi-p7.mp4 matroska "-map 0 -c copy -cues_to_front yes -reserve_index_space 40  -metadata_header_padding 64339" "-map 0 -c copy" "-show_entries stream_side_data_list"

FATE_MATROSKA_FFMPEG_FFPROBE-$(call REMUX, MATROSKA, MOV_DEMUXER     \
                                           HEVC_DECODER AAC_DECODER) \
                               += fate-matroska-dovi-write-config8
fate-matroska-dovi-write-config8: CMD = transcode mov $(TARGET_SAMPLES)/hevc/dv84.mov matroska "-c copy" "-map 0 -c copy -t 0.4" "-show_entries stream_side_data_list -select_streams v"

# This tests the scenario like tickets #4536, #5784 where
# the first packet (with the overall lowest dts) is a video packet,
# whereas an audio packet to be muxed later has the overall lowest pts
# which happens to be negative and therefore needs to be shifted.
# (-ss 1.09 ensures that a video frame has the lowest dts of all packets;
# yet there is an audio packet with the overall lowest pts. output_ts_offset
# makes the pts of the audio packet, but not the leading video packet negative
# so that we run into the above issue.)
FATE_MATROSKA-$(call REMUX, MATROSKA, MPEGTS_DEMUXER MPEGVIDEO_PARSER \
                            MPEG2VIDEO_DECODER EXTRACT_EXTRADATA_BSF  \
                            MP3FLOAT_DECODER) \
                += fate-matroska-avoid-negative-ts
fate-matroska-avoid-negative-ts: CMD = transcode mpegts $(TARGET_SAMPLES)/mpeg2/t.mpg matroska "-c copy -ss 1.09 -output_ts_offset -60ms" "-c copy -t 0.4"

# This tests writing the MS-compatibility modes V_MS/VFW/FOURCC and A_MS/ACM.
# It furthermore tests writing the Cues at the front if the cues_to_front
# option is set and more than enough space has been reserved in advance.
# (Btw: The keyframe flags of the input video stream seem wrong.)
FATE_MATROSKA-$(call REMUX, MATROSKA, AVI_DEMUXER SPEEX_DECODER) += fate-matroska-ms-mode
fate-matroska-ms-mode: CMD = transcode avi $(TARGET_SAMPLES)/vp5/potter512-400-partial.avi matroska "-map 0 -c copy -cues_to_front yes -reserve_index_space 5000" "-map 0 -c copy -t 1"

# This tests Matroska's QT-compatibility mode.
FATE_MATROSKA-$(call REMUX, MATROSKA, MOV_DEMUXER) += fate-matroska-qt-mode
fate-matroska-qt-mode: CMD = transcode mov $(TARGET_SAMPLES)/svq1/marymary-shackles.mov matroska "-c copy" "-c copy -t 3"

# This test the following features of the Matroska muxer: Writing projection
# stream side-data; not setting any track to default if the user requested it;
# and modifying and writing colorspace properties.
FATE_MATROSKA_FFMPEG_FFPROBE-$(call REMUX, MATROSKA, H264_DECODER H264_PARSER) \
                               += fate-matroska-spherical-mono-remux
fate-matroska-spherical-mono-remux: CMD = transcode matroska $(TARGET_SAMPLES)/mkv/spherical.mkv matroska "-map 0 -map 0 -c copy -disposition:0 -default+forced -disposition:1 -default -default_mode passthrough -color_primaries:1 bt709 -color_trc:1 smpte170m -colorspace:1 bt2020c -color_range:1 pc"  "-map 0 -c copy -t 0" "-show_entries stream_side_data_list:stream_disposition=default,forced:stream=color_range,color_space,color_primaries,color_transfer"

# The input file of the following test contains Content Light Level as well as
# Mastering Display Metadata and so this test tests correct muxing and demuxing
# of these. It furthermore also tests that this data is correctly propagated
# when reencoding (here to ffv1).
# Both input audio tracks are completely zero, so the noise bsf is used
# to make this test interesting.
FATE_MATROSKA_FFMPEG_FFPROBE-$(call TRANSCODE, FFV1 PRORES, MATROSKA, MXF_DEMUXER \
                                               PCM_S24LE_DECODER ARESAMPLE_FILTER \
                                               PCM_S16BE_ENCODER NOISE_BSF)       \
                               += fate-matroska-mastering-display-metadata
fate-matroska-mastering-display-metadata: CMD = transcode mxf $(TARGET_SAMPLES)/mxf/Meridian-Apple_ProResProxy-HDR10.mxf matroska "-map 0 -map 0:0 -c:v:0 copy -c:v:1 ffv1 -c:a:0 copy -bsf:a:0 noise=amount=3 -filter:a:1 aresample -c:a:1 pcm_s16be -bsf:a:1 noise=amount=-1:drop=-4" "-map 0 -c copy" "-show_entries stream_side_data_list:stream=index,codec_name"

# This test tests remuxing annex B H.264 into Matroska. It also tests writing
# the correct interlaced flags and overriding the sample aspect ratio, leading
# to anamorphic video. Given that the input file has lots of filler material,
# the h264_metadata filter is used to remove it as well as the H.264 AUD.
# The video is decoded twice to show that this did not change the decoded
# output. Furthermore, this also tests writing PCM with bitdepth 32.
FATE_MATROSKA_FFMPEG_FFPROBE-$(call TRANSCODE, PCM_S32LE MP2, MATROSKA,      \
                                               MPEGTS_DEMUXER H264_PARSER    \
                                               H264_DECODER MPEGAUDIO_PARSER \
                                               EXTRACT_EXTRADATA_BSF         \
                                               H264_METADATA_BSF             \
                                               ARESAMPLE_FILTER              \
                                               PCM_S32BE_ENCODER)            \
                               += fate-matroska-h264-remux
fate-matroska-h264-remux: CMD = transcode mpegts $(TARGET_SAMPLES)/h264/h264_intra_first-small.ts matroska "-map 0:0 -map 0 -c:v copy -sar:0 3:4 -bsf:v:1 h264_metadata=aud=remove:delete_filler=1 -disposition:v +hearing_impaired -af aresample -c:a:0 pcm_s32le -c:a:1 pcm_s32be -disposition:a:0 original -metadata:s:a:0 title=swedish_silence -metadata:s:a:1 title=norwegian_silence -disposition:a:1 dub" "-map 0:v" "-show_entries stream=index,codec_name:stream_tags=title,language"

# Tests writing BlockAdditional and BlockGroups with ReferenceBlock elements;
# it also tests setting a track as suitable for hearing impaired.
# It also tests the capability of the VP8 parser to set the keyframe flag
# (the input file lacks ReferenceBlock elements making everything a keyframe).
FATE_MATROSKA_FFMPEG_FFPROBE-$(call REMUX, MATROSKA, VP8_PARSER)  \
                               += fate-matroska-vp8-alpha-remux
fate-matroska-vp8-alpha-remux: CMD = transcode matroska $(TARGET_SAMPLES)/vp8_alpha/vp8_video_with_alpha.webm matroska "-c copy -disposition +hearing_impaired -cluster_size_limit 100000" "-c copy -t 0.2" "-show_entries stream_disposition:stream_side_data_list"

# The audio stream to be remuxed here has AV_DISPOSITION_VISUAL_IMPAIRED.
FATE_MATROSKA_FFMPEG_FFPROBE-$(call REMUX, MATROSKA, MPEGTS_DEMUXER AC3_DECODER) \
                               += fate-matroska-mpegts-remux
fate-matroska-mpegts-remux: CMD = transcode mpegts $(TARGET_SAMPLES)/mpegts/pmtchange.ts matroska "-map 0:2 -map 0:2 -c copy -disposition:a:1 -visual_impaired+hearing_impaired -default_mode infer" "-map 0 -c copy" "-show_entries stream_disposition:stream=index"

# Tests maintaining codec delay while remuxing from Matroska.
# For some reason, ffmpeg shifts the timestamps of the input file
# to make them zero before reaching the muxer while it does not
# for the ogg-opus-remux test. -avoid_negative_ts make_zero counters this.
FATE_MATROSKA_FFMPEG_FFPROBE-$(call REMUX, MATROSKA, OPUS_PARSER OPUS_DECODER) += fate-matroska-opus-remux
fate-matroska-opus-remux: CMD = transcode matroska $(TARGET_SAMPLES)/mkv/codec_delay_opus.mkv matroska "-avoid_negative_ts make_zero -c copy" "-copyts -c copy" "-show_packets -show_entries stream=codec_name,initial_padding -read_intervals %0.05"

# Tests maintaining codec delay while remuxing from ogg.
FATE_MATROSKA_FFMPEG_FFPROBE-$(call REMUX, MATROSKA, OGG_DEMUXER OPUS_PARSER OPUS_DECODER) += fate-matroska-ogg-opus-remux
fate-matroska-ogg-opus-remux: CMD = transcode ogg $(TARGET_SAMPLES)/ogg/intro-partial.opus matroska "-c copy" "-copyts -c copy" "-show_packets -show_entries stream=codec_name,initial_padding -read_intervals %0.05"

# This tests reencoding with an audio encoder that adds initial padding.
# The initial padding is currently not maintained.
FATE_MATROSKA_FFMPEG_FFPROBE-$(call REMUX, MATROSKA, MXF_DEMUXER PCM_S16LE_DECODER \
                                           MP2FIXED_ENCODER ARESAMPLE_FILTER       \
                                           MPEG2VIDEO_DECODER MPEGVIDEO_PARSER     \
                                           EXTRACT_EXTRADATA_BSF) += fate-matroska-encoding-delay
fate-matroska-encoding-delay: CMD = transcode mxf $(TARGET_SAMPLES)/mxf/Sony-00001.mxf matroska "-c:v copy -af aresample -c:a mp2fixed" "-copyts -c copy" "-show_packets -show_entries stream=codec_name,initial_padding -read_intervals %0.05"

FATE_MATROSKA-$(call REMUX, MATROSKA, SUP_DEMUXER) += fate-matroska-pgs-remux
fate-matroska-pgs-remux: CMD = transcode sup $(TARGET_SAMPLES)/sub/pgs_sub.sup matroska "-copyts -c:s copy" "-copyts -c:s copy"

# This test uses the setts bsf to derive the duration of every packet
# except the last from the next packet's pts.
FATE_MATROSKA-$(call REMUX, MATROSKA, SUP_DEMUXER PGS_FRAME_MERGE_BSF SETTS_BSF) += fate-matroska-pgs-remux-durations
fate-matroska-pgs-remux-durations: CMD = transcode sup $(TARGET_SAMPLES)/sub/pgs_sub.sup matroska "-copyts -c:s copy -bsf pgs_frame_merge,setts=duration=if(gt(DURATION\,0)\,DURATION\,if(eq(PTS\,NOPTS)\,0\,if(eq(NEXT_PTS\,NOPTS)\,0\,NEXT_PTS-PTS))):pts=PTS" "-copyts -c:s copy"

# This test muxes DVB subtitles twice into Matroska: Once normally
# and once with durations derived via the setts filter. Said filter
# sets the duration for every packet except the last it receives.
# The "-t 20" also tests that the BSF is properly flushed even
# when processing ended due to something else than the input's EOF.
# Notice that the last packet of stream 0 before 20s is present,
# but has no duration (like stream 1).
FATE_MATROSKA-$(call REMUX, MATROSKA, MPEGTS_DEMUXER DVBSUB_PARSER SETTS_BSF) += fate-matroska-dvbsub-remux
fate-matroska-dvbsub-remux: CMD = transcode mpegts $(TARGET_SAMPLES)/sub/dvbsubtest_filter.ts matroska "-map 0:s -map 0:s -t 20 -c copy -bsf:0 setts=duration=if(gt(DURATION\,0)\,DURATION\,if(eq(PTS\,NOPTS)\,0\,if(eq(NEXT_PTS\,NOPTS)\,0\,NEXT_PTS-PTS))):pts=PTS" "-map 0 -c copy"

FATE_MATROSKA_FFPROBE-$(call ALLYES, MATROSKA_DEMUXER) += fate-matroska-spherical-mono
fate-matroska-spherical-mono: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_entries stream_side_data_list -select_streams v -v 0 $(TARGET_SAMPLES)/mkv/spherical.mkv

# This test tests the handling of AVStereo3D information, in particular
# the ability to set it via metadata in the muxer (the file itself is
# actually an ordinary file with a single view). It also tests
# correctly writing the display dimensions in the presence of stereo metadata.
# The test also covers reformatting Theora extradata as well as testing
# default_mode infer in the presence of tracks already marked as default.
# It furthermore tests tag languages as well as stream languages,
# in particular in their various forms (e.g. de vs deu vs ger for German)
# and also the language-country code form.
FATE_MATROSKA_FFMPEG_FFPROBE-$(call REMUX, MATROSKA, OGG_DEMUXER THEORA_DECODER) += fate-matroska-stereo_mode
fate-matroska-stereo_mode: CMD = transcode ogg $(TARGET_SAMPLES)/vp3/offset_test.ogv matroska \
    "-c copy -write_crc32 0 -default_mode infer \
     -map 0 -disposition:s:0 +original+dub -metadata:s:0 language=ger \
     -map 0 -metadata:s:1 stereo_mode=left_right -metadata:s:1 language=ger-at -metadata:s:1 description-ger=Deutsch -metadata:s:1 description-fre=Francais \
     -map 0 -metadata:s:2 stereo_mode=bottom_top -metadata:s:2 language=eng -metadata:s:2 description-de=Deutsch -metadata:s:2 description-fra=Francais \
     -map 0 -metadata:s:3 stereo_mode=row_interleaved_rl -sar:3 3:1 -disposition:3 +default -metadata:s:3 language=deu-at \
     -map 0 -metadata:s:4 stereo_mode=col_interleaved_rl -sar:4 16:9 -metadata:s:4 language=fre -metadata:s:4 description-deu-at=Oesterreichisch \
     -map 0 -metadata:s:5 stereo_mode=anaglyph_cyan_red -sar:5 16:9 -disposition:5 +default -metadata:s:5 language=fra \
     -map 0 -metadata:s:6 stereo_mode=12 -sar:6 2:1 -metadata:s:6 language=de -metadata:s:6 description-deu=Deutsch" \
    "-map 0 -c copy" \
    "-show_entries stream_disposition=default,original,dub:stream_tags:stream_side_data_list"


# The following test tests the various flavours of WebVTT in WebM.
# It also tests that dispositions not supported by WebM are not written
# (and therefore lost). It moreover tests that the muxer writes CuePoints
# with multiple CueTrackPositions if the timestamps coincide.
FATE_MATROSKA_FFMPEG_FFPROBE-$(call REMUX, WEBM MATROSKA, WEBVTT_DEMUXER) \
                               += fate-webm-webvtt-remux
fate-webm-webvtt-remux: CMD = transcode webvtt $(TARGET_SAMPLES)/sub/WebVTT_capability_tester.vtt webm "-map 0 -map 0 -map 0 -map 0 -c:s copy -disposition:0 original+descriptions+hearing_impaired -disposition:1 lyrics+default+metadata -disposition:2 comment+forced -disposition:3 karaoke+captions+dub" "-map 0:0 -map 0:1 -c copy" "-show_entries stream_disposition:stream=index,codec_name:packet=stream_index,pts:packet_side_data_list -show_data_hash CRC32"

FATE_MATROSKA_FFMPEG_FFPROBE-$(call REMUX, WEBM MATROSKA, VP9_PARSER) \
                               += fate-webm-hdr10-plus-remux
fate-webm-hdr10-plus-remux: CMD = transcode webm $(TARGET_SAMPLES)/mkv/hdr10_plus_vp9_sample.webm webm "-map 0 -c:v copy" "-map 0 -c:v copy" "-show_packets"

FATE_MATROSKA_FFMPEG_FFPROBE-$(call REMUX, MATROSKA, VP9_PARSER) \
                               += fate-matroska-hdr10-plus-remux
fate-matroska-hdr10-plus-remux: CMD = transcode webm $(TARGET_SAMPLES)/mkv/hdr10_plus_vp9_sample.webm matroska "-map 0 -c:v copy" "-map 0 -c:v copy" "-show_packets"

fate-matroska-side-data-pref-codec: CMD = run ffprobe$(PROGSSUF)$(EXESUF) $(TARGET_SAMPLES)/mkv/hdr10tags-both.mkv \
    -select_streams v:0 -show_streams -show_frames -show_entries stream=stream_side_data:frame=frame_side_data_list
fate-matroska-side-data-pref-packet: CMD = run ffprobe$(PROGSSUF)$(EXESUF) $(TARGET_SAMPLES)/mkv/hdr10tags-both.mkv \
    -select_streams v:0 -show_streams -show_frames -show_entries stream=stream_side_data:frame=frame_side_data_list -side_data_prefer_packet mastering_display_metadata,content_light_level
FATE_MATROSKA_FFPROBE-$(call ALLYES, MATROSKA_DEMUXER HEVC_DECODER) += fate-matroska-side-data-pref-codec fate-matroska-side-data-pref-packet

FATE_SAMPLES_AVCONV += $(FATE_MATROSKA-yes)
FATE_SAMPLES_FFPROBE += $(FATE_MATROSKA_FFPROBE-yes)
FATE_SAMPLES_FFMPEG_FFPROBE += $(FATE_MATROSKA_FFMPEG_FFPROBE-yes)

fate-matroska: $(FATE_MATROSKA-yes) $(FATE_MATROSKA_FFPROBE-yes) $(FATE_MATROSKA_FFMPEG_FFPROBE-yes)
