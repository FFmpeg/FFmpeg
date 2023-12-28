FATE_TRUEHD-$(call DEMDEC, TRUEHD, TRUEHD) += fate-truehd-5.1
fate-truehd-5.1: CMD = md5pipe -f truehd -i $(TARGET_SAMPLES)/lossless-audio/truehd_5.1.raw -f s32le
fate-truehd-5.1: CMP = oneline
fate-truehd-5.1: REF = 95d8aac39dd9f0d7fb83dc7b6f88df35

FATE_TRUEHD-$(call DEMDEC, TRUEHD, TRUEHD) += fate-truehd-5.1-downmix-2.0
fate-truehd-5.1-downmix-2.0: CMD = md5pipe -f truehd -downmix FL+FR -i $(TARGET_SAMPLES)/lossless-audio/truehd_5.1.raw -f s32le
fate-truehd-5.1-downmix-2.0: CMP = oneline
fate-truehd-5.1-downmix-2.0: REF = a269aee0051d4400c9117136f08c9767

FATE_TRUEHD-$(call ALLYES, TRUEHD_DEMUXER TRUEHD_MUXER TRUEHD_CORE_BSF) += fate-truehd-core-bsf
fate-truehd-core-bsf: CMD = md5pipe -i $(TARGET_SAMPLES)/truehd/atmos.thd -c:a copy -bsf:a truehd_core -fflags +bitexact -f truehd
fate-truehd-core-bsf: CMP = oneline
fate-truehd-core-bsf: REF = 3aa5d0c7825051f3657b71fd6135183b

FATE_TRUEHD-$(call DEMDEC, TRUEHD, TRUEHD) += fate-truehd-mono1726
fate-truehd-mono1726: CMD = md5pipe -f truehd -i $(TARGET_SAMPLES)/truehd/ticket-1726-monocut.thd -f s32le
fate-truehd-mono1726: CMP = oneline
fate-truehd-mono1726: REF = 9be9551fac418440bb02101bfdb11df9

FATE_SAMPLES_AUDIO += $(FATE_TRUEHD-yes)
fate-truehd: $(FATE_TRUEHD-yes)
