HEADER_DIRS += ogg

clean::
	$(RM) $(CLEANSUFFIXES:%=libavformat/ogg/%)
	$(RM) $(CLEANSUFFIXES:%=libavformat/ogg/demuxer/%)
	$(RM) $(CLEANSUFFIXES:%=libavformat/ogg/muxer/%)

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

OBJS-$(CONFIG_OGG_MUXER) += ogg/muxer.o              \
                            ogg/muxer/flac.o         \
                            ogg/muxer/opus.o         \
                            ogg/muxer/speex.o        \
                            ogg/muxer/theora.o       \
                            ogg/muxer/vorbis.o       \
                            ogg/muxer/vp8.o
