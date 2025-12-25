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
reg [`SEW-1:0] sum;
reg [`SEW-1:0] max;

always @(*) begin
    valu_result_o = {`VREG_WIDTH{1'b0}};
    sum = 0;
    max = 0;
    
    if (rst) begin
        valu_result_o = {`VREG_WIDTH{1'b0}};
    end else begin
        
        case (valu_opcode_i)
            `VALU_OP_NOP: begin
                valu_result_o = {`VREG_WIDTH{1'b0}};
            end

            `VALU_OP_VADD: begin
                for (i = 0; i < `VLMAX; i = i + 1) begin
                    valu_result_o[i*`SEW +: `SEW] =
                        operand_v2_i[i*`SEW +: `SEW] + operand_v1_i[i*`SEW +: `SEW];
                end
            end

            `VALU_OP_VMUL: begin
                for (i = 0; i < `VLMAX; i = i + 1) begin
                    valu_result_o[i*`SEW +: `SEW] =
                        $signed(operand_v2_i[i*`SEW +: `SEW]) * $signed(operand_v1_i[i*`SEW +: `SEW]);
                end
            end

            `VALU_OP_VSUB: begin
                for (i = 0; i < `VLMAX; i = i + 1) begin
                    valu_result_o[i*`SEW +: `SEW] =
                        operand_v2_i[i*`SEW +: `SEW] + ~operand_v1_i[i*`SEW +: `SEW] + 1;
                end
            end

            `VALU_OP_VDIV: begin
                for (i = 0; i < `VLMAX; i = i + 1) begin
                    valu_result_o[i*`SEW +: `SEW] =
                        $signed(operand_v2_i[i*`SEW +: `SEW]) / $signed(operand_v1_i[i*`SEW +: `SEW]);
                end
            end

            `VALU_OP_VMV_V_X: begin
                valu_result_o = operand_v1_i;
            end

            `VALU_OP_VMIN: begin
                for (i = 0; i < `VLMAX; i = i + 1) begin
                    valu_result_o[i*`SEW +: `SEW] =
                        ($signed(operand_v2_i[i*`SEW +: `SEW]) < $signed(operand_v1_i[i*`SEW +: `SEW])) ?
                        operand_v2_i[i*`SEW +: `SEW] : operand_v1_i[i*`SEW +: `SEW];
                end
            end

            `VALU_OP_VMAX: begin
                for (i = 0; i < `VLMAX; i = i + 1) begin
                    valu_result_o[i*`SEW +: `SEW] =
                        ($signed(operand_v2_i[i*`SEW +: `SEW]) > $signed(operand_v1_i[i*`SEW +: `SEW])) ?
                        operand_v2_i[i*`SEW +: `SEW] : operand_v1_i[i*`SEW +: `SEW];
                end
            end

            `VALU_OP_VSRA: begin
                for (i = 0; i < `VLMAX; i = i + 1) begin
                    valu_result_o[i*`SEW +: `SEW] =
                        $signed(operand_v2_i[i*`SEW +: `SEW]) >>> operand_v1_i[i*`SEW +: 5];
                end
            end

            `VALU_OP_VREDSUM_VS: begin
                // VREDSUM.VS: vd[0] = sum(vs2[i]) + vs1[0]
                sum = operand_v1_i[0 +: `SEW];  // 从vs1[0]开始
                for (i = 0; i < `VLMAX; i = i + 1) begin
                    sum = sum + operand_v2_i[i*`SEW +: `SEW];
                end
                valu_result_o[0 +: `SEW] = sum;
                // 其他元素保持为0
                for (i = 1; i < `VLMAX; i = i + 1) begin
                    valu_result_o[i*`SEW +: `SEW] = {`SEW{1'b0}};
                end
            end

            `VALU_OP_VREDMAX_VS: begin
                max = operand_v2_i[0 +: `SEW];
                for (i = 1; i < `VLMAX; i = i + 1) begin
                    if ($signed(operand_v2_i[i*`SEW +: `SEW]) > $signed(max)) begin
                        max = operand_v2_i[i*`SEW +: `SEW];
                    end
                end
                for (i = 0; i < `VLMAX; i = i + 1) begin
                    if ($signed(operand_v1_i[i*`SEW +: `SEW]) > $signed(max)) begin
                        max = operand_v1_i[i*`SEW +: `SEW];
                    end
                end
                valu_result_o[0 +: `SEW] = max;
            end

            default: begin
                valu_result_o = {`VREG_WIDTH{1'b0}};
            end
        endcase
    end
end

endmodule
