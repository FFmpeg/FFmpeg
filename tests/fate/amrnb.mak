FATE_AMRNB += fate-amrnb-4k75
fate-amrnb-4k75: CMD = pcm -i $(TARGET_SAMPLES)/amrnb/4.75k.amr
fate-amrnb-4k75: REF = $(SAMPLES)/amrnb/4.75k.pcm

FATE_AMRNB += fate-amrnb-5k15
fate-amrnb-5k15: CMD = pcm -i $(TARGET_SAMPLES)/amrnb/5.15k.amr
fate-amrnb-5k15: REF = $(SAMPLES)/amrnb/5.15k.pcm

FATE_AMRNB += fate-amrnb-5k9
fate-amrnb-5k9: CMD = pcm -i $(TARGET_SAMPLES)/amrnb/5.9k.amr
fate-amrnb-5k9: REF = $(SAMPLES)/amrnb/5.9k.pcm

FATE_AMRNB += fate-amrnb-6k7
fate-amrnb-6k7: CMD = pcm -i $(TARGET_SAMPLES)/amrnb/6.7k.amr
fate-amrnb-6k7: REF = $(SAMPLES)/amrnb/6.7k.pcm

FATE_AMRNB += fate-amrnb-7k4
fate-amrnb-7k4: CMD = pcm -i $(TARGET_SAMPLES)/amrnb/7.4k.amr
fate-amrnb-7k4: REF = $(SAMPLES)/amrnb/7.4k.pcm

FATE_AMRNB += fate-amrnb-7k95
fate-amrnb-7k95: CMD = pcm -i $(TARGET_SAMPLES)/amrnb/7.95k.amr
fate-amrnb-7k95: REF = $(SAMPLES)/amrnb/7.95k.pcm

FATE_AMRNB += fate-amrnb-10k2
fate-amrnb-10k2: CMD = pcm -i $(TARGET_SAMPLES)/amrnb/10.2k.amr
fate-amrnb-10k2: REF = $(SAMPLES)/amrnb/10.2k.pcm

FATE_AMRNB += fate-amrnb-12k2
fate-amrnb-12k2: CMD = pcm -i $(TARGET_SAMPLES)/amrnb/12.2k.amr
fate-amrnb-12k2: REF = $(SAMPLES)/amrnb/12.2k.pcm

FATE_AMRNB_REMUX-$(call REMUX, AMR, AMR_PARSER ARESAMPLE_FILTER) += fate-amrnb-remux
fate-amrnb-remux: CMD = transcode amr $(TARGET_SAMPLES)/amrnb/10.2k.amr amr "-c copy" "-c copy -t 1"

AMRNB_DTX_DEPS = AMR_DEMUXER AMR_PARSER AMRNB_DECODER ARESAMPLE_FILTER \
                 ASTATS_FILTER ATRIM_FILTER AMIX_FILTER PCM_F32LE_ENCODER \
                 NULL_MUXER FILE_PROTOCOL

# Frame 18 of amrnb-sid-update.amr is the first speech frame after a SID update.
FATE_AMRNB_DTX-$(call ALLYES, $(AMRNB_DTX_DEPS)) += fate-amrnb-sid-update
fate-amrnb-sid-update: CMD = ffmpeg -i $(TARGET_PATH)/tests/data/fate/amrnb-sid-update.amr -af "atrim=start_sample=2880:end_sample=3040,aresample=osf=flt,astats=metadata=0:reset=0" -c:a pcm_f32le -f null -
fate-amrnb-sid-update: CMP = grep
fate-amrnb-sid-update: REF = RMS level dB: -56\.

# Frame 13 of amrnb-state-resume-update.amr is a louder SID_UPDATE while
# comfort noise is already being generated.
FATE_AMRNB_DTX-$(call ALLYES, $(AMRNB_DTX_DEPS)) += fate-amrnb-sid-update-during-dtx
fate-amrnb-sid-update-during-dtx: CMD = ffmpeg -i $(TARGET_PATH)/tests/data/fate/amrnb-state-resume-update.amr -af "atrim=start_sample=2080:end_sample=2240,aresample=osf=flt,astats=metadata=0:reset=0" -c:a pcm_f32le -f null -
fate-amrnb-sid-update-during-dtx: CMP = grep
fate-amrnb-sid-update-during-dtx: REF = RMS level dB: -79\.

# Frame 18 of amrnb-state-resume-update.amr follows a louder SID than the
# update that opened the silent period.
FATE_AMRNB_DTX-$(call ALLYES, $(AMRNB_DTX_DEPS)) += fate-amrnb-state-resume-update
fate-amrnb-state-resume-update: CMD = ffmpeg -i $(TARGET_PATH)/tests/data/fate/amrnb-state-resume-update.amr -af "atrim=start_sample=2880:end_sample=3040,aresample=osf=flt,astats=metadata=0:reset=0" -c:a pcm_f32le -f null -
fate-amrnb-state-resume-update: CMP = grep
fate-amrnb-state-resume-update: REF = RMS level dB: -53\.

# NO_DATA in speech state ignores Q. Frame 8 is Q=1, frame 17 is Q=0, and
# frame 26 is a reserved frame type.
FATE_AMRNB_DTX-$(call ALLYES, $(AMRNB_DTX_DEPS)) += fate-amrnb-nodata-ft15-q1 fate-amrnb-nodata-ft15-q0 fate-amrnb-nodata-reserved-q1
fate-amrnb-nodata-ft15-q1: CMD = ffmpeg -i $(TARGET_PATH)/tests/data/fate/amrnb-nodata-cases.amr -af "atrim=start_sample=1280:end_sample=1440,aresample=osf=flt,astats=metadata=0:reset=0" -c:a pcm_f32le -f null -
fate-amrnb-nodata-ft15-q1: CMP = grep
fate-amrnb-nodata-ft15-q1: REF = RMS level dB: -52\.
fate-amrnb-nodata-ft15-q0: CMD = ffmpeg -i $(TARGET_PATH)/tests/data/fate/amrnb-nodata-cases.amr -af "atrim=start_sample=2720:end_sample=2880,aresample=osf=flt,astats=metadata=0:reset=0" -c:a pcm_f32le -f null -
fate-amrnb-nodata-ft15-q0: CMP = grep
fate-amrnb-nodata-ft15-q0: REF = RMS level dB: -53\.
fate-amrnb-nodata-reserved-q1: CMD = ffmpeg -i $(TARGET_PATH)/tests/data/fate/amrnb-nodata-cases.amr -af "atrim=start_sample=4160:end_sample=4320,aresample=osf=flt,astats=metadata=0:reset=0" -c:a pcm_f32le -f null -
fate-amrnb-nodata-reserved-q1: CMP = grep
fate-amrnb-nodata-reserved-q1: REF = RMS level dB: -52\.

# A SID frame arriving after a lost frame must still update the comfort
# noise. Frame 10 is the first comfort-noise frame after that SID.
FATE_AMRNB_DTX-$(call ALLYES, $(AMRNB_DTX_DEPS)) += fate-amrnb-sid-after-nodata
fate-amrnb-sid-after-nodata: CMD = ffmpeg -i $(TARGET_PATH)/tests/data/fate/amrnb-sid-after-nodata.amr -af "atrim=start_sample=1600:end_sample=1760,aresample=osf=flt,astats=metadata=0:reset=0" -c:a pcm_f32le -f null -
fate-amrnb-sid-after-nodata: CMP = grep
fate-amrnb-sid-after-nodata: REF = RMS level dB: -70\.

# FT15 must decode identically for Q=0 and Q=1. The two files differ only
# in that bit; amrnb-nodata-cases.amr is the Q=1 input. amix keeps the sign
# of a weight only while normalize is on, so the two streams cancel.
FATE_AMRNB_DTX-$(call ALLYES, $(AMRNB_DTX_DEPS)) += fate-amrnb-nodata-q-equal
fate-amrnb-nodata-q-equal: CMD = ffmpeg -i $(TARGET_PATH)/tests/data/fate/amrnb-nodata-qswap-q0.amr -i $(TARGET_PATH)/tests/data/fate/amrnb-nodata-cases.amr -filter_complex "[0:a][1:a]amix=inputs=2:duration=longest:dropout_transition=0:normalize=1:weights=1|-1,aresample=osf=flt,astats=metadata=0:reset=0" -c:a pcm_f32le -f null -
fate-amrnb-nodata-q-equal: CMP = grep
fate-amrnb-nodata-q-equal: REF = RMS level dB: -inf

# A silent period keeps every frame, including one that runs long enough
# for the comfort noise to be muted before speech resumes.
FATE_AMRNB_DTX-$(call ALLYES, $(AMRNB_DTX_DEPS)) += fate-amrnb-dtx-duration
fate-amrnb-dtx-duration: CMD = ffmpeg -i $(TARGET_PATH)/tests/data/fate/amrnb-dtx-mute-resume.amr -af "aresample=osf=flt,astats=metadata=0:reset=0" -c:a pcm_f32le -f null -
fate-amrnb-dtx-duration: CMP = grep
fate-amrnb-dtx-duration: REF = Number of samples: 10400

# Frame 63 is the first speech frame after the comfort noise has been muted.
FATE_AMRNB_DTX-$(call ALLYES, $(AMRNB_DTX_DEPS)) += fate-amrnb-dtx-mute-resume
fate-amrnb-dtx-mute-resume: CMD = ffmpeg -i $(TARGET_PATH)/tests/data/fate/amrnb-dtx-mute-resume.amr -af "atrim=start_sample=10080:end_sample=10240,aresample=osf=flt,astats=metadata=0:reset=0" -c:a pcm_f32le -f null -
fate-amrnb-dtx-mute-resume: CMP = grep
fate-amrnb-dtx-mute-resume: REF = RMS level dB: -89\.

fate-amrnb-sid-update: tests/data/fate/amrnb-sid-update.amr
fate-amrnb-sid-update-during-dtx: tests/data/fate/amrnb-state-resume-update.amr
fate-amrnb-state-resume-update: tests/data/fate/amrnb-state-resume-update.amr
fate-amrnb-nodata-ft15-q1 fate-amrnb-nodata-ft15-q0 fate-amrnb-nodata-reserved-q1: tests/data/fate/amrnb-nodata-cases.amr
fate-amrnb-sid-after-nodata: tests/data/fate/amrnb-sid-after-nodata.amr
fate-amrnb-nodata-q-equal: tests/data/fate/amrnb-nodata-qswap-q0.amr tests/data/fate/amrnb-nodata-cases.amr
fate-amrnb-dtx-duration fate-amrnb-dtx-mute-resume: tests/data/fate/amrnb-dtx-mute-resume.amr

fate-amrnb: $(FATE_AMRNB_DTX-yes)
FATE_FFMPEG += $(FATE_AMRNB_DTX-yes)

$(FATE_AMRNB): CMP = stddev

FATE_SAMPLES_FFMPEG-$(call PCM, AMR, AMRNB, ARESAMPLE_FILTER) += $(FATE_AMRNB)
FATE_SAMPLES_FFMPEG += $(FATE_AMRNB_REMUX-yes)
fate-amrnb: $(FATE_AMRNB) $(FATE_AMRNB_REMUX-yes)
