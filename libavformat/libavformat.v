LIBAVFORMAT_$MAJOR {
        global: av*;
                #FIXME those are for ffserver
                ff_inet_aton;
                ff_socket_nonblock;
                ff_mpegts_parse_close;
                ff_mpegts_parse_open;
                ff_mpegts_parse_packet;
                ff_rtsp_parse_line;
                ff_rtp_get_local_rtp_port;
                ff_rtp_get_local_rtcp_port;
                ffio_open_dyn_packet_buf;
                ffio_set_buf_size;
                ffurl_close;
                ffurl_open;
                ffurl_read_complete;
                ffurl_seek;
                ffurl_size;
                ffurl_write;
                ffurl_protocol_next;
                #those are deprecated, remove on next bump
                url_feof;
                get_*;
                ff_codec_get_id;
        local: *;
};
