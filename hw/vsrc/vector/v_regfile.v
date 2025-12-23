//负责从register读写数据

`include "v_defines.v"

module v_regfile (
    input                           clk,
    input                           rst,

    input                           vwb_en_i,
    input       [`VREG_ADDR_BUS]    vwb_addr_i,
    input       [`VREG_BUS]         vwb_data_i,

    input                           vs1_en_i,
    input       [`VREG_ADDR_BUS]    vs1_addr_i,
    output reg  [`VREG_BUS]         vs1_data_o,

    input                           vs2_en_i,
    input       [`VREG_ADDR_BUS]    vs2_addr_i,
    output reg  [`VREG_BUS]         vs2_data_o
);

    integer i;
    reg [`VREG_BUS] vregfile [0:(1<<5)-1]; // 32 regs

    // write + reset
    always @(posedge clk) begin
        if (rst) begin
            for (i = 0; i < (1<<5); i = i + 1) begin
                vregfile[i] <= {`VREG_WIDTH{1'b0}};
            end
        end else begin
            if (vwb_en_i && (vwb_addr_i != 5'd0)) begin
                vregfile[vwb_addr_i] <= vwb_data_i;
            end
        end
    end

    // read port 1 (combinational)
    always @(*) begin
        if (rst) begin
            vs1_data_o = {`VREG_WIDTH{1'b0}};
        end else if (vs1_en_i) begin
            vs1_data_o = vregfile[vs1_addr_i];
        end else begin
            vs1_data_o = {`VREG_WIDTH{1'b0}};
        end
    end

    // read port 2 (combinational)
    always @(*) begin
        if (rst) begin
            vs2_data_o = {`VREG_WIDTH{1'b0}};
        end else if (vs2_en_i) begin
            vs2_data_o = vregfile[vs2_addr_i];
        end else begin
            vs2_data_o = {`VREG_WIDTH{1'b0}};
        end
    end

endmodule
