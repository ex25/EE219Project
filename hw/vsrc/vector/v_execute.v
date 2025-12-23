//进行计算操作的实际执行。

`include "v_defines.v"

module v_execute (
    input                      clk,
    input                      rst,
    input [`ALU_OP_BUS]        valu_opcode_i,
    input [`VREG_BUS]          operand_v1_i,
    input [`VREG_BUS]          operand_v2_i,
    output reg [`VREG_BUS]     valu_result_o
);

integer i;

always @(*) begin
    if (rst) begin
        valu_result_o = {`VREG_WIDTH{1'b0}};
    end else begin
        // 默认清零，避免综合出锁存
        valu_result_o = {`VREG_WIDTH{1'b0}};
        case (valu_opcode_i)
            `VALU_OP_NOP: begin
                valu_result_o = {`VREG_WIDTH{1'b0}};
            end

            `VALU_OP_ADD: begin
                for (i = 0; i < `VLMAX; i = i + 1) begin
                    valu_result_o[i*`SEW +: `SEW] =
                        operand_v1_i[i*`SEW +: `SEW] + operand_v2_i[i*`SEW +: `SEW];
                end
            end

            `VALU_OP_MUL: begin
                for (i = 0; i < `VLMAX; i = i + 1) begin
                    valu_result_o[i*`SEW +: `SEW] =
                        operand_v1_i[i*`SEW +: `SEW] * operand_v2_i[i*`SEW +: `SEW];
                end
            end

            default: begin
                valu_result_o = {`VREG_WIDTH{1'b0}};
            end
        endcase
    end
end

endmodule
