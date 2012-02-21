# use pkg-config for getting CFLAGS abd LDFLAGS
FFMPEG_LIBS=libavdevice libavformat libavfilter libavcodec libswscale libavutil
CFLAGS+=-Wall $(shell pkg-config  --cflags $(FFMPEG_LIBS))
LDFLAGS+=$(shell pkg-config --libs $(FFMPEG_LIBS))

EXAMPLES=decoding_encoding filtering_video filtering_audio metadata muxing

OBJS=$(addsuffix .o,$(EXAMPLES))

%: %.o
	$(CC) $< $(LDFLAGS) -o $@

%.o: %.c
	$(CC) $< $(CFLAGS) -c -o $@

.phony: all clean

all: $(OBJS) $(EXAMPLES)

clean:
	rm -rf $(EXAMPLES) $(OBJS)
