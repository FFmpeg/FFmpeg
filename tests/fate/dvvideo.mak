
fate-bsf-dv-error-marker: CMD = md5 -i $(TARGET_SAMPLES)/dv/dvcprohd_720p50.mov -flags +bitexact -fflags +bitexact -c:v copy -bsf noise=100,dv_error_marker=color=blue -f avi
fate-bsf-dv-error-marker: CMP = oneline
fate-bsf-dv-error-marker: REF = 4e884d3e8045299d0d45ec920fa58a44
FATE_DVVIDEO-$(call ALLYES, MOV_DEMUXER DV_ERROR_MARKER_BSF NOISE_BSF AVI_MUXER) += fate-bsf-dv-error-marker

FATE_SAMPLES_FFMPEG += $(FATE_DVVIDEO-yes)
fate-dvvideo: $(FATE_DVVIDEO-yes)
