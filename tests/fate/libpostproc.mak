FATE_LIBPOSTPROC += fate-blocktest
fate-blocktest: libpostproc/tests/blocktest$(EXESUF)
fate-blocktest: CMD = run libpostproc/tests/blocktest$(EXESUF)

FATE-$(CONFIG_POSTPROC) += $(FATE_LIBPOSTPROC)
fate-libpostproc: $(FATE_LIBPOSTPROC)
