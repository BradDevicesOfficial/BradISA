# Makefile for BradISA V1 FPGA Builds

# Tools
VIVADO   ?= vivado
QUARTUS  ?= quartus_sh
GHDL     ?= ghdl
VERILATOR ?= verilator
YOSYS    ?= yosys
IVERILOG ?= iverilog
VVP      ?= vvp

# Directories
VERILOG_DIR = ../verilog
VHDL_DIR    = ../vhdl
FPGA_DIR    = .
GOWIN_DIR   = tangnano9k

# Sources (Verilog)
VERILOG_SRCS = \
    $(VERILOG_DIR)/bradisa_defines.v \
    $(VERILOG_DIR)/brad_regfile.v \
    $(VERILOG_DIR)/brad_alu.v \
    $(VERILOG_DIR)/brad_core.v \
    $(VERILOG_DIR)/tb_brad_core.v

# Sources (VHDL)
VHDL_SRCS = \
    $(VHDL_DIR)/bradisa_pkg.vhd \
    $(VHDL_DIR)/brad_regfile.vhd \
    $(VHDL_DIR)/brad_alu.vhd \
    $(VHDL_DIR)/brad_core.vhd \
    $(VHDL_DIR)/tb_brad_core.vhd

.PHONY: all vivado quartus gowin sim_board lint_verilog lint_vhdl clean

all: lint_verilog lint_vhdl

# ─── Vivado (Xilinx) ───────────────────────────────────────────
vivado:
	cd $(FPGA_DIR) && $(VIVADO) -mode batch -source vivado_build.tcl

# ─── Quartus (Intel) ───────────────────────────────────────────
quartus:
	cd $(FPGA_DIR) && $(QUARTUS) -t quartus_build.tcl

# ─── Gowin: Tang Nano 9K (GW1NR-9C) ───────────────────────────
# Synthesises the board top onto Gowin primitives and reports real
# cell counts.  This is a synthesis receipt, not a bitstream: GW1N
# place-and-route needs nextpnr-himbaechel, Apicula, or Gowin EDA,
# and "runs on real gates" needs the board in hand.  See
# tangnano9k/README.md for the resource findings, including why the
# 32x32 MUL currently stops this design from fitting the part.
gowin:
	cd $(FPGA_DIR)/$(GOWIN_DIR) && $(YOSYS) -s synth_gowin.ys

# ─── Board-level simulation: the boot program drives the LEDs ──
# The shipped board runs one core cycle per 0.5 s, which is right for
# hardware and unfinishable in simulation, so -DBRAD_FAST_SIM shrinks
# the clock divider.  Only the divider constant changes.
sim_board:
	$(IVERILOG) -g2012 -DBRAD_FAST_SIM -I$(VERILOG_DIR) \
	  -o /tmp/brad_top_sim.out -s tb_top_tangnano9k \
	  $(FPGA_DIR)/$(GOWIN_DIR)/tb_top_tangnano9k.v \
	  $(FPGA_DIR)/$(GOWIN_DIR)/top_tangnano9k.v \
	  $(VERILOG_DIR)/bradisa_defines.v \
	  $(VERILOG_DIR)/brad_regfile.v \
	  $(VERILOG_DIR)/brad_alu.v \
	  $(VERILOG_DIR)/brad_core.v
	$(VVP) /tmp/brad_top_sim.out

# ─── Verilator lint ────────────────────────────────────────────
lint_verilog:
	$(VERILATOR) --lint-only -Wall $(VERILOG_SRCS)

# ─── GHDL lint / analyse ──────────────────────────────────────
lint_vhdl:
	cd $(VHDL_DIR) && $(GHDL) -s --std=08 bradisa_pkg.vhd brad_regfile.vhd brad_alu.vhd brad_core.vhd tb_brad_core.vhd

# ─── Clean ─────────────────────────────────────────────────────
clean:
	rm -rf $(FPGA_DIR)/brad_core $(FPGA_DIR)/*.rpt $(FPGA_DIR)/*.bit
	rm -rf $(GOWIN_DIR)/*.json $(GOWIN_DIR)/*.rpt
	rm -rf $(VHDL_DIR)/*.o $(VHDL_DIR)/*.cf $(VHDL_DIR)/work-obj08.cf
	rm -rf $(VERILOG_DIR)/*.o
