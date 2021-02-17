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
fate-matroska-remux: REF = 26fabd90326e3de4bb6afe1b216ce232

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
FATE_MATROSKA-$(call ALLYES, MATROSKA_DEMUXER LZO) += fate-matroska-lzo-decompression
fate-matroska-lzo-decompression: CMD = framecrc -i $(TARGET_SAMPLES)/mkv/lzo.mka -c copy

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

# This test tests demuxing Vorbis and chapters from ogg and muxing it in and
# demuxing it from Matroska/WebM. It furthermore tests the WebM muxer, in
# particular its DASH mode. Finally, it tests writing the Cues at the front.
FATE_MATROSKA_FFMPEG_FFPROBE-$(call ALLYES, MATROSKA_DEMUXER OGG_DEMUXER  \
                                 VORBIS_DECODER VORBIS_PARSER WEBM_MUXER) \
                               += fate-webm-dash-chapters
fate-webm-dash-chapters: CMD = transcode ogg $(TARGET_SAMPLES)/vorbis/vorbis_chapter_extension_demo.ogg webm "-c copy -cluster_time_limit 1500 -dash 1 -dash_track_number 124 -reserve_index_space 400" "-c copy -t 0.5" "" -show_chapters

# The input file has a Block whose payload has a size of zero before reversing
# header removal compression; it furthermore uses chained SeekHeads and has
# level 1-elements after the Cluster. This is tested on the demuxer's side.
# For the muxer this tests that it can correctly write huge TrackNumbers and
# that it can expand the Cues element's length field by one byte if necessary.
# It furthermore tests correct propagation of the description tag.
FATE_MATROSKA_FFMPEG_FFPROBE-$(call DEMMUX, MATROSKA, MATROSKA) \
                               += fate-matroska-zero-length-block
fate-matroska-zero-length-block: CMD = transcode matroska $(TARGET_SAMPLES)/mkv/zero_length_block.mks matroska "-c:s copy -dash 1 -dash_track_number 2000000000 -reserve_index_space 62 -metadata_header_padding 1" "-c:s copy" "" "-show_entries stream_tags=description"

# This test the following features of the Matroska muxer: Writing projection
# stream side-data; not setting any track to default if the user requested it;
# and modifying and writing colorspace properties.
FATE_MATROSKA_FFMPEG_FFPROBE-$(call ALLYES, MATROSKA_DEMUXER MATROSKA_MUXER \
                                            H264_DECODER H264_PARSER) \
                               += fate-matroska-spherical-mono-remux
fate-matroska-spherical-mono-remux: CMD = transcode matroska $(TARGET_SAMPLES)/mkv/spherical.mkv matroska "-map 0 -map 0 -c copy -disposition:0 -default+forced -disposition:1 -default -default_mode passthrough -color_primaries:1 bt709 -color_trc:1 smpte170m -colorspace:1 bt2020c -color_range:1 pc"  "-map 0 -c copy -t 0" "" "-show_entries stream_side_data_list:stream_disposition=default,forced:stream=color_range,color_space,color_primaries,color_transfer"

# The input file of the following test contains Content Light Level as well as
# Mastering Display Metadata and so this test tests correct muxing and demuxing
# of these. It furthermore also tests that this data is correctly propagated
# when reencoding (here to ffv1).
# Both input audio tracks are completely zero, so the noise bsf is used
# to make this test interesting.
FATE_MATROSKA_FFMPEG_FFPROBE-$(call ALLYES, FILE_PROTOCOL MXF_DEMUXER        \
                                            PRORES_DECODER PCM_S24LE_DECODER \
                                            FFV1_ENCODER ARESAMPLE_FILTER    \
                                            PCM_S16BE_ENCODER NOISE_BSF      \
                                            MATROSKA_MUXER MATROSKA_DEMUXER  \
                                            FRAMECRC_MUXER PIPE_PROTOCOL)    \
                               += fate-matroska-mastering-display-metadata
fate-matroska-mastering-display-metadata: CMD = transcode mxf $(TARGET_SAMPLES)/mxf/Meridian-Apple_ProResProxy-HDR10.mxf matroska "-map 0 -map 0:0 -c:v:0 copy -c:v:1 ffv1 -c:a:0 copy -bsf:a:0 noise=amount=3 -filter:a:1 aresample -c:a:1 pcm_s16be -bsf:a:1 noise=dropamount=4" "-map 0 -c copy" "" "-show_entries stream_side_data_list:stream=index,codec_name"

# Tests writing BlockAdditional and BlockGroups with ReferenceBlock elements;
# it also tests setting a track as suitable for hearing impaired.
# It also tests the capability of the VP8 parser to set the keyframe flag
# (the input file lacks ReferenceBlock elements making everything a keyframe).
FATE_MATROSKA_FFMPEG_FFPROBE-$(call ALLYES, FILE_PROTOCOL MATROSKA_DEMUXER \
                                            VP8_PARSER MATROSKA_MUXER      \
                                            FRAMECRC_MUXER PIPE_PROTOCOL)  \
                               += fate-matroska-vp8-alpha-remux
fate-matroska-vp8-alpha-remux: CMD = transcode matroska $(TARGET_SAMPLES)/vp8_alpha/vp8_video_with_alpha.webm matroska "-c copy -disposition +hearing_impaired -cluster_size_limit 100000" "-c copy -t 0.2" "" "-show_entries stream_disposition:stream_side_data_list"

# The audio stream to be remuxed here has AV_DISPOSITION_VISUAL_IMPAIRED.
FATE_MATROSKA_FFMPEG_FFPROBE-$(call ALLYES, FILE_PROTOCOL MPEGTS_DEMUXER    \
                                            AC3_DECODER MATROSKA_MUXER      \
                                            MATROSKA_DEMUXER FRAMECRC_MUXER \
                                            PIPE_PROTOCOL)                  \
                               += fate-matroska-mpegts-remux
fate-matroska-mpegts-remux: CMD = transcode mpegts $(TARGET_SAMPLES)/mpegts/pmtchange.ts matroska "-map 0:2 -map 0:2 -c copy -disposition:a:1 -visual_impaired+hearing_impaired" "-map 0 -c copy" "" "-show_entries stream_disposition:stream=index"

FATE_MATROSKA_FFPROBE-$(call ALLYES, MATROSKA_DEMUXER) += fate-matroska-spherical-mono
fate-matroska-spherical-mono: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_entries stream_side_data_list -select_streams v -v 0 $(TARGET_SAMPLES)/mkv/spherical.mkv

FATE_SAMPLES_AVCONV += $(FATE_MATROSKA-yes)
FATE_SAMPLES_FFPROBE += $(FATE_MATROSKA_FFPROBE-yes)
FATE_SAMPLES_FFMPEG_FFPROBE += $(FATE_MATROSKA_FFMPEG_FFPROBE-yes)

fate-matroska: $(FATE_MATROSKA-yes) $(FATE_MATROSKA_FFPROBE-yes) $(FATE_MATROSKA_FFMPEG_FFPROBE-yes)
