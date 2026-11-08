# One link of a tone, given its frequency $(1), encoder options $(2) and
# output file $(3).
OGG_CHAIN_COPY_LINK = ffmpeg -auto_conversion_filters -f lavfi -i sine=frequency=$(1):duration=1 \
                        $(2) -flags +bitexact -fflags +bitexact -f ogg -y $(3)
OGG_CHAIN_COPY_DECODE = ffmpeg -auto_conversion_filters -i $(1) -f s16le -y $(2)

# Lists the beginning and end of each link of $(1).
OGG_CHAIN_COPY_PAGES = run libavformat/tests/ogg_pages$(EXESUF) $(1) > $(1).pages && \
                       grep -o "flags=..." $(1).pages | grep "[be]"
# Checksums of the headers of each link of $(1), written to $(2).
OGG_CHAIN_COPY_HEADERS = ffmpeg -i $(1) -c copy -f framecrc -y $(1).crc && \
                         sed -n -e "s/^\#extradata [0-9]*: *//p" \
                                -e "s/.*New Extradata, *\([0-9]*, 0x[0-9a-f]*\).*/\1/p" $(1).crc > $(2)
OGG_CHAIN_COPY_COMPARE = { cmp -s $(1) $(2); case $$? in 0) echo "$(3): same";; 1) echo "$(3): different";; *) false;; esac; }

# Stream copies the chain of $(1).1.ogg and $(1).2.ogg, encoded with options
# $(2) and $(3), whole and cut at the start of the second link. The beginning
# and end of each link written are listed, then whether the copies keep the
# headers of the links they start and the whole copy decodes as the original.
OGG_CHAIN_COPY = \
    $(call OGG_CHAIN_COPY_LINK,440,$(2),$(1).1.ogg) && \
    $(call OGG_CHAIN_COPY_LINK,660,$(3) -serial_offset 1,$(1).2.ogg) && \
    cat $(1).1.ogg $(1).2.ogg > $(1).ogg && \
    ffmpeg -i $(1).ogg -c copy -fflags +bitexact -f ogg -y $(1).copy.ogg && \
    echo copy && $(call OGG_CHAIN_COPY_PAGES,$(1).copy.ogg) && \
    ffmpeg -i $(1).ogg -ss 1 -c copy -fflags +bitexact -f ogg -y $(1).cut.ogg && \
    echo cut && $(call OGG_CHAIN_COPY_PAGES,$(1).cut.ogg) && \
    $(call OGG_CHAIN_COPY_HEADERS,$(1).ogg,$(1).headers) && \
    $(call OGG_CHAIN_COPY_HEADERS,$(1).copy.ogg,$(1).copy.headers) && \
    $(call OGG_CHAIN_COPY_HEADERS,$(1).cut.ogg,$(1).cut.headers) && \
    $(call OGG_CHAIN_COPY_HEADERS,$(1).2.ogg,$(1).2.headers) && \
    $(call OGG_CHAIN_COPY_COMPARE,$(1).headers,$(1).copy.headers,copy headers) && \
    $(call OGG_CHAIN_COPY_COMPARE,$(1).2.headers,$(1).cut.headers,cut headers) && \
    $(call OGG_CHAIN_COPY_DECODE,$(1).ogg,$(1).pcm) && \
    $(call OGG_CHAIN_COPY_DECODE,$(1).copy.ogg,$(1).copy.pcm) && \
    $(call OGG_CHAIN_COPY_COMPARE,$(1).pcm,$(1).copy.pcm,decoded copy)

# Stream copies the concatenation of two copies of a link of two FLAC streams,
# the second with packet metadata, and checks the copy decodes as the input.
OGG_CHAIN_COPY_TWO_STREAMS = \
    ffmpeg -auto_conversion_filters -f lavfi -i sine=frequency=440:duration=1 \
           -f lavfi -i sine=frequency=660:duration=1 -map 0 -map 1 -c:a flac \
           -flags +bitexact -fflags +bitexact -f ogg -y $(1).ogg && \
    printf "ffconcat version 1.0\nfile %s\nfile %s\nfile_packet_meta title second\n" \
           $(notdir $(1)).ogg $(notdir $(1)).ogg > $(1).ffconcat && \
    ffmpeg -i $(1).ffconcat -map 0 -c copy -fflags +bitexact -f ogg -y $(1).copy.ogg && \
    ffmpeg -i $(1).ffconcat -map 0 -f framecrc -y $(1).crc && \
    ffmpeg -i $(1).copy.ogg -map 0 -f framecrc -y $(1).copy.crc && \
    $(call OGG_CHAIN_COPY_COMPARE,$(1).crc,$(1).copy.crc,decoded copy)

OGG_CHAIN_COPY_FILES = .1.ogg .2.ogg .ogg .copy.ogg .copy.ogg.pages .cut.ogg .cut.ogg.pages \
                       .ogg.crc .2.ogg.crc .copy.ogg.crc .cut.ogg.crc \
                       .headers .copy.headers .cut.headers .2.headers .pcm .copy.pcm

OGG_CHAIN_COPY_DEPS = OGG_MUXER OGG_DEMUXER LAVFI_INDEV SINE_FILTER ARESAMPLE_FILTER \
                      PCM_S16LE_DECODER PCM_S16LE_ENCODER PCM_S16LE_MUXER FRAMECRC_MUXER FILE_PROTOCOL

OGG_CHAIN_COPY_VORBIS = tests/data/fate/ogg-vorbis-copy-chained.links
FATE_OGG_CHAIN_COPY-$(call ALLYES, LIBVORBIS_ENCODER VORBIS_DECODER $(OGG_CHAIN_COPY_DEPS)) += fate-ogg-vorbis-copy-chained
fate-ogg-vorbis-copy-chained: CMD = $(call OGG_CHAIN_COPY,$(OGG_CHAIN_COPY_VORBIS),-c:a libvorbis -q:a 1,-c:a libvorbis -q:a 9)
fate-ogg-vorbis-copy-chained: CLEANFILES = $(addprefix $(OGG_CHAIN_COPY_VORBIS),$(OGG_CHAIN_COPY_FILES))

OGG_CHAIN_COPY_OPUS = tests/data/fate/ogg-opus-copy-chained.links
FATE_OGG_CHAIN_COPY-$(call ALLYES, OPUS_ENCODER OPUS_DECODER OPUS_METADATA_BSF $(OGG_CHAIN_COPY_DEPS)) += fate-ogg-opus-copy-chained
fate-ogg-opus-copy-chained: CMD = $(call OGG_CHAIN_COPY,$(OGG_CHAIN_COPY_OPUS),-c:a opus -strict experimental,-c:a opus -strict experimental -bsf:a opus_metadata=gain=-1536)
fate-ogg-opus-copy-chained: CLEANFILES = $(addprefix $(OGG_CHAIN_COPY_OPUS),$(OGG_CHAIN_COPY_FILES))

OGG_CHAIN_COPY_FLAC = tests/data/fate/ogg-flac-copy-chained.links
FATE_OGG_CHAIN_COPY-$(call ALLYES, FLAC_ENCODER FLAC_DECODER FLAC_PARSER $(OGG_CHAIN_COPY_DEPS)) += fate-ogg-flac-copy-chained
fate-ogg-flac-copy-chained: CMD = $(call OGG_CHAIN_COPY,$(OGG_CHAIN_COPY_FLAC),-c:a flac -frame_size 1024,-c:a flac -frame_size 4096)
fate-ogg-flac-copy-chained: CLEANFILES = $(addprefix $(OGG_CHAIN_COPY_FLAC),$(OGG_CHAIN_COPY_FILES))

OGG_CHAIN_COPY_TWO_STREAMS_FILE = tests/data/fate/ogg-flac-copy-two-streams-metadata.links
FATE_OGG_CHAIN_COPY-$(call ALLYES, FLAC_ENCODER FLAC_DECODER FLAC_PARSER CONCAT_DEMUXER $(OGG_CHAIN_COPY_DEPS)) += fate-ogg-flac-copy-two-streams-metadata
fate-ogg-flac-copy-two-streams-metadata: CMD = $(call OGG_CHAIN_COPY_TWO_STREAMS,$(OGG_CHAIN_COPY_TWO_STREAMS_FILE))
fate-ogg-flac-copy-two-streams-metadata: CLEANFILES = $(addprefix $(OGG_CHAIN_COPY_TWO_STREAMS_FILE),.ogg .ffconcat .copy.ogg .crc .copy.crc)

$(FATE_OGG_CHAIN_COPY-yes): libavformat/tests/ogg_pages$(EXESUF)

FATE_FFMPEG += $(FATE_OGG_CHAIN_COPY-yes)

fate-ogg-chain-copy: $(FATE_OGG_CHAIN_COPY-yes)
