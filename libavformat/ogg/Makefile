HEADER_DIRS += ogg

clean::
	$(RM) $(CLEANSUFFIXES:%=libavformat/ogg/%)
	$(RM) $(CLEANSUFFIXES:%=libavformat/ogg/demuxer/%)

OBJS-$(CONFIG_OGG_DEMUXER) +=  \
    ogg/demuxer.o              \
    ogg/demuxer/dirac.o        \
    ogg/demuxer/flac.o         \
    ogg/demuxer/ogm.o          \
    ogg/demuxer/opus.o         \
    ogg/demuxer/skeleton.o     \
    ogg/demuxer/speex.o        \
    ogg/demuxer/theora.o       \
    ogg/demuxer/vorbis.o       \
    ogg/demuxer/vp8.o          \

OBJS-$(CONFIG_OGA_MUXER)   += ogg/muxer.o
OBJS-$(CONFIG_OGG_MUXER)   += ogg/muxer.o
OBJS-$(CONFIG_OGV_MUXER)   += ogg/muxer.o
OBJS-$(CONFIG_OPUS_MUXER)  += ogg/muxer.o
OBJS-$(CONFIG_SPEEX_MUXER) += ogg/muxer.o
