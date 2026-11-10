# Two links of a test pattern piped as one chained stream, the second at a
# larger frame size. List each decoded frame's timestamp and size, since the
# encoded bytes depend on the libtheora version.
OGG_THEORA_CHAIN = \
    { ffmpeg -auto_conversion_filters -f lavfi -i testsrc=s=160x120:r=15:d=1 -c:v libtheora \
        -flags +bitexact -fflags +bitexact -f ogg - && \
      ffmpeg -auto_conversion_filters -f lavfi -i testsrc=s=320x240:r=15:d=1 -c:v libtheora \
        -flags +bitexact -fflags +bitexact -serial_offset 1 -f ogg -; } | \
    run ffprobe$(PROGSSUF)$(EXESUF) -bitexact -of compact=p=0 -show_entries frame=pts,width,height

FATE_OGG_THEORA_CHAIN-$(call ENCDEC, LIBTHEORA THEORA, OGG, LAVFI_INDEV TESTSRC_FILTER SCALE_FILTER WRAPPED_AVFRAME_DECODER PIPE_PROTOCOL) += fate-ogg-theora-chained-size
fate-ogg-theora-chained-size: CMD = $(OGG_THEORA_CHAIN) -threads 1 -i pipe:

FATE_OGG_THEORA_CHAIN-$(call ENCDEC, LIBTHEORA THEORA, OGG, LAVFI_INDEV TESTSRC_FILTER SCALE_FILTER WRAPPED_AVFRAME_DECODER PIPE_PROTOCOL) += fate-ogg-theora-chained-size-frame-threads
fate-ogg-theora-chained-size-frame-threads: CMD = $(OGG_THEORA_CHAIN) -threads 2 -thread_type frame -i pipe:

FATE_FFMPEG_FFPROBE += $(FATE_OGG_THEORA_CHAIN-yes)

# One link of a test pattern, given its frame size $(1), further muxer
# options $(2) and output file $(3).
OGG_THEORA_LINK = ffmpeg -auto_conversion_filters -f lavfi -i testsrc=s=$(1):r=15:d=1 -c:v libtheora \
                    -flags +bitexact -fflags +bitexact $(2) -f ogg -y $(3)
OGG_THEORA_DECODE = ffmpeg -i $(1) -noautoscale -fps_mode passthrough -f rawvideo -

# Each link of a chain is a stream of its own, so decoding the links
# $(1).1.ogg and $(1).2.ogg chained must give what decoding them one by one
# does, written to $(1).raw as the reference. $(2) holds further options for
# the second link.
OGG_THEORA_CHAINED_LINKS = \
    $(call OGG_THEORA_LINK,160x120,,$(1).1.ogg) && \
    $(call OGG_THEORA_LINK,320x240,-serial_offset 1 $(2),$(1).2.ogg) && \
    { $(call OGG_THEORA_DECODE,$(1).1.ogg) && $(call OGG_THEORA_DECODE,$(1).2.ogg); } > $(1).raw && \
    test -s $(1).raw
OGG_THEORA_CHAINED = \
    $(call OGG_THEORA_CHAINED_LINKS,$(1),$(2)) && \
    cat $(1).1.ogg $(1).2.ogg | $(call OGG_THEORA_DECODE,pipe:)

OGG_THEORA_CHAINED_CONTENT_DEPS = LAVFI_INDEV TESTSRC_FILTER SCALE_FILTER WRAPPED_AVFRAME_DECODER RAWVIDEO_MUXER

OGG_THEORA_CHAINED_CONTENT_LINKS = tests/data/fate/ogg-theora-chained-content.links
FATE_OGG_THEORA_CONTENT-$(call TRANSCODE, LIBTHEORA THEORA, OGG, $(OGG_THEORA_CHAINED_CONTENT_DEPS)) += fate-ogg-theora-chained-content
fate-ogg-theora-chained-content: CMD = $(call OGG_THEORA_CHAINED,$(OGG_THEORA_CHAINED_CONTENT_LINKS))
fate-ogg-theora-chained-content: REF = $(OGG_THEORA_CHAINED_CONTENT_LINKS).raw
fate-ogg-theora-chained-content: CLEANFILES = $(addprefix $(OGG_THEORA_CHAINED_CONTENT_LINKS),.1.ogg .2.ogg .raw)
fate-ogg-theora-chained-content: CMP = stddev
fate-ogg-theora-chained-content: CMP_TARGET = 0
fate-ogg-theora-chained-content: FUZZ = 0

OGG_THEORA_CHAINED_CONTENT_FT_LINKS = tests/data/fate/ogg-theora-chained-content-frame-threads.links
FATE_OGG_THEORA_CONTENT-$(call TRANSCODE, LIBTHEORA THEORA, OGG, $(OGG_THEORA_CHAINED_CONTENT_DEPS)) += fate-ogg-theora-chained-content-frame-threads
fate-ogg-theora-chained-content-frame-threads: CMD = $(call OGG_THEORA_CHAINED,$(OGG_THEORA_CHAINED_CONTENT_FT_LINKS))
fate-ogg-theora-chained-content-frame-threads: REF = $(OGG_THEORA_CHAINED_CONTENT_FT_LINKS).raw
fate-ogg-theora-chained-content-frame-threads: CLEANFILES = $(addprefix $(OGG_THEORA_CHAINED_CONTENT_FT_LINKS),.1.ogg .2.ogg .raw)
fate-ogg-theora-chained-content-frame-threads: CMP = stddev
fate-ogg-theora-chained-content-frame-threads: CMP_TARGET = 0
fate-ogg-theora-chained-content-frame-threads: FUZZ = 0
fate-ogg-theora-chained-content-frame-threads: THREADS = 2
fate-ogg-theora-chained-content-frame-threads: THREAD_TYPE = frame

# The second link's comment header is larger than 64 KiB.
OGG_THEORA_CHAINED_LARGE_COMMENT_LINKS = tests/data/fate/ogg-theora-chained-large-comment.links
FATE_OGG_THEORA_CONTENT-$(call TRANSCODE, LIBTHEORA THEORA, OGG, $(OGG_THEORA_CHAINED_CONTENT_DEPS)) += fate-ogg-theora-chained-large-comment
fate-ogg-theora-chained-large-comment: CMD = \
    printf "comment=%070000d" 0 > $(OGG_THEORA_CHAINED_LARGE_COMMENT_LINKS).meta && \
    $(call OGG_THEORA_CHAINED,$(OGG_THEORA_CHAINED_LARGE_COMMENT_LINKS),-/metadata $(OGG_THEORA_CHAINED_LARGE_COMMENT_LINKS).meta)
fate-ogg-theora-chained-large-comment: REF = $(OGG_THEORA_CHAINED_LARGE_COMMENT_LINKS).raw
fate-ogg-theora-chained-large-comment: CLEANFILES = $(addprefix $(OGG_THEORA_CHAINED_LARGE_COMMENT_LINKS),.meta .1.ogg .2.ogg .raw)
fate-ogg-theora-chained-large-comment: CMP = stddev
fate-ogg-theora-chained-large-comment: CMP_TARGET = 0
fate-ogg-theora-chained-large-comment: FUZZ = 0

# The chain stream copied must decode as its links do.
OGG_THEORA_CHAINED_COPY_LINKS = tests/data/fate/ogg-theora-chained-copy.links
FATE_OGG_THEORA_CONTENT-$(call TRANSCODE, LIBTHEORA THEORA, OGG, $(OGG_THEORA_CHAINED_CONTENT_DEPS)) += fate-ogg-theora-chained-copy
fate-ogg-theora-chained-copy: CMD = \
    $(call OGG_THEORA_CHAINED_LINKS,$(OGG_THEORA_CHAINED_COPY_LINKS)) && \
    cat $(OGG_THEORA_CHAINED_COPY_LINKS).1.ogg $(OGG_THEORA_CHAINED_COPY_LINKS).2.ogg | \
    ffmpeg -i pipe: -c copy -fflags +bitexact -f ogg -y $(OGG_THEORA_CHAINED_COPY_LINKS).copy.ogg && \
    $(call OGG_THEORA_DECODE,$(OGG_THEORA_CHAINED_COPY_LINKS).copy.ogg)
fate-ogg-theora-chained-copy: REF = $(OGG_THEORA_CHAINED_COPY_LINKS).raw
fate-ogg-theora-chained-copy: CLEANFILES = $(addprefix $(OGG_THEORA_CHAINED_COPY_LINKS),.1.ogg .2.ogg .copy.ogg .raw)
fate-ogg-theora-chained-copy: CMP = stddev
fate-ogg-theora-chained-copy: CMP_TARGET = 0
fate-ogg-theora-chained-copy: FUZZ = 0

FATE_FFMPEG += $(FATE_OGG_THEORA_CONTENT-yes)

fate-ogg-theora: $(FATE_OGG_THEORA_CHAIN-yes) $(FATE_OGG_THEORA_CONTENT-yes)
