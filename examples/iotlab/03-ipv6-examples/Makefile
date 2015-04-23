-include Makefile.target

APPLIS = rpl-dag-root rpl-node-info rpl-border-router

all: $(addprefix build-, $(APPLIS))
clean: distclean
distclean: $(addprefix distclean-,$(APPLIS))

build-%:
	make -C $* TARGET=$(TARGET)

distclean-%:
	make -C $* TARGET=$(TARGET) distclean

.PHONY: all clean distclean clean-% build-%
