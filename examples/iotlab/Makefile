# Forward to subdirectories

# all subdirectories
SUBFOLDERS = $(shell find . -maxdepth 1 -type d ! -name .)

ifneq (iotlab-a8-m3,$(TARGET))
TARGETS = $(SUBFOLDERS)
else
TARGETS = $(filter-out ./06-shell-cfs-elfloader,$(SUBFOLDERS))
endif

all:
%:
	for i in $(TARGETS); do\
		make -C $$i $@ || exit $$? ;\
	done
