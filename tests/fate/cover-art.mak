FATE_COVER_ART-$(CONFIG_APE_DEMUXER) += fate-cover-art-ape
fate-cover-art-ape: CMD = md5 -i $(TARGET_SAMPLES)/cover_art/luckynight_cover.ape -an -c:v copy -f rawvideo
fate-cover-art-ape: REF = 45333c983c45af54449dff10af144317

FATE_COVER_ART-$(CONFIG_FLAC_DEMUXER) += fate-cover-art-flac
fate-cover-art-flac: CMD = md5 -i $(TARGET_SAMPLES)/cover_art/cover_art.flac -an -c:v copy -f rawvideo
fate-cover-art-flac: REF = 0de1fc6200596fa32b8f7300a14c0261

FATE_COVER_ART-$(CONFIG_MOV_DEMUXER) += fate-cover-art-m4a
fate-cover-art-m4a: CMD = md5 -i $(TARGET_SAMPLES)/cover_art/Owner-iTunes_9.0.3.15.m4a -an -c:v copy -f rawvideo
fate-cover-art-m4a: REF = 08ba70a3b594ff6345a93965e96a9d3e

FATE_COVER_ART-$(CONFIG_OGG_DEMUXER) += fate-cover-art-ogg
fate-cover-art-ogg: CMD = md5 -i $(TARGET_SAMPLES)/cover_art/ogg_vorbiscomment_cover.opus -map 0:v -c:v copy -f rawvideo
fate-cover-art-ogg: REF = 7f117e073620eabb4ed02680cf70af41

FATE_COVER_ART-$(CONFIG_ASF_DEMUXER) += fate-cover-art-wma
fate-cover-art-wma: CMD = md5 -i $(TARGET_SAMPLES)/cover_art/Californication_cover.wma -an -c:v copy -f rawvideo
fate-cover-art-wma: REF = 0808bd0e1b61542a16e1906812dd924b

FATE_COVER_ART-$(CONFIG_ASF_DEMUXER) += fate-cover-art-wma-id3
fate-cover-art-wma-id3: CMD = md5 -i $(TARGET_SAMPLES)/cover_art/wma_with_ID3_APIC_trimmed.wma -an -c:v copy -f rawvideo
fate-cover-art-wma-id3: REF = e6a8dd03687d5178bc13fc7d3316696e

FATE_COVER_ART-$(CONFIG_ASF_DEMUXER) += fate-cover-art-wma-metadatalib
fate-cover-art-wma-metadatalib: CMD = md5 -i $(TARGET_SAMPLES)/cover_art/wma_with_metadata_library_object_tag_trimmed.wma -map 0:v -c:v copy -f rawvideo
fate-cover-art-wma-metadatalib: REF = 32e8bd4fad546f63d881a0256f083aea

FATE_COVER_ART-$(CONFIG_WV_DEMUXER) += fate-cover-art-wv
fate-cover-art-wv: CMD = md5 -i $(TARGET_SAMPLES)/cover_art/luckynight_cover.wv -an -c:v copy -f rawvideo
fate-cover-art-wv: REF = 45333c983c45af54449dff10af144317

# Tests writing id3v2 tags (some with non-ASCII characters) and apics.
FATE_COVER_ART_REMUX-$(call ALLYES, FILE_PROTOCOL FLAC_DEMUXER MJPEG_DECODER \
                                    FLAC_DECODER SCALE_FILTER PNG_ENCODER    \
                                    BMP_ENCODER PCM_S16BE_ENCODER AIFF_MUXER \
                                    AIFF_DEMUXER BMP_DECODER PNG_DECODER     \
                                    FRAMECRC_MUXER PIPE_PROTOCOL)            \
                       += fate-cover-art-aiff-id3v2-remux
fate-cover-art-aiff-id3v2-remux: CMD = transcode flac $(TARGET_SAMPLES)/cover_art/cover_art.flac aiff "-map 0 -map 0:v -map 0:v -map 0:v -c:a pcm_s16be -c:v:0 copy -filter:v:1 scale -c:v:1 png -filter:v:2 scale -c:v:2 bmp -c:v:3 copy -write_id3v2 1 -metadata:g unknown_key=unknown_value -metadata compilation=foo -metadata:s:v:0 title=first -metadata:s:v:1 title=second -metadata:s:v:1 comment=Illustration -metadata:s:v:2 title=third -metadata:s:v:2 comment=Conductor -metadata:s:v:3 title=fourth -metadata:s:v:3 comment=Composer" "-map 0 -c copy -t 0.1" "" "-show_entries format_tags:stream_tags:stream_disposition=attached_pic:stream=index,codec_name"

FATE_COVER_ART_REMUX-$(call ALLYES, FILE_PROTOCOL MP3_DEMUXER MJPEG_DECODER \
                                    SCALE_FILTER PNG_ENCODER BMP_ENCODER    \
                                    MP3_MUXER BMP_DECODER PNG_DECODER       \
                                    FRAMECRC_MUXER PIPE_PROTOCOL)           \
                       += fate-cover-art-mp3-id3v2-remux
fate-cover-art-mp3-id3v2-remux: CMD = transcode mp3 $(TARGET_SAMPLES)/exif/embedded_small.mp3 mp3 "-map 0 -map 0:v -map 0:v -c:a copy -filter:v:0 scale -filter:v:2 scale -c:v:0 bmp -c:v:1 copy -c:v:2 png -metadata:s:v:0 comment=Band/Orchestra" "-map 0 -c copy -t 0.1" "" "-show_entries stream_tags:stream_disposition=attached_pic:stream=index,codec_name"

# Also covers muxing and demuxing of nonstandard channel layouts into FLAC
# as well as the unorthodox multi_dim_quant option of the FLAC encoder.
FATE_COVER_ART_REMUX-$(call ALLYES, FILE_PROTOCOL MOV_DEMUXER OGG_DEMUXER   \
                                    ALAC_DECODER MJPEG_DECODER SCALE_FILTER \
                                    CHANNELMAP_FILTER ARESAMPLE_FILTER      \
                                    FLAC_ENCODER BMP_ENCODER PNG_ENCODER    \
                                    FLAC_MUXER FLAC_DEMUXER FLAC_DECODER    \
                                    FRAMECRC_MUXER PIPE_PROTOCOL)           \
                       += fate-cover-art-flac-remux
fate-cover-art-flac-remux: CMD = transcode mov $(TARGET_SAMPLES)/lossless-audio/inside.m4a flac "-map 0 -map 1:v -map 1:v -af channelmap=channel_layout=FL+FC,aresample -c:a flac -multi_dim_quant 1 -c:v:0 copy -metadata:s:v:0 comment=Illustration -metadata:s:v:0 title=OpenMusic  -filter:v:1 scale -c:v:1 png -metadata:s:v:1 title=landscape -c:v:2 copy -filter:v:3 scale -metadata:s:v:2 title=portrait -c:v:3 bmp  -metadata:s:v:3 comment=Conductor -c:v:4 copy -t 0.4" "-map 0 -map 0:a -c:a:0 copy -c:v copy" "" "-show_entries format_tags:stream_tags:stream_disposition=attached_pic:stream=index,codec_name" "-f ogg -i $(TARGET_SAMPLES)/cover_art/ogg_vorbiscomment_cover.opus"

FCA_TEMP-$(call ALLYES, RAWVIDEO_MUXER FILE_PROTOCOL) = $(FATE_COVER_ART-yes)
FATE_COVER_ART = $(FCA_TEMP-yes)
$(FATE_COVER_ART): CMP = oneline

FATE_SAMPLES_AVCONV += $(FATE_COVER_ART)
FATE_SAMPLES_FFMPEG_FFPROBE += $(FATE_COVER_ART_REMUX-yes)
fate-cover-art: $(FATE_COVER_ART) $(FATE_COVER_ART_REMUX-yes)
