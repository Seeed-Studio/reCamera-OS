TOPDIR := $(shell pwd)
EXTERNAL = $(TOPDIR)/external
DEFCONFIGS = $(EXTERNAL)/configs

TARGETS := $(notdir $(patsubst %_defconfig, %, $(wildcard $(DEFCONFIGS)/*_defconfig)))

# Set O variable if not already done on the command line
ifneq ("$(origin OUTDIR)", "command line")
OUTDIR := $(TOPDIR)/output
else
override OUTDIR := $(TOPDIR)/$(OUTDIR)
endif

export TOPDIR EXTERNAL DEFCONFIGS OUTDIR

$(TARGETS): %:
	@echo "build $@"
	@bash $(EXTERNAL)/build.sh $@

clean:
	@rm -rf $(OUTDIR)
	@echo "clean finished"

help:
	@echo "Supported targets: $(TARGETS)"
	@echo "Run 'make <target>' to build a target image."
	@echo "Run 'make all' to build all target images."
	@echo "Run 'make clean' to clean the build output."
#	@echo "Run 'make <target>-config' to configure buildroot for a target."
