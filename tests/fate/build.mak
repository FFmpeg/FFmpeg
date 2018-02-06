FATE_BUILD += fate-build-alltools
fate-build-alltools: alltools

FATE_BUILD += fate-build-checkheaders
fate-build-checkheaders: checkheaders

FATE_BUILD += fate-build-examples
fate-build-examples: examples

FATE_BUILD += fate-build-testprogs
fate-build-testprogs: testprogs

$(FATE_BUILD): CMD = null
$(FATE_BUILD): CMP = null

# FATE += $(FATE_BUILD)
fate-build: $(FATE_BUILD)
