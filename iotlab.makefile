tests: clean compile_tests clean
all: tests


compile_tests: compile_tests.iotlab-m3 compile_tests.iotlab-a8-m3
compile_tests: compile_tests.openlab-fox compile_tests.openlab-newt
compile_tests: compile_tests.wsn430

compile_tests.%:
	@# build supported contiki examples
	make -s V=1 -C examples/$(shell echo $* | cut -d'-' -f1) TARGET=$*

compile_tests.wsn430:
	@# build some firmwares, not tested though for the moment
	@# CC1100 Not working for the moment
	@# make -s -C examples/hello-world TARGET=wsn430 WITH_CC1100=1 clean all
	make -s V=1 -C examples/hello-world TARGET=wsn430 WITH_CC2420=1 clean all

clean: clean.iotlab-m3 clean.iotlab-a8-m3
clean: clean.openlab-fox clean.openlab-newt
clean: clean.wsn430
clean.%:
	make -s V=1 -C examples/$(shell echo $* | cut -d'-' -f1) TARGET=$* distclean
clean.wsn430:
	make -s V=1 -C examples/hello-world TARGET=wsn430 distclean

.PHONY: tests compile_tests clean
