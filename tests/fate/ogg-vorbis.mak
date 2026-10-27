FATE_OGG_VORBIS += fate-ogg-vorbis-chained-meta
fate-ogg-vorbis-chained-meta: REF = $(SRC_PATH)/tests/ref/fate/ogg-vorbis-chained-meta.txt
fate-ogg-vorbis-chained-meta: CMD = run $(APITESTSDIR)/api-dump-stream-meta-test$(EXESUF) $(TARGET_SAMPLES)/ogg-vorbis/chained-meta.ogg

FATE_OGG_COPY_VORBIS-$(call DEMDEC, OGG, VORBIS, OGG_MUXER) += fate-ogg-vorbis-copy-chained-meta
fate-ogg-vorbis-copy-chained-meta: $(APITESTSDIR)/api-dump-stream-meta-test$(EXESUF)
fate-ogg-vorbis-copy-chained-meta: REF = $(SRC_PATH)/tests/ref/fate/ogg-vorbis-chained-meta.txt
fate-ogg-vorbis-copy-chained-meta: CMD = run_with_temp "$(FFMPEG) -nostdin -hide_banner -loglevel quiet -i $(TARGET_SAMPLES)/ogg-vorbis/chained-meta.ogg -c copy -f ogg -y" "$(APITESTSDIR)/api-dump-stream-meta-test$(EXESUF)" ogg

# Chain two streams cut from the same sample, the second one with a comment
# header larger than 64 KiB (as with embedded cover art).
FATE_OGG_COPY_VORBIS-$(call FRAMECRC, OGG FFMETADATA, VORBIS, OGG_MUXER CONCAT_PROTOCOL) += fate-ogg-vorbis-chained-large-comment
fate-ogg-vorbis-chained-large-comment: CMD = \
    cleanfiles="$${cleanfiles} $${outfile}.txt $${outfile}.1.ogg $${outfile}.2.ogg"; \
    printf ";FFMETADATA1\ncomment=%070000d\n" 0 > "$${outfile}.txt" && \
    ffmpeg -i $(TARGET_SAMPLES)/vorbis/moog_small.ogg -c copy -t 0.5 -fflags +bitexact \
        -y "$$(target_path "$${outfile}.1.ogg")" && \
    ffmpeg -i $(TARGET_SAMPLES)/vorbis/moog_small.ogg -i "$$(target_path "$${outfile}.txt")" \
        -map 0 -map_metadata 1 -c copy -t 0.5 -fflags +bitexact -serial_offset 1 \
        -y "$$(target_path "$${outfile}.2.ogg")" && \
    framecrc -i "concat:$$(target_path "$${outfile}.1.ogg")|$$(target_path "$${outfile}.2.ogg")" -c copy

FATE_OGG_VORBIS_PROBE-$(call DEMDEC, OGG, VORBIS) += fate-ogg-vorbis-initial-padding-chained
fate-ogg-vorbis-initial-padding-chained: CMD = ffprobe_demux -of default -show_entries stream=codec_name,initial_padding $(TARGET_SAMPLES)/ogg-vorbis/chained-meta.ogg

# Regression test for encoder delay (ts_offset): pts must stay in sync with accumulated nb_samples across page boundaries
FATE_OGG_VORBIS_PROBE-$(call DEMDEC, OGG, VORBIS) += fate-ogg-vorbis-ts-offset
fate-ogg-vorbis-ts-offset: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_frames -of default $(TARGET_SAMPLES)/ogg-vorbis/tos.ogg 2>/dev/null | awk -F= "/nb_samples=/ {predicted_pts += \$$2} /pts=/ {pts = \$$2; printf \"%d-%d=%d\\n\", predicted_pts, pts, predicted_pts-pts}"

FATE_OGG_VORBIS_PROBE-$(call DEMDEC, OGG, VORBIS) += fate-ogg-vorbis-ts-seek
fate-ogg-vorbis-ts-seek: CMD = ffprobe_demux -of default -bitexact -select_streams a -read_intervals 2.5%+\#5 -show_packets -show_entries packet=pts,dts,duration,flags -show_entries stream=codec_name,initial_padding $(TARGET_SAMPLES)/ogg-vorbis/tos.ogg

FATE_OGG_VORBIS_PROBE-$(call DEMDEC, OGG, VORBIS) += fate-ogg-vorbis-ts-seek-1.5s
fate-ogg-vorbis-ts-seek-1.5s: CMD = ffprobe_demux -of default -bitexact -select_streams a -read_intervals 1.5%+\#5 -show_packets -show_entries packet=pts,dts,duration,flags -show_entries stream=codec_name,initial_padding $(TARGET_SAMPLES)/ogg-vorbis/tos.ogg

# Regression test for files with granule=0 on audio pages
# See https://trac.ffmpeg.org/ticket/3710
FATE_OGG_VORBIS_PROBE-$(call DEMDEC, OGG, VORBIS) += fate-ogg-vorbis-bear
fate-ogg-vorbis-bear: CMD = ffprobe_demux -of default -bitexact -select_streams a -read_intervals %+\#10 -show_packets -show_entries packet=pts,dts,duration,flags -show_entries stream=codec_name,initial_padding $(TARGET_SAMPLES)/ogg/bear.ogv

FATE_OGG_VORBIS_ENCODE_PROBE-$(call ENCDEC, LIBVORBIS VORBIS, OGG, LAVFI_INDEV SINE_FILTER) += fate-ogg-vorbis-libvorbis-1s
fate-ogg-vorbis-libvorbis-1s: CMD = run_with_temp "$(FFMPEG) -nostdin -hide_banner -loglevel quiet -f lavfi -i sine=frequency=440:duration=1:sample_rate=44100 -c:a libvorbis -bitexact -f ogg -y" "ffprobe$(PROGSSUF)$(EXESUF) -print_filename $$test -of default -bitexact -threads 1 -read_intervals %+\#5 -show_entries stream=codec_name,initial_padding:packet=pts,pts_time,dts,dts_time,duration,duration_time,flags:packet_side_data_list:format=filename,nb_streams,nb_programs,nb_stream_groups,format_name,start_time,duration,probe_score" ogg

FATE_OGG_VORBIS_ENCODE_PROBE-$(call ENCDEC, LIBVORBIS VORBIS, OGG, LAVFI_INDEV SINE_FILTER) += fate-ogg-vorbis-libvorbis-30s
fate-ogg-vorbis-libvorbis-30s: CMD = run_with_temp "$(FFMPEG) -nostdin -hide_banner -loglevel quiet -f lavfi -i sine=frequency=440:duration=30:sample_rate=44100 -c:a libvorbis -bitexact -f ogg -y" "ffprobe$(PROGSSUF)$(EXESUF) -print_filename $$test -of default -bitexact -threads 1 -read_intervals %+\#5 -show_entries stream=codec_name,initial_padding:packet=pts,pts_time,dts,dts_time,duration,duration_time,flags:packet_side_data_list:format=filename,nb_streams,nb_programs,nb_stream_groups,format_name,start_time,duration,probe_score" ogg

FATE_OGG_VORBIS-$(call DEMDEC, OGG, VORBIS) += $(FATE_OGG_VORBIS)

FATE_SAMPLES_DUMP_STREAM_META += $(FATE_OGG_VORBIS-yes)

FATE_EXTERN += $(FATE_OGG_VORBIS-yes)

FATE_SAMPLES_FFMPEG += $(FATE_OGG_COPY_VORBIS-yes)

FATE_SAMPLES_FFPROBE += $(FATE_OGG_VORBIS_PROBE-yes)

FATE_FFMPEG_FFPROBE += $(FATE_OGG_VORBIS_ENCODE_PROBE-yes)

fate-ogg-vorbis: $(FATE_OGG_VORBIS-yes) $(FATE_OGG_COPY_VORBIS-yes) $(FATE_OGG_VORBIS_PROBE-yes) $(FATE_OGG_VORBIS_ENCODE_PROBE-yes)
