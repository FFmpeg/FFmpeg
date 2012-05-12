# use pkg-config for getting CFLAGS and LDLIBS
FFMPEG_LIBS=    libavdevice                        \
                libavformat                        \
                libavfilter                        \
                libavcodec                         \
                libavresample                      \
                libswresample                      \
                libswscale                         \
                libavutil                          \

CFLAGS += -Wall -O2 -g
CFLAGS += $(shell pkg-config --cflags $(FFMPEG_LIBS))
LDLIBS += $(shell pkg-config --libs $(FFMPEG_LIBS))

EXAMPLES=       decoding_encoding                  \
                filtering_video                    \
                filtering_audio                    \
                metadata                           \
                muxing                             \

OBJS=$(addsuffix .o,$(EXAMPLES))

# the following examples make explicit use of the math library
decoding_encoding: LDLIBS += -lm
muxing:            LDLIBS += -lm

.phony: all clean

all: $(OBJS) $(EXAMPLES)

clean:
	rm -rf $(EXAMPLES) $(OBJS)
