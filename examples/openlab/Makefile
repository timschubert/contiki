# Forward to subdirectories

# all subdirectories
TARGETS = $(shell find . -maxdepth 1 -type d ! -name .)
all:

%:
	for i in $(TARGETS); do\
		make -C $$i $@ || exit $$? ;\
	done
