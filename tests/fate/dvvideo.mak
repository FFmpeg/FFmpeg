
fate-bsf-dv-error-marker: CMD = md5 -i $(TARGET_SAMPLES)/dv/dvcprohd_720p50.mov -flags +bitexact -fflags +bitexact -c:v copy -bsf noise=100,dv_error_marker=color=blue -f avi
fate-bsf-dv-error-marker: CMP = oneline
fate-bsf-dv-error-marker: REF = 3190a334b1ceef2d9fd050a1590da7c6
FATE_DVVIDEO-$(call ALLYES, MOV_DEMUXER DV_ERROR_MARKER_BSF NOISE_BSF AVI_MUXER) += fate-bsf-dv-error-marker

FATE_SAMPLES_FFMPEG += $(FATE_DVVIDEO-yes)
fate-dvvideo: $(FATE_DVVIDEO-yes)

# Three workers give an odd-sized task ring; seven frames cross its wraparound.
FATE_DVVIDEO_SYNTH-$(call ALLYES, LAVFI_INDEV TESTSRC2_FILTER FORMAT_FILTER WRAPPED_AVFRAME_DECODER DVVIDEO_ENCODER FRAMECRC_MUXER PIPE_PROTOCOL FRAME_THREAD_ENCODER) += fate-dvvideo-720p50-frame-threads fate-dvvideo-720p60-frame-threads
fate-dvvideo-720p50-frame-threads: RATE = 50
fate-dvvideo-720p60-frame-threads: RATE = 60000/1001
fate-dvvideo-720p50-frame-threads fate-dvvideo-720p60-frame-threads: CMD = framecrc -f lavfi -i testsrc2=size=960x720:rate=$(RATE),format=yuv422p -frames:v 7 -c:v dvvideo -dct int -threads 3 -thread_type frame

FATE_FFMPEG += $(FATE_DVVIDEO_SYNTH-yes)
fate-dvvideo: $(FATE_DVVIDEO_SYNTH-yes)
