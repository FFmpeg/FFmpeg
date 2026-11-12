FATE_PRORES = fate-prores-422                                           \
              fate-prores-422_hq                                        \
              fate-prores-422_lt                                        \
              fate-prores-422_proxy                                     \
              fate-prores-alpha                                         \
              fate-prores-alpha_skip                                    \
              fate-prores-transparency                                  \
              fate-prores-transparency_skip                             \
              $(if $(call ALLYES, ARESAMPLE_FILTER AAC_FIXED_DECODER),fate-prores-gray) \

FATE_SAMPLES_FFMPEG-$(call FRAMECRC, MOV, PRORES, SCALE_FILTER) += $(FATE_PRORES)
fate-prores: $(FATE_PRORES)

fate-prores-422:       CMD = framecrc -flags +bitexact -i $(TARGET_SAMPLES)/prores/Sequence_1-Apple_ProRes_422.mov -pix_fmt yuv422p10le -vf scale
fate-prores-422_hq:    CMD = framecrc -flags +bitexact -i $(TARGET_SAMPLES)/prores/Sequence_1-Apple_ProRes_422_HQ.mov -pix_fmt yuv422p10le -vf scale
fate-prores-422_lt:    CMD = framecrc -flags +bitexact -i $(TARGET_SAMPLES)/prores/Sequence_1-Apple_ProRes_422_LT.mov -pix_fmt yuv422p10le -vf scale
fate-prores-422_proxy: CMD = framecrc -flags +bitexact -i $(TARGET_SAMPLES)/prores/Sequence_1-Apple_ProRes_422_Proxy.mov -pix_fmt yuv422p10le -vf scale
fate-prores-alpha:     CMD = framecrc -flags +bitexact -i $(TARGET_SAMPLES)/prores/Sequence_1-Apple_ProRes_with_Alpha.mov -pix_fmt yuva444p12le -vf scale
fate-prores-alpha_skip: CMD = framecrc -flags +bitexact -skip_alpha 1 -i $(TARGET_SAMPLES)/prores/Sequence_1-Apple_ProRes_with_Alpha.mov -pix_fmt yuv444p12le -vf scale
fate-prores-transparency: CMD = framecrc -flags +bitexact -i $(TARGET_SAMPLES)/prores/prores4444_with_transparency.mov -pix_fmt yuva444p12le -vf scale
fate-prores-transparency_skip: CMD = framecrc -flags +bitexact -skip_alpha 1 -i $(TARGET_SAMPLES)/prores/prores4444_with_transparency.mov -pix_fmt yuv444p12le -vf scale
fate-prores-gray:      CMD = framecrc -flags +bitexact -c:a aac_fixed -i $(TARGET_SAMPLES)/prores/gray.mov -pix_fmt yuv422p10le -vf scale -af aresample

#Test bsf prores-metadata
FATE_PRORES_METADATA_BSF += fate-prores-metadata
fate-prores-metadata: CMD = md5 -i $(TARGET_SAMPLES)/prores/Sequence_1-Apple_ProRes_422_Proxy.mov -c:v copy -bsf:v prores_metadata=color_primaries=bt470bg:color_trc=bt709:colorspace=smpte170m -bitexact -f mov

FATE_SAMPLES_FFMPEG_FFPROBE-$(call FRAMECRC, MOV, PRORES,) += fate-prores-probe
fate-prores-probe: CMD = stream_demux mov $(TARGET_SAMPLES)/prores/Sequence_1-Apple_ProRes_422_HQ.mov "" "-c:v copy" "-show_frames"

# A frame header can carry 0 for "unknown"; the colr tags must win over it
FATE_SAMPLES_FFMPEG_FFPROBE-$(call REMUX, MOV, PRORES_METADATA_BSF PRORES_DECODER) += fate-prores-unknown-color-tags
fate-prores-unknown-color-tags: CMD = stream_remux mov $(TARGET_SAMPLES)/prores/Sequence_1-Apple_ProRes_422_Proxy.mov "" mov \
    "-map 0:v -bsf:v prores_metadata=color_primaries=0:color_trc=0:colorspace=0 -color_primaries smpte170m -color_trc smpte170m -colorspace smpte170m" "" "-c:v copy" \
    "-select_streams v -read_intervals %+\#1 -show_entries frame=color_primaries,color_transfer,color_space -of default=nw=1"

# colr set to smpte170m, frame headers set to 1/1/1 then 0/0/0: the second frame must not inherit the first's tags
FATE_PRORES_FFPROBE-$(call DEMDEC, MOV, PRORES) += fate-prores-color-tags-fallback
fate-prores-color-tags-fallback: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -v error \
    -show_entries frame=color_primaries,color_transfer,color_space -of default=nw=1 \
    $(TARGET_SAMPLES)/prores/color-tags-fallback.mov

# Frame tags without a ProRes code get an equivalent code or unspecified in the frame header.
# The decoder is told the container tags are unspecified so the decoded tags come from the frame header.
PRORES_COLOR_TAGS_ENC = -frames:v 1 -vf scale,format=yuv422p10,setparams=color_primaries=smpte240m:color_trc=bt2020-10:colorspace=bt470bg
PRORES_COLOR_TAGS_PROBE = "-color_primaries unknown -color_trc unknown -colorspace unknown -show_entries frame=color_primaries,color_transfer,color_space -of default=nw=1"
PRORES_COLOR_TAGS_DEPS = RAWVIDEO_DEMUXER RAWVIDEO_DECODER SETPARAMS_FILTER FORMAT_FILTER SCALE_FILTER
FATE_PRORES_COLOR_TAGS-$(call TRANSCODE, PRORES_AW PRORES, MOV, $(PRORES_COLOR_TAGS_DEPS)) += fate-prores-aw-color-tags
fate-prores-aw-color-tags: CMD = transcode "rawvideo -s 352x288 -pix_fmt yuv420p" tests/data/vsynth1.yuv mov \
    "-c:v prores_aw $(PRORES_COLOR_TAGS_ENC)" "-c:v copy" $(PRORES_COLOR_TAGS_PROBE)
FATE_PRORES_COLOR_TAGS-$(call TRANSCODE, PRORES_KS PRORES, MOV, $(PRORES_COLOR_TAGS_DEPS)) += fate-prores-ks-color-tags
fate-prores-ks-color-tags: CMD = transcode "rawvideo -s 352x288 -pix_fmt yuv420p" tests/data/vsynth1.yuv mov \
    "-c:v prores_ks $(PRORES_COLOR_TAGS_ENC)" "-c:v copy" $(PRORES_COLOR_TAGS_PROBE)
$(FATE_PRORES_COLOR_TAGS-yes): tests/data/vsynth1.yuv
FATE_FFMPEG_FFPROBE += $(FATE_PRORES_COLOR_TAGS-yes)
fate-prores-color-tags: $(FATE_PRORES_COLOR_TAGS-yes)

FATE_SAMPLES_FFMPEG-$(call DEMMUX, MOV, MOV, PRORES_METADATA_BSF) += $(FATE_PRORES_METADATA_BSF)
FATE_SAMPLES_FFPROBE += $(FATE_PRORES_FFPROBE-yes)
