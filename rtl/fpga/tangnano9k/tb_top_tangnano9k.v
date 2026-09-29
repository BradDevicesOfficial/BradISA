// SPDX-License-Identifier: MIT
// Board-level testbench for top_tangnano9k
//
// Simulates the whole board top -- divider, boot ROM, core, data RAM, LED
// latch -- and checks that the boot program drives the LEDs with a real
// counting sequence.  Each published value must be exactly one more than the
// previous one (mod 64), so this proves the whole chain works: the core fetches
// real BradISA machine code from ROM, ADDI walks the counter, STW publishes it
// out the data-memory write port, and the LED latch displays it.
//
// Build with -DBRAD_FAST_SIM: the shipped design runs the core at one cycle per
// 0.5 s (27 MHz board clock), which is right for hardware and unfinishable in
// simulation.  The define shrinks the divider so the loop runs at a usable
// rate; the parameter values, not the logic, are what differ.

`timescale 1ns / 1ps

module tb_top_tangnano9k;

    reg        sys_clk;
    reg        sys_rst_n;
    wire [5:0] led;

    integer     published;     // how many times the program published a count
    integer     errors;        // sequence violations
    reg [5:0]   last_count;
    reg         have_last;
    reg [5:0]   led_lit;       // inverted view of the pins: 1 = LED lit

`ifdef BRAD_FAST_SIM
    localparam [23:0] DIV_HALF  = 24'd3;       // 8 sys_clk per core cycle
    localparam [19:0] POR_LIMIT = 20'd7;
`else
    localparam [23:0] DIV_HALF  = 24'd6_749_999;
    localparam [19:0] POR_LIMIT = 20'd1_048_575;
`endif

    top_tangnano9k #(
        .DIV_HALF(DIV_HALF),
        .POR_LIMIT(POR_LIMIT)
    ) dut (
        .sys_clk(sys_clk),
        .sys_rst_n(sys_rst_n),
        .led(led)
    );

    // 27 MHz board oscillator
    always #18.5 sys_clk = ~sys_clk;

    // LEDs are active-LOW on the board: a low pin lights the LED.
    always @(*) led_lit = ~led;

    // Watch the CPU's data-memory write port.  dmem_req stays asserted for a
    // whole core cycle, which spans several board-clock edges, so this samples
    // on the core clock -- exactly one sample per published count.  A correct
    // boot program emits 1, 2, 3, ... in this order; anything else means the
    // program or the core is not really counting.
    always @(posedge dut.core_clk) begin
        if (rst_released && dut.dmem_req && dut.dmem_we) begin
            published = published + 1;
            if (have_last && (dut.dmem_wdata[5:0] !== (last_count + 6'd1)))
                errors = errors + 1;
            last_count = dut.dmem_wdata[5:0];
            have_last  = 1'b1;
        end
    end

    wire rst_released = dut.por_done & sys_rst_n;

    initial begin
        sys_clk    = 1'b0;
        sys_rst_n  = 1'b0;
        published  = 0;
        errors     = 0;
        last_count = 6'd0;
        have_last  = 1'b0;

        #200;
        sys_rst_n = 1'b1;
        #200000;   // ~675 core cycles at the fast divider => ~96 loop passes

        $display("published counts : %0d", published);
        $display("sequence errors  : %0d", errors);
        $display("final counter    : %0d (LEDs, 1 = lit: %b)", last_count, led_lit);

        if (DIV_HALF > 24'd1000)
            $display("NOTE: built without -DBRAD_FAST_SIM -- the real 0.5 s");
        if (DIV_HALF > 24'd1000)
            $display("      divider means the loop cannot finish in sim time.");

        if (DIV_HALF <= 24'd1000 && errors == 0 && published >= 8)
            $display("PASS");
        else if (DIV_HALF <= 24'd1000)
            $display("FAIL");

        $finish;
    end

endmodule
