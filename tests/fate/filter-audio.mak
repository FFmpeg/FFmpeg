FATE_AFILTER-$(call FILTERDEMDECENCMUX, ADELAY, WAV, PCM_S16LE, PCM_S16LE, WAV) += fate-filter-adelay
fate-filter-adelay: tests/data/asynth-44100-2.wav
fate-filter-adelay: SRC = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
fate-filter-adelay: CMD = framecrc -i $(SRC) -af adelay=42

FATE_AFILTER-$(call FILTERDEMDECENCMUX, AECHO, WAV, PCM_S16LE, PCM_S16LE, WAV) += fate-filter-aecho
fate-filter-aecho: tests/data/asynth-44100-2.wav
fate-filter-aecho: SRC = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
fate-filter-aecho: CMD = framecrc -i $(SRC) -af aecho=0.5:0.5:32:0.5

FATE_FILTER_AEMPHASIS += fate-filter-aemphasis-50fm
fate-filter-aemphasis-50fm: tests/data/asynth-44100-2.wav
fate-filter-aemphasis-50fm: SRC = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
fate-filter-aemphasis-50fm: CMD = framecrc -i $(SRC) -af aemphasis=1:5:reproduction:50fm

FATE_FILTER_AEMPHASIS += fate-filter-aemphasis-75kf
fate-filter-aemphasis-75kf: tests/data/asynth-44100-2.wav
fate-filter-aemphasis-75kf: SRC = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
fate-filter-aemphasis-75kf: CMD = framecrc -i $(SRC) -af aemphasis=2:8:reproduction:75kf

FATE_AFILTER-$(call FILTERDEMDECENCMUX, AEMPHASIS, WAV, PCM_S16LE, PCM_S16LE, WAV) += $(FATE_FILTER_AEMPHASIS)

FATE_FILTER_AFADE += fate-filter-afade-qsin
fate-filter-afade-qsin: tests/data/asynth-44100-2.wav
fate-filter-afade-qsin: SRC = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
fate-filter-afade-qsin: CMD = framecrc -i $(SRC) -af afade=t=in:ss=0:d=2:curve=qsin

FATE_FILTER_AFADE += fate-filter-afade-iqsin
fate-filter-afade-iqsin: tests/data/asynth-44100-2.wav
fate-filter-afade-iqsin: SRC = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
fate-filter-afade-iqsin: CMD = framecrc -i $(SRC) -af afade=t=in:ss=0:d=2:curve=iqsin

FATE_FILTER_AFADE += fate-filter-afade-esin
fate-filter-afade-esin: tests/data/asynth-44100-2.wav
fate-filter-afade-esin: SRC = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
fate-filter-afade-esin: CMD = framecrc -i $(SRC) -af afade=t=in:ss=0:d=2:curve=esin

FATE_FILTER_AFADE += fate-filter-afade-hsin
fate-filter-afade-hsin: tests/data/asynth-44100-2.wav
fate-filter-afade-hsin: SRC = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
fate-filter-afade-hsin: CMD = framecrc -i $(SRC) -af afade=t=in:ss=0:d=2:curve=hsin

FATE_FILTER_AFADE += fate-filter-afade-exp
fate-filter-afade-exp: tests/data/asynth-44100-2.wav
fate-filter-afade-exp: SRC = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
fate-filter-afade-exp: CMD = framecrc -i $(SRC) -af afade=t=in:ss=0:d=2:curve=exp

FATE_FILTER_AFADE += fate-filter-afade-log
fate-filter-afade-log: tests/data/asynth-44100-2.wav
fate-filter-afade-log: SRC = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
fate-filter-afade-log: CMD = framecrc -i $(SRC) -af afade=t=in:ss=1:d=2.5:curve=log

FATE_AFILTER-$(call FILTERDEMDECENCMUX, AFADE, WAV, PCM_S16LE, PCM_S16LE, WAV) += $(FATE_FILTER_AFADE)

FATE_AFILTER_SAMPLES-$(call FILTERDEMDECENCMUX, ACROSSFADE, WAV, PCM_S16LE, PCM_S16LE, WAV) += fate-filter-acrossfade
fate-filter-acrossfade: tests/data/asynth-44100-2.wav
fate-filter-acrossfade: SRC = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
fate-filter-acrossfade: SRC2 = $(TARGET_SAMPLES)/audio-reference/luckynight_2ch_44kHz_s16.wav
fate-filter-acrossfade: CMD = framecrc -i $(SRC) -i $(SRC2) -filter_complex acrossfade=d=2:c1=log:c2=exp

FATE_AFILTER-$(call FILTERDEMDECENCMUX, AFADE, WAV, PCM_S16LE, PCM_S16LE, WAV) += fate-filter-agate
fate-filter-agate: tests/data/asynth-44100-2.wav
fate-filter-agate: SRC = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
fate-filter-agate: CMD = framecrc -i $(SRC) -af agate=level_in=10:range=0:threshold=1:ratio=1:attack=1:knee=1:makeup=4

FATE_AFILTER-$(call FILTERDEMDECENCMUX, AFADE, WAV, PCM_S16LE, PCM_S16LE, WAV) += fate-filter-alimiter
fate-filter-alimiter: tests/data/asynth-44100-2.wav
fate-filter-alimiter: SRC = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
fate-filter-alimiter: CMD = framecrc -i $(SRC) -af alimiter=level_in=1:level_out=2:limit=0.2

FATE_AFILTER-$(call FILTERDEMDECENCMUX, AMERGE, WAV, PCM_S16LE, PCM_S16LE, WAV) += fate-filter-amerge
fate-filter-amerge: tests/data/asynth-44100-1.wav
fate-filter-amerge: SRC = $(TARGET_PATH)/tests/data/asynth-44100-1.wav
fate-filter-amerge: CMD = framecrc -i $(SRC) -i $(SRC) -filter_complex "[0:a][1:a]amerge=inputs=2[aout]" -map "[aout]"

FATE_AFILTER-$(call FILTERDEMDECENCMUX, APAD, WAV, PCM_S16LE, PCM_S16LE, WAV) += fate-filter-apad
fate-filter-apad: tests/data/asynth-44100-2.wav
fate-filter-apad: SRC = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
fate-filter-apad: CMD = framecrc -i $(SRC) -af apad=pad_len=10

FATE_AFILTER-$(call FILTERDEMDECENCMUX, ANEQUALIZER, WAV, PCM_S16LE, PCM_S16LE, WAV) += fate-filter-anequalizer
fate-filter-anequalizer: tests/data/asynth-44100-2.wav
fate-filter-anequalizer: tests/data/filtergraphs/anequalizer
fate-filter-anequalizer: SRC = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
fate-filter-anequalizer: CMD = framecrc -i $(SRC) -filter_complex_script $(TARGET_PATH)/tests/data/filtergraphs/anequalizer

FATE_AFILTER-$(call FILTERDEMDECENCMUX, ASETNSAMPLES, WAV, PCM_S16LE, PCM_S16LE, WAV) += fate-filter-asetnsamples-pad
fate-filter-asetnsamples-pad: tests/data/asynth-44100-2.wav
fate-filter-asetnsamples-pad: SRC = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
fate-filter-asetnsamples-pad: CMD = framecrc -i $(SRC) -af asetnsamples=512:p=1

FATE_AFILTER-$(call FILTERDEMDECENCMUX, ASETNSAMPLES, WAV, PCM_S16LE, PCM_S16LE, WAV) += fate-filter-asetnsamples-nopad
fate-filter-asetnsamples-nopad: tests/data/asynth-44100-2.wav
fate-filter-asetnsamples-nopad: SRC = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
fate-filter-asetnsamples-nopad: CMD = framecrc -i $(SRC) -af asetnsamples=512:p=0

FATE_AFILTER-$(call FILTERDEMDECENCMUX, ASETRATE, WAV, PCM_S16LE, PCM_S16LE, WAV) += fate-filter-asetrate
fate-filter-asetrate: tests/data/asynth-44100-2.wav
fate-filter-asetrate: SRC = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
fate-filter-asetrate: CMD = framecrc -i $(SRC) -frames:a 20 -af asetrate=20000

FATE_AFILTER-$(call FILTERDEMDECENCMUX, CHORUS, WAV, PCM_S16LE, PCM_S16LE, WAV) += fate-filter-chorus
fate-filter-chorus: tests/data/asynth-22050-1.wav
fate-filter-chorus: SRC = $(TARGET_PATH)/tests/data/asynth-22050-1.wav
fate-filter-chorus: CMD = framecrc -i $(SRC) -frames:a 10 -af chorus=0.050001:0.050002:64:0.050001:0.025003:2.00004

FATE_AFILTER-$(call FILTERDEMDECENCMUX, DCSHIFT, WAV, PCM_S16LE, PCM_S16LE, WAV) += fate-filter-dcshift
fate-filter-dcshift: tests/data/asynth-44100-2.wav
fate-filter-dcshift: SRC = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
fate-filter-dcshift: CMD = framecrc -i $(SRC) -frames:a 20 -af dcshift=shift=0.25:limitergain=0.05

FATE_AFILTER-$(call FILTERDEMDECENCMUX, EARWAX, WAV, PCM_S16LE, PCM_S16LE, WAV) += fate-filter-earwax
fate-filter-earwax: tests/data/asynth-44100-2.wav
fate-filter-earwax: SRC = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
fate-filter-earwax: CMD = framecrc -i $(SRC) -frames:a 20 -af earwax

FATE_AFILTER-$(call FILTERDEMDECENCMUX, EXTRASTEREO, WAV, PCM_S16LE, PCM_S16LE, WAV) += fate-filter-extrastereo
fate-filter-extrastereo: tests/data/asynth-44100-2.wav
fate-filter-extrastereo: SRC = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
fate-filter-extrastereo: CMD = framecrc -i $(SRC) -frames:a 20 -af extrastereo=m=2

FATE_AFILTER-$(call FILTERDEMDECENCMUX, FIREQUALIZER ATRIM VOLUME, WAV, PCM_S16LE, PCM_S16LE, WAV) += fate-filter-firequalizer
fate-filter-firequalizer: tests/data/asynth-44100-2.wav
fate-filter-firequalizer: tests/data/filtergraphs/firequalizer
fate-filter-firequalizer: REF = tests/data/asynth-44100-2.wav
fate-filter-firequalizer: CMD = ffmpeg -i $(TARGET_PATH)/tests/data/asynth-44100-2.wav -filter_script $(TARGET_PATH)/tests/data/filtergraphs/firequalizer -f wav -c:a pcm_s16le -
fate-filter-firequalizer: CMP = oneoff
fate-filter-firequalizer: CMP_UNIT = s16
fate-filter-firequalizer: SIZE_TOLERANCE = 1058400 - 1097208

FATE_AFILTER-$(call FILTERDEMDECENCMUX, PAN, WAV, PCM_S16LE, PCM_S16LE, WAV) += fate-filter-pan-mono1
fate-filter-pan-mono1: tests/data/asynth-44100-2.wav
fate-filter-pan-mono1: SRC = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
fate-filter-pan-mono1: CMD = framecrc -ss 3.14 -i $(SRC) -frames:a 20 -filter:a "pan=mono|FC=FL"

FATE_AFILTER-$(call FILTERDEMDECENCMUX, PAN, WAV, PCM_S16LE, PCM_S16LE, WAV) += fate-filter-pan-mono2
fate-filter-pan-mono2: tests/data/asynth-44100-2.wav
fate-filter-pan-mono2: SRC = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
fate-filter-pan-mono2: CMD = framecrc -ss 3.14 -i $(SRC) -frames:a 20 -filter:a "pan=1C|c0=c0+c1"

FATE_AFILTER-$(call FILTERDEMDECENCMUX, PAN, WAV, PCM_S16LE, PCM_S16LE, WAV) += fate-filter-pan-stereo1
fate-filter-pan-stereo1: tests/data/asynth-44100-3.wav
fate-filter-pan-stereo1: SRC = $(TARGET_PATH)/tests/data/asynth-44100-3.wav
fate-filter-pan-stereo1: CMD = framecrc -ss 3.14 -i $(SRC) -frames:a 20 -filter:a "pan=2c|FL=FR|FR=FL"

FATE_AFILTER-$(call FILTERDEMDECENCMUX, PAN, WAV, PCM_S16LE, PCM_S16LE, WAV) += fate-filter-pan-stereo2
fate-filter-pan-stereo2: tests/data/asynth-44100-3.wav
fate-filter-pan-stereo2: SRC = $(TARGET_PATH)/tests/data/asynth-44100-3.wav
fate-filter-pan-stereo2: CMD = framecrc -ss 3.14 -i $(SRC) -frames:a 20 -filter:a "pan=stereo|c0=c0-c2|c1=c1-c2"

FATE_AFILTER-$(call FILTERDEMDECENCMUX, PAN, WAV, PCM_S16LE, PCM_S16LE, WAV) += fate-filter-pan-stereo3
fate-filter-pan-stereo3: tests/data/asynth-44100-2.wav
fate-filter-pan-stereo3: SRC = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
fate-filter-pan-stereo3: CMD = framecrc -ss 3.14 -i $(SRC) -frames:a 20 -filter:a "pan=FL+FR|FL<3*c0+2*c1|FR<2*c0+3*c1"

FATE_AFILTER-$(call FILTERDEMDECENCMUX, PAN, WAV, PCM_S16LE, PCM_S16LE, WAV) += fate-filter-pan-stereo4
fate-filter-pan-stereo4: tests/data/asynth-44100-2.wav
fate-filter-pan-stereo4: SRC = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
fate-filter-pan-stereo4: CMD = framecrc -ss 3.14 -guess_layout_max 0 -i $(SRC) -frames:a 20 -filter:a "pan=2C|c0=c0-0.5*c1|c1=c1+0.5*c0"

FATE_AFILTER-$(call FILTERDEMDECENCMUX, PAN, WAV, PCM_S16LE, PCM_S16LE, WAV) += fate-filter-pan-upmix1
fate-filter-pan-upmix1: tests/data/asynth-44100-2.wav
fate-filter-pan-upmix1: SRC = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
fate-filter-pan-upmix1: CMD = framecrc -ss 3.14 -guess_layout_max 0 -i $(SRC) -frames:a 20 -filter:a "pan=4C|c0=c0-0.5*c1|c1=c1+0.5*c0|c2=0*c0|c3=0*c0"

FATE_AFILTER-$(call FILTERDEMDECENCMUX, PAN, WAV, PCM_S16LE, PCM_S16LE, WAV) += fate-filter-pan-upmix2
fate-filter-pan-upmix2: tests/data/asynth-44100-4.wav
fate-filter-pan-upmix2: SRC = $(TARGET_PATH)/tests/data/asynth-44100-4.wav
fate-filter-pan-upmix2: CMD = framecrc -ss 3.14 -i $(SRC) -frames:a 20 -filter:a "pan=9C|c0=c0-c1|c1=c2+c3|c2=c0+c1|c3=c2-c3|c4=c1-c0|c5=c3+c2|c6=c1+c0|c7=c3-c2|c8=c0-c3"

FATE_AFILTER-$(call FILTERDEMDECENCMUX, PAN, WAV, PCM_S16LE, PCM_S16LE, WAV) += fate-filter-pan-downmix1
fate-filter-pan-downmix1: tests/data/asynth-44100-4.wav
fate-filter-pan-downmix1: SRC = $(TARGET_PATH)/tests/data/asynth-44100-4.wav
fate-filter-pan-downmix1: CMD = framecrc -ss 3.14 -i $(SRC) -frames:a 20 -filter:a "pan=2c|FL<FL+0.5*FC+0.6*BL+0.6*SL|FR<FR+0.5*FC+0.6*BR+0.6*SR"

FATE_AFILTER-$(call FILTERDEMDECENCMUX, PAN, WAV, PCM_S16LE, PCM_S16LE, WAV) += fate-filter-pan-downmix2
fate-filter-pan-downmix2: tests/data/asynth-44100-11.wav
fate-filter-pan-downmix2: SRC = $(TARGET_PATH)/tests/data/asynth-44100-11.wav
fate-filter-pan-downmix2: CMD = framecrc -ss 3.14 -i $(SRC) -frames:a 20 -filter:a "pan=5C|c0=0.7*c0+0.7*c10|c1=c9|c2=c8|c3=c7|c4=c6"

FATE_AFILTER_SAMPLES-$(call FILTERDEMDECENCMUX, SILENCEREMOVE, WAV, PCM_S16LE, PCM_S16LE, WAV) += fate-filter-silenceremove
fate-filter-silenceremove: SRC = $(TARGET_SAMPLES)/audio-reference/divertimenti_2ch_96kHz_s24.wav
fate-filter-silenceremove: CMD = framecrc -i $(SRC) -frames:a 30 -af silenceremove=start_periods=0:start_duration=0:start_threshold=0:stop_periods=-1:stop_duration=0:stop_threshold=-90dB

FATE_AFILTER_SAMPLES-$(call FILTERDEMDECENCMUX, STEREOTOOLS, WAV, PCM_S16LE, PCM_S16LE, WAV) += fate-filter-stereotools
fate-filter-stereotools: SRC = $(TARGET_SAMPLES)/audio-reference/luckynight_2ch_44kHz_s16.wav
fate-filter-stereotools: CMD = framecrc -i $(SRC) -frames:a 20 -af stereotools=mlev=0.015625

FATE_AFILTER-$(call FILTERDEMDECENCMUX, TREMOLO, WAV, PCM_S16LE, PCM_S16LE, WAV) += fate-filter-tremolo
fate-filter-tremolo: tests/data/asynth-44100-2.wav
fate-filter-tremolo: SRC = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
fate-filter-tremolo: CMD = framecrc -i $(SRC) -frames:a 20 -af tremolo

FATE_AFILTER-$(call FILTERDEMDECENCMUX, COMPAND, WAV, PCM_S16LE, PCM_S16LE, WAV) += fate-filter-compand
fate-filter-compand: tests/data/asynth-44100-2.wav
fate-filter-compand: tests/data/filtergraphs/compand
fate-filter-compand: SRC = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
fate-filter-compand: CMD = framecrc -i $(SRC) -frames:a 20 -filter_complex_script $(TARGET_PATH)/tests/data/filtergraphs/compand

tests/data/hls-list.m3u8: TAG = GEN
tests/data/hls-list.m3u8: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< \
        -f lavfi -i "aevalsrc=cos(2*PI*t)*sin(2*PI*(440+4*t)*t):d=20" -f segment -segment_time 10 -map 0 -flags +bitexact -codec:a mp2fixed \
        -segment_list $(TARGET_PATH)/$@ -y $(TARGET_PATH)/tests/data/hls-out-%03d.ts 2>/dev/null

FATE_AFILTER-$(call ALLYES, HLS_DEMUXER MPEGTS_MUXER MPEGTS_DEMUXER AEVALSRC_FILTER LAVFI_INDEV MP2FIXED_ENCODER) += fate-filter-hls
fate-filter-hls: tests/data/hls-list.m3u8
fate-filter-hls: CMD = framecrc -flags +bitexact -i $(TARGET_PATH)/tests/data/hls-list.m3u8

tests/data/hls-list-append.m3u8: TAG = GEN
tests/data/hls-list-append.m3u8: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< \
        -f lavfi -i "aevalsrc=cos(2*PI*t)*sin(2*PI*(440+4*t)*t):d=20" -f segment -segment_time 10 -map 0 -flags +bitexact -codec:a mp2fixed \
        -segment_list $(TARGET_PATH)/$@ -y $(TARGET_PATH)/tests/data/hls-append-out-%03d.ts 2>/dev/null; \
        $(TARGET_EXEC) $(TARGET_PATH)/$< \
        -f lavfi -i "aevalsrc=cos(2*PI*t)*sin(2*PI*(440+4*t)*t):d=20" -f hls -hls_time 10 -map 0 -flags +bitexact \
        -hls_flags append_list -codec:a mp2fixed -hls_segment_filename $(TARGET_PATH)/tests/data/hls-append-out-%03d.ts \
        $(TARGET_PATH)/tests/data/hls-list-append.m3u8 2>/dev/null

FATE_AFILTER-$(call ALLYES, HLS_DEMUXER MPEGTS_MUXER MPEGTS_DEMUXER AEVALSRC_FILTER LAVFI_INDEV MP2FIXED_ENCODER) += fate-filter-hls-append
fate-filter-hls-append: tests/data/hls-list-append.m3u8
fate-filter-hls-append: CMD = framecrc -flags +bitexact -i $(TARGET_PATH)/tests/data/hls-list-append.m3u8 -af asetpts=N*23

FATE_AMIX += fate-filter-amix-simple
fate-filter-amix-simple: CMD = ffmpeg -filter_complex amix -i $(SRC) -ss 3 -i $(SRC1) -f f32le -
fate-filter-amix-simple: REF = $(SAMPLES)/filter/amix_simple.pcm

FATE_AMIX += fate-filter-amix-first
fate-filter-amix-first: CMD = ffmpeg -filter_complex amix=duration=first -ss 4 -i $(SRC) -i $(SRC1) -f f32le -
fate-filter-amix-first: REF = $(SAMPLES)/filter/amix_first.pcm

FATE_AMIX += fate-filter-amix-transition
fate-filter-amix-transition: tests/data/asynth-44100-2-3.wav
fate-filter-amix-transition: SRC2 = $(TARGET_PATH)/tests/data/asynth-44100-2-3.wav
fate-filter-amix-transition: CMD = ffmpeg -filter_complex amix=inputs=3:dropout_transition=0.5 -i $(SRC) -ss 2 -i $(SRC1) -ss 4 -i $(SRC2) -f f32le -
fate-filter-amix-transition: REF = $(SAMPLES)/filter/amix_transition.pcm

FATE_AFILTER_SAMPLES-$(call FILTERDEMDECENCMUX, AMIX, WAV, PCM_S16LE, PCM_F32LE, PCM_F32LE) += $(FATE_AMIX)
$(FATE_AMIX): tests/data/asynth-44100-2.wav tests/data/asynth-44100-2-2.wav
$(FATE_AMIX): SRC  = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
$(FATE_AMIX): SRC1 = $(TARGET_PATH)/tests/data/asynth-44100-2-2.wav
$(FATE_AMIX): CMP  = oneoff
$(FATE_AMIX): CMP_UNIT = f32

FATE_AFILTER_SAMPLES-$(CONFIG_ARESAMPLE_FILTER) += fate-filter-aresample
fate-filter-aresample: SRC = $(TARGET_SAMPLES)/nellymoser/nellymoser-discont.flv
fate-filter-aresample: CMD = pcm -analyzeduration 10000000 -i $(SRC) -af aresample=min_comp=0.001:min_hard_comp=0.1:first_pts=0
fate-filter-aresample: CMP = oneoff
fate-filter-aresample: REF = $(SAMPLES)/nellymoser/nellymoser-discont.pcm

FATE_ATRIM += fate-filter-atrim-duration
fate-filter-atrim-duration: CMD = framecrc -i $(SRC) -af atrim=start=0.1:duration=0.01
FATE_ATRIM += fate-filter-atrim-mixed
fate-filter-atrim-mixed: CMD = framecrc -i $(SRC) -af atrim=start=0.05:start_sample=1025:end=0.1:end_sample=4411

FATE_ATRIM += fate-filter-atrim-samples
fate-filter-atrim-samples: CMD = framecrc -i $(SRC) -af atrim=start_sample=26:end_sample=80

FATE_ATRIM += fate-filter-atrim-time
fate-filter-atrim-time: CMD = framecrc -i $(SRC) -af atrim=0.1:0.2

$(FATE_ATRIM): tests/data/asynth-44100-2.wav
$(FATE_ATRIM): SRC = $(TARGET_PATH)/tests/data/asynth-44100-2.wav

FATE_AFILTER-$(call FILTERDEMDECENCMUX, ATRIM, WAV, PCM_S16LE, PCM_S16LE, WAV) += $(FATE_ATRIM)

FATE_FILTER_CHANNELMAP += fate-filter-channelmap-one-int
fate-filter-channelmap-one-int: tests/data/filtergraphs/channelmap_one_int
fate-filter-channelmap-one-int: SRC = $(TARGET_PATH)/tests/data/asynth-44100-6.wav
fate-filter-channelmap-one-int: tests/data/asynth-44100-6.wav
fate-filter-channelmap-one-int: CMD = md5 -i $(SRC) -filter_complex_script $(TARGET_PATH)/tests/data/filtergraphs/channelmap_one_int -f wav -fflags +bitexact
fate-filter-channelmap-one-int: CMP = oneline
fate-filter-channelmap-one-int: REF = 8cfe553d65ed4696756d8c1b824fcdd3

FATE_FILTER_CHANNELMAP += fate-filter-channelmap-one-str
fate-filter-channelmap-one-str: tests/data/filtergraphs/channelmap_one_str
fate-filter-channelmap-one-str: SRC = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
fate-filter-channelmap-one-str: tests/data/asynth-44100-2.wav
fate-filter-channelmap-one-str: CMD = md5 -i $(SRC) -filter_complex_script $(TARGET_PATH)/tests/data/filtergraphs/channelmap_one_str -f wav -fflags +bitexact
fate-filter-channelmap-one-str: CMP = oneline
fate-filter-channelmap-one-str: REF = 0ea3052e482c95d5d3bd9da6dac1b5fa

FATE_AFILTER-$(call FILTERDEMDECENCMUX, CHANNELMAP, WAV, PCM_S16LE, PCM_S16LE, WAV) += $(FATE_FILTER_CHANNELMAP)

FATE_AFILTER-$(call FILTERDEMDECENCMUX, CHANNELSPLIT, WAV, PCM_S16LE, PCM_S16LE, PCM_S16LE) += fate-filter-channelsplit
fate-filter-channelsplit: SRC = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
fate-filter-channelsplit: tests/data/asynth-44100-2.wav
fate-filter-channelsplit: CMD = md5 -i $(SRC) -filter_complex channelsplit -f s16le
fate-filter-channelsplit: CMP = oneline
fate-filter-channelsplit: REF = d92988d0fe2dd92236763f47b07ab597

FATE_AFILTER-$(call FILTERDEMDECENCMUX, JOIN, WAV, PCM_S16LE, PCM_S16LE, PCM_S16LE) += fate-filter-join
fate-filter-join: SRC1 = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
fate-filter-join: SRC2 = $(TARGET_PATH)/tests/data/asynth-44100-3.wav
fate-filter-join: tests/data/asynth-44100-2.wav tests/data/asynth-44100-3.wav
fate-filter-join: CMD = md5 -i $(SRC1) -i $(SRC2) -filter_complex join=channel_layout=5c -f s16le
fate-filter-join: CMP = oneline
fate-filter-join: REF = 88b0d24a64717ba8635b29e8dac6ecd8

FATE_AFILTER-$(call ALLYES, WAV_DEMUXER PCM_S16LE_DECODER PCM_S16LE_ENCODER PCM_S16LE_MUXER APERMS_FILTER VOLUME_FILTER) += fate-filter-volume
fate-filter-volume: SRC = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
fate-filter-volume: tests/data/asynth-44100-2.wav
fate-filter-volume: CMD = md5 -i $(SRC) -af aperms=random,volume=precision=fixed:volume=0.5 -f s16le
fate-filter-volume: CMP = oneline
fate-filter-volume: REF = 4d6ba75ef3e32d305d066b9bc771d6f4

# hdcd-mix.flac is a mix of three different sources which are interesting for various reasons:
# first 5 seconds uses packet format A and max LLE of -7.0db
# second 5 seconds uses packet format B and has a gain mismatch between channels
# last 10 seconds is not HDCD but has a coincidental HDCD packet, it needs to be 10 seconds because it also tests the cdt expiration
FATE_AFILTER_SAMPLES-$(call FILTERDEMDECENCMUX, HDCD, FLAC, FLAC, PCM_S24LE, PCM_S24LE) += fate-filter-hdcd-mix
fate-filter-hdcd-mix: SRC = $(TARGET_SAMPLES)/filter/hdcd-mix.flac
fate-filter-hdcd-mix: CMD = md5 -i $(SRC) -af hdcd -f s24le
fate-filter-hdcd-mix: CMP = oneline
fate-filter-hdcd-mix: REF = e7079913e90c124460cdbc712df5b84c

# output will be different because of the gain mismatch in the second and third parts
FATE_AFILTER_SAMPLES-$(call FILTERDEMDECENCMUX, HDCD, FLAC, FLAC, PCM_S24LE, PCM_S24LE) += fate-filter-hdcd-mix-psoff
fate-filter-hdcd-mix-psoff: SRC = $(TARGET_SAMPLES)/filter/hdcd-mix.flac
fate-filter-hdcd-mix-psoff: CMD = md5 -i $(SRC) -af hdcd=process_stereo=false -f s24le
fate-filter-hdcd-mix-psoff: CMP = oneline
fate-filter-hdcd-mix-psoff: REF = bd0e81fe17696b825ee3515ab928e6bb

# test the different analyze modes
FATE_AFILTER_SAMPLES-$(call FILTERDEMDECENCMUX, HDCD, FLAC, FLAC, PCM_S24LE, PCM_S24LE) += fate-filter-hdcd-analyze-pe
fate-filter-hdcd-analyze-pe: SRC = $(TARGET_SAMPLES)/filter/hdcd-mix.flac
fate-filter-hdcd-analyze-pe: CMD = md5 -i $(SRC) -af hdcd=analyze_mode=pe -f s24le
fate-filter-hdcd-analyze-pe: CMP = oneline
fate-filter-hdcd-analyze-pe: REF = bb83e97bbd0064b9b1c0ef2f2c8f0c77
FATE_AFILTER_SAMPLES-$(call FILTERDEMDECENCMUX, HDCD, FLAC, FLAC, PCM_S24LE, PCM_S24LE) += fate-filter-hdcd-analyze-lle
fate-filter-hdcd-analyze-lle: SRC = $(TARGET_SAMPLES)/filter/hdcd-mix.flac
fate-filter-hdcd-analyze-lle: CMD = md5 -i $(SRC) -af hdcd=analyze_mode=lle -f s24le
fate-filter-hdcd-analyze-lle: CMP = oneline
fate-filter-hdcd-analyze-lle: REF = 121cc4a681aa0caef5c664fece7a3ddc
FATE_AFILTER_SAMPLES-$(call FILTERDEMDECENCMUX, HDCD, FLAC, FLAC, PCM_S24LE, PCM_S24LE) += fate-filter-hdcd-analyze-cdt
fate-filter-hdcd-analyze-cdt: SRC = $(TARGET_SAMPLES)/filter/hdcd-mix.flac
fate-filter-hdcd-analyze-cdt: CMD = md5 -i $(SRC) -af hdcd=analyze_mode=cdt -f s24le
fate-filter-hdcd-analyze-cdt: CMP = oneline
fate-filter-hdcd-analyze-cdt: REF = 12136e6a00dd532994f6edcc347af1d4
FATE_AFILTER_SAMPLES-$(call FILTERDEMDECENCMUX, HDCD, FLAC, FLAC, PCM_S24LE, PCM_S24LE) += fate-filter-hdcd-analyze-tgm
fate-filter-hdcd-analyze-tgm: SRC = $(TARGET_SAMPLES)/filter/hdcd-mix.flac
fate-filter-hdcd-analyze-tgm: CMD = md5 -i $(SRC) -af hdcd=analyze_mode=tgm -f s24le
fate-filter-hdcd-analyze-tgm: CMP = oneline
fate-filter-hdcd-analyze-tgm: REF = a3c39f62e9b9b42c9c440d0045d5fb2f
# the two additional analyze modes from libhdcd
FATE_AFILTER_SAMPLES-$(call FILTERDEMDECENCMUX, HDCD, FLAC, FLAC, PCM_S24LE, PCM_S24LE) += fate-filter-hdcd-analyze-ltgm
fate-filter-hdcd-analyze-ltgm: SRC = $(TARGET_SAMPLES)/filter/hdcd-mix.flac
fate-filter-hdcd-analyze-ltgm: CMD = md5 -i $(SRC) -af hdcd=analyze_mode=lle:process_stereo=false -f s24le
fate-filter-hdcd-analyze-ltgm: CMP = oneline
fate-filter-hdcd-analyze-ltgm: REF = 76ffd86b762b5a93332039f27e4c0c0e
FATE_AFILTER_SAMPLES-$(call FILTERDEMDECENCMUX, HDCD, FLAC, FLAC, PCM_S24LE, PCM_S24LE) += fate-filter-hdcd-analyze-pel
fate-filter-hdcd-analyze-pel: SRC = $(TARGET_SAMPLES)/filter/hdcd-mix.flac
fate-filter-hdcd-analyze-pel: CMD = md5 -i $(SRC) -af hdcd=analyze_mode=pe:force_pe=true -f s24le
fate-filter-hdcd-analyze-pel: CMP = oneline
fate-filter-hdcd-analyze-pel: REF = 8156c5a3658d789ab46447d62151f5e9

FATE_AFILTER_SAMPLES-$(call FILTERDEMDECENCMUX, HDCD, FLAC, FLAC, PCM_S24LE, PCM_S24LE) += fate-filter-hdcd-false-positive
fate-filter-hdcd-false-positive: SRC = $(TARGET_SAMPLES)/filter/hdcd-false-positive.flac
fate-filter-hdcd-false-positive: CMD = md5 -i $(SRC) -af hdcd -f s24le
fate-filter-hdcd-false-positive: CMP = grep
fate-filter-hdcd-false-positive: REF = HDCD detected: no

FATE_AFILTER_SAMPLES-$(call FILTERDEMDECENCMUX, HDCD, FLAC, FLAC, PCM_S24LE, PCM_S24LE) += fate-filter-hdcd-detect-errors
fate-filter-hdcd-detect-errors: SRC = $(TARGET_SAMPLES)/filter/hdcd-encoding-errors.flac
fate-filter-hdcd-detect-errors: CMD = md5 -i $(SRC) -af hdcd -f s24le
fate-filter-hdcd-detect-errors: CMP = grep
fate-filter-hdcd-detect-errors: REF = detectable errors: [1-9]

# 20bit HDCD
FATE_AFILTER_SAMPLES-$(call FILTERDEMDECENCMUX, HDCD, FLAC, FLAC, PCM_S32LE, PCM_S32LE) += fate-filter-hdcd-20bit
fate-filter-hdcd-20bit: SRC = $(TARGET_SAMPLES)/filter/hdcd-fake20bit.flac
fate-filter-hdcd-20bit: CMD = md5 -i $(SRC) -af hdcd=bits_per_sample=20 -f s32le
fate-filter-hdcd-20bit: CMP = oneline
fate-filter-hdcd-20bit: REF = 365ded883a4a92483b15b69babc81390

# non-hdcd tests of different input formats for code coverage
FATE_AFILTER_SAMPLES-$(call FILTERDEMDECENCMUX, HDCD, WAV, PCM_S16LE, PCM_S24LE, PCM_S24LE) += fate-filter-hdcd-mono
fate-filter-hdcd-mono: SRC = $(TARGET_SAMPLES)/audiomatch/tones_44100_mono.wav
fate-filter-hdcd-mono: CMD = md5 -i $(SRC) -af hdcd -f s24le
fate-filter-hdcd-mono: CMP = oneline
fate-filter-hdcd-mono: REF = f51b114b20728e6a463a9491c643d166
FATE_AFILTER_SAMPLES-$(call FILTERDEMDECENCMUX, HDCD, WV, WAVPACK, PCM_S32LE, PCM_S32LE) += fate-filter-hdcd-s16p
fate-filter-hdcd-s16p: SRC = $(TARGET_SAMPLES)/wavpack/lossless/16bit-partial.wv
fate-filter-hdcd-s16p: CMD = md5 -i $(SRC) -af hdcd -f s32le
fate-filter-hdcd-s16p: CMP = oneline
fate-filter-hdcd-s16p: REF = 4e767f436b891ac59810a8b2b1d7e96b
FATE_AFILTER_SAMPLES-$(call FILTERDEMDECENCMUX, HDCD, WV, WAVPACK, PCM_S32LE, PCM_S32LE) += fate-filter-hdcd-s32p
fate-filter-hdcd-s32p: SRC = $(TARGET_SAMPLES)/wavpack/lossless/24bit-partial.wv
fate-filter-hdcd-s32p: CMD = md5 -i $(SRC) -af hdcd -f s32le
fate-filter-hdcd-s32p: CMP = oneline
fate-filter-hdcd-s32p: REF = 0c5513e83eedaa10ab6fac9ddc173cf5

FATE_AFILTER-yes += fate-filter-formats
fate-filter-formats: libavfilter/tests/formats$(EXESUF)
fate-filter-formats: CMD = run libavfilter/tests/formats

FATE_SAMPLES_AVCONV += $(FATE_AFILTER_SAMPLES-yes)
FATE_FFMPEG += $(FATE_AFILTER-yes)
fate-afilter: $(FATE_AFILTER-yes) $(FATE_AFILTER_SAMPLES-yes)
