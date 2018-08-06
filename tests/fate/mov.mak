FATE_MOV = fate-mov-3elist \
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
           fate-mov-440hz-10ms \
           fate-mov-ibi-elst-starts-b \
           fate-mov-elst-ends-betn-b-and-i \
           fate-mov-frag-overlap \
           fate-mov-bbi-elst-starts-b \
           fate-mov-neg-firstpts-discard-frames \
           fate-mov-stream-shorter-than-movie \

FATE_MOV_FFPROBE = fate-mov-neg-firstpts-discard \
                   fate-mov-neg-firstpts-discard-vorbis \
                   fate-mov-aac-2048-priming \
                   fate-mov-zombie \
                   fate-mov-init-nonkeyframe \
                   fate-mov-displaymatrix \
                   fate-mov-spherical-mono \
                   fate-mov-guess-delay-1 \
                   fate-mov-guess-delay-2 \
                   fate-mov-guess-delay-3 \

FATE_MOV_FASTSTART = fate-mov-faststart-4gb-overflow \

FATE_SAMPLES_AVCONV += $(FATE_MOV)
FATE_SAMPLES_FFPROBE += $(FATE_MOV_FFPROBE)
FATE_SAMPLES_FASTSTART += $(FATE_MOV_FASTSTART)

fate-mov: $(FATE_MOV) $(FATE_MOV_FFPROBE) $(FATE_MOV_FASTSTART)

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
fate-mov-440hz-10ms: CMD = framemd5 -i $(TARGET_SAMPLES)/mov/440hz-10ms.m4a

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

# Makes sure that we pick the right frames according to edit list when there is no keyframe with PTS < edit list start.
# For example, when video starts on a B-frame, and edit list starts on that B-frame too.
# GOP structure : B B I in presentation order.
fate-mov-bbi-elst-starts-b: CMD = framemd5 -flags +bitexact -acodec aac_fixed -i $(TARGET_SAMPLES)/h264/twofields_packet.mp4

# Makes sure that the stream start_time is not negative when the first packet is a DISCARD packet with negative timestamp.
fate-mov-neg-firstpts-discard: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_entries stream=start_time -bitexact $(TARGET_SAMPLES)/mov/mov_neg_first_pts_discard.mov

# Makes sure that the VORBIS audio stream start_time is not negative when the first few packets are DISCARD packets
# with negative timestamps (skip_samples is not set for Vorbis, so ffmpeg computes start_time as negative if not specified by demuxer).
fate-mov-neg-firstpts-discard-vorbis: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_entries stream=start_time -bitexact $(TARGET_SAMPLES)/mov/mov_neg_first_pts_discard_vorbis.mp4

# Makes sure that expected frames are generated for mov_neg_first_pts_discard.mov with -vsync 1
fate-mov-neg-firstpts-discard-frames: CMD = framemd5 -flags +bitexact -i $(TARGET_SAMPLES)/mov/mov_neg_first_pts_discard.mov -vsync 1

# Makes sure that no frame is dropped/duplicated with fps filter due to start_time / duration miscalculations.
fate-mov-stream-shorter-than-movie: CMD = framemd5 -flags +bitexact -i $(TARGET_SAMPLES)/mov/mov_stream_shorter_than_movie.mov -vf fps=fps=24 -an

fate-mov-aac-2048-priming: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_packets -print_format compact $(TARGET_SAMPLES)/mov/aac-2048-priming.mov

fate-mov-zombie: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_streams -show_packets -show_frames -bitexact -print_format compact $(TARGET_SAMPLES)/mov/white_zombie_scrunch-part.mov

fate-mov-init-nonkeyframe: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_packets -print_format compact -select_streams v $(TARGET_SAMPLES)/mov/mp4-init-nonkeyframe.mp4

fate-mov-displaymatrix: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_entries stream=display_aspect_ratio,sample_aspect_ratio:stream_side_data_list -select_streams v -v 0 $(TARGET_SAMPLES)/mov/displaymatrix.mov

fate-mov-spherical-mono: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_entries stream_side_data_list -select_streams v -v 0 $(TARGET_SAMPLES)/mov/spherical.mov

fate-mov-gpmf-remux: CMD = md5 -i $(TARGET_SAMPLES)/mov/fake-gp-media-with-real-gpmf.mp4 -map 0 -c copy -fflags +bitexact -f mp4
fate-mov-gpmf-remux: CMP = oneline
fate-mov-gpmf-remux: REF = 8f48e435ee1f6b7e173ea756141eabf3

fate-mov-guess-delay-1: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_entries stream=has_b_frames -select_streams v $(TARGET_SAMPLES)/h264/h264_3bf_nopyramid_nobsrestriction.mp4
fate-mov-guess-delay-2: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_entries stream=has_b_frames -select_streams v $(TARGET_SAMPLES)/h264/h264_3bf_pyramid_nobsrestriction.mp4
fate-mov-guess-delay-3: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_entries stream=has_b_frames -select_streams v $(TARGET_SAMPLES)/h264/h264_4bf_pyramid_nobsrestriction.mp4

fate-mov-faststart-4gb-overflow: CMD = run tools/qt-faststart$(EXESUF) $(TARGET_SAMPLES)/mov/faststart-4gb-overflow.mov faststart-4gb-overflow-output.mov > /dev/null ; md5sum faststart-4gb-overflow-output.mov | cut -d " " -f1 ; rm faststart-4gb-overflow-output.mov
fate-mov-faststart-4gb-overflow: CMP = oneline
fate-mov-faststart-4gb-overflow: REF = bc875921f151871e787c4b4023269b29
