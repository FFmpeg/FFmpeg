FATE_MPC-$(CONFIG_MPC_DEMUXER) += fate-mpc7-demux
fate-mpc7-demux: CMD = crc -i $(TARGET_SAMPLES)/musepack/inside-mp7.mpc -c:a copy

FATE_MPC-$(CONFIG_MPC8_DEMUXER) += fate-mpc8-demux
fate-mpc8-demux: CMD = crc -i $(TARGET_SAMPLES)/musepack/inside-mp8.mpc -c:a copy

FATE_MPC-$(call DEMDEC, MPC, MPC7, ARESAMPLE_FILTER) += fate-musepack7
fate-musepack7: CMD = pcm -i $(TARGET_SAMPLES)/musepack/inside-mp7.mpc
fate-musepack7: CMP = oneoff
fate-musepack7: REF = $(SAMPLES)/musepack/inside-mp7.pcm

FATE_MPC-$(call ALLYES, FILE_PROTOCOL MPC8_DEMUXER MPC8_DECODER  \
                        ARESAMPLE_FILTER PCM_S16LE_ENCODER  \
                        FRAMECRC_MUXER PIPE_PROTOCOL) += fate-musepack8
fate-musepack8: CMD = pcm -i $(TARGET_SAMPLES)/musepack/inside-mp8.mpc -ss 8.4 -af aresample
fate-musepack8: CMP = oneoff
fate-musepack8: REF = $(SAMPLES)/musepack/inside-mp8.pcm

FATE_SAMPLES_AVCONV += $(FATE_MPC-yes)
fate-mpc: $(FATE_MPC-yes)
