HEADER_DIRS += ogg

clean::
	$(RM) $(CLEANSUFFIXES:%=libavformat/ogg/%)
	$(RM) $(CLEANSUFFIXES:%=libavformat/ogg/mapping/%)

OBJS-$(CONFIG_OGG_DEMUXER) +=  \
    ogg/demuxer.o              \
    ogg/mapping/dirac.o        \
    ogg/mapping/flac.o         \
    ogg/mapping/ogm.o          \
    ogg/mapping/opus.o         \
    ogg/mapping/skeleton.o     \
    ogg/mapping/speex.o        \
    ogg/mapping/theora.o       \
    ogg/mapping/vorbis.o       \
    ogg/mapping/vp8.o          \

OBJS-$(CONFIG_OGA_MUXER)   += ogg/muxer.o
OBJS-$(CONFIG_OGG_MUXER)   += ogg/muxer.o
OBJS-$(CONFIG_OGV_MUXER)   += ogg/muxer.o
OBJS-$(CONFIG_OPUS_MUXER)  += ogg/muxer.o
OBJS-$(CONFIG_SPEEX_MUXER) += ogg/muxer.o
