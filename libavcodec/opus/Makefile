clean::
	$(RM) $(CLEANSUFFIXES:%=libavcodec/opus/%)

OBJS-$(CONFIG_OPUS_DECODER) +=  \
    opus/dec.o                  \
    opus/dec_celt.o             \
    opus/celt.o                 \
    opus/frame_duration_tab.o   \
    opus/pvq.o                  \
    opus/silk.o                 \
    opus/tab.o                  \
    opus/dsp.o                  \
    opus/parse.o                \
    opus/rc.o                   \


OBJS-$(CONFIG_OPUS_PARSER) +=   \
    opus/frame_duration_tab.o   \
    opus/parser.o               \
    opus/parse.o                \


OBJS-$(CONFIG_OPUS_ENCODER) +=  \
    opus/enc.o                  \
    opus/enc_psy.o              \
    opus/celt.o                 \
    opus/pvq.o                  \
    opus/rc.o                   \
    opus/tab.o                  \

STLIBOBJS-$(CONFIG_MATROSKA_MUXER)   += opus/frame_duration_tab.o
STLIBOBJS-$(CONFIG_WEBM_MUXER)       += opus/frame_duration_tab.o

libavcodec/opus/%.o: CPPFLAGS += -I$(SRC_PATH)/libavcodec/
