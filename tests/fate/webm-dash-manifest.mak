FATE_WEBM_DASH_MANIFEST += fate-webm-dash-manifest
fate-webm-dash-manifest: CMD = run $(FFMPEG) -nostdin -f webm_dash_manifest -i $(TARGET_SAMPLES)/vp8/dash_video1.webm -f webm_dash_manifest -i $(TARGET_SAMPLES)/vp8/dash_video2.webm -f webm_dash_manifest -i $(TARGET_SAMPLES)/vp8/dash_audio1.webm -f webm_dash_manifest -i $(TARGET_SAMPLES)/vp8/dash_audio2.webm -c copy -map 0 -map 1 -map 2 -map 3 -f webm_dash_manifest -adaptation_sets "id=0,streams=0,1 id=1,streams=2,3" -

FATE_WEBM_DASH_MANIFEST += fate-webm-dash-manifest-unaligned-video-streams
fate-webm-dash-manifest-unaligned-video-streams: CMD = run $(FFMPEG) -nostdin -f webm_dash_manifest -i $(TARGET_SAMPLES)/vp8/dash_video1.webm -f webm_dash_manifest -i $(TARGET_SAMPLES)/vp8/dash_video3.webm -c copy -map 0 -map 1 -f webm_dash_manifest -adaptation_sets "id=0,streams=0,1" -

FATE_WEBM_DASH_MANIFEST += fate-webm-dash-manifest-unaligned-audio-streams
fate-webm-dash-manifest-unaligned-audio-streams: CMD = run $(FFMPEG) -nostdin -f webm_dash_manifest -i $(TARGET_SAMPLES)/vp8/dash_audio1.webm -f webm_dash_manifest -i $(TARGET_SAMPLES)/vp8/dash_audio3.webm -c copy -map 0 -map 1 -f webm_dash_manifest -adaptation_sets "id=0,streams=0,1" -

FATE_WEBM_DASH_MANIFEST += fate-webm-dash-manifest-representations
fate-webm-dash-manifest-representations: CMD = run $(FFMPEG) -nostdin -f webm_dash_manifest -i $(TARGET_SAMPLES)/vp8/dash_video1.webm -f webm_dash_manifest -i $(TARGET_SAMPLES)/vp8/dash_video4.webm -c copy -map 0 -map 1 -f webm_dash_manifest -adaptation_sets "id=0,streams=0,1" -

FATE_WEBM_DASH_MANIFEST += fate-webm-dash-manifest-live
fate-webm-dash-manifest-live: CMD = run $(FFMPEG) -nostdin -f webm_dash_manifest -live 1 -i $(TARGET_SAMPLES)/vp8/dash_live_video_360.hdr -f webm_dash_manifest -live 1 -i $(TARGET_SAMPLES)/vp8/dash_live_audio_171.hdr -c copy -map 0 -map 1 -fflags +bitexact -f webm_dash_manifest -live 1 -adaptation_sets "id=0,streams=0 id=1,streams=1" -chunk_start_index 1 -chunk_duration_ms 5000 -time_shift_buffer_depth 7200 -minimum_update_period 60 -

FATE_WEBM_DASH_MANIFEST += fate-webm-dash-manifest-live-bandwidth
fate-webm-dash-manifest-live-bandwidth: CMD = run $(FFMPEG) -nostdin -f webm_dash_manifest -live 1 -bandwidth 100 -i $(TARGET_SAMPLES)/vp8/dash_live_video_360.hdr -f webm_dash_manifest -live 1 -bandwidth 200 -i $(TARGET_SAMPLES)/vp8/dash_live_audio_171.hdr -c copy -map 0 -map 1 -fflags +bitexact -f webm_dash_manifest -live 1 -adaptation_sets "id=0,streams=0 id=1,streams=1" -chunk_start_index 1 -chunk_duration_ms 5000 -time_shift_buffer_depth 7200 -minimum_update_period 60 -

FATE_WEBM_DASH_MANIFEST-$(call DEMMUX, WEBM_DASH_MANIFEST, WEBM_DASH_MANIFEST, PIPE_PROTOCOL) += $(FATE_WEBM_DASH_MANIFEST)
fate-webm-dash-manifests: $(FATE_WEBM_DASH_MANIFEST-yes)
FATE_SAMPLES_FFMPEG += $(FATE_WEBM_DASH_MANIFEST-yes)
