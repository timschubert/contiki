.PHONY: all clean-% build-%

ifndef TARGET
TARGET=iotlab-m3
endif

FIRWMARES = rpl-dag-root rpl-node-info
FILES = $(addsuffix .$(TARGET).elf, $(FIRWMARES))

all: $(FILES)


$(FILES): %.$(TARGET).elf: build-%
	cp $*/$*.$(TARGET)   $@



build-%:
	make -C $* TARGET=$(TARGET)

clean-%:
	make -C $* TARGET=$(TARGET) clean
	rm -f $*/$*.$(TARGET)

clean: $(addprefix clean-,$(FIRWMARES))
	rm -f *.elf



