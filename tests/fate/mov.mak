FATE_MOV-$(call FRAMEMD5, MOV) = fate-mov-3elist \
           fate-mov-3elist-1ctts \
           fate-mov-1elist-1ctts \
           fate-mov-1elist-noctts \
           fate-mov-elist-starts-ctts-2ndsample \
           fate-mov-1elist-ends-last-bframe \
           fate-mov-2elist-elist1-ends-bframe \
           fate-mov-3elist-encrypted \
           fate-mov-frag-encrypted \
           fate-mov-tenc-only-encrypted \
           fate-mov-invalid-elst-entry-count \
           fate-mov-gpmf-remux \
           fate-mov-ibi-elst-starts-b \
           fate-mov-elst-ends-betn-b-and-i \
           fate-mov-frag-overlap \
           fate-mov-neg-firstpts-discard-frames \
           fate-mov-stream-shorter-than-movie \
           fate-mov-pcm-remux \

FATE_MOV_FFPROBE-$(call FRAMEMD5, MOV) = fate-mov-neg-firstpts-discard \
                   fate-mov-neg-firstpts-discard-vorbis \
                   fate-mov-aac-2048-priming \
                   fate-mov-zombie \
                   fate-mov-init-nonkeyframe \
                   fate-mov-displaymatrix \
                   fate-mov-read-amve \
                   fate-mov-spherical-mono \
                   fate-mov-guess-delay-1 \
                   fate-mov-guess-delay-2 \
                   fate-mov-guess-delay-3 \
                   fate-mov-mp4-with-mov-in24-ver \
                   fate-mov-mp4-extended-atom \

FATE_MOV_FASTSTART = fate-mov-faststart-4gb-overflow \

FATE_SAMPLES_FFMPEG += $(FATE_MOV-yes)
FATE_SAMPLES_FFPROBE += $(FATE_MOV_FFPROBE-yes)
FATE_SAMPLES_FASTSTART += $(FATE_MOV_FASTSTART)

# Make sure we handle edit lists correctly in normal cases.
fate-mov-1elist-noctts: CMD = framemd5 -i $(TARGET_SAMPLES)/mov/mov-1elist-noctts.mov
fate-mov-1elist-1ctts: CMD = framemd5 -i $(TARGET_SAMPLES)/mov/mov-1elist-1ctts.mov
fate-mov-3elist: CMD = framemd5 -i $(TARGET_SAMPLES)/mov/mov-3elist.mov
fate-mov-3elist-1ctts: CMD = framemd5 -i $(TARGET_SAMPLES)/mov/mov-3elist-1ctts.mov

# Edit list with encryption
fate-mov-3elist-encrypted: CMD = framemd5 -decryption_key 12345678901234567890123456789012 -i $(TARGET_SAMPLES)/mov/mov-3elist-encrypted.mov

# Fragmented encryption with senc boxes in movie fragments.
fate-mov-frag-encrypted: CMD = framemd5 -decryption_key 12345678901234567890123456789012 -i $(TARGET_SAMPLES)/mov/mov-frag-encrypted.mp4

# Full-sample encryption and constant IV using only tenc atom (no senc/saio/saiz).
fate-mov-tenc-only-encrypted: CMD = framemd5 -decryption_key 12345678901234567890123456789012 -i $(TARGET_SAMPLES)/mov/mov-tenc-only-encrypted.mp4

# Makes sure that the CTTS is also modified when we fix avindex in mov.c while parsing edit lists.
fate-mov-elist-starts-ctts-2ndsample: CMD = framemd5 -i $(TARGET_SAMPLES)/mov/mov-elist-starts-ctts-2ndsample.mov

# Makes sure that we handle edit lists ending on a B-frame correctly.
# The last frame in decoding order which is B-frame should be output, but the last but-one P-frame shouldn't be
# output.
fate-mov-1elist-ends-last-bframe: CMD = framemd5 -i $(TARGET_SAMPLES)/mov/mov-1elist-ends-last-bframe.mov

# Makes sure that we handle timestamps of packets in case of multiple edit lists with one of them ending on a B-frame correctly.
fate-mov-2elist-elist1-ends-bframe: CMD = framemd5 -i $(TARGET_SAMPLES)/mov/mov-2elist-elist1-ends-bframe.mov

# Makes sure that if edit list ends on a B-frame but before the I-frame, then we output the B-frame but discard the I-frame.
fate-mov-elst-ends-betn-b-and-i: CMD = framemd5 -i $(TARGET_SAMPLES)/mov/elst_ends_betn_b_and_i.mp4

# Makes sure that we handle edit lists and start padding correctly.
FATE_MOV_FFMPEG_SAMPLES-$(call FRAMEMD5, MOV, AAC, ARESAMPLE_FILTER) \
                            += fate-mov-440hz-10ms
fate-mov-440hz-10ms: CMD = framemd5 -i $(TARGET_SAMPLES)/mov/440hz-10ms.m4a -af aresample

# Makes sure that we handle invalid edit list entry count correctly.
fate-mov-invalid-elst-entry-count: CMD = framemd5 -idct simple -flags +bitexact -i $(TARGET_SAMPLES)/mov/invalid_elst_entry_count.mov

# Makes sure that 1st key-frame is picked when,
#    i) One B-frame between 2 key-frames
#   ii) Edit list starts on B-frame.
#  iii) Both key-frames have their DTS < edit list start
# i.e.  Pts Order: I-B-I
fate-mov-ibi-elst-starts-b: CMD = framemd5 -flags +bitexact -i $(TARGET_SAMPLES)/mov/mov_ibi_elst_starts_b.mov

# Makes sure that we handle overlapping framgments
fate-mov-frag-overlap: CMD = framemd5 -i $(TARGET_SAMPLES)/mov/frag_overlap.mp4

fate-mov-mp4-frag-flush: CMD = md5 -f lavfi -i color=blue,format=rgb24,trim=duration=0.04 -f lavfi -i anullsrc,aformat=s16,atrim=duration=2 -c:v png -c:a pcm_s16le -movflags +empty_moov+hybrid_fragmented -frag_duration 1000000 -frag_interleave 1 -bitexact -f mp4
fate-mov-mp4-frag-flush: CMP = oneline
fate-mov-mp4-frag-flush: REF = c9d0236bde4a0b24a01f6a032fd72e04
FATE_MOV_FFMPEG-$(call ALLYES, LAVFI_INDEV COLOR_FILTER FORMAT_FILTER TRIM_FILTER \
                               ANULLSRC_FILTER AFORMAT_FILTER ATRIM_FILTER        \
                               WRAPPED_AVFRAME_DECODER PCM_S16LE_DECODER PCM_S16BE_DECODER \
                               PNG_ENCODER PCM_S16LE_ENCODER MP4_MUXER) += fate-mov-mp4-frag-flush

# Makes sure that we pick the right frames according to edit list when there is no keyframe with PTS < edit list start.
# For example, when video starts on a B-frame, and edit list starts on that B-frame too.
# GOP structure : B B I in presentation order.
FATE_MOV_FFMPEG_SAMPLES-$(call FRAMEMD5, MOV, AAC_FIXED, ARESAMPLE_FILTER) \
                            += fate-mov-bbi-elst-starts-b
fate-mov-bbi-elst-starts-b: CMD = framemd5 -flags +bitexact -acodec aac_fixed -i $(TARGET_SAMPLES)/h264/twofields_packet.mp4 -af aresample

# Makes sure that the stream start_time is not negative when the first packet is a DISCARD packet with negative timestamp.
fate-mov-neg-firstpts-discard: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_entries stream=start_time -bitexact $(TARGET_SAMPLES)/mov/mov_neg_first_pts_discard.mov

# Makes sure that the VORBIS audio stream start_time is not negative when the first few packets are DISCARD packets
# with negative timestamps (skip_samples is not set for Vorbis, so ffmpeg computes start_time as negative if not specified by demuxer).
fate-mov-neg-firstpts-discard-vorbis: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_entries stream=start_time -bitexact $(TARGET_SAMPLES)/mov/mov_neg_first_pts_discard_vorbis.mp4

# Makes sure that expected frames are generated for mov_neg_first_pts_discard.mov with -fps_mode cfr
fate-mov-neg-firstpts-discard-frames: CMD = framemd5 -flags +bitexact -i $(TARGET_SAMPLES)/mov/mov_neg_first_pts_discard.mov -fps_mode cfr

# Makes sure that no frame is dropped/duplicated with fps filter due to start_time / duration miscalculations.
fate-mov-stream-shorter-than-movie: CMD = framemd5 -flags +bitexact -i $(TARGET_SAMPLES)/mov/mov_stream_shorter_than_movie.mov -vf fps=fps=24 -an

fate-mov-aac-2048-priming: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_packets -print_format compact $(TARGET_SAMPLES)/mov/aac-2048-priming.mov

fate-mov-zombie: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_streams -show_packets -show_frames -bitexact -print_format compact $(TARGET_SAMPLES)/mov/white_zombie_scrunch-part.mov

fate-mov-init-nonkeyframe: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_packets -print_format compact -select_streams v $(TARGET_SAMPLES)/mov/mp4-init-nonkeyframe.mp4

fate-mov-displaymatrix: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_entries stream=display_aspect_ratio,sample_aspect_ratio:stream_side_data_list -select_streams v -v 0 $(TARGET_SAMPLES)/mov/displaymatrix.mov

fate-mov-read-amve: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_entries stream_side_data_list -select_streams v -v 0 $(TARGET_SAMPLES)/mov/amve.mov

fate-mov-spherical-mono: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_entries stream_side_data_list -select_streams v -v 0 $(TARGET_SAMPLES)/mov/spherical.mov

fate-mov-gpmf-remux: CMD = md5 -i $(TARGET_SAMPLES)/mov/fake-gp-media-with-real-gpmf.mp4 -map 0 -c copy -fflags +bitexact -f mp4
fate-mov-gpmf-remux: CMP = oneline
fate-mov-gpmf-remux: REF = 6361cf3c2b9e6962c2eafbda138125f4

fate-mov-guess-delay-1: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_entries stream=has_b_frames -select_streams v $(TARGET_SAMPLES)/h264/h264_3bf_nopyramid_nobsrestriction.mp4
fate-mov-guess-delay-2: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_entries stream=has_b_frames -select_streams v $(TARGET_SAMPLES)/h264/h264_3bf_pyramid_nobsrestriction.mp4
fate-mov-guess-delay-3: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_entries stream=has_b_frames -select_streams v $(TARGET_SAMPLES)/h264/h264_4bf_pyramid_nobsrestriction.mp4

fate-mov-faststart-4gb-overflow: CMD = run tools/qt-faststart$(EXESUF) $(TARGET_SAMPLES)/mov/faststart-4gb-overflow.mov $(TARGET_PATH)/faststart-4gb-overflow-output.mov > /dev/null ; do_md5sum faststart-4gb-overflow-output.mov | cut -d " " -f1 ; rm faststart-4gb-overflow-output.mov
fate-mov-faststart-4gb-overflow: CMP = oneline
fate-mov-faststart-4gb-overflow: REF = bc875921f151871e787c4b4023269b29

fate-mov-mp4-with-mov-in24-ver: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_entries stream=codec_name -select_streams 1 $(TARGET_SAMPLES)/mov/mp4-with-mov-in24-ver.mp4

fate-mov-mp4-extended-atom: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_packets -print_format compact -select_streams v $(TARGET_SAMPLES)/mov/extended_atom_size_probe

FATE_MOV_FFMPEG_FFPROBE_SAMPLES-$(call REMUX, MP4 MOV, OGG_DEMUXER VORBIS_DECODER) \
                          += fate-mov-mp4-chapters
fate-mov-mp4-chapters: CMD = transcode ogg $(TARGET_SAMPLES)/vorbis/vorbis_chapter_extension_demo.ogg mp4 "-c copy" "-c copy -t 0.1" "-show_chapters"

FATE_MOV_FFMPEG_FFPROBE_SAMPLES-$(call TRANSCODE, PNG, MP4 MOV, MJPEG_DECODER SCALE_FILTER) \
                          += fate-mov-cover-image
fate-mov-cover-image: CMD = transcode mov $(TARGET_SAMPLES)/cover_art/Owner-iTunes_9.0.3.15.m4a mp4 "-map 0 -map 0:v -c:a copy -c:v:0 copy -filter:v:1 scale -c:v:1 png" "-map 0 -t 0.1 -c copy" "-show_entries stream_disposition=attached_pic:stream=index,codec_name"

FATE_MOV_FFMPEG_FFPROBE_SAMPLES-$(call TRANSCODE, TTML SUBRIP, MP4 MOV, SRT_DEMUXER TTML_MUXER) += fate-mov-mp4-ttml-stpp fate-mov-mp4-ttml-dfxp
fate-mov-mp4-ttml-stpp: CMD = transcode srt $(TARGET_SAMPLES)/sub/SubRip_capability_tester.srt mp4 "-map 0:s -c:s ttml -time_base:s 1:1000" "-map 0 -c copy" "-of json -show_entries packet:stream=index,codec_type,codec_tag_string,codec_tag,codec_name,time_base,start_time,duration_ts,duration,nb_frames,nb_read_packets:stream_tags"
fate-mov-mp4-ttml-dfxp: CMD = transcode srt $(TARGET_SAMPLES)/sub/SubRip_capability_tester.srt mp4 "-map 0:s -c:s ttml -time_base:s 1:1000 -tag:s dfxp -strict unofficial" "-map 0 -c copy" "-of json -show_entries packet:stream=index,codec_type,codec_tag_string,codec_tag,codec_name,time_base,start_time,duration_ts,duration,nb_frames,nb_read_packets:stream_tags"

# avif demuxing - still image with 1 item.
FATE_MOV_FFMPEG_SAMPLES-$(call FRAMECRC, MOV, AV1, AV1_PARSER) \
                           += fate-mov-avif-demux-still-image-1-item
fate-mov-avif-demux-still-image-1-item: CMD = framecrc -c:v av1 -i $(TARGET_SAMPLES)/avif/still_image.avif -c:v copy

# avif demuxing - still image with multiple items.
FATE_MOV_FFMPEG_SAMPLES-$(call FRAMECRC, MOV, AV1, AV1_PARSER) \
                           += fate-mov-avif-demux-still-image-multiple-items
fate-mov-avif-demux-still-image-multiple-items: CMD = framecrc -c:v av1 -i $(TARGET_SAMPLES)/avif/still_image_exif.avif -c:v copy

# heic demuxing - still image with 1 item.
FATE_MOV_FFMPEG_SAMPLES-$(call FRAMECRC, MOV, HEVC, HEVC_PARSER) \
                           += fate-mov-heic-demux-still-image-1-item
fate-mov-heic-demux-still-image-1-item: CMD = framecrc -i $(TARGET_SAMPLES)/heif-conformance/C002.heic -c:v copy

# heic demuxing - still image with multiple items.
FATE_MOV_FFMPEG_SAMPLES-$(call FRAMECRC, MOV, HEVC, HEVC_PARSER) \
                           += fate-mov-heic-demux-still-image-multiple-items
fate-mov-heic-demux-still-image-multiple-items: CMD = framecrc -i $(TARGET_SAMPLES)/heif-conformance/C003.heic -c:v copy -map 0

# heic demuxing - still image with multiple items, exporting cropping and/or rotation information.
FATE_MOV_FFMPEG_FFPROBE_SAMPLES-$(call FRAMECRC, MOV, HEVC, HEVC_PARSER) \
                           += fate-mov-heic-demux-clap-irot-imir
fate-mov-heic-demux-clap-irot-imir: CMD = stream_demux mov $(TARGET_SAMPLES)/heif-conformance/MIAF007.heic "" "-c:v copy -map 0" \
  "-show_entries stream=index,id:stream_disposition:stream_side_data_list"

# heic demuxing - still image with multiple items in a grid.
FATE_MOV_FFMPEG_FFPROBE_SAMPLES-$(call DEMMUX, MOV, FRAMECRC, HEVC_DECODER HEVC_PARSER) \
                           += fate-mov-heic-demux-still-image-grid
fate-mov-heic-demux-still-image-grid: CMD = stream_demux mov $(TARGET_SAMPLES)/heif-conformance/C007.heic "" "-c:v copy -map 0:g:0" \
  "-show_entries stream_group=index,id,nb_streams,type:stream_group_components:stream_group_disposition:stream_group_tags:stream_group_stream=index,id:stream_group_stream_disposition"

# heic demuxing - still image with multiple items in an overlay canvas.
FATE_MOV_FFMPEG_FFPROBE_SAMPLES-$(call DEMMUX, MOV, FRAMECRC, HEVC_DECODER HEVC_PARSER) \
                           += fate-mov-heic-demux-still-image-iovl
fate-mov-heic-demux-still-image-iovl: CMD = stream_demux mov $(TARGET_SAMPLES)/heif-conformance/C015.heic "" "-c:v copy -map 0:g:0" \
  "-show_entries stream_group=index,id,nb_streams,type:stream_group_components:stream_group_disposition:stream_group_tags:stream_group_stream=index,id:stream_group_stream_disposition"

# heic demuxing - still image where one image item is placed twice on an overlay canvas.
FATE_MOV_FFMPEG_FFPROBE_SAMPLES-$(call DEMMUX, MOV, FRAMECRC, HEVC_DECODER HEVC_PARSER) \
                           += fate-mov-heic-demux-still-image-iovl-2
fate-mov-heic-demux-still-image-iovl-2: CMD = stream_demux mov $(TARGET_SAMPLES)/heif-conformance/C021.heic "" "-c:v copy -map 0:g:0" \
  "-show_entries stream_group=index,id,nb_streams,type:stream_group_components:stream_group_disposition:stream_group_tags:stream_group_stream=index,id:stream_group_stream_disposition"

# Resulting remux should have:
# 1. first audio stream with AV_DISPOSITION_HEARING_IMPAIRED
# 2. second audio stream with AV_DISPOSITION_VISUAL_IMPAIRED | DESCRIPTIONS
FATE_MOV_FFMPEG_FFPROBE_SAMPLES-$(call REMUX, MP4 MOV, MPEGTS_DEMUXER AC3_DECODER) \
                          += fate-mov-mp4-disposition-mpegts-remux
fate-mov-mp4-disposition-mpegts-remux: CMD = transcode mpegts $(TARGET_SAMPLES)/mpegts/pmtchange.ts mp4 "-map 0:1 -map 0:2 -c copy -disposition:a:0 +hearing_impaired" "-map 0 -c copy" "-of json -show_entries stream_disposition:stream=index"

FATE_MOV_FFMPEG_FFPROBE_SAMPLES-$(call REMUX, MP4 MOV) \
                          += fate-mov-write-amve
fate-mov-write-amve: CMD = transcode mov $(TARGET_SAMPLES)/mov/amve.mov mp4 "-c:v copy" "-c:v copy -t 0.5" "-show_entries stream_side_data_list"

FATE_SAMPLES_FFMPEG_FFPROBE += $(FATE_MOV_FFMPEG_FFPROBE_SAMPLES-yes)
FATE_SAMPLES_FFMPEG += $(FATE_MOV_FFMPEG_SAMPLES-yes)

FATE_MOV_FFMPEG-$(call TRANSCODE, PCM_S16LE, MOV, WAV_DEMUXER PAN_FILTER) \
                          += fate-mov-channel-description
fate-mov-channel-description: tests/data/asynth-44100-1.wav tests/data/filtergraphs/mov-channel-description
fate-mov-channel-description: CMD = transcode wav $(TARGET_PATH)/tests/data/asynth-44100-1.wav mov "-/filter_complex $(TARGET_PATH)/tests/data/filtergraphs/mov-channel-description -map [outFL] -map [outFR] -map [outFC] -map [outLFE] -map [outBL] -map [outBR] -map [outDL] -map [outDR] -c:a pcm_s16le" "-map 0 -c copy -frames:a 0"

# Test PCM in mp4 and channel layout
FATE_MOV_FFMPEG-$(call TRANSCODE, PCM_S16LE, MOV, WAV_DEMUXER PAN_FILTER) \
                          += fate-mov-mp4-pcm
fate-mov-mp4-pcm: tests/data/asynth-44100-1.wav tests/data/filtergraphs/mov-mp4-pcm
fate-mov-mp4-pcm: CMD = transcode wav $(TARGET_PATH)/tests/data/asynth-44100-1.wav mp4 "-/filter_complex $(TARGET_PATH)/tests/data/filtergraphs/mov-mp4-pcm -map [mono] -map [stereo] -map [2.1] -map [5.1] -map [7.1] -c:a pcm_s16le" "-map 0 -c copy -frames:a 0"

# Test floating sample format PCM in mp4 and unusual channel layout
FATE_MOV_FFMPEG-$(call TRANSCODE, PCM_S16LE, MOV, WAV_DEMUXER PAN_FILTER) \
                          += fate-mov-mp4-pcm-float
fate-mov-mp4-pcm-float: tests/data/asynth-44100-1.wav
fate-mov-mp4-pcm-float: CMD = transcode wav $(TARGET_PATH)/tests/data/asynth-44100-1.wav mp4 "-af aresample,pan=FR+FL+FR|c0=c0|c1=c0|c2=c0 -c:a pcm_f32le" "-map 0 -c copy -frames:a 0"

fate-mov-pcm-remux: tests/data/asynth-44100-1.wav
fate-mov-pcm-remux: CMD = md5 -i $(TARGET_PATH)/tests/data/asynth-44100-1.wav -map 0 -c copy -fflags +bitexact -f mp4
fate-mov-pcm-remux: CMP = oneline
fate-mov-pcm-remux: REF = e76115bc392d702da38f523216bba165

FATE_MOV_FFMPEG-$(call TRANSCODE, RAWVIDEO, MOV, TESTSRC_FILTER SETPTS_FILTER) += fate-mov-vfr
fate-mov-vfr: CMD = md5 -filter_complex testsrc=size=2x2:duration=1,setpts=N*N:strip_fps=1 -c rawvideo -fflags +bitexact -f mov
fate-mov-vfr: CMP = oneline
fate-mov-vfr: REF = 1558b4a9398d8635783c93f84eb5a60d

FATE_MOV_FFMPEG_FFPROBE-$(call TRANSCODE, FLAC, MOV, WAV_DEMUXER PCM_S16LE_DECODER) += fate-mov-mp4-iamf-stereo
fate-mov-mp4-iamf-stereo: tests/data/asynth-44100-2.wav tests/data/streamgroups/audio_element-stereo tests/data/streamgroups/mix_presentation-stereo
fate-mov-mp4-iamf-stereo: SRC = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
fate-mov-mp4-iamf-stereo: CMD = transcode wav $(SRC) mp4 " \
  -/stream_group $(TARGET_PATH)/tests/data/streamgroups/audio_element-stereo \
  -/stream_group $(TARGET_PATH)/tests/data/streamgroups/mix_presentation-stereo \
  -streamid 0:0 -c:a flac -t 1" "-c:a copy -map 0" \
  "-show_entries stream_group=index,id,nb_streams,type:stream_group_components:stream_group_disposition:stream_group_tags:stream_group_stream=index,id:stream_group_stream_disposition"

FATE_MOV_FFMPEG_FFPROBE-$(call TRANSCODE, FLAC, MOV, WAV_DEMUXER PCM_S16LE_DECODER ARESAMPLE_FILTER) += fate-mov-mp4-iamf-5_1_4
fate-mov-mp4-iamf-5_1_4: tests/data/asynth-44100-10.wav tests/data/filtergraphs/iamf_5_1_4 tests/data/streamgroups/audio_element-5_1_4 tests/data/streamgroups/mix_presentation-5_1_4
fate-mov-mp4-iamf-5_1_4: SRC = $(TARGET_PATH)/tests/data/asynth-44100-10.wav
fate-mov-mp4-iamf-5_1_4: CMD = transcode wav $(SRC) mp4 "-auto_conversion_filters \
  -/filter_complex $(TARGET_PATH)/tests/data/filtergraphs/iamf_5_1_4 \
  -/stream_group $(TARGET_PATH)/tests/data/streamgroups/audio_element-5_1_4 \
  -/stream_group $(TARGET_PATH)/tests/data/streamgroups/mix_presentation-5_1_4 \
  -streamid 0:0 -streamid 1:1 -streamid 2:2 -streamid 3:3 -streamid 4:4 -streamid 5:5 -map [FRONT] -map [BACK] -map [CENTER] -map [LFE] -map [TOP_FRONT] -map [TOP_BACK] -c:a flac -t 1" "-c:a copy -map 0" \
  "-show_entries stream_group=index,id,nb_streams,type:stream_group_components:stream_group_disposition:stream_group_tags:stream_group_stream=index,id:stream_group_stream_disposition"

# Test muxing an IAMF track alongside a video one, with video as the first track.
FATE_MOV_FFMPEG_FFPROBE-$(call TRANSCODE, MPEG4 FLAC, MOV, WAV_DEMUXER RAWVIDEO_DEMUXER PCM_S16LE_DECODER ARESAMPLE_FILTER) += fate-mov-mp4-iamf-7_1_4-video-first
fate-mov-mp4-iamf-7_1_4-video-first: tests/data/asynth-44100-12.wav tests/data/vsynth1.yuv tests/data/filtergraphs/iamf_7_1_4 tests/data/streamgroups/audio_element-7_1_4-2 tests/data/streamgroups/mix_presentation-7_1_4
fate-mov-mp4-iamf-7_1_4-video-first: SRC = $(TARGET_PATH)/tests/data/asynth-44100-12.wav
fate-mov-mp4-iamf-7_1_4-video-first: SRC2 = $(TARGET_PATH)/tests/data/vsynth1.yuv
fate-mov-mp4-iamf-7_1_4-video-first: CMD = transcode wav $(SRC) mp4 "-auto_conversion_filters \
  -/filter_complex $(TARGET_PATH)/tests/data/filtergraphs/iamf_7_1_4 \
  -/stream_group $(TARGET_PATH)/tests/data/streamgroups/audio_element-7_1_4-2 \
  -/stream_group $(TARGET_PATH)/tests/data/streamgroups/mix_presentation-7_1_4 \
  -streamid 0:1 -streamid 1:2 -streamid 2:3 -streamid 3:4 -streamid 4:5 -streamid 5:6 -streamid 6:7 -streamid 7:8 -map 1:v:0 -map [FRONT] -map [BACK] -map [CENTER] -map [LFE] -map [SIDE] -map [TOP_FRONT] -map [TOP_BACK] -c:a flac -c:v mpeg4 -t 1" "-c:a copy -c:v copy -map 0" \
  "-show_entries stream_group=index,id,nb_streams,type:stream_group_components:stream_group_disposition:stream_group_tags:stream_group_stream=index,id:stream_group_stream_disposition:stream=index,id" \
  "-f rawvideo -s 352x288 -pix_fmt yuv420p -i $(SRC2)"

# Test muxing an IAMF track alongside a video one, with video as the last track. Also, use stream ids as track ids.
FATE_MOV_FFMPEG_FFPROBE-$(call TRANSCODE, MPEG4 FLAC, MOV, WAV_DEMUXER RAWVIDEO_DEMUXER PCM_S16LE_DECODER ARESAMPLE_FILTER) += fate-mov-mp4-iamf-7_1_4-video-last
fate-mov-mp4-iamf-7_1_4-video-last: tests/data/asynth-44100-12.wav tests/data/vsynth1.yuv tests/data/filtergraphs/iamf_7_1_4 tests/data/streamgroups/audio_element-7_1_4 tests/data/streamgroups/mix_presentation-7_1_4
fate-mov-mp4-iamf-7_1_4-video-last: SRC = $(TARGET_PATH)/tests/data/asynth-44100-12.wav
fate-mov-mp4-iamf-7_1_4-video-last: SRC2 = $(TARGET_PATH)/tests/data/vsynth1.yuv
fate-mov-mp4-iamf-7_1_4-video-last: CMD = transcode wav $(SRC) mp4 "-auto_conversion_filters \
  -/filter_complex $(TARGET_PATH)/tests/data/filtergraphs/iamf_7_1_4 \
  -/stream_group $(TARGET_PATH)/tests/data/streamgroups/audio_element-7_1_4 \
  -/stream_group $(TARGET_PATH)/tests/data/streamgroups/mix_presentation-7_1_4 \
  -streamid 0:1 -streamid 1:2 -streamid 2:3 -streamid 3:4 -streamid 4:5 -streamid 5:6 -streamid 6:7 -streamid 7:8 -map [FRONT] -map [BACK] -map [CENTER] -map [LFE] -map [SIDE] -map [TOP_FRONT] -map [TOP_BACK] -map 1:v:0 -use_stream_ids_as_track_ids true -c:a flac -c:v mpeg4 -t 1" "-c:a copy -c:v copy -map 0" \
  "-show_entries stream_group=index,id,nb_streams,type:stream_group_components:stream_group_disposition:stream_group_tags:stream_group_stream=index,id:stream_group_stream_disposition:stream=index,id" \
  "-f rawvideo -s 352x288 -pix_fmt yuv420p -i $(SRC2)"

FATE_MOV_FFMPEG_FFPROBE-$(call TRANSCODE, FLAC, MOV, WAV_DEMUXER PCM_S16LE_DECODER ARESAMPLE_FILTER) += fate-mov-mp4-iamf-ambisonic_1
fate-mov-mp4-iamf-ambisonic_1: tests/data/asynth-44100-4.wav tests/data/filtergraphs/iamf_ambisonic_1 tests/data/streamgroups/audio_element-ambisonic_1 tests/data/streamgroups/mix_presentation-ambisonic_1
fate-mov-mp4-iamf-ambisonic_1: SRC = $(TARGET_PATH)/tests/data/asynth-44100-4.wav
fate-mov-mp4-iamf-ambisonic_1: CMD = transcode wav $(SRC) mp4 "-auto_conversion_filters \
  -/filter_complex $(TARGET_PATH)/tests/data/filtergraphs/iamf_ambisonic_1 \
  -/stream_group $(TARGET_PATH)/tests/data/streamgroups/audio_element-ambisonic_1 \
  -/stream_group $(TARGET_PATH)/tests/data/streamgroups/mix_presentation-ambisonic_1 \
  -streamid 0:0 -streamid 1:1 -streamid 2:2 -streamid 3:3 -map [MONO0] -map [MONO1] -map [MONO2] -map [MONO3] -c:a flac -t 1" "-c:a copy -map 0" \
  "-show_entries stream_group=index,id,nb_streams,type:stream_group_components:stream_group_disposition:stream_group_tags:stream_group_stream=index,id:stream_group_stream_disposition"

FATE_FFMPEG += $(FATE_MOV_FFMPEG-yes)
FATE_FFMPEG_FFPROBE += $(FATE_MOV_FFMPEG_FFPROBE-yes)

fate-mov: $(FATE_MOV-yes) $(FATE_MOV_FFMPEG-yes) $(FATE_MOV_FFMPEG_FFPROBE-yes) $(FATE_MOV_FFPROBE-yes) $(FATE_MOV_FASTSTART) $(FATE_MOV_FFMPEG_SAMPLES-yes) $(FATE_MOV_FFMPEG_FFPROBE_SAMPLES-yes)
