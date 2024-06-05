clean::
	$(RM) $(CLEANSUFFIXES:%=libavcodec/hevc/%)

OBJS-$(CONFIG_HEVC_DECODER) += \
    hevc/cabac.o               \
    hevc/data.o                \
    hevc/dsp.o                 \
    hevc/filter.o              \
    hevc/hevcdec.o             \
    hevc/mvs.o                 \
    hevc/pred.o                \
    hevc/refs.o                \

OBJS-$(CONFIG_HEVC_PARSER) += \
    hevc/parser.o             \


OBJS-$(CONFIG_HEVCPARSE) += \
    hevc/data.o             \
    hevc/parse.o            \
    hevc/ps.o               \


OBJS-$(CONFIG_HEVC_SEI) +=  \
    hevc/sei.o              \


libavcodec/hevc/%.o: CPPFLAGS += -I$(SRC_PATH)/libavcodec/
