FATE_SAMPLES_AUDIO-$(call TRANSCODE, APTX, APTX, WAV_DEMUXER PCM_S16LE_DECODER ARESAMPLE_FILTER) += fate-aptx
fate-aptx: CMD = transcode wav $(TARGET_SAMPLES)/audio-reference/luckynight_2ch_44kHz_s16.wav aptx "-af aresample -c aptx" "-af aresample -c:a pcm_s16le -t 0.25" "" "" "-f aptx -sample_rate 44100"

FATE_SAMPLES_AUDIO-$(call TRANSCODE, APTX_HD, APTX_HD, WAV_DEMUXER PCM_S16LE_DECODER \
                          ARESAMPLE_FILTER PCM_S32LE_ENCODER) += fate-aptx-hd
fate-aptx-hd: CMD = transcode wav $(TARGET_SAMPLES)/audio-reference/luckynight_2ch_44kHz_s16.wav aptx_hd "-af aresample -c aptx_hd" "-af aresample -c:a pcm_s32le -t 0.25" "" "" "-f aptx_hd -sample_rate 44100"

FATE_BINKAUDIO-$(call DEMDEC, BINK, BINKAUDIO_DCT, ARESAMPLE_FILTER) += fate-binkaudio-dct
fate-binkaudio-dct: CMD = pcm -i $(TARGET_SAMPLES)/bink/binkaudio_dct.bik
fate-binkaudio-dct: REF = $(SAMPLES)/bink/binkaudio_dct.pcm
fate-binkaudio-dct: FUZZ = 2

FATE_BINKAUDIO-$(call DEMDEC, BINK, BINKAUDIO_RDFT, ARESAMPLE_FILTER) += fate-binkaudio-rdft
fate-binkaudio-rdft: CMD = pcm -i $(TARGET_SAMPLES)/bink/binkaudio_rdft.bik
fate-binkaudio-rdft: REF = $(SAMPLES)/bink/binkaudio_rdft.pcm
fate-binkaudio-rdft: FUZZ = 2

$(FATE_BINKAUDIO-yes): CMP = oneoff

FATE_SAMPLES_AUDIO += $(FATE_BINKAUDIO-yes)
fate-binkaudio: $(FATE_BINKAUDIO-yes)

FATE_SAMPLES_AUDIO-$(call DEMDEC, BMV, BMV_AUDIO) += fate-bmv-audio
fate-bmv-audio: CMD = framecrc -i $(TARGET_SAMPLES)/bmv/SURFING-partial.BMV -vn

FATE_SAMPLES_AUDIO-$(call DEMDEC, DSICIN, DSICINAUDIO) += fate-delphine-cin-audio
fate-delphine-cin-audio: CMD = framecrc -i $(TARGET_SAMPLES)/delphine-cin/LOGO-partial.CIN -vn

FATE_SAMPLES_AUDIO-$(call DEMDEC, S337M, DOLBY_E, ARESAMPLE_FILTER) += fate-dolby-e
fate-dolby-e: CMD = pcm -i $(TARGET_SAMPLES)/dolby_e/16-11
fate-dolby-e: CMP = oneoff
fate-dolby-e: REF = $(SAMPLES)/dolby_e/16-11.pcm

FATE_SAMPLES_AUDIO-$(call DEMDEC, DSS, DSS_SP, ARESAMPLE_FILTER) += fate-dss-lp
fate-dss-lp: CMD = framecrc -i $(TARGET_SAMPLES)/dss/lp.dss -frames 30 -af aresample

FATE_SAMPLES_AUDIO-$(call DEMDEC, DSS, DSS_SP) += fate-dss-sp
fate-dss-sp: CMD = framecrc -i $(TARGET_SAMPLES)/dss/sp.dss -frames 30

FATE_SAMPLES_AUDIO-$(call DEMDEC, DSF, DST, ARESAMPLE_FILTER) += fate-dsf-dst
fate-dsf-dst: CMD = pcm -i $(TARGET_SAMPLES)/dst/dst-64fs44-2ch.dff
fate-dsf-dst: CMP = oneoff
fate-dsf-dst: REF = $(SAMPLES)/dst/dst-64fs44-2ch.pcm

FATE_SAMPLES_AUDIO-$(call DEMDEC, AVI, IMC, ARESAMPLE_FILTER) += fate-imc
fate-imc: CMD = pcm -i $(TARGET_SAMPLES)/imc/imc.avi
fate-imc: CMP = oneoff
fate-imc: CMP_TARGET = 59416
fate-imc: REF = $(SAMPLES)/imc/imc-201706.pcm

FATE_SAMPLES_AUDIO-$(call DEMDEC, WAV, MSNSIREN, ARESAMPLE_FILTER) += fate-msnsiren
fate-msnsiren: CMD = pcm -i $(TARGET_SAMPLES)/msnsiren/msnsiren2.wav
fate-msnsiren: CMP = oneoff
fate-msnsiren: REF = $(SAMPLES)/msnsiren/msnsiren2.pcm

FATE_SAMPLES_AUDIO-$(call DEMDEC, FLV, NELLYMOSER, ARESAMPLE_FILTER) += fate-nellymoser
fate-nellymoser: CMD = pcm -i $(TARGET_SAMPLES)/nellymoser/nellymoser.flv
fate-nellymoser: CMP = oneoff
fate-nellymoser: REF = $(SAMPLES)/nellymoser/nellymoser.pcm

FATE_SAMPLES_AUDIO-$(call ENCMUX, NELLYMOSER, FLV, ARESAMPLE_FILTER) += fate-nellymoser-aref-encode
fate-nellymoser-aref-encode: $(AREF) ./tests/data/asynth-16000-1.wav
fate-nellymoser-aref-encode: CMD = enc_dec_pcm flv wav s16le $(REF) -c:a nellymoser
fate-nellymoser-aref-encode: CMP = stddev
fate-nellymoser-aref-encode: REF = ./tests/data/asynth-16000-1.wav
fate-nellymoser-aref-encode: CMP_SHIFT = -256
fate-nellymoser-aref-encode: CMP_TARGET = 3863
fate-nellymoser-aref-encode: SIZE_TOLERANCE = 268

FATE_SAMPLES_AUDIO-$(call DEMDEC, AVI, ON2AVC, ARESAMPLE_FILTER) += fate-on2avc
fate-on2avc: CMD = framecrc -i $(TARGET_SAMPLES)/vp7/potter-40.vp7 -frames 30 -vn -af aresample

FATE_SAMPLES_AUDIO-$(call DEMDEC, PAF, PAF_AUDIO) += fate-paf-audio
fate-paf-audio: CMD = framecrc -i $(TARGET_SAMPLES)/paf/hod1-partial.paf -vn

FATE_SAMPLES_AUDIO-$(call DEMDEC, VMD, VMDAUDIO, ARESAMPLE_FILTER) += fate-sierra-vmd-audio
fate-sierra-vmd-audio: CMD = framecrc -i $(TARGET_SAMPLES)/vmd/12.vmd -vn -af aresample

FATE_SAMPLES_AUDIO-$(call DEMDEC, SMACKER, SMACKAUD, ARESAMPLE_FILTER) += fate-smacker-audio
fate-smacker-audio: CMD = framecrc -i $(TARGET_SAMPLES)/smacker/wetlogo.smk -vn -af aresample

FATE_SAMPLES_AUDIO-$(call DEMDEC, WSVQA, WS_SND1, ARESAMPLE_FILTER) += fate-ws_snd
fate-ws_snd: CMD = md5 -i $(TARGET_SAMPLES)/vqa/ws_snd.vqa -f s16le -af aresample

FATE_SAMPLES_AUDIO_FFPROBE-$(call DEMDEC, WAV, WMAV2, FILE_PROTOCOL) += fate-flcl1905
fate-flcl1905: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_frames -show_packets -print_format compact $(TARGET_SAMPLES)/wav/FLCL_Ending_My-short.wav

FATE_SAMPLES_AUDIO += $(FATE_SAMPLES_AUDIO-yes)
FATE_SAMPLES_AUDIO_FFPROBE += $(FATE_SAMPLES_AUDIO_FFPROBE-yes)

FATE_SAMPLES_FFMPEG += $(FATE_SAMPLES_AUDIO)
FATE_SAMPLES_FFPROBE += $(FATE_SAMPLES_AUDIO_FFPROBE)
fate-audio: $(FATE_SAMPLES_AUDIO) $(FATE_SAMPLES_AUDIO_FFPROBE)
