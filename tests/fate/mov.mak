FATE_MOV-$(call FRAMEMD5, MOV, H264) += fate-mov-3elist \
           fate-mov-3elist-1ctts \
           fate-mov-1elist-1ctts \
           fate-mov-1elist-noctts \
           fate-mov-elist-starts-ctts-2ndsample \
           fate-mov-1elist-ends-last-bframe \
           fate-mov-2elist-elist1-ends-bframe \
           fate-mov-3elist-encrypted \
           fate-mov-frag-encrypted \
           fate-mov-tenc-only-encrypted \
           fate-mov-3elist-encrypted-kid \
           fate-mov-frag-encrypted-kid \
           fate-mov-tenc-only-encrypted-kid \
           fate-mov-frag-overlap \
           fate-mov-neg-firstpts-discard-frames \

FATE_MOV-$(call FRAMEMD5, MOV, H264, FPS_FILTER) += fate-mov-stream-shorter-than-movie \

FATE_MOV-$(call FRAMEMD5, MOV, MPEG4) += fate-mov-invalid-elst-entry-count \

FATE_MOV-$(call FRAMEMD5, MOV, HEVC) += fate-mov-ibi-elst-starts-b \
           fate-mov-elst-ends-betn-b-and-i \

FATE_MOV_REMUX-$(call REMUX, MP4 MOV) += fate-mov-gpmf-remux \

FATE_MOV_REMUX-$(call REMUX, MP4 WAV) += fate-mov-pcm-remux \

FATE_MOV_FFPROBE-$(call FRAMEMD5, MOV, H264, H264_PARSER) += fate-mov-neg-firstpts-discard \
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
                   fate-mov-mime-codecstring \
                   fate-mov-t35-cdsc-track \
                   fate-mov-t35-rndr-track \

FATE_MOV_FFPROBE-$(call FRAMEMD5, MOV, MPEG4, H264_PARSER) += fate-mov-mp4-extended-atom \

FATE_MOV_FFPROBE-$(call DEMDEC, MOV, HEVC) += fate-mov-dovi-hvce-mp4-read

FATE_MOV_FFPROBE-$(call DEMDEC, MOV, H264) += fate-mov-vfr-bframes-duration

# vpcC set to 3/3/3, with no colr box
FATE_MOV_FFPROBE-$(call DEMDEC, MOV, VP9) += fate-mov-vpcc-reserved-color-tags
fate-mov-vpcc-reserved-color-tags: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -v error \
    -show_entries stream=color_primaries,color_transfer,color_space -of default=nw=1 \
    $(TARGET_SAMPLES)/mov/vpcc-reserved-color-tags.mp4

FATE_MOV-$(call DEMDEC, MOV, RAWVIDEO) += fate-mov-uncv-rgb24
fate-mov-uncv-rgb24: CMD = framecrc -i $(TARGET_SAMPLES)/mov/uncv-rgb24.mp4

# Qclp with a version 0 sound description, no frame size stored;
# 30 packets cross the first 25-packet chunk
FATE_MOV-$(call DEMMUX, MOV, FRAMECRC, PIPE_PROTOCOL) += fate-mov-qclp-stsd-v0
fate-mov-qclp-stsd-v0: CMD = framecrc -i $(TARGET_SAMPLES)/mov/clip43qt3-hi.mov -map 0:a -c copy -frames:a 30

FATE_MOV_FFPROBE-$(CONFIG_MOV_DEMUXER) += fate-ffprobe-stream-entries-tmcd \
                                       fate-ffprobe-group-stream-entries-tmcd \
                                       fate-ffprobe-show-groups-tmcd

fate-ffprobe-stream-entries-tmcd: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -v error -select_streams v:0 \
    -show_entries stream=index -of compact $(TARGET_SAMPLES)/mov/fcp_export8-236.mov
fate-ffprobe-group-stream-entries-tmcd: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -v error -select_streams v:0 \
    -show_entries stream_group_stream=index -of compact $(TARGET_SAMPLES)/mov/fcp_export8-236.mov
fate-ffprobe-show-groups-tmcd: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -v error -select_streams v:0 -show_stream_groups \
    -show_entries stream_group=:stream_group_disposition=:stream_group_tags=:stream_group_components=:stream_group_stream_disposition=:stream_group_stream_tags=:stream_group_stream_side_data=:stream=index \
    -of compact $(TARGET_SAMPLES)/mov/fcp_export8-236.mov

FATE_MOV_FASTSTART = fate-mov-faststart-4gb-overflow \

FATE_SAMPLES_FFMPEG += $(FATE_MOV-yes) $(FATE_MOV_REMUX-yes)
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

# Edit list with encryption, using the decryption_keys option.
fate-mov-3elist-encrypted-kid: CMD = framemd5 -decryption_keys 12345678901234567890123456789012=12345678901234567890123456789012 -i $(TARGET_SAMPLES)/mov/mov-3elist-encrypted.mov

# Fragmented encryption with senc boxes in movie fragments, using the decryption_keys option.
fate-mov-frag-encrypted-kid: CMD = framemd5 -decryption_keys abba271e8bcf552bbd2e86a434a9a5d9=12345678901234567890123456789012 -i $(TARGET_SAMPLES)/mov/mov-frag-encrypted.mp4

# Full-sample encryption and constant IV using only tenc atom (no senc/saio/saiz), using the decryption_keys option.
fate-mov-tenc-only-encrypted-kid: CMD = framemd5 -decryption_keys abba271e8bcf552bbd2e86a434a9a5d9=12345678901234567890123456789012 -i $(TARGET_SAMPLES)/mov/mov-tenc-only-encrypted.mp4

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
fate-mov-mp4-frag-flush: REF = 23006f7699c8ec31c680b7d40b09b7b5
FATE_MOV_FFMPEG-$(call ALLYES, LAVFI_INDEV COLOR_FILTER FORMAT_FILTER TRIM_FILTER \
                               ANULLSRC_FILTER AFORMAT_FILTER ATRIM_FILTER        \
                               WRAPPED_AVFRAME_DECODER PCM_S16LE_DECODER PCM_S16BE_DECODER \
                               PNG_ENCODER PCM_S16LE_ENCODER MP4_MUXER) += fate-mov-mp4-frag-flush

# Makes sure that we pick the right frames according to edit list when there is no keyframe with PTS < edit list start.
# For example, when video starts on a B-frame, and edit list starts on that B-frame too.
# GOP structure : B B I in presentation order.
FATE_MOV_FFMPEG_SAMPLES-$(call FRAMEMD5, MOV, AAC_FIXED H264, ARESAMPLE_FILTER) \
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

# The container's priming and the pre-roll a seek needs are different things.
FATE_MOV_FFPROBE-$(call DEMDEC, MOV, AAC) += fate-mov-aac-2048-priming-preroll
fate-mov-aac-2048-priming-preroll: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_entries stream=initial_padding,seek_preroll -print_format compact $(TARGET_SAMPLES)/mov/aac-2048-priming.mov

# Where a seek lands.
FATE_MOV_FFPROBE-$(call DEMDEC, MOV, AAC) += fate-mov-aac-2048-priming-seek-landing
fate-mov-aac-2048-priming-seek-landing: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -seek_flags preroll -read_intervals 2.3%+\#3 -show_entries packet=pts,dts,duration -print_format compact $(TARGET_SAMPLES)/mov/aac-2048-priming.mov

# HE-AAC whose track clock is the core rate, half the output rate.
FATE_MOV_FFPROBE-$(call DEMDEC, MOV, AAC) += fate-mov-aac-he-core-rate-priming
fate-mov-aac-he-core-rate-priming: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_entries stream=sample_rate,time_base,initial_padding -print_format compact $(TARGET_SAMPLES)/audiomatch/tones_fdkaac_44100_stereo_aac_he.m4a

# The edit starts 64 core ticks into the third packet.
FATE_MOV_FFPROBE-$(call DEMDEC, MOV, AAC) += fate-mov-aac-he-core-rate-mid-packet
fate-mov-aac-he-core-rate-mid-packet: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -read_intervals %+\#3 -show_entries frame=pts,nb_samples -print_format compact $(TARGET_SAMPLES)/aac/sbr_edit_end.mp4

# Seeking into the stream must not drop samples to the container's priming.
FATE_MOV-$(call FRAMECRC, MOV, AAC, PCM_S16LE_ENCODER ARESAMPLE_FILTER VOLUME_FILTER) += fate-mov-aac-2048-priming-seek
fate-mov-aac-2048-priming-seek: CMD = framecrc -ss 2.3 -t 0.15 -i $(TARGET_SAMPLES)/mov/aac-2048-priming.mov -af volume=0,aresample -c:a pcm_s16le

# Dual-rate low-delay SBR ELD: check the output rate independently of edit-list trimming.
FATE_MOV-$(call DEMMUX, MOV, FRAMECRC, PIPE_PROTOCOL) += fate-mov-aac-eld-dual-rate
fate-mov-aac-eld-dual-rate: CMD = framecrc -nofind_stream_info -ignore_editlist 1 -i $(TARGET_SAMPLES)/mov/aac-eld-dual-rate.mp4 -c:a copy

fate-mov-zombie: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_streams -show_packets -show_frames -bitexact -print_format compact $(TARGET_SAMPLES)/mov/white_zombie_scrunch-part.mov

fate-mov-init-nonkeyframe: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_packets -print_format compact -select_streams v $(TARGET_SAMPLES)/mov/mp4-init-nonkeyframe.mp4

fate-mov-displaymatrix: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_entries stream=display_aspect_ratio,sample_aspect_ratio:stream_side_data_list -select_streams v -v 0 $(TARGET_SAMPLES)/mov/displaymatrix.mov

fate-mov-read-amve: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_entries stream_side_data_list -select_streams v -v 0 $(TARGET_SAMPLES)/mov/amve.mov

fate-mov-spherical-mono: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_entries stream_side_data_list -select_streams v -v 0 $(TARGET_SAMPLES)/mov/spherical.mov

fate-mov-dovi-hvce-mp4-read: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_entries stream_side_data_list -select_streams v -v 0 $(TARGET_SAMPLES)/mov/dovi-p7-hvce.mp4

fate-mov-vfr-bframes-duration: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_packets -show_entries packet=pts,dts,duration -print_format compact -select_streams v -v 0 $(TARGET_SAMPLES)/mov/vfr-7-12-1-sequence.mp4

fate-mov-gpmf-remux: CMD = md5 -i $(TARGET_SAMPLES)/mov/fake-gp-media-with-real-gpmf.mp4 -map 0 -c copy -fflags +bitexact -f mp4
fate-mov-gpmf-remux: CMP = oneline
fate-mov-gpmf-remux: REF = 79393b0ea470997c2a33984188b0b415

fate-mov-guess-delay-1: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_entries stream=has_b_frames -select_streams v $(TARGET_SAMPLES)/h264/h264_3bf_nopyramid_nobsrestriction.mp4
fate-mov-guess-delay-2: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_entries stream=has_b_frames -select_streams v $(TARGET_SAMPLES)/h264/h264_3bf_pyramid_nobsrestriction.mp4
fate-mov-guess-delay-3: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_entries stream=has_b_frames -select_streams v $(TARGET_SAMPLES)/h264/h264_4bf_pyramid_nobsrestriction.mp4

fate-mov-faststart-4gb-overflow: CMD = run tools/qt-faststart$(EXESUF) $(TARGET_SAMPLES)/mov/faststart-4gb-overflow.mov $(TARGET_PATH)/faststart-4gb-overflow-output.mov > /dev/null ; do_md5sum faststart-4gb-overflow-output.mov | cut -d " " -f1 ; rm faststart-4gb-overflow-output.mov
fate-mov-faststart-4gb-overflow: CMP = oneline
fate-mov-faststart-4gb-overflow: REF = bc875921f151871e787c4b4023269b29

fate-mov-mp4-with-mov-in24-ver: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_entries stream=codec_name -select_streams 1 $(TARGET_SAMPLES)/mov/mp4-with-mov-in24-ver.mp4

fate-mov-mp4-extended-atom: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_packets -print_format compact -select_streams v $(TARGET_SAMPLES)/mov/extended_atom_size_probe

fate-mov-mime-codecstring: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_entries stream=mime_codec_string -v 0 $(TARGET_SAMPLES)/mov/mov_stream_shorter_than_movie.mov

fate-mov-t35-cdsc-track: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_entries stream_group=index,id,nb_streams,type:stream_group_stream=index,id,codec_name,codec_type,codec_tag_string,extradata_size $(TARGET_SAMPLES)/mov/mov-t35-cdsc-track.mp4

fate-mov-t35-rndr-track: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_entries stream_group=index,id,nb_streams,type:stream_group_stream=index,id,codec_name,codec_type,codec_tag_string,extradata_size $(TARGET_SAMPLES)/mov/mov-t35-rndr-track.mp4

FATE_MOV_FFMPEG_FFPROBE_SAMPLES-$(call REMUX, MP4 MOV, OGG_DEMUXER VORBIS_DECODER) \
                          += fate-mov-mp4-chapters
fate-mov-mp4-chapters: CMD = transcode ogg $(TARGET_SAMPLES)/vorbis/vorbis_chapter_extension_demo.ogg mp4 "-c copy" "-c copy -t 0.1" "-show_chapters"

# Seek in a fragmented file with a chapter track: the chapter track's samples
# all sit in the first fragment, and its own seek must not send the read
# position of the seeked video stream back there.
FATE_MOV_FFMPEG_FFPROBE_SAMPLES-$(call TRANSCODE, MPEG4, MP4 MOV, OGG_DEMUXER VORBIS_DECODER TESTSRC2_FILTER FORMAT_FILTER SINE_FILTER) \
                          += fate-mov-mp4-frag-chapters-seek
fate-mov-mp4-frag-chapters-seek: CMD = transcode ogg $(TARGET_SAMPLES)/vorbis/vorbis_chapter_extension_demo.ogg mp4 \
  "-filter_complex testsrc2=size=64x64:rate=25:duration=12,format=yuv420p[v] -filter_complex sine=d=12[a] -map [v] -map [a] -map_chapters 0 -c:v mpeg4 -c:a pcm_s16le -g 60 -bf 2 -movflags frag_keyframe -frag_duration 1000000" \
  "-c copy -t 0.1" \
  "-use_mfra_for pts -read_intervals 8%+\#40 -show_entries packet=stream_index,pts,dts,pos -print_format compact"

FATE_MOV_FFMPEG_FFPROBE_SAMPLES-$(call TRANSCODE, PNG, MP4 MOV, MJPEG_DECODER SCALE_FILTER) \
                          += fate-mov-cover-image
fate-mov-cover-image: CMD = transcode mov $(TARGET_SAMPLES)/cover_art/Owner-iTunes_9.0.3.15.m4a mp4 "-map 0 -map 0:v -c:a copy -c:v:0 copy -filter:v:1 scale -c:v:1 png" "-map 0 -t 0.1 -c copy" "-show_entries stream_disposition=attached_pic:stream=index,codec_name"

FATE_MOV_FFMPEG_FFPROBE_SAMPLES-$(call TRANSCODE, TTML SUBRIP, MP4 MOV, SRT_DEMUXER TTML_MUXER) += fate-mov-mp4-ttml-stpp fate-mov-mp4-ttml-dfxp
fate-mov-mp4-ttml-stpp: CMD = transcode srt $(TARGET_SAMPLES)/sub/SubRip_capability_tester.srt mp4 "-map 0:s -c:s ttml -time_base:s 1:1000" "-map 0 -c copy" "-of json -show_entries packet:stream=index,codec_type,codec_tag_string,codec_tag,codec_name,time_base,start_time,duration_ts,duration,nb_frames,nb_read_packets:stream_tags"
fate-mov-mp4-ttml-dfxp: CMD = transcode srt $(TARGET_SAMPLES)/sub/SubRip_capability_tester.srt mp4 "-map 0:s -c:s ttml -time_base:s 1:1000 -tag:s dfxp -strict unofficial" "-map 0 -c copy" "-of json -show_entries packet:stream=index,codec_type,codec_tag_string,codec_tag,codec_name,time_base,start_time,duration_ts,duration,nb_frames,nb_read_packets:stream_tags"

FATE_MOV_FFMPEG_FFPROBE_SAMPLES-$(call TRANSCODE, TTML SUBRIP, MP4 MOV, LAVFI_INDEV SMPTEHDBARS_FILTER SRT_DEMUXER MPEG2VIDEO_ENCODER TTML_MUXER RAWVIDEO_MUXER) += fate-mov-mp4-fragmented-ttml-stpp
fate-mov-mp4-fragmented-ttml-stpp: CMD = transcode srt $(TARGET_SAMPLES)/sub/SubRip_capability_tester.srt mp4 \
  "-map 1:v -map 0:s \
   -c:v mpeg2video -b:v 2M -g 48 -sc_threshold 1000000000 \
   -c:s ttml -time_base:s 1:1000 \
   -movflags +cmaf" \
  "-map 0:s -c copy" \
  "-select_streams s -of csv -show_packets -show_data_hash crc32" \
  "-f lavfi -i smptehdbars=duration=24.5245:size=320x180:rate=24000/1001,format=yuv420p" \
  "" "" "data"

FATE_MOV_FFMPEG_FFPROBE_SAMPLES-$(call TRANSCODE, TTML SUBRIP, ISMV MOV, LAVFI_INDEV SMPTEHDBARS_FILTER SRT_DEMUXER MPEG2VIDEO_ENCODER TTML_MUXER RAWVIDEO_MUXER) += fate-mov-mp4-fragmented-ttml-dfxp
fate-mov-mp4-fragmented-ttml-dfxp: CMD = transcode srt $(TARGET_SAMPLES)/sub/SubRip_capability_tester.srt ismv \
  "-map 1:v -map 0:s \
   -c:v mpeg2video -b:v 2M -g 48 -sc_threshold 1000000000 \
   -c:s ttml -tag:s dfxp -time_base:s 1:1000" \
  "-map 0:s -c copy" \
  "-select_streams s -of csv -show_packets -show_data_hash crc32" \
  "-f lavfi -i smptehdbars=duration=24.5245:size=320x180:rate=24000/1001,format=yuv420p" \
  "" "" "data"

define FATE_MOV_CENC_TEST
FATE_MOV_FFMPEG_SAMPLES-$(call REMUX, MP4 $(4), MOV_DEMUXER $(2)_PARSER EXTRACT_EXTRADATA_BSF) += fate-mov-$(1)-cenc
fate-mov-$(1)-cenc: CMD = transcode $(3) $(TARGET_SAMPLES)/$(5) mp4 \
    "-c:v copy -encryption_scheme cenc-aes-ctr -encryption_key 00112233445566778899aabbccddeeff -encryption_kid 00112233445566778899aabbccddeeff" \
    "-c:v copy" "" "" "-decryption_key 00112233445566778899aabbccddeeff"
endef

$(eval $(call FATE_MOV_CENC_TEST,av1,AV1,ivf,IVF,av1/non_uniform_tiling.ivf))
$(eval $(call FATE_MOV_CENC_TEST,h264,H264,h264,H264,h264-conformance/CAMACI3_Sony_C.jsv))
$(eval $(call FATE_MOV_CENC_TEST,h264_iso,H264,matroska,MATROSKA,h264/dts_5frames.mkv))
$(eval $(call FATE_MOV_CENC_TEST,hevc,HEVC,hevc,HEVC,hevc-conformance/ipcm_A_NEC_3.bit))
$(eval $(call FATE_MOV_CENC_TEST,hevc_iso,HEVC,mov,MOV,hevc/extradata-reload-multi-stsd.mov))

# Two encrypted tracks sharing each moof.
FATE_MOV_FFMPEG_SAMPLES-$(call REMUX, MP4 MOV, AAC_DECODER) += fate-mov-aac-cenc-frag-2tracks
fate-mov-aac-cenc-frag-2tracks: CMD = transcode mp4 $(TARGET_SAMPLES)/aac/al04_44.mp4 mp4 \
  "-map 0:a -map 0:a -t 0.2 -c:a copy -movflags +empty_moov -frag_duration 50000 -encryption_scheme cenc-aes-ctr -encryption_key 00112233445566778899aabbccddeeff -encryption_kid 00112233445566778899aabbccddeeff" \
  "-map 0:a -c copy" "" "" "-decryption_key 00112233445566778899aabbccddeeff"

# The empty moov written by delay_moov must not count the buffered audio.
FATE_MOV_FFMPEG_FFPROBE-$(call ALLYES, PCM_S16LE_DECODER PCM_S16LE_ENCODER WAV_DEMUXER MOV_MUXER MOV_DEMUXER FRAMECRC_MUXER FILE_PROTOCOL PIPE_PROTOCOL) += fate-mov-pcm-delay-moov
fate-mov-pcm-delay-moov: tests/data/asynth-44100-2.wav
fate-mov-pcm-delay-moov: CMD = transcode wav $(TARGET_PATH)/tests/data/asynth-44100-2.wav mov \
  "-t 0.05 -c:a pcm_s16le -movflags +delay_moov -frag_duration 25000" "-c:a copy" "-show_entries stream=duration_ts,nb_frames"

# avif muxing - still image with 1 item.
FATE_MOV_FFMPEG_SAMPLES-$(call REMUX, AVIF OBU, AV1_DECODER AV1_PARSER) \
                           += fate-mov-avif-mux-still-image-1-item
fate-mov-avif-mux-still-image-1-item: CMD = stream_remux obu $(TARGET_SAMPLES)/av1/still_picture_rgb.obu "" avif \
  "" "-c:v av1" "-c:v copy"

# Loop a color tile and its alpha tile to exercise animated AVIF track headers.
FATE_MOV_FFMPEG_SAMPLES-$(call REMUX, AVIF MOV, AV1_DECODER AV1_PARSER) \
                           += fate-mov-avif-mux-animated-alpha
fate-mov-avif-mux-animated-alpha: CMD = stream_remux mov $(TARGET_SAMPLES)/avif/gain_map_alpha_grid.avif \
  "-c:v av1 -stream_loop 1" avif "-map 0:0 -map 0:12" "-c:v av1" "-map 0 -c:v copy"

# avif demuxing - still image with 1 item.
FATE_MOV_FFMPEG_SAMPLES-$(call FRAMECRC, MOV, AV1, AV1_PARSER) \
                           += fate-mov-avif-demux-still-image-1-item
fate-mov-avif-demux-still-image-1-item: CMD = framecrc -c:v av1 -i $(TARGET_SAMPLES)/avif/still_image.avif -c:v copy

# avif demuxing - still image with multiple items.
FATE_MOV_FFMPEG_SAMPLES-$(call FRAMECRC, MOV, AV1, AV1_PARSER) \
                           += fate-mov-avif-demux-still-image-multiple-items
fate-mov-avif-demux-still-image-multiple-items: CMD = framecrc -c:v av1 -i $(TARGET_SAMPLES)/avif/still_image_exif.avif -c:v copy

# avif demuxing - still image with multiple items forming two grids (color + alpha), which themselves are handled as an Alpha derived image
FATE_MOV_FFMPEG_FFPROBE_SAMPLES-$(call FRAMECRC, MOV, AV1, AV1_PARSER) \
                           += fate-mov-avif-demux-still-image-alpha-grid
fate-mov-avif-demux-still-image-alpha-grid: CMD = stream_demux mov $(TARGET_SAMPLES)/avif/gain_map_alpha_grid.avif "-c:v av1" "-c:v copy -map 0:g:0 -map 0:g:1" \
  "-of compact -show_entries stream_group=index,id,nb_streams,type:stream_group_components:stream_group_disposition:stream_group_tags:stream_group_side_data:stream_group_stream=index,id:stream_group_stream_disposition:stream_group_stream_side_data_list"

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

# heic demuxing - still image with multiple thumbnails, and item entries stored in different order in iloc/iinf.
FATE_MOV_FFMPEG_FFPROBE_SAMPLES-$(call FRAMECRC, MOV, HEVC MJPEG, HEVC_PARSER) \
                           += fate-mov-heic-demux-still-image-multiple-thumb
fate-mov-heic-demux-still-image-multiple-thumb: CMD = stream_demux mov $(TARGET_SAMPLES)/heif/P1001091.HIF "" "-c:v copy -map 0" \
  "-show_entries stream=index,id:stream_disposition:stream_side_data_list:frame=stream_index:frame_side_data_list:frame_tags"

# heic demuxing - still image with multiple items in a grid.
FATE_MOV_FFMPEG_FFPROBE_SAMPLES-$(call FRAMECRC, MOV, HEVC, HEVC_PARSER) \
                           += fate-mov-heic-demux-still-image-grid
fate-mov-heic-demux-still-image-grid: CMD = stream_demux mov $(TARGET_SAMPLES)/heif-conformance/C007.heic "" "-c:v copy -map 0:g:0" \
  "-show_entries stream_group=index,id,nb_streams,type:stream_group_components:stream_group_disposition:stream_group_tags:stream_group_stream=index,id:stream_group_stream_disposition"

# heic demuxing - still image with multiple items in an overlay canvas.
FATE_MOV_FFMPEG_FFPROBE_SAMPLES-$(call FRAMECRC, MOV, HEVC, HEVC_PARSER) \
                           += fate-mov-heic-demux-still-image-iovl
fate-mov-heic-demux-still-image-iovl: CMD = stream_demux mov $(TARGET_SAMPLES)/heif-conformance/C015.heic "" "-c:v copy -map 0:g:0" \
  "-show_entries stream_group=index,id,nb_streams,type:stream_group_components:stream_group_disposition:stream_group_tags:stream_group_stream=index,id:stream_group_stream_disposition"

# heic demuxing - still image where one image item is placed twice on an overlay canvas.
FATE_MOV_FFMPEG_FFPROBE_SAMPLES-$(call FRAMECRC, MOV, HEVC, HEVC_PARSER) \
                           += fate-mov-heic-demux-still-image-iovl-2
fate-mov-heic-demux-still-image-iovl-2: CMD = stream_demux mov $(TARGET_SAMPLES)/heif-conformance/C021.heic "" "-c:v copy -map 0:g:0" \
  "-show_entries stream_group=index,id,nb_streams,type:stream_group_components:stream_group_disposition:stream_group_tags:stream_group_stream=index,id:stream_group_stream_disposition"

# heic demuxing - still image with a Gain Map stream group signaled with a tmap derived image
FATE_MOV_FFMPEG_FFPROBE_SAMPLES-$(call FRAMECRC, MOV, HEVC, HEVC_PARSER) \
                           += fate-mov-heic-demux-still-image-gainmap-tmap
fate-mov-heic-demux-still-image-gainmap-tmap: CMD = stream_demux mov $(TARGET_SAMPLES)/heif/gain_map_tmap.heic "" "-c:v copy -map 0:g:0 -map 0:4" \
  "-of compact -show_entries stream_group=index,id,nb_streams,type:stream_group_components:stream_group_disposition:stream_group_tags:stream_group_side_data:stream_group_stream=index,id:stream_group_stream_disposition:stream_group_stream_side_data_list"

# Resulting remux should have:
# 1. first audio stream with AV_DISPOSITION_HEARING_IMPAIRED
# 2. second audio stream with AV_DISPOSITION_VISUAL_IMPAIRED | DESCRIPTIONS
FATE_MOV_FFMPEG_FFPROBE_SAMPLES-$(call REMUX, MP4 MOV, MPEGTS_DEMUXER AC3_DECODER) \
                          += fate-mov-mp4-disposition-mpegts-remux
fate-mov-mp4-disposition-mpegts-remux: CMD = transcode mpegts $(TARGET_SAMPLES)/mpegts/pmtchange.ts mp4 "-map 0:1 -map 0:2 -c copy -disposition:a:0 +hearing_impaired" "-map 0 -c copy" "-of json -show_entries stream_disposition:stream=index"

FATE_MOV_FFMPEG_FFPROBE_SAMPLES-$(call REMUX, MP4 MOV, H264_DECODER) \
                          += fate-mov-write-amve
fate-mov-write-amve: CMD = transcode mov $(TARGET_SAMPLES)/mov/amve.mov mp4 "-c:v copy" "-c:v copy -t 0.5" "-show_entries stream_side_data_list"

# These tests check that Dolby Vision Profile 7 dual-layer content with an
# hvcE (enhancement-layer HEVC configuration) is preserved on MP4 remux.
FATE_MOV_FFMPEG_FFPROBE_SAMPLES-$(call REMUX, MP4 MOV, HEVC_DECODER) \
                          += fate-mov-dovi-hvce-mp4-to-mp4
fate-mov-dovi-hvce-mp4-to-mp4: CMD = transcode mov $(TARGET_SAMPLES)/mov/dovi-p7-hvce.mp4 mp4 "-map 0 -c copy -strict unofficial" "-map 0 -c copy" "-show_entries stream_side_data_list -select_streams v"

FATE_MOV_FFMPEG_FFPROBE_SAMPLES-$(call REMUX, MP4 MOV, MATROSKA_DEMUXER HEVC_DECODER) \
                          += fate-mov-dovi-hvce-mkv-to-mp4
fate-mov-dovi-hvce-mkv-to-mp4: CMD = transcode matroska $(TARGET_SAMPLES)/mkv/dovi-p7-hvce.mkv mp4 "-map 0 -c copy -strict unofficial" "-map 0 -c copy" "-show_entries stream_side_data_list -select_streams v"

FATE_MOV_FFMPEG_SAMPLES-$(call REMUX, MP4 MOV, H264_PARSER) \
                          += fate-mov-mp4-multiple-stsd-muxing
fate-mov-mp4-multiple-stsd-muxing: CMD = transcode mov $(TARGET_SAMPLES)/h264/extradata-reload-multi-stsd.mov mp4 "-c:v copy" "-c:v copy"

# ALAC config stored in the wave atom without an atom header
FATE_MOV_FFMPEG_SAMPLES-$(call REMUX, MP4 MOV, ALAC_DECODER) += fate-mov-alac-bare-wave-remux
fate-mov-alac-bare-wave-remux: CMD = transcode mov $(TARGET_SAMPLES)/mov/01snd09.mov mp4 "-c copy -frames:a 1" "-c copy"

FATE_MOV_FFMPEG_SAMPLES-$(call REMUX, MP4 MOV, AAC_PARSER) \
                          += fate-mov-mp4-edst-remainder
fate-mov-mp4-edst-remainder: CMD = stream_remux mov $(TARGET_SAMPLES)/audiomatch/tones_fdkaac_44100_stereo_aac_lc.m4a "" mp4 "" "" "-c:a copy"

# QDMC configuration must be written back in a wave atom
FATE_MOV_FFMPEG_SAMPLES-$(call REMUX, MOV) += fate-mov-qdmc-remux
fate-mov-qdmc-remux: CMD = transcode mov $(TARGET_SAMPLES)/qdmc/rumcoke.mov mov "-c copy -t 0.4" "-c copy"

# Reserved colour values must not be written to colr
FATE_MOV_FFMPEG_FFPROBE_SAMPLES-$(call REMUX, MP4 MOV, H264_PARSER H264_DECODER) += fate-mov-colr-reserved-color-tags
fate-mov-colr-reserved-color-tags: CMD = stream_remux mov $(TARGET_SAMPLES)/mov/test_iibbibb.mp4 "" mp4 \
    "-map 0:v -frames:v 1 -color_primaries 3 -color_trc 3 -colorspace 3" "" "-c:v copy" \
    "-select_streams v -show_entries stream=color_primaries,color_transfer,color_space -of default=nw=1"

# nclc signals gamma 2.8 as an unspecified transfer with a gama atom
FATE_MOV_FFMPEG_FFPROBE_SAMPLES-$(call REMUX, MOV, H264_PARSER H264_DECODER) += fate-mov-gama-color-trc
fate-mov-gama-color-trc: CMD = stream_remux mov $(TARGET_SAMPLES)/mov/test_iibbibb.mp4 "" mov \
    "-map 0:v -frames:v 1 -color_primaries smpte170m -color_trc gamma28 -colorspace smpte170m" "" "-c:v copy" \
    "-select_streams v -show_entries stream=color_primaries,color_transfer,color_space -of default=nw=1"

# without colr, gamma 2.2 is still signaled with a gama atom
FATE_MOV_FFMPEG_FFPROBE_SAMPLES-$(call REMUX, MOV, H264_PARSER H264_DECODER) += fate-mov-gama-only-color-trc
fate-mov-gama-only-color-trc: CMD = stream_remux mov $(TARGET_SAMPLES)/mov/test_iibbibb.mp4 "" mov \
    "-map 0:v -frames:v 1 -color_trc gamma22" "" "-c:v copy" \
    "-select_streams v -show_entries stream=color_primaries,color_transfer,color_space -of default=nw=1"

# format-level branding: major_brand, minor_version, compatible_brands should be deleted on re-encode
FATE_MOV_FFMPEG_FFPROBE_SAMPLES-$(call ENCDEC, AAC AAC, NUT MOV) += fate-mov-reenc-delete-format-metadata
fate-mov-reenc-delete-format-metadata: CMD = transcode mov $(TARGET_SAMPLES)/cover_art/Owner-iTunes_9.0.3.15.m4a nut "-map 0:a:0 -c:a aac -bitexact -t 0.1" "-c copy -t 0.1" "-show_entries format_tags" "" "" "" null

# audio-only format tags (gapless_playback, iTunSMPB, iTunNORM) must survive when
# only the cover-art video stream is re-encoded and the audio is stream-copied
FATE_MOV_FFMPEG_FFPROBE_SAMPLES-$(call ENCDEC, PNG MJPEG, NUT MOV, SCALE_FILTER) += fate-mov-cover-reenc-keeps-audio-format-tags
fate-mov-cover-reenc-keeps-audio-format-tags: CMD = transcode mov $(TARGET_SAMPLES)/cover_art/Owner-iTunes_9.0.3.15.m4a nut "-map 0:a:0 -c:a copy -map 0:v:0 -filter:v scale -c:v png -bitexact -t 0.1" "-c copy -t 0.1" "-show_entries format_tags" "" "" "" null

# stream-level branding: vendor_id should be deleted on re-encode
FATE_MOV_FFMPEG_FFPROBE_SAMPLES-$(call ENCDEC, FLAC PCM_S16BE, NUT MOV) += fate-mov-reenc-delete-stream-metadata
fate-mov-reenc-delete-stream-metadata: CMD = transcode mov $(TARGET_SAMPLES)/qt-surge-suite/surge-2-16-B-twos.mov nut "-c:a flac -bitexact -t 0.1" "-c copy -t 0.1" "-show_entries stream_tags" "" "" "" null

# plain -metadata (global scope) must not suppress stream-level vendor_id pruning
FATE_MOV_FFMPEG_FFPROBE_SAMPLES-$(call ENCDEC, FLAC PCM_S16BE, NUT MOV) += fate-mov-reenc-delete-stream-metadata-global-tag
fate-mov-reenc-delete-stream-metadata-global-tag: CMD = transcode mov $(TARGET_SAMPLES)/qt-surge-suite/surge-2-16-B-twos.mov nut "-c:a flac -bitexact -t 0.1 -metadata vendor_id=custom" "-c copy -t 0.1" "-show_entries format_tags:stream_tags" "" "" "" null

FATE_MOV_FFMPEG_FFPROBE_SAMPLES-$(call DEMMUX, MOV, MP4, FRAMECRC_MUXER) += fate-mov-mp4-mpegh-5_1_4
fate-mov-mp4-mpegh-5_1_4: CMD = stream_demux mp4 $(TARGET_SAMPLES)/mov/mpegh_layout16.mp4 "" "-c:a copy" "-show_entries stream=profile,level,codec_tag_string"

FATE_MOV_FFMPEG_FFPROBE_SAMPLES-$(call REMUX, MP4 MOV) += fate-mov-mp4-mpegh-5_1_4-copy
fate-mov-mp4-mpegh-5_1_4-copy: CMD = stream_remux mp4 $(TARGET_SAMPLES)/mov/mpegh_layout16.mp4 "" mp4 "" "" "-c:a copy" "-show_entries stream=profile,level,codec_tag_string"

FATE_SAMPLES_FFMPEG_FFPROBE += $(FATE_MOV_FFMPEG_FFPROBE_SAMPLES-yes)
FATE_SAMPLES_FFMPEG += $(FATE_MOV_FFMPEG_SAMPLES-yes)

FATE_MOV_FFMPEG-$(call TRANSCODE, PCM_S16LE, MOV, WAV_DEMUXER PAN_FILTER) \
                          += fate-mov-channel-description
fate-mov-channel-description: tests/data/asynth-44100-1.wav tests/data/filtergraphs/mov-channel-description
fate-mov-channel-description: CMD = transcode wav $(TARGET_PATH)/tests/data/asynth-44100-1.wav mov "-/filter_complex $(TARGET_PATH)/tests/data/filtergraphs/mov-channel-description -map [outFL] -map [outFR] -map [outFC] -map [outLFE] -map [outBL] -map [outBR] -map [outDL] -map [outDR] -c:a pcm_s16le" "-map 0 -c copy -frames:a 0"

# Test PCM in mp4 and channel layout
FATE_MOV_FFMPEG-$(call TRANSCODE, PCM_S16LE, MP4 MOV, WAV_DEMUXER PAN_FILTER) \
                          += fate-mov-mp4-pcm
fate-mov-mp4-pcm: tests/data/asynth-96000-1.wav tests/data/filtergraphs/mov-mp4-pcm
fate-mov-mp4-pcm: CMD = transcode wav $(TARGET_PATH)/tests/data/asynth-96000-1.wav mp4 "-/filter_complex $(TARGET_PATH)/tests/data/filtergraphs/mov-mp4-pcm -map [mono] -map [stereo] -map [2.1] -map [5.1] -map [7.1] -c:a pcm_s16le" "-map 0 -c copy -frames:a 0"

# Test floating sample format PCM in mp4 and unusual channel layout
FATE_MOV_FFMPEG-$(call TRANSCODE, PCM_F32LE, MP4 MOV, WAV_DEMUXER PAN_FILTER) \
                          += fate-mov-mp4-pcm-float
fate-mov-mp4-pcm-float: tests/data/asynth-44100-1.wav
fate-mov-mp4-pcm-float: CMD = transcode wav $(TARGET_PATH)/tests/data/asynth-44100-1.wav mp4 "-af aresample,pan=FR+FL+FR|c0=c0|c1=c0|c2=c0 -c:a pcm_f32le" "-map 0 -c copy -frames:a 0"

# Test IMA4 above 65535 Hz, in a version 2 sound description
FATE_MOV_FFMPEG-$(call REMUX, MOV, ADPCM_IMA_QT_ENCODER WAV_DEMUXER PCM_S16LE_DECODER ARESAMPLE_FILTER) \
                          += fate-mov-ima4-96k
fate-mov-ima4-96k: tests/data/asynth-96000-1.wav
fate-mov-ima4-96k: CMD = transcode wav $(TARGET_PATH)/tests/data/asynth-96000-1.wav mov "-af aresample -t 0.002 -c:a adpcm_ima_qt" "-c copy"

fate-mov-pcm-remux: tests/data/asynth-44100-1.wav
fate-mov-pcm-remux: CMD = md5 -i $(TARGET_PATH)/tests/data/asynth-44100-1.wav -map 0 -c copy -fflags +bitexact -f mp4
fate-mov-pcm-remux: CMP = oneline
fate-mov-pcm-remux: REF = 0e17606ac1d7f4071c9bbacc7d521da1

FATE_MOV_FFMPEG-$(call TRANSCODE, RAWVIDEO, MOV, TESTSRC_FILTER SETPTS_FILTER) += fate-mov-vfr
fate-mov-vfr: CMD = md5 -filter_complex testsrc=size=2x2:duration=1,setpts=N*N:strip_fps=1 -c rawvideo -fflags +bitexact -f mov

FATE_MOV_FFMPEG-$(call TRANSCODE, DFPWM, MOV, LAVFI_INDEV SINE_FILTER ATRIM_FILTER PCM_S16LE_DECODER ARESAMPLE_FILTER) += fate-mov-dfpwm-last-packet
fate-mov-dfpwm-last-packet: CMD = transcode lavfi "sine=r=48000,atrim=end_sample=85" mov "-auto_conversion_filters -c:a dfpwm -movie_timescale 48000" "-c copy"
fate-mov-vfr: CMP = oneline
fate-mov-vfr: REF = 54167f70fb0c2d164587b6af1aad9623

# The QuickTime user data list has to carry the composer atom the demuxer
# already reads: without it a mov to mov remux dropped the tag.
FATE_MOV_FFMPEG_FFPROBE-$(call TRANSCODE, PCM_S16LE, MOV, WAV_DEMUXER ARESAMPLE_FILTER) += fate-mov-composer-metadata
fate-mov-composer-metadata: tests/data/asynth-44100-2.wav
fate-mov-composer-metadata: CMD = transcode wav ./tests/data/asynth-44100-2.wav mov \
    "-af aresample -c:a pcm_s16le -t 0.05 -metadata composer=Bach" \
    "-af aresample -c:a pcm_s16le" "-show_entries format_tags"

# 3GPP loci location: ties rounded to even, the altitude clipped to its
# 16.16 range, and a missing altitude written as zero.
FATE_MOV_FFMPEG_FFPROBE-$(call TRANSCODE, PCM_S16LE, MP4 MOV, WAV_DEMUXER ARESAMPLE_FILTER) \
                       += fate-mov-mp4-loci fate-mov-mp4-loci-no-altitude
fate-mov-mp4-loci fate-mov-mp4-loci-no-altitude: tests/data/asynth-44100-2.wav
fate-mov-mp4-loci: CMD = transcode wav ./tests/data/asynth-44100-2.wav mp4 \
    "-af aresample -c:a pcm_s16le -t 0.05 -metadata location=+00.03125-000.15625+32767.999999/Place" \
    "-af aresample -c:a pcm_s16le" "-show_entries format_tags=location"
fate-mov-mp4-loci-no-altitude: CMD = transcode wav ./tests/data/asynth-44100-2.wav mp4 \
    "-af aresample -c:a pcm_s16le -t 0.05 -metadata location=-33.8568+151.2153/" \
    "-af aresample -c:a pcm_s16le" "-show_entries format_tags=location"

# The 3GPP TS 26.244 user data boxes have to be read back with their languages.
FATE_MOV_FFMPEG_FFPROBE-$(call TRANSCODE, MPEG4, TGP MOV, LAVFI_INDEV COLOR_FILTER WRAPPED_AVFRAME_DECODER) += fate-mov-3gp-udta-metadata
fate-mov-3gp-udta-metadata: CMD = transcode lavfi "color=s=32x32:r=1:d=1" 3gp \
    "-c:v mpeg4 -metadata title=Title -metadata artist=Artist -metadata author=Author \
     -metadata genre=Genre -metadata comment=Comment -metadata album=Album -metadata track=7 \
     -metadata copyright=Copyright -metadata date=2024 \
     -metadata title-eng=Title -metadata artist-eng=Artist -metadata author-eng=Author \
     -metadata genre-eng=Genre -metadata comment-eng=Comment -metadata album-eng=Album \
     -metadata copyright-eng=Copyright" \
    "-c copy" "-show_entries format_tags"

# yuv2 defaults to full range BT.601; a colr matrix or a decoder option still takes precedence.
MOV_YUV2_FILTERS = COLOR FORMAT SCALE
FATE_MOV_FFMPEG_FFPROBE-$(call FILTERDEMDECENCMUX, $(MOV_YUV2_FILTERS), MOV, RAWVIDEO, RAWVIDEO, MOV) \
    += fate-mov-yuv2-color-default fate-mov-yuv2-color-range
FATE_MOV_FFMPEG_FFPROBE-$(call FILTERDEMDECENCMUX, $(MOV_YUV2_FILTERS) SETPARAMS, MOV, RAWVIDEO, RAWVIDEO, MOV) \
    += fate-mov-yuv2-color-colr

MOV_YUV2_SRC = color=c=black:s=32x32:r=1:d=1,format=yuv422p
MOV_YUV2_COLR_SRC = $(MOV_YUV2_SRC),setparams=colorspace=bt709:color_primaries=bt709:color_trc=bt709
MOV_YUV2_GEN = $(FFMPEG) -nostdin -v error -filter_complex $(1) \
    -pix_fmt yuyv422 -color_range pc -c:v rawvideo -tag:v yuv2 \
    -threads 1 -flags +bitexact -fflags +bitexact -f mov -y
MOV_YUV2_PROBE = ffprobe$(PROGSSUF)$(EXESUF) -v error -bitexact \
    -show_entries frame=color_range,color_space -of default=nw=1

fate-mov-yuv2-color-default: CMD = run_with_temp \
    "$(call MOV_YUV2_GEN,$(MOV_YUV2_SRC))" "$(MOV_YUV2_PROBE)" mov
fate-mov-yuv2-color-colr: CMD = run_with_temp \
    "$(call MOV_YUV2_GEN,$(MOV_YUV2_COLR_SRC))" "$(MOV_YUV2_PROBE)" mov
fate-mov-yuv2-color-range: CMD = run_with_temp \
    "$(call MOV_YUV2_GEN,$(MOV_YUV2_SRC))" "$(MOV_YUV2_PROBE) -color_range tv" mov

FATE_MOV_FFMPEG_FFPROBE-$(call ALLYES, COLOR_FILTER SETPTS_FILTER MPEG4_ENCODER \
                                      MOV_MUXER MOV_DEMUXER FILE_PROTOCOL)      \
                                      += fate-mov-vfr-bframes-derived-duration

FATE_MOV_FFMPEG_FFPROBE-$(call ALLYES, COLOR_FILTER SETPTS_FILTER MPEG4_ENCODER \
                                      MP4_MUXER MOV_DEMUXER FILE_PROTOCOL)      \
                                      += fate-mov-trun-large-sample-duration \
                                         fate-mov-trun-zero-sample-size

# Generate a fragmented VFR file, then replace the last sample duration in
# its first trun with UINT32_MAX.
fate-mov-trun-large-sample-duration: CMD = run_with_patched_temp \
    "$(FFMPEG) -nostdin -v error \
    -filter_complex color=c=black:s=2x2:r=1,setpts=N*N \
    -frames:v 10 -fps_mode vfr -c:v mpeg4 -g 5 -bf 0 -q:v 2 -threads 1 \
    -flags +bitexact -fflags +bitexact \
    -movflags empty_moov+frag_keyframe+default_base_moof -f mp4 -y" \
    "ffprobe$(PROGSSUF)$(EXESUF) -show_packets \
    -show_entries packet=pts,dts,duration -print_format compact \
    -select_streams v -v 0" mp4 923 "\\0377\\0377\\0377\\0377"

# An explicitly empty sample must not prevent reading the following fragment.
# Set the last sample size in the first trun to zero; its unreferenced payload
# remains in mdat. Disable parsing to observe the empty sample and its duration.
fate-mov-trun-zero-sample-size: CMD = run_with_patched_temp \
    "$(FFMPEG) -nostdin -v error \
    -filter_complex color=c=black:s=2x2:r=1,setpts=N*N \
    -frames:v 10 -fps_mode vfr -c:v mpeg4 -g 5 -bf 0 -q:v 2 -threads 1 \
    -flags +bitexact -fflags +bitexact \
    -movflags empty_moov+frag_keyframe+default_base_moof -f mp4 -y" \
    "ffprobe$(PROGSSUF)$(EXESUF) -fflags +noparse+nofillin -show_packets \
    -show_entries packet=pts,dts,duration,size,pos -print_format compact \
    -select_streams v -v 0" mp4 927 "\\0000\\0000\\0000\\0000"

# Version 0 composition offsets are unsigned: an offset of 2^31 is accepted.
FATE_MOV_FFMPEG-$(call ALLYES, COLOR_FILTER MPEG4_ENCODER MP4_MUXER FILE_PROTOCOL) \
                += fate-mov-mp4-ctts-v0-boundary
fate-mov-mp4-ctts-v0-boundary: CMD = md5 -filter_complex color=s=2x2:r=1:d=4 \
    -c:v mpeg4 -g 12 -bf 1 -q:v 2 -threads 1 -flags +bitexact -fflags +bitexact \
    -video_track_timescale 1073741824 -f mp4

MOV_NOELST_VIDEO_DELAY_COMPONENTS = COLOR_FILTER SETPTS_FILTER SINE_FILTER \
                                    MPEG4_ENCODER PCM_S16LE_ENCODER \
                                    MP4_MUXER FILE_PROTOCOL

MOV_NOELST_VIDEO_DELAY_FILTER = color=s=2x2:r=24:d=1,setpts=PTS+0.5/TB[v];sine=r=8000:d=1[a]

MOV_NOELST_VIDEO_DELAY = -map [v] -map [a] -fps_mode passthrough \
    -c:v mpeg4 -g 12 -bf 2 -q:v 2 -threads 1 -c:a pcm_s16le \
    -flags +bitexact -fflags +bitexact \
    -use_editlist 0 -movflags +negative_cts_offsets -f mp4

FATE_MOV_FFMPEG_FFPROBE-$(call ALLYES, $(MOV_NOELST_VIDEO_DELAY_COMPONENTS) MOV_DEMUXER) \
                                    += fate-mov-mp4-noelst-video-delay

# Without edit lists, video starting 0.5 s after the audio keeps its delay in
# the composition offsets, and the durations cover the delayed last frame.
fate-mov-mp4-noelst-video-delay: CMD = run_with_temp \
    "$(FFMPEG) -nostdin -v error -filter_complex $(MOV_NOELST_VIDEO_DELAY_FILTER) $(MOV_NOELST_VIDEO_DELAY) -y" \
    "ffprobe$(PROGSSUF)$(EXESUF) -show_entries format=duration:stream=index,start_time:packet=stream_index,pts,dts \
    -print_format compact -v 0" mp4

# Include container headers: the MOV demuxer caps stream duration to the
# stts duration, so the packet probe above cannot check the mdhd extension.
FATE_MOV_FFMPEG-$(call ALLYES, $(MOV_NOELST_VIDEO_DELAY_COMPONENTS)) \
                += fate-mov-mp4-noelst-video-delay-md5
fate-mov-mp4-noelst-video-delay-md5: CMD = md5 -filter_complex "$(MOV_NOELST_VIDEO_DELAY_FILTER)" $(MOV_NOELST_VIDEO_DELAY)

FATE_MOV_FFMPEG_FFPROBE-$(call ALLYES, COLOR_FILTER SETPTS_FILTER SINE_FILTER \
                                      ASETPTS_FILTER ARESAMPLE_FILTER MPEG4_ENCODER \
                                      AAC_ENCODER \
                                      MP4_MUXER MOV_DEMUXER FILE_PROTOCOL)      \
                                      += fate-mov-mp4-noelst-common-origin

# Video and AAC audio sharing a negative origin start together: the audio
# priming does not make the video look delayed.
fate-mov-mp4-noelst-common-origin: CMD = run_with_temp \
    "$(FFMPEG) -nostdin -v error \
    -filter_complex color=s=2x2:r=24:d=1,setpts=PTS-1/TB[v];sine=r=8000:d=1,asetpts=PTS-1/TB[a] \
    -map [v] -map [a] -fps_mode passthrough \
    -c:v mpeg4 -g 12 -bf 2 -q:v 2 -threads 1 -c:a aac \
    -flags +bitexact -fflags +bitexact \
    -use_editlist 0 -movflags +negative_cts_offsets -f mp4 -y" \
    "ffprobe$(PROGSSUF)$(EXESUF) -show_entries stream=index,start_time:packet=stream_index,pts,dts \
    -print_format compact -v 0" mp4

FATE_MOV_FFMPEG_FFPROBE-$(call ALLYES, COLOR_FILTER SETTB_FILTER SETPTS_FILTER \
                                      SINE_FILTER MPEG4_ENCODER PCM_S16LE_ENCODER \
                                      MP4_MUXER MOV_DEMUXER FILE_PROTOCOL)      \
                                      += fate-mov-mp4-noelst-frag-delay

# Fragmented output keeps no start delay: reordering at a 1e9 timescale would
# overflow the 32-bit offsets once the first fragment is written.
fate-mov-mp4-noelst-frag-delay: CMD = run_with_temp \
    "$(FFMPEG) -nostdin -v error \
    -filter_complex color=s=2x2:r=1:d=5,settb=1/1000,setpts=PTS+0.5/TB[v];sine=r=8000:d=5[a] \
    -map [v] -map [a] -fps_mode passthrough \
    -c:v mpeg4 -g 12 -bf 2 -q:v 2 -threads 1 -c:a pcm_s16le \
    -flags +bitexact -fflags +bitexact -video_track_timescale 1000000000 \
    -use_editlist 0 -movflags +negative_cts_offsets -frag_duration 1000000 -f mp4 -y" \
    "ffprobe$(PROGSSUF)$(EXESUF) -select_streams v -show_entries stream=start_time:packet=pts,dts \
    -print_format compact -v 0" mp4

FATE_MOV_FFMPEG_FFPROBE-$(call ALLYES, LAVFI_INDEV ANULLSRC_FILTER AFORMAT_FILTER \
                                      PCM_S16$(if $(filter yes,$(HAVE_BIGENDIAN)),BE,LE)_DECODER \
                                      PCM_S16BE_ENCODER                          \
                                      MOV_MUXER MOV_DEMUXER FILE_PROTOCOL)       \
                                      += fate-mov-stsd-v2-sample-rate-nan \
                                         fate-mov-stsd-v2-sample-rate-overflow \
                                         fate-mov-stsd-v2-sample-rate-negative \
                                         fate-mov-stsd-v2-sample-rate-zero

# A version 2 SoundDescription stores the sample rate as a double. Requesting
# a rate that does not fit the legacy 16.16 field makes the muxer write one,
# and empty_moov puts it ahead of mdat, so the double always starts at offset
# 489 of the generated file. The tests below replace it with values that must
# not be converted to codecpar->sample_rate.
#
# The diagnostic also identifies which check rejected the file: it prints the
# double, while the later sample_rate check runs after the conversion and can
# only report its result.
#
# The decoder dependency follows the host endianness, because the lavfi input
# device describes its s16 output with the native-endian PCM codec id.
MOV_STSD_V2_GEN = $(FFMPEG) -nostdin -v error \
    -f lavfi -i anullsrc=cl=stereo:r=96000,aformat=sample_fmts=s16 \
    -t 0.01 -c:a pcm_s16be -threads 1 \
    -flags +bitexact -fflags +bitexact -movflags empty_moov+frag_keyframe \
    -f mov -y
MOV_STSD_V2_PROBE = ffprobe$(PROGSSUF)$(EXESUF) -v error \
    -show_entries stream=sample_rate -print_format compact

# NaN: converting it to an integer is undefined.
fate-mov-stsd-v2-sample-rate-nan: CMD = run_with_patched_temp \
    "$(MOV_STSD_V2_GEN)" "$(MOV_STSD_V2_PROBE)" mov \
    489 "\\0177\\0370\\0000\\0000\\0000\\0000\\0000\\0000" ; test $$? -ne 0
fate-mov-stsd-v2-sample-rate-nan: CMP = grep
fate-mov-stsd-v2-sample-rate-nan: REF = Invalid sample rate nan

# INT_MAX + 1: the smallest positive double that does not fit an int.
fate-mov-stsd-v2-sample-rate-overflow: CMD = run_with_patched_temp \
    "$(MOV_STSD_V2_GEN)" "$(MOV_STSD_V2_PROBE)" mov \
    489 "\\0101\\0340\\0000\\0000\\0000\\0000\\0000\\0000" ; test $$? -ne 0
fate-mov-stsd-v2-sample-rate-overflow: CMP = grep
fate-mov-stsd-v2-sample-rate-overflow: REF = Invalid sample rate 2.14748e+09

# A negative rate fits an int, and is rejected for being negative.
fate-mov-stsd-v2-sample-rate-negative: CMD = run_with_patched_temp \
    "$(MOV_STSD_V2_GEN)" "$(MOV_STSD_V2_PROBE)" mov \
    489 "\\0277\\0360\\0000\\0000\\0000\\0000\\0000\\0000" ; test $$? -ne 0
fate-mov-stsd-v2-sample-rate-negative: CMP = grep
fate-mov-stsd-v2-sample-rate-negative: REF = Invalid sample rate -1

# Zero must stay accepted: mov_finalize_stsd_codec() then falls back to the
# media timescale, which is 96000 here.
fate-mov-stsd-v2-sample-rate-zero: CMD = run_with_patched_temp \
    "$(MOV_STSD_V2_GEN)" "$(MOV_STSD_V2_PROBE)" mov \
    489 "\\0000\\0000\\0000\\0000\\0000\\0000\\0000\\0000"

# Create VFR B-frames whose presentation durations are not a permutation of
# the STTS sample deltas.
tests/data/mov-vfr-bframes-derived-duration.mov: TAG = GEN
tests/data/mov-vfr-bframes-derived-duration.mov: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin -v error \
	    -filter_complex "color=c=black:s=2x2:r=1,setpts=N+N*N" \
	    -frames:v 3 -fps_mode vfr -c:v mpeg4 -bf 2 -q:v 2 -threads 1 \
	    -flags +bitexact -fflags +bitexact \
	    -f mov $(TARGET_PATH)/$@ -y

fate-mov-vfr-bframes-derived-duration: tests/data/mov-vfr-bframes-derived-duration.mov
fate-mov-vfr-bframes-derived-duration: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_packets -show_entries packet=pts,dts,duration -print_format compact -select_streams v -v 0 $(TARGET_PATH)/tests/data/mov-vfr-bframes-derived-duration.mov

FATE_MOV_FFMPEG_FFPROBE-$(call TRANSCODE, FLAC, MP4 MOV, WAV_DEMUXER PCM_S16LE_DECODER IAMFENC IAMFDEC) += fate-mov-mp4-iamf-stereo
fate-mov-mp4-iamf-stereo: tests/data/asynth-44100-2.wav tests/data/streamgroups/audio_element-stereo tests/data/streamgroups/mix_presentation-stereo
fate-mov-mp4-iamf-stereo: SRC = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
fate-mov-mp4-iamf-stereo: CMD = transcode wav $(SRC) mp4 " \
  -/stream_group $(TARGET_PATH)/tests/data/streamgroups/audio_element-stereo \
  -/stream_group $(TARGET_PATH)/tests/data/streamgroups/mix_presentation-stereo \
  -streamid 0:0 -c:a flac -t 1" "-c:a copy -map 0" \
  "-show_entries stream_group=index,id,nb_streams,type:stream_group_components:stream_group_disposition:stream_group_tags:stream_group_stream=index,id:stream_group_stream_disposition"

# Two audio elements and a mix presentation whose id equals the id of the first audio element.
FATE_MOV_FFMPEG_FFPROBE-$(call TRANSCODE, FLAC, MP4 MOV, WAV_DEMUXER PCM_S16LE_DECODER IAMFENC IAMFDEC) += fate-mov-mp4-iamf-stereo-id-collision
fate-mov-mp4-iamf-stereo-id-collision: tests/data/asynth-44100-2.wav tests/data/streamgroups/audio_element-stereo tests/data/streamgroups/audio_element-stereo-2 tests/data/streamgroups/mix_presentation-stereo-id-1
fate-mov-mp4-iamf-stereo-id-collision: SRC = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
fate-mov-mp4-iamf-stereo-id-collision: CMD = transcode wav $(SRC) mp4 " \
  -/stream_group $(TARGET_PATH)/tests/data/streamgroups/audio_element-stereo \
  -/stream_group $(TARGET_PATH)/tests/data/streamgroups/audio_element-stereo-2 \
  -/stream_group $(TARGET_PATH)/tests/data/streamgroups/mix_presentation-stereo-id-1 \
  -map 0:a -map 0:a -streamid 0:0 -streamid 1:1 -c:a flac -t 1" "-c:a copy -map 0" \
  "-show_entries stream_group=index,id,nb_streams,type:stream_group_stream=index,id:stream=index,id,duration_ts"

FATE_MOV_FFMPEG_FFPROBE-$(call TRANSCODE, FLAC, MP4 MOV, WAV_DEMUXER PCM_S16LE_DECODER ARESAMPLE_FILTER CHANNELMAP_FILTER IAMFENC IAMFDEC) += fate-mov-mp4-iamf-5_1_4
fate-mov-mp4-iamf-5_1_4: tests/data/asynth-44100-10.wav tests/data/filtergraphs/iamf_5_1_4 tests/data/streamgroups/audio_element-5_1_4 tests/data/streamgroups/mix_presentation-5_1_4
fate-mov-mp4-iamf-5_1_4: SRC = $(TARGET_PATH)/tests/data/asynth-44100-10.wav
fate-mov-mp4-iamf-5_1_4: CMD = transcode wav $(SRC) mp4 "-auto_conversion_filters \
  -/filter_complex $(TARGET_PATH)/tests/data/filtergraphs/iamf_5_1_4 \
  -/stream_group $(TARGET_PATH)/tests/data/streamgroups/audio_element-5_1_4 \
  -/stream_group $(TARGET_PATH)/tests/data/streamgroups/mix_presentation-5_1_4 \
  -streamid 0:0 -streamid 1:1 -streamid 2:2 -streamid 3:3 -streamid 4:4 -streamid 5:5 -map [FRONT] -map [SIDE] -map [CENTER] -map [LFE] -map [TOP_FRONT] -map [TOP_BACK] -c:a flac -t 1" "-c:a copy -map 0" \
  "-show_entries stream_group=index,id,nb_streams,type:stream_group_components:stream_group_disposition:stream_group_tags:stream_group_stream=index,id:stream_group_stream_disposition"

# Test muxing an IAMF track alongside a video one, with video as the first track.
tests/data/mov-mp4-iamf-7_1_4-video-first.mp4: TAG = GEN
tests/data/mov-mp4-iamf-7_1_4-video-first.mp4: tests/data/asynth-44100-12.wav tests/data/vsynth1.yuv tests/data/filtergraphs/iamf_7_1_4 tests/data/streamgroups/audio_element-7_1_4-2 tests/data/streamgroups/mix_presentation-7_1_4
tests/data/mov-mp4-iamf-7_1_4-video-first.mp4: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
  -f wav -i $(TARGET_PATH)/tests/data/asynth-44100-12.wav \
  -f rawvideo -s 352x288 -pix_fmt yuv420p -flags +bitexact -sws_flags +accurate_rnd+bitexact -i $(TARGET_PATH)/tests/data/vsynth1.yuv \
  -/filter_complex $(TARGET_PATH)/tests/data/filtergraphs/iamf_7_1_4 \
  -/stream_group $(TARGET_PATH)/tests/data/streamgroups/audio_element-7_1_4-2 \
  -/stream_group $(TARGET_PATH)/tests/data/streamgroups/mix_presentation-7_1_4 \
  -streamid 0:1 -streamid 1:2 -streamid 2:3 -streamid 3:4 -streamid 4:5 -streamid 5:6 -streamid 6:7 -streamid 7:8 -map 1:v:0 -map "[FRONT]" -map "[TOP_FRONT]" -map "[CENTER]" -map "[LFE]" -map "[SIDE]" -map "[BACK]" -map "[TOP_BACK]" \
  -flags +bitexact -c:a flac -idct simple -dct fastint -sws_flags +accurate_rnd+bitexact -flags +bitexact -threads 1 -c:v mpeg4 -t 1 -fflags +bitexact -f mp4 $(TARGET_PATH)/tests/data/mov-mp4-iamf-7_1_4-video-first.mp4 -y 2>/dev/null

# Demux everything
FATE_MOV_FFMPEG_FFPROBE-$(call TRANSCODE, MPEG4, MP4 MOV, WAV_DEMUXER FLAC_ENCODER FLAC_DECODER RAWVIDEO_DEMUXER RAWVIDEO_DECODER PCM_S16LE_DECODER ARESAMPLE_FILTER CHANNELMAP_FILTER IAMFENC IAMFDEC) += fate-mov-mp4-iamf-7_1_4-video-first
fate-mov-mp4-iamf-7_1_4-video-first: tests/data/mov-mp4-iamf-7_1_4-video-first.mp4
fate-mov-mp4-iamf-7_1_4-video-first: CMD = stream_demux mp4 $(TARGET_PATH)/tests/data/mov-mp4-iamf-7_1_4-video-first.mp4 \
  "" "-c:a copy -c:v copy -map 0" \
  "-show_entries stream_group=index,id,nb_streams,type:stream_group_components:stream_group_disposition:stream_group_tags:stream_group_stream=index,id:stream_group_stream_disposition:stream=index,id"

# Demux the first stream in the IAMF struct
FATE_MOV_FFMPEG-$(call TRANSCODE, MPEG4, MP4 MOV, WAV_DEMUXER FLAC_ENCODER FLAC_DECODER RAWVIDEO_DEMUXER RAWVIDEO_DECODER PCM_S16LE_DECODER ARESAMPLE_FILTER CHANNELMAP_FILTER IAMFENC IAMFDEC) += fate-mov-mp4-iamf-7_1_4-video-first-2
fate-mov-mp4-iamf-7_1_4-video-first-2: tests/data/mov-mp4-iamf-7_1_4-video-first.mp4
fate-mov-mp4-iamf-7_1_4-video-first-2: CMD = stream_demux mp4 $(TARGET_PATH)/tests/data/mov-mp4-iamf-7_1_4-video-first.mp4 \
  "" "-c:a copy -map 0:g:0:0" \

# Demux the fourth stream in the IAMF struct. See 16ee3d8d99a2
FATE_MOV_FFMPEG-$(call TRANSCODE, MPEG4, MP4 MOV, WAV_DEMUXER FLAC_ENCODER FLAC_DECODER RAWVIDEO_DEMUXER RAWVIDEO_DECODER PCM_S16LE_DECODER ARESAMPLE_FILTER CHANNELMAP_FILTER IAMFENC IAMFDEC) += fate-mov-mp4-iamf-7_1_4-video-first-3
fate-mov-mp4-iamf-7_1_4-video-first-3: tests/data/mov-mp4-iamf-7_1_4-video-first.mp4
fate-mov-mp4-iamf-7_1_4-video-first-3: CMD = stream_demux mp4 $(TARGET_PATH)/tests/data/mov-mp4-iamf-7_1_4-video-first.mp4 \
  "" "-c:a copy -map 0:g:0:3" \

# Test muxing an IAMF track alongside a video one, with video as the last track. Also, use stream ids as track ids.
FATE_MOV_FFMPEG_FFPROBE-$(call TRANSCODE, MPEG4, MP4 MOV, WAV_DEMUXER FLAC_ENCODER FLAC_DECODER RAWVIDEO_DEMUXER RAWVIDEO_DECODER PCM_S16LE_DECODER ARESAMPLE_FILTER CHANNELMAP_FILTER IAMFENC IAMFDEC) += fate-mov-mp4-iamf-7_1_4-video-last
fate-mov-mp4-iamf-7_1_4-video-last: tests/data/asynth-44100-12.wav tests/data/vsynth1.yuv tests/data/filtergraphs/iamf_7_1_4 tests/data/streamgroups/audio_element-7_1_4 tests/data/streamgroups/mix_presentation-7_1_4
fate-mov-mp4-iamf-7_1_4-video-last: SRC = $(TARGET_PATH)/tests/data/asynth-44100-12.wav
fate-mov-mp4-iamf-7_1_4-video-last: SRC2 = $(TARGET_PATH)/tests/data/vsynth1.yuv
fate-mov-mp4-iamf-7_1_4-video-last: CMD = transcode wav $(SRC) mp4 "-auto_conversion_filters \
  -/filter_complex $(TARGET_PATH)/tests/data/filtergraphs/iamf_7_1_4 \
  -/stream_group $(TARGET_PATH)/tests/data/streamgroups/audio_element-7_1_4 \
  -/stream_group $(TARGET_PATH)/tests/data/streamgroups/mix_presentation-7_1_4 \
  -streamid 0:1 -streamid 1:2 -streamid 2:3 -streamid 3:4 -streamid 4:5 -streamid 5:6 -streamid 6:7 -streamid 7:8 -map [FRONT] -map [TOP_FRONT] -map [CENTER] -map [LFE] -map [SIDE] -map [BACK] -map [TOP_BACK] -map 1:v:0 -use_stream_ids_as_track_ids true -c:a flac -c:v mpeg4 -t 1" "-c:a copy -c:v copy -map 0" \
  "-show_entries stream_group=index,id,nb_streams,type:stream_group_components:stream_group_disposition:stream_group_tags:stream_group_stream=index,id:stream_group_stream_disposition:stream=index,id" \
  "-f rawvideo -s 352x288 -pix_fmt yuv420p -i $(SRC2)"

FATE_MOV_FFMPEG_FFPROBE-$(call TRANSCODE, FLAC, MP4 MOV, WAV_DEMUXER PCM_S16LE_DECODER ARESAMPLE_FILTER CHANNELMAP_FILTER IAMFENC IAMFDEC) += fate-mov-mp4-iamf-ambisonic_1
fate-mov-mp4-iamf-ambisonic_1: tests/data/asynth-44100-4.wav tests/data/filtergraphs/iamf_ambisonic_1 tests/data/streamgroups/audio_element-ambisonic_1 tests/data/streamgroups/mix_presentation-ambisonic_1
fate-mov-mp4-iamf-ambisonic_1: SRC = $(TARGET_PATH)/tests/data/asynth-44100-4.wav
fate-mov-mp4-iamf-ambisonic_1: CMD = transcode wav $(SRC) mp4 "-auto_conversion_filters \
  -/filter_complex $(TARGET_PATH)/tests/data/filtergraphs/iamf_ambisonic_1 \
  -/stream_group $(TARGET_PATH)/tests/data/streamgroups/audio_element-ambisonic_1 \
  -/stream_group $(TARGET_PATH)/tests/data/streamgroups/mix_presentation-ambisonic_1 \
  -streamid 0:0 -streamid 1:1 -streamid 2:2 -streamid 3:3 -map [MONO0] -map [MONO1] -map [MONO2] -map [MONO3] -c:a flac -t 1" "-c:a copy -map 0" \
  "-show_entries stream_group=index,id,nb_streams,type:stream_group_components:stream_group_disposition:stream_group_tags:stream_group_stream=index,id:stream_group_stream_disposition"

# Encode-path injection of -stereo3d into MP4 st3d (requires unofficial).
FATE_MOV_FFMPEG_FFPROBE-$(call ALLYES, FILE_PROTOCOL PIPE_PROTOCOL FRAMECRC_MUXER \
                                             RAWVIDEO_DEMUXER RAWVIDEO_DECODER \
                                             MPEG4_ENCODER MPEG4_DECODER MP4_MUXER MOV_DEMUXER) \
                          += fate-mov-stereo3d-sbs-mp4
fate-mov-stereo3d-sbs-mp4: tests/data/vsynth1.yuv
fate-mov-stereo3d-sbs-mp4: CMD = transcode rawvideo $(TARGET_PATH)/tests/data/vsynth1.yuv mp4 \
  "-c:v mpeg4 -qscale:v 10 -stereo3d:v sbsl -strict unofficial -frames:v 2" \
  "-c:v copy" "-show_entries stream_side_data_list" "" "" "-s 352x288 -pix_fmt yuv420p"

# chpl titles are limited to 255 bytes; truncation must not split a UTF-8
# character. 3gp is used because MP4/MOV read the chapter track, not chpl.
tests/data/chpl-utf8.ffmeta: TAG = COPY
tests/data/chpl-utf8.ffmeta: $(SRC_PATH)/tests/chpl-utf8.ffmeta | tests/data
	$(M)cp $< $@
fate-mov-chpl-utf8: tests/data/chpl-utf8.ffmeta
FATE_MOV_FFMPEG_FFPROBE-$(call ALLYES, COLOR_FILTER MPEG4_ENCODER FFMETADATA_DEMUXER TGP_MUXER MOV_DEMUXER FILE_PROTOCOL) += fate-mov-chpl-utf8
fate-mov-chpl-utf8: CMD = run_with_temp "$(FFMPEG) -nostdin -v error -filter_complex color=c=black:s=2x2:r=1 -f ffmetadata -i $(TARGET_PATH)/tests/data/chpl-utf8.ffmeta -map_chapters 0 -frames:v 1 -c:v mpeg4 -threads 1 -flags +bitexact -fflags +bitexact -f 3gp -y" "ffprobe$(PROGSSUF)$(EXESUF) -loglevel error -bitexact -show_chapters" 3gp

FATE_FFMPEG += $(FATE_MOV_FFMPEG-yes)
FATE_FFMPEG_FFPROBE += $(FATE_MOV_FFMPEG_FFPROBE-yes)

fate-mov: $(FATE_MOV-yes) $(FATE_MOV_REMUX-yes) $(FATE_MOV_FFMPEG-yes) $(FATE_MOV_FFMPEG_FFPROBE-yes) $(FATE_MOV_FFPROBE-yes) $(FATE_MOV_FASTSTART) $(FATE_MOV_FFMPEG_SAMPLES-yes) $(FATE_MOV_FFMPEG_FFPROBE_SAMPLES-yes)
