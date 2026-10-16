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

FATE_SAMPLES_FFMPEG-$(call DEMMUX, MOV, MOV, PRORES_METADATA_BSF) += $(FATE_PRORES_METADATA_BSF)
FATE_SAMPLES_FFPROBE += $(FATE_PRORES_FFPROBE-yes)
