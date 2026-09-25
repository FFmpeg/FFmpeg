FATE_ENC_EXTERNAL-$(call ENCDEC, LIBX264 H264, MP4 MOV, H264_DEMUXER) += fate-libx264-simple
fate-libx264-simple: CMD = enc_external $(TARGET_SAMPLES)/h264-conformance/BA1_Sony_D.jsv \
    mp4 "-c:v libx264" "-show_entries frame=width,height,pix_fmt,pts,pkt_dts -of flat"

# test for SVT-AV1 MDCV and CLL passthrough during encoding
FATE_ENC_EXTERNAL-$(call ENCDEC, LIBSVTAV1 HEVC, MP4 MOV, HEVC_DEMUXER LIBDAV1D_DECODER) += fate-libsvtav1-hdr10
fate-libsvtav1-hdr10: CMD = enc_external $(TARGET_SAMPLES)/hevc/hdr10_plus_h265_sample.hevc \
    mp4 "-c:v libsvtav1" "-show_frames -show_entries frame=side_data_list -of flat"

# test for x264 MDCV and CLL passthrough during encoding
FATE_ENC_EXTERNAL-$(call ENCDEC, LIBX264 HEVC, MP4 MOV, LIBX264_HDR10 HEVC_DEMUXER H264_DECODER) += fate-libx264-hdr10
fate-libx264-hdr10: CMD = enc_external $(TARGET_SAMPLES)/hevc/hdr10_plus_h265_sample.hevc \
    mp4 "-c:v libx264" "-show_frames -show_entries frame=side_data_list -of flat"

FATE_SAMPLES_FFMPEG_FFPROBE += $(FATE_ENC_EXTERNAL-yes)
fate-enc-external: $(FATE_ENC_EXTERNAL-yes)

# Check decoded frame counts without depending on libspeex's encoded bytes.
FATE_LIBSPEEX-$(call ALLYES, LIBSPEEX_ENCODER SPEEX_DECODER OGG_MUXER OGG_DEMUXER LAVFI_INDEV SINE_FILTER ARESAMPLE_FILTER \
                               PCM_S16$(if $(filter yes,$(HAVE_BIGENDIAN)),BE,LE)_DECODER FILE_PROTOCOL) += fate-libspeex-encode fate-libspeex-encode-stereo
fate-libspeex-encode: SPEEX_OPTS = -ac 1
fate-libspeex-encode-stereo: SPEEX_OPTS = -ac 2 -frames_per_packet 2
$(FATE_LIBSPEEX-yes): CMD = run_with_temp \
    "$(FFMPEG) -nostdin -hide_banner -loglevel error \
    -f lavfi -i sine=frequency=1000:sample_rate=32000:duration=1 \
    -c:a libspeex -b:a 30k $(SPEEX_OPTS) -f ogg -y" \
    "ffprobe$(PROGSSUF)$(EXESUF) -v error -bitexact -count_frames \
    -show_entries stream=codec_name,sample_rate,channels,nb_read_frames" ogg

FATE_FFMPEG_FFPROBE += $(FATE_LIBSPEEX-yes)
fate-enc-external: $(FATE_LIBSPEEX-yes)
