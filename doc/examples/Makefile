# use pkg-config for getting CFLAGS abd LDLIBS
FFMPEG_LIBS=    libavdevice                        \
                libavformat                        \
                libavfilter                        \
                libavcodec                         \
                libswscale                         \
                libavutil                          \

CFLAGS+=-Wall $(shell pkg-config  --cflags $(FFMPEG_LIBS))
LDLIBS+=$(shell pkg-config --libs $(FFMPEG_LIBS))

EXAMPLES=       decoding_encoding                  \
                filtering_video                    \
                filtering_audio                    \
                metadata                           \
                muxing                             \

OBJS=$(addsuffix .o,$(EXAMPLES))

%: %.o
	$(CC) $< $(LDLIBS) -o $@

%.o: %.c
	$(CC) $< $(CFLAGS) -c -o $@

.phony: all clean

all: $(OBJS) $(EXAMPLES)

clean:
	rm -rf $(EXAMPLES) $(OBJS)
