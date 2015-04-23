-include Makefile.target

RIME_FOLDER = ../../rime
RIME_EXAMPLES += example-abc example-mesh example-collect example-trickle
RIME_EXAMPLES += example-polite example-rucb example-runicast example-unicast
RIME_EXAMPLES += example-neighbors
ifeq (iotlab-m3,$(TARGET))
RIME_EXAMPLES += example-rudolph0 example-rudolph1 example-rudolph2
endif

TARGET_EXAMPLES = $(addsuffix .$(TARGET), $(RIME_EXAMPLES))


all: $(TARGET_EXAMPLES)

%.${TARGET}: ${RIME_FOLDER}/%.${TARGET}
	mv $^ $@

${RIME_FOLDER}/%:
	make -C ${RIME_FOLDER} TARGET=${TARGET} $*


clean distclean: %:
	rm -f $(TARGET_EXAMPLES)
	make -C ${RIME_FOLDER} TARGET=${TARGET} $@

