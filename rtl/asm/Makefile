# Makefile for the shared full-ISA program both testbenches execute.
#
# rtl/verilog/tb_brad_isa.v and rtl/vhdl/tb_brad_isa.vhd load
# tb_brad_isa_prog.mem, so the program they run is written once, in
# assembly, and the encodings the RTL sees are the ones bradasm produced.
# The .mem is checked in so the trees simulate without a C compiler; CI
# runs `make check`, which regenerates it and fails on a diff.

ASM        ?= ../../tools/bradasm/build/bradasm
BRADASM_DIR ?= ../../tools/bradasm

PROG_SRC = tb_brad_isa.s
PROG_MEM = tb_brad_isa_prog.mem

.PHONY: all prog check

all: $(PROG_MEM)

$(ASM):
	$(MAKE) -C $(BRADASM_DIR)

$(PROG_MEM): $(PROG_SRC) $(ASM)
	$(ASM) -m $@ $<

prog: $(PROG_MEM)
	@echo "$(PROG_MEM) regenerated"

# The CI gate: assemble to a temp file and fail unless it is byte-identical
# to the checked-in image.  No git involved, so it works before commit too.
check:
	$(ASM) -m /tmp/tb_brad_isa_prog.check.mem $(PROG_SRC)
	@cmp -s /tmp/tb_brad_isa_prog.check.mem $(PROG_MEM) || { \
	  echo "ERROR: $(PROG_MEM) is stale; re-run 'make prog' and commit"; \
	  rm -f /tmp/tb_brad_isa_prog.check.mem; \
	  exit 1; }
	@rm -f /tmp/tb_brad_isa_prog.check.mem
	@echo "$(PROG_MEM) matches $(PROG_SRC)"