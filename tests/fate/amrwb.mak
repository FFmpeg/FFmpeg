FATE_AMRWB += fate-amrwb-6k60
fate-amrwb-6k60: CMD = pcm -i $(TARGET_SAMPLES)/amrwb/seed-6k60.awb
fate-amrwb-6k60: REF = $(SAMPLES)/amrwb/seed-6k60.pcm

FATE_AMRWB += fate-amrwb-8k85
fate-amrwb-8k85: CMD = pcm -i $(TARGET_SAMPLES)/amrwb/seed-8k85.awb
fate-amrwb-8k85: REF = $(SAMPLES)/amrwb/seed-8k85.pcm

FATE_AMRWB += fate-amrwb-12k65
fate-amrwb-12k65: CMD = pcm -i $(TARGET_SAMPLES)/amrwb/seed-12k65.awb
fate-amrwb-12k65: REF = $(SAMPLES)/amrwb/seed-12k65.pcm

FATE_AMRWB += fate-amrwb-14k25
fate-amrwb-14k25: CMD = pcm -i $(TARGET_SAMPLES)/amrwb/seed-14k25.awb
fate-amrwb-14k25: REF = $(SAMPLES)/amrwb/seed-14k25.pcm
fate-amrwb-14k25: FUZZ = 2.6

FATE_AMRWB += fate-amrwb-15k85
fate-amrwb-15k85: CMD = pcm -i $(TARGET_SAMPLES)/amrwb/seed-15k85.awb
fate-amrwb-15k85: REF = $(SAMPLES)/amrwb/seed-15k85.pcm

FATE_AMRWB += fate-amrwb-18k25
fate-amrwb-18k25: CMD = pcm -i $(TARGET_SAMPLES)/amrwb/seed-18k25.awb
fate-amrwb-18k25: REF = $(SAMPLES)/amrwb/seed-18k25.pcm

FATE_AMRWB += fate-amrwb-19k85
fate-amrwb-19k85: CMD = pcm -i $(TARGET_SAMPLES)/amrwb/seed-19k85.awb
fate-amrwb-19k85: REF = $(SAMPLES)/amrwb/seed-19k85.pcm

FATE_AMRWB += fate-amrwb-23k05
fate-amrwb-23k05: CMD = pcm -i $(TARGET_SAMPLES)/amrwb/seed-23k05.awb
fate-amrwb-23k05: REF = $(SAMPLES)/amrwb/seed-23k05.pcm
fate-amrwb-23k05: FUZZ = 2

FATE_AMRWB += fate-amrwb-23k85
fate-amrwb-23k85: CMD = pcm -i $(TARGET_SAMPLES)/amrwb/seed-23k85.awb
fate-amrwb-23k85: REF = $(SAMPLES)/amrwb/seed-23k85.pcm
fate-amrwb-23k85: FUZZ = 2

FATE_AMRWB += fate-amrwb-23k85-2
fate-amrwb-23k85-2: CMD = pcm -i $(TARGET_SAMPLES)/amrwb/deus-23k85.awb
fate-amrwb-23k85-2: REF = $(SAMPLES)/amrwb/deus-23k85.pcm

FATE_AMRWB_REMUX-$(call REMUX, AMR, MOV_DEMUXER AMR_PARSER) += fate-amrwb-remux
fate-amrwb-remux: CMD = transcode mov $(TARGET_SAMPLES)/amrwb/seed-23k85.awb amr "-c copy" "-c copy -t 1"

$(FATE_AMRWB): CMP = stddev

# Check that SID frames are decoded to comfort noise instead of being dropped,
# so that a stream using DTX keeps its duration (see ticket #24921).
FATE_AMRWB_DTX-$(call ALLYES, AMR_DEMUXER AMR_PARSER AMRWB_DECODER ARESAMPLE_FILTER \
                              ASTATS_FILTER PCM_F32LE_ENCODER NULL_MUXER FILE_PROTOCOL) += fate-amrwb-dtx-duration
FATE_AMRWB_DTX-$(call ALLYES, AMR_DEMUXER AMR_PARSER AMRWB_DECODER ARESAMPLE_FILTER \
                              ASTATS_FILTER ATRIM_FILTER PCM_F32LE_ENCODER NULL_MUXER \
                              FILE_PROTOCOL) += fate-amrwb-dtx-cn
fate-amrwb-dtx-duration fate-amrwb-dtx-cn: tests/data/fate/amrwb-dtx.amr
fate-amrwb-dtx-duration: CMD = ffmpeg -i $(TARGET_PATH)/tests/data/fate/amrwb-dtx.amr -af "aresample=osf=flt,astats=metadata=0:reset=0" -c:a pcm_f32le -f null -
fate-amrwb-dtx-duration: CMP = grep
fate-amrwb-dtx-duration: REF = Number of samples: 3520
fate-amrwb-dtx-cn: CMD = ffmpeg -i $(TARGET_PATH)/tests/data/fate/amrwb-dtx.amr -af "atrim=start_sample=320:end_sample=3520,aresample=osf=flt,astats=metadata=0:reset=0" -c:a pcm_f32le -f null -
fate-amrwb-dtx-cn: CMP = grep
fate-amrwb-dtx-cn: REF = RMS level dB: -19\.

FATE_SAMPLES_FFMPEG-$(call PCM, MOV, AMRWB, ARESAMPLE_FILTER) += $(FATE_AMRWB)
FATE_FFMPEG += $(FATE_AMRWB_DTX-yes)
FATE_SAMPLES_FFMPEG += $(FATE_AMRWB_REMUX-yes)
fate-amrwb: $(FATE_AMRWB) $(FATE_AMRWB_REMUX-yes) $(FATE_AMRWB_DTX-yes)
