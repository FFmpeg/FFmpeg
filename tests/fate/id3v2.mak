FATE_ID3V2_FFPROBE-$(CONFIG_MP3_DEMUXER) += fate-id3v2-priv
fate-id3v2-priv: CMD = probetags $(TARGET_SAMPLES)/id3v2/id3v2_priv.mp3

FATE_ID3V2_FFMPEG-$(CONFIG_MP3_DEMUXER) += fate-id3v2-invalid-tags
fate-id3v2-invalid-tags: CMD = run $(FFMPEG) -nostdin -hide_banner -i $(TARGET_SAMPLES)/id3v2/invalid-tags.mp3 -f null - || true
fate-id3v2-invalid-tags: CMP = null

FATE_ID3V2_FFMPEG-$(CONFIG_MP3_DEMUXER) += fate-id3v2-keep-metadata-invalid-type
fate-id3v2-keep-metadata-invalid-type: CMD = run $(FFMPEG) -nostdin -hide_banner -i $(TARGET_SAMPLES)/gapless/gapless-itunes.mp3 -c copy -keep_metadata:x vendor_id -f null - ; true
fate-id3v2-keep-metadata-invalid-type: CMP = grep
fate-id3v2-keep-metadata-invalid-type: REF = Invalid metadata type x

FATE_ID3V2_FFMPEG-$(CONFIG_MP3_DEMUXER) += fate-id3v2-keep-metadata-invalid-stream-spec
fate-id3v2-keep-metadata-invalid-stream-spec: CMD = run $(FFMPEG) -nostdin -hide_banner -i $(TARGET_SAMPLES)/gapless/gapless-itunes.mp3 -c copy -keep_metadata:s:bogus vendor_id -f null - ; true
fate-id3v2-keep-metadata-invalid-stream-spec: CMP = grep
fate-id3v2-keep-metadata-invalid-stream-spec: REF = Trailing garbage at the end of a stream specifier: bogus

FATE_ID3V2_FFMPEG_FFPROBE-$(call REMUX, MP3) += fate-id3v2-priv-remux
fate-id3v2-priv-remux: CMD = transcode mp3 $(TARGET_SAMPLES)/id3v2/id3v2_priv.mp3 mp3 "-c copy" "-c copy -t 0.1" "-show_entries format_tags"

ID3V2_TESTBIN = libavformat/tests/id3v2$(EXESUF)

FATE_ID3V2_RAW-$(call REMUX, MP3) += fate-id3v2-comm
fate-id3v2-comm: $(ID3V2_TESTBIN)
fate-id3v2-comm: CMD = run_with_temp "$(FFMPEG) -nostdin -hide_banner -loglevel error -i $(TARGET_SAMPLES)/id3v2/id3v2_priv.mp3 -map_metadata -1 -c copy -fflags +bitexact -metadata comment=test -metadata comment-eng=test2 -f mp3 -y" "$(ID3V2_TESTBIN)" mp3

FATE_ID3V2_RAW-$(call ALLYES, MP3_DEMUXER, FILE_PROTOCOL) += fate-id3v2-lang-xxx
fate-id3v2-lang-xxx: $(ID3V2_TESTBIN)
fate-id3v2-lang-xxx: CMD = run "$(ID3V2_TESTBIN)" "$(TARGET_SAMPLES)/id3v2/lang_xxx.mp3"

FATE_ID3V2_RAW-$(call REMUX, MP3) += fate-id3v2-lang-xxx-remux
fate-id3v2-lang-xxx-remux: $(ID3V2_TESTBIN)
fate-id3v2-lang-xxx-remux: CMD = run_with_temp "$(FFMPEG) -nostdin -hide_banner -loglevel error -i $(TARGET_SAMPLES)/id3v2/lang_xxx.mp3 -c copy -fflags +bitexact -f mp3 -y" "$(ID3V2_TESTBIN)" mp3

FATE_ID3V2_RAW-$(call REMUX, MP3) += fate-id3v2-lang-und
fate-id3v2-lang-und: $(ID3V2_TESTBIN)
fate-id3v2-lang-und: CMD = run_with_temp "$(FFMPEG) -nostdin -hide_banner -loglevel error -i $(TARGET_SAMPLES)/id3v2/id3v2_priv.mp3 -map_metadata -1 -c copy -fflags +bitexact -id3v2_version 3 -metadata lyrics-und=test -metadata comment-und=test2 -f mp3 -y" "$(ID3V2_TESTBIN)" mp3

FATE_ID3V2_RAW-$(call REMUX, MP3) += fate-id3v2-lyrics
fate-id3v2-lyrics: $(ID3V2_TESTBIN)
fate-id3v2-lyrics: CMD = run_with_temp "$(FFMPEG) -nostdin -hide_banner -loglevel error -i $(TARGET_SAMPLES)/id3v2/id3v2_priv.mp3 -map_metadata -1 -c copy -fflags +bitexact -id3v2_version 3 -metadata lyrics=test -metadata lyrics-fra=test2 -f mp3 -y" "$(ID3V2_TESTBIN)" mp3

FATE_ID3V2_RAW-$(call REMUX, MP3) += fate-id3v2-txxx
fate-id3v2-txxx: $(ID3V2_TESTBIN)
fate-id3v2-txxx: CMD = run_with_temp "$(FFMPEG) -nostdin -hide_banner -loglevel error -i $(TARGET_SAMPLES)/id3v2/id3v2_priv.mp3 -map_metadata -1 -c copy -fflags +bitexact -metadata foobar=baz -f mp3 -y" "$(ID3V2_TESTBIN)" mp3

FATE_ID3V2_FFMPEG_FFPROBE-$(call ENCDEC, AAC MP3, NUT) += fate-id3v2-reenc-delete-metadata
fate-id3v2-reenc-delete-metadata: CMD = transcode mp3 $(TARGET_SAMPLES)/gapless/gapless-itunes.mp3 nut "-c:a aac -bitexact -t 0.1" "-c copy -t 0.1" "-show_entries format_tags" "" "" "" null

FATE_ID3V2_FFMPEG_FFPROBE-$(call ENCDEC, AAC MP3, NUT) += fate-id3v2-reenc-delete-metadata-keep
fate-id3v2-reenc-delete-metadata-keep: CMD = transcode mp3 $(TARGET_SAMPLES)/gapless/gapless-itunes.mp3 nut "-c:a aac -bitexact -keep_metadata comment-iTunSMPB-eng -t 0.1" "-c copy -t 0.1" "-show_entries format_tags" "" "" "" null

# -map_metadata must not bypass stale metadata pruning
FATE_ID3V2_FFMPEG_FFPROBE-$(call ENCDEC, AAC MP3, NUT) += fate-id3v2-reenc-delete-metadata-map-metadata
fate-id3v2-reenc-delete-metadata-map-metadata: CMD = transcode mp3 $(TARGET_SAMPLES)/gapless/gapless-itunes.mp3 nut "-c:a aac -bitexact -map_metadata 0 -t 0.1" "-c copy -t 0.1" "-show_entries format_tags" "" "" "" null

# :g specifier targets format metadata — iTunSMPB lives there, so it should be kept
FATE_ID3V2_FFMPEG_FFPROBE-$(call ENCDEC, AAC MP3, NUT) += fate-id3v2-reenc-delete-metadata-keep-format
fate-id3v2-reenc-delete-metadata-keep-format: CMD = transcode mp3 $(TARGET_SAMPLES)/gapless/gapless-itunes.mp3 nut "-c:a aac -bitexact -keep_metadata:g comment-iTunSMPB-eng -t 0.1" "-c copy -t 0.1" "-show_entries format_tags" "" "" "" null

# :s:a specifier targets stream metadata — iTunSMPB is format-level, so it should still be deleted
FATE_ID3V2_FFMPEG_FFPROBE-$(call ENCDEC, AAC MP3, NUT) += fate-id3v2-reenc-delete-metadata-keep-stream
fate-id3v2-reenc-delete-metadata-keep-stream: CMD = transcode mp3 $(TARGET_SAMPLES)/gapless/gapless-itunes.mp3 nut "-c:a aac -bitexact -keep_metadata:s:a comment-iTunSMPB-eng -t 0.1" "-c copy -t 0.1" "-show_entries format_tags" "" "" "" null

FATE_ID3V2_FFMPEG_FFPROBE-$(call REMUX, MP3) += fate-id3v2-reenc-remux-keep
fate-id3v2-reenc-remux-keep: CMD = transcode mp3 $(TARGET_SAMPLES)/gapless/gapless-itunes.mp3 mp3 "-c copy" "-c copy -t 0.1" "-show_entries format_tags" "" "" "" null

FATE_ID3V2_FFMPEG_FFPROBE-$(call REMUX, AIFF, WAV_DEMUXER) += fate-id3v2-chapters
fate-id3v2-chapters: CMD = transcode wav $(TARGET_SAMPLES)/wav/200828-005.wav aiff "-c copy -metadata:c:0 description=foo -metadata:c:0 date=2021 -metadata:c copyright=none -metadata:c:1 genre=nonsense -write_id3v2 1" "-c copy -t 0.05" "-show_entries format_tags:chapters"

# Tests reading and writing UTF-16 BOM strings; also tests
# the AIFF muxer's and demuxer's ability to preserve channel layouts.
FATE_ID3V2_FFMPEG_FFPROBE-$(call REMUX, AIFF, WAV_DEMUXER FLAC_DEMUXER PCM_S16LE_DECODER MJPEG_DECODER ARESAMPLE_FILTER CHANNELMAP_FILTER PCM_S24BE_ENCODER) += fate-id3v2-utf16-bom
fate-id3v2-utf16-bom: CMD = transcode wav $(TARGET_SAMPLES)/audio-reference/yo.raw-short.wav aiff "-map 0:a -map 1:v -af aresample,channelmap=channel_layout=hexagonal,aresample -c:a pcm_s24be -c:v copy -write_id3v2 1 -id3v2_version 3 -map_metadata:g:0 1:g -map_metadata:s:v 1:g" "-c copy -t 0.05" "-show_entries stream=channel_layout:stream_tags:format_tags" "-i $(TARGET_SAMPLES)/cover_art/cover_art.flac"

# MusicMatch tools embed artist bio etc. as COMM frames with a descriptor.
FATE_ID3V2_FFPROBE-$(CONFIG_ASF_DEMUXER) += fate-id3v2-wma-comm
fate-id3v2-wma-comm: CMD = probetags $(TARGET_SAMPLES)/cover_art/wma_with_ID3_APIC_trimmed.wma

FATE_ID3V2_FFPROBE-$(CONFIG_ASF_O_DEMUXER) += fate-id3v2-wma-comm-asf_o
fate-id3v2-wma-comm-asf_o: CMD = probetags -f asf_o $(TARGET_SAMPLES)/cover_art/wma_with_ID3_APIC_trimmed.wma

# Same file with the deprecated descriptor-as-key export enabled.
FATE_ID3V2_FFPROBE-$(CONFIG_ASF_DEMUXER) += fate-id3v2-wma-comm-legacy-keys
fate-id3v2-wma-comm-legacy-keys: CMD = probetags -fflags +legacy_id3v2_comm_keys $(TARGET_SAMPLES)/cover_art/wma_with_ID3_APIC_trimmed.wma

FATE_ID3V2_RAW-$(call REMUX, MP3) += fate-id3v2-comm-descriptor
fate-id3v2-comm-descriptor: $(ID3V2_TESTBIN)
fate-id3v2-comm-descriptor: CMD = run_with_temp "$(FFMPEG) -nostdin -hide_banner -loglevel error -i $(TARGET_SAMPLES)/id3v2/id3v2_priv.mp3 -map_metadata -1 -c copy -fflags +bitexact -metadata comment-MusicMatch_Bio-eng=test -f mp3 -y" "$(ID3V2_TESTBIN)" mp3

# Round-trip: COMM descriptor containing a dash is preserved.
FATE_ID3V2_RAW-$(call REMUX, MP3) += fate-id3v2-comm-dashed-descriptor
fate-id3v2-comm-dashed-descriptor: $(ID3V2_TESTBIN)
fate-id3v2-comm-dashed-descriptor: CMD = run_with_temp "$(FFMPEG) -nostdin -hide_banner -loglevel error -i $(TARGET_SAMPLES)/id3v2/id3v2_priv.mp3 -map_metadata -1 -c copy -fflags +bitexact -metadata comment-Foo-Bar-eng=test -f mp3 -y" "$(ID3V2_TESTBIN)" mp3

# Round-trip: COMM with descriptor and empty lang (trailing dash).
FATE_ID3V2_RAW-$(call REMUX, MP3) += fate-id3v2-comm-descriptor-no-lang
fate-id3v2-comm-descriptor-no-lang: $(ID3V2_TESTBIN)
fate-id3v2-comm-descriptor-no-lang: CMD = run_with_temp "$(FFMPEG) -nostdin -hide_banner -loglevel error -i $(TARGET_SAMPLES)/id3v2/id3v2_priv.mp3 -map_metadata -1 -c copy -fflags +bitexact -metadata comment-MusicMatch_Bio-=test -f mp3 -y" "$(ID3V2_TESTBIN)" mp3

# Descriptor that looks like a lang code (eng) with empty lang: must round-trip
# as comment-eng-und, not comment-eng (which means lang-only).
FATE_ID3V2_RAW-$(call REMUX, MP3) += fate-id3v2-comm-lang-as-descriptor
fate-id3v2-comm-lang-as-descriptor: $(ID3V2_TESTBIN)
fate-id3v2-comm-lang-as-descriptor: CMD = run_with_temp "$(FFMPEG) -nostdin -hide_banner -loglevel error -i $(TARGET_SAMPLES)/id3v2/id3v2_priv.mp3 -map_metadata -1 -c copy -fflags +bitexact -metadata comment-eng-=test -f mp3 -y" "$(ID3V2_TESTBIN)" mp3

# Descriptor that looks like a foo-lang code (eng) with empty lang: must round-trip
# as comment-foo-eng-und, not comment-Foo-eng (which means eng lang)..
FATE_ID3V2_RAW-$(call REMUX, MP3) += fate-id3v2-comm-foo-lang-as-descriptor
fate-id3v2-comm-foo-lang-as-descriptor: $(ID3V2_TESTBIN)
fate-id3v2-comm-foo-lang-as-descriptor: CMD = run_with_temp "$(FFMPEG) -nostdin -hide_banner -loglevel error -i $(TARGET_SAMPLES)/id3v2/id3v2_priv.mp3 -map_metadata -1 -c copy -fflags +bitexact -metadata comment-foo-eng-=test -f mp3 -y" "$(ID3V2_TESTBIN)" mp3

# Raw 4CC key "COMM" (length 4): treated as bare COMM frame, no descriptor, no lang.
FATE_ID3V2_RAW-$(call REMUX, MP3) += fate-id3v2-comm-raw-key
fate-id3v2-comm-raw-key: $(ID3V2_TESTBIN)
fate-id3v2-comm-raw-key: CMD = run_with_temp "$(FFMPEG) -nostdin -hide_banner -loglevel error -i $(TARGET_SAMPLES)/id3v2/id3v2_priv.mp3 -map_metadata -1 -c copy -fflags +bitexact -metadata COMM=test -f mp3 -y" "$(ID3V2_TESTBIN)" mp3

# Raw 4CC key with suffix ("COMM-eng"): not an exact 4CC match, must pass through as TXXX.
FATE_ID3V2_RAW-$(call REMUX, MP3) += fate-id3v2-comm-raw-key-suffix
fate-id3v2-comm-raw-key-suffix: $(ID3V2_TESTBIN)
fate-id3v2-comm-raw-key-suffix: CMD = run_with_temp "$(FFMPEG) -nostdin -hide_banner -loglevel error -i $(TARGET_SAMPLES)/id3v2/id3v2_priv.mp3 -map_metadata -1 -c copy -fflags +bitexact -metadata COMM-eng=test -f mp3 -y" "$(ID3V2_TESTBIN)" mp3

# comment-xyz: single 3-char suffix that is NOT a valid ISO 639-2 lang → descriptor only, no lang.
FATE_ID3V2_RAW-$(call REMUX, MP3) += fate-id3v2-comm-invalid-lang
fate-id3v2-comm-invalid-lang: $(ID3V2_TESTBIN)
fate-id3v2-comm-invalid-lang: CMD = run_with_temp "$(FFMPEG) -nostdin -hide_banner -loglevel error -i $(TARGET_SAMPLES)/id3v2/id3v2_priv.mp3 -map_metadata -1 -c copy -fflags +bitexact -metadata comment-xyz=test -f mp3 -y" "$(ID3V2_TESTBIN)" mp3

# comment-foobar: single suffix longer than 3 chars → descriptor only, no lang.
FATE_ID3V2_RAW-$(call REMUX, MP3) += fate-id3v2-comm-long-descriptor
fate-id3v2-comm-long-descriptor: $(ID3V2_TESTBIN)
fate-id3v2-comm-long-descriptor: CMD = run_with_temp "$(FFMPEG) -nostdin -hide_banner -loglevel error -i $(TARGET_SAMPLES)/id3v2/id3v2_priv.mp3 -map_metadata -1 -c copy -fflags +bitexact -metadata comment-foobar=test -f mp3 -y" "$(ID3V2_TESTBIN)" mp3

# comment-desc-xyz: multi-dash, last component "xyz" not a valid ISO 639-2 lang → full "desc-xyz" as descriptor.
FATE_ID3V2_RAW-$(call REMUX, MP3) += fate-id3v2-comm-multi-invalid-lang
fate-id3v2-comm-multi-invalid-lang: $(ID3V2_TESTBIN)
fate-id3v2-comm-multi-invalid-lang: CMD = run_with_temp "$(FFMPEG) -nostdin -hide_banner -loglevel error -i $(TARGET_SAMPLES)/id3v2/id3v2_priv.mp3 -map_metadata -1 -c copy -fflags +bitexact -metadata comment-desc-xyz=test -f mp3 -y" "$(ID3V2_TESTBIN)" mp3

# comment-sort must be treated as COMM with descriptor "sort"
FATE_ID3V2_RAW-$(call REMUX, MP3) += fate-id3v2-comm-sort
fate-id3v2-comm-sort: $(ID3V2_TESTBIN)
fate-id3v2-comm-sort: CMD = run_with_temp "$(FFMPEG) -nostdin -hide_banner -loglevel error -i $(TARGET_SAMPLES)/id3v2/id3v2_priv.mp3 -map_metadata -1 -c copy -fflags +bitexact -metadata comment-sort=test -f mp3 -y" "$(ID3V2_TESTBIN)" mp3

# comment-sort- must be treated as COMM with descriptor "sort"
FATE_ID3V2_RAW-$(call REMUX, MP3) += fate-id3v2-comm-sort-
fate-id3v2-comm-sort-: $(ID3V2_TESTBIN)
fate-id3v2-comm-sort-: CMD = run_with_temp "$(FFMPEG) -nostdin -hide_banner -loglevel error -i $(TARGET_SAMPLES)/id3v2/id3v2_priv.mp3 -map_metadata -1 -c copy -fflags +bitexact -metadata comment-sort-=test -f mp3 -y" "$(ID3V2_TESTBIN)" mp3

# comment-sort-<lang> must be treated as COMM with descriptor "sort" and language <lang>
FATE_ID3V2_RAW-$(call REMUX, MP3) += fate-id3v2-comm-sort-eng
fate-id3v2-comm-sort-eng: $(ID3V2_TESTBIN)
fate-id3v2-comm-sort-eng: CMD = run_with_temp "$(FFMPEG) -nostdin -hide_banner -loglevel error -i $(TARGET_SAMPLES)/id3v2/id3v2_priv.mp3 -map_metadata -1 -c copy -fflags +bitexact -metadata comment-sort-eng=test -f mp3 -y" "$(ID3V2_TESTBIN)" mp3

FATE_SAMPLES_FFPROBE        += $(FATE_ID3V2_FFPROBE-yes)
FATE_SAMPLES_FFMPEG         += $(FATE_ID3V2_FFMPEG-yes) $(FATE_ID3V2_RAW-yes)
FATE_SAMPLES_FFMPEG_FFPROBE += $(FATE_ID3V2_FFMPEG_FFPROBE-yes)
fate-id3v2: $(FATE_ID3V2_FFPROBE-yes) $(FATE_ID3V2_FFMPEG_FFPROBE-yes) $(FATE_ID3V2_FFMPEG-yes) $(FATE_ID3V2_RAW-yes)
