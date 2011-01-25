LIBAVFORMAT_$MAJOR {
        global: *;
        local:
                ff_*_demuxer;
                ff_*_muxer;
                ff_*_protocol;
};
