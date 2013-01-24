FATE_MPC-$(CONFIG_MPC_DEMUXER) += fate-mpc7-demux
fate-mpc7-demux: CMD = crc -i $(SAMPLES)/musepack/inside-mp7.mpc -acodec copy

FATE_MPC-$(CONFIG_MPC8_DEMUXER) += fate-mpc8-demux
fate-mpc8-demux: CMD = crc -i $(SAMPLES)/musepack/inside-mp8.mpc -acodec copy

FATE_MPC-$(call DEMDEC, MPC, MPC7) += fate-musepack7
fate-musepack7: CMD = pcm -i $(SAMPLES)/musepack/inside-mp7.mpc
fate-musepack7: CMP = oneoff
fate-musepack7: REF = $(SAMPLES)/musepack/inside-mp7.pcm

FATE_SAMPLES_AVCONV += $(FATE_MPC-yes)
fate-mpc: $(FATE_MPC-yes)
