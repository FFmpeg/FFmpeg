FATE_LIBPOSTPROC += fate-blocktest
fate-blocktest: libpostproc/tests/blocktest$(EXESUF)
fate-blocktest: CMD = run libpostproc/tests/blocktest$(EXESUF)

FATE_LIBPOSTPROC += fate-stripetest
fate-stripetest: libpostproc/tests/stripetest$(EXESUF)
fate-stripetest: CMD = run libpostproc/tests/stripetest$(EXESUF)

FATE_LIBPOSTPROC += fate-temptest
fate-temptest: libpostproc/tests/temptest$(EXESUF)
fate-temptest: CMD = run libpostproc/tests/temptest$(EXESUF)

FATE-$(CONFIG_POSTPROC) += $(FATE_LIBPOSTPROC)
fate-libpostproc: $(FATE_LIBPOSTPROC)
