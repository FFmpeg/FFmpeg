clean::
	$(RM) $(CLEANSUFFIXES:%=libavcodec/hevc/%)

OBJS-$(CONFIG_HEVC_DECODER) += \
    aom_film_grain.o           \
    h274.o                     \
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
    h2645data.o             \
    h2645_parse.o           \
    h2645_vui.o             \
    hevc/data.o             \
    hevc/parse.o            \
    hevc/ps.o               \


OBJS-$(CONFIG_HEVC_SEI) +=  \
    hevc/sei.o              \
    h2645_sei.o             \
    dynamic_hdr_vivid.o     \
    aom_film_grain.o        \


libavcodec/hevc/%.o: CPPFLAGS += -I$(SRC_PATH)/libavcodec/
