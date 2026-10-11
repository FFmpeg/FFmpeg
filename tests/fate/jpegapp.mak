# jpegapp bitstream filter: per-packet injection of APP segments from a
# u32be-length-prefixed sidecar. The sidecars below are written by printf;
# each record is "u32be length, FF En marker, u16be segment length, payload"
# and injects a segment carrying the payload FATE.

FATE_JPEGAPP-$(call ALLYES, SMJPEG_DEMUXER JPEGAPP_BSF MJPEG_MUXER FILE_PROTOCOL) += fate-bsf-jpegapp
fate-bsf-jpegapp: CMD = \
    cleanfiles="$${cleanfiles} $${outfile}.bin"; \
    printf "\\000\\000\\000\\010\\377\\341\\000\\006FATE\\000\\000\\000\\000\\000\\000\\000\\010\\377\\340\\000\\006FATE" > "$${outfile}.bin"; \
    md5 -i $(TARGET_SAMPLES)/smjpeg/scenwin.mjpg -map 0:v:0 -frames:v 3 -c:v copy \
        -bsf:v "jpegapp=filename=$$(target_path "$${outfile}.bin" | sed "s/:/\\\\\\\\:/g")" -fflags +bitexact -f mjpeg

# One record for three packets: the second packet must fail instead of
# silently passing through without its metadata.
FATE_JPEGAPP-$(call ALLYES, SMJPEG_DEMUXER JPEGAPP_BSF MJPEG_MUXER FILE_PROTOCOL) += fate-bsf-jpegapp-missing
fate-bsf-jpegapp-missing: CMP = grep
fate-bsf-jpegapp-missing: REF = jpegapp: no sidecar record for packet 1
fate-bsf-jpegapp-missing: CMD = \
    cleanfiles="$${cleanfiles} $${outfile}.bin $${outfile}.mjpeg"; \
    printf "\\000\\000\\000\\000" > "$${outfile}.bin"; \
    ffmpeg -xerror -i $(TARGET_SAMPLES)/smjpeg/scenwin.mjpg -map 0:v:0 -frames:v 3 -c:v copy \
        -bsf:v "jpegapp=filename=$$(target_path "$${outfile}.bin" | sed "s/:/\\\\\\\\:/g")" \
        -fflags +bitexact -f mjpeg -y "$$(target_path "$${outfile}.mjpeg")" ; \
    test $$? -ne 0

# A record longer than the 16 MiB limit must be rejected.
FATE_JPEGAPP-$(call ALLYES, SMJPEG_DEMUXER JPEGAPP_BSF MJPEG_MUXER FILE_PROTOCOL) += fate-bsf-jpegapp-oversize
fate-bsf-jpegapp-oversize: CMP = grep
fate-bsf-jpegapp-oversize: REF = jpegapp: record length 16777217 too large
fate-bsf-jpegapp-oversize: CMD = \
    cleanfiles="$${cleanfiles} $${outfile}.bin $${outfile}.mjpeg"; \
    printf "\\001\\000\\000\\001\\377\\341\\000\\006FATE" > "$${outfile}.bin"; \
    ffmpeg -xerror -i $(TARGET_SAMPLES)/smjpeg/scenwin.mjpg -map 0:v:0 -frames:v 3 -c:v copy \
        -bsf:v "jpegapp=filename=$$(target_path "$${outfile}.bin" | sed "s/:/\\\\\\\\:/g")" \
        -fflags +bitexact -f mjpeg -y "$$(target_path "$${outfile}.mjpeg")" ; \
    test $$? -ne 0

# Without -xerror, subsequent packets must still fail after an oversized
# record. Only the first (passthrough) packet may reach the output; the bytes
# after the oversized prefix would form valid records if reading resumed.
FATE_JPEGAPP-$(call ALLYES, SMJPEG_DEMUXER JPEGAPP_BSF MJPEG_MUXER FILE_PROTOCOL) += fate-bsf-jpegapp-oversize-continuation
fate-bsf-jpegapp-oversize-continuation: CMD = \
    cleanfiles="$${cleanfiles} $${outfile}.bin"; \
    printf "\\000\\000\\000\\000\\001\\000\\000\\001\\000\\000\\000\\010\\377\\341\\000\\006FATE\\000\\000\\000\\000" > "$${outfile}.bin"; \
    md5 -i $(TARGET_SAMPLES)/smjpeg/scenwin.mjpg -map 0:v:0 -frames:v 4 -c:v copy \
        -bsf:v "jpegapp=filename=$$(target_path "$${outfile}.bin" | sed "s/:/\\\\\\\\:/g")" -fflags +bitexact -f mjpeg

FATE_SAMPLES_FFMPEG += $(FATE_JPEGAPP-yes)
