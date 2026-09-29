// Gate-level testbench for top_tangnano9k
//
// Drives the POST-SYNTHESIS Gowin netlist (produced by synth_gate.ys) against
// yosys's own behavioural models of the Gowin primitives.  The RTL testbench
// proves the design is right; this one proves the design survives being turned
// into gates -- it catches synthesis and mapping bugs that no RTL simulation
// can see.
//
// Only the port list survives synthesis, so this watches the LEDs and checks
// the value they show walks 1, 2, 3, ... -- which means the netlist's CPU ran
// the loop, incremented its counter by exactly one, published it through a
// real store, and the latch captured it.  Any of those stages breaking shows
// up as a skipped, repeated, or stuck LED value.
//
// One wrinkle worth knowing: yosys's Verilog backend emits the LED bus as six
// scalar escaped ports, so the bus is reassembled by hand below.  And because
// the pins are active-LOW, the number on display is ~led -- the pin pattern
// walks *down* while the counter walks up, which is an easy way to write a
// testbench that fails for the right-looking wrong reason.
//
// LEDs are active-LOW on the board, so the counter is ~led.

`timescale 1ns / 1ps

module tb_gl_tangnano9k;

    reg        sys_clk = 1'b0;
    reg        sys_rst_n = 1'b0;
    wire [5:0] led;

    // yosys's Verilog backend emits the LED bus as six scalar escaped ports,
    // so the bus is reassembled here rather than passed as a vector.
    wire led0, led1, led2, led3, led4, led5;
    assign led = {led5, led4, led3, led2, led1, led0};

    integer steps;        // observed LED value changes
    integer errors;       // sequence violations
    reg [5:0] last_shown;
    reg        have_last;

    always #18.5 sys_clk = ~sys_clk;

    top_tangnano9k dut (
        .sys_clk(sys_clk),
        .sys_rst_n(sys_rst_n),
        .\led[0] (led0),
        .\led[1] (led1),
        .\led[2] (led2),
        .\led[3] (led3),
        .\led[4] (led4),
        .\led[5] (led5)
    );

    // The pins are active-LOW, so the number the LEDs are displaying is the
    // inverse of the pin pattern.  Observe the number, not the pins.
    wire [5:0] counter = ~led;

    // The LED latch holds its value for several board clocks, so watching on
    // every board-clock edge and acting only on changes yields one sample per
    // published count.
    always @(posedge sys_clk) begin
        if (sys_rst_n && (counter !== last_shown)) begin
            if (have_last && (counter !== (last_shown + 6'd1)))
                errors = errors + 1;
            last_shown = counter;
            have_last  = 1'b1;
            steps      = steps + 1;
        end
    end

    initial begin
        steps      = 0;
        errors     = 0;
        have_last  = 1'b0;
        last_shown = 6'd63;   // reset display: led_r = 63, inverted onto pins
        #200;
        sys_rst_n = 1'b1;
        #200000;              // ~96 loop passes at the fast divider

        $display("LED steps seen    : %0d", steps);
        $display("sequence errors   : %0d", errors);
        $display("final LED count   : %0d (pins %b, 1 = lit: %b)",
                 counter, led, counter);
        if (errors == 0 && steps >= 8)
            $display("PASS");
        else
            $display("FAIL");
        $finish;
    end

endmodule
