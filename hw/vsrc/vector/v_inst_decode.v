//将32位操作码解码
`include "v_defines.v"

module v_inst_decode (
    input                      rst,

    input   [`VINST_BUS]       inst_i,

    output                     rs1_en_o,//标量寄存器，应对vi或vx类型。
    output  [`SREG_ADDR_BUS]   rs1_addr_o,
    input   [`SREG_BUS]        rs1_dout_i,

    output                     vs1_en_o,
    output  [`VREG_ADDR_BUS]   vs1_addr_o,
    input   [`VREG_BUS]        vs1_dout_i,

    output                     vs2_en_o,
    output  [`VREG_ADDR_BUS]   vs2_addr_o,
    input   [`VREG_BUS]        vs2_dout_i,

    output  [`ALU_OP_BUS ]     valu_opcode_o,//给execute的简化指令码
    output  [`VREG_BUS]        operand_v1_o,
    output  [`VREG_BUS]        operand_v2_o,

    output                     vmem_ren_o,
    output                     vmem_wen_o,
    output  [`VMEM_ADDR_BUS]   vmem_addr_o,
    output  [`VMEM_DATA_BUS]   vmem_din_o,
    output  [2:0]              vmem_width_o,    // VLX/VSX的width字段
    output  [2:0]              vmem_len_o,      // VLX/VSX的len字段
    output                     vmem_is_vlx_o,   // 是否为VLX指令
    output                     vmem_is_vsx_o,   // 是否为VSX指令

    output                     vid_wb_en_o,
    output                     vid_wb_sel_o,
    output  [`VREG_ADDR_BUS]   vid_wb_addr_o
);

    // ----------------------------
    // 32位指令码拆解，基于RVV OP-V
    // ----------------------------
    wire [5:0] funct6;
    wire       vm;
    wire [4:0] vs2;
    wire [4:0] vs1;
    wire [2:0] funct3;
    wire [4:0] vd;
    wire [4:0] rs1;
    wire [4:0] imm;
    wire [6:0] opcode;

    assign {funct6, vm} = inst_i[31:25];
    assign vs2          = inst_i[24:20];
    assign vs1          = inst_i[19:15];
    assign rs1          = inst_i[19:15];
    assign imm          = inst_i[19:15];
    assign funct3       = inst_i[14:12];
    assign vd           = inst_i[11:7];
    assign opcode       = inst_i[6:0];

    // ----------------------------
    // 内部寄存器
    // ----------------------------
    reg                   rs1_en;
    reg [`SREG_ADDR_BUS]  rs1_addr;
    reg                   vs1_en;
    reg [`VREG_ADDR_BUS]  vs1_addr;
    reg                   vs2_en;
    reg [`VREG_ADDR_BUS]  vs2_addr;
    reg [`ALU_OP_BUS]     valu_opcode;
    reg [`VREG_BUS]       operand_v1;
    reg [`VREG_BUS]       operand_v2;
    reg                   vmem_ren;
    reg                   vmem_wen;
    reg [`VMEM_ADDR_BUS]  vmem_addr;
    reg [`VMEM_DATA_BUS]  vmem_din;
    reg [2:0]             vmem_width;
    reg [2:0]             vmem_len;
    reg                   vmem_is_vlx;
    reg                   vmem_is_vsx;
    reg                   vid_wb_en;
    reg                   vid_wb_sel;
    reg [`VREG_ADDR_BUS]  vid_wb_addr;

    // 辅助变量用于扩展64位标量寄存器到512位向量
    // VLMAX = 8 (512/64), 每个元素64位，需要扩展8份
    wire [`VREG_BUS] rs1_extended;
    assign rs1_extended = {rs1_dout_i, rs1_dout_i, rs1_dout_i, rs1_dout_i,
                           rs1_dout_i, rs1_dout_i, rs1_dout_i, rs1_dout_i};
    
    // 辅助变量用于扩展5位立即数到512位向量
    wire [`VREG_BUS] imm_extended;
    wire [63:0] imm_sext;
    assign imm_sext = {{59{imm[4]}}, imm};
    assign imm_extended = {imm_sext, imm_sext, imm_sext, imm_sext,
                           imm_sext, imm_sext, imm_sext, imm_sext};

    always @(*) begin
        rs1_en = 0;
        rs1_addr = 0;
        vs1_en = 0;
        vs1_addr = 0;
        vs2_en = 0;
        vs2_addr = 0;
        valu_opcode = `VALU_OP_NOP;
        operand_v1 = 0;
        operand_v2 = 0;
        vmem_ren = 0;
        vmem_wen = 0;
        vmem_addr = 0;
        vmem_din = 0;
        vmem_width = 0;
        vmem_len = 0;
        vmem_is_vlx = 0;
        vmem_is_vsx = 0;
        vid_wb_en = 0;
        vid_wb_sel = 0;
        vid_wb_addr = 0;

        case (opcode)
            `OPCODE_VEC: begin
                vid_wb_en = 1;
                vid_wb_addr = vd;
                vs2_en = 1;
                vs2_addr = vs2;
                operand_v2 = vs2_dout_i;

                case (funct6)
                    `FUNCT6_VADD: begin
                        valu_opcode = `VALU_OP_VADD;
                        case (funct3)
                            `FUNCT3_IVV: begin
                                vs1_en = 1;
                                vs1_addr = vs1;
                                operand_v1 = vs1_dout_i;
                            end
                            `FUNCT3_IVX: begin
                                rs1_en = 1;
                                rs1_addr = rs1;
                                operand_v1 = rs1_extended;
                            end
                            `FUNCT3_IVI: begin
                                operand_v1 = imm_extended;
                            end
                            3'b010: begin  // VREDSUM.VS
                                valu_opcode = `VALU_OP_VREDSUM_VS;
                                vs1_en = 1;
                                vs1_addr = vs1;
                                operand_v1 = vs1_dout_i;
                            end
                            default: begin end
                        endcase
                    end

                    `FUNCT6_VSUB: begin
                        valu_opcode = `VALU_OP_VSUB;
                        case (funct3)
                            `FUNCT3_IVV: begin
                                vs1_en = 1;
                                vs1_addr = vs1;
                                operand_v1 = vs1_dout_i;
                            end
                            `FUNCT3_IVX: begin
                                rs1_en = 1;
                                rs1_addr = rs1;
                                operand_v1 = rs1_extended;
                            end
                            default: begin end
                        endcase
                    end

                    `FUNCT6_VMUL: begin
                        valu_opcode = `VALU_OP_VMUL;
                        case (funct3)
                            `FUNCT3_IVV: begin  // VV form
                                vs1_en = 1;
                                vs1_addr = vs1;
                                operand_v1 = vs1_dout_i;
                            end
                            `FUNCT3_IVX: begin  // VX form
                                rs1_en = 1;
                                rs1_addr = rs1;
                                operand_v1 = rs1_extended;
                            end
                            default: begin end
                        endcase
                    end

                    `FUNCT6_VDIV: begin
                        valu_opcode = `VALU_OP_VDIV;
                        case (funct3)
                            `FUNCT3_IVV: begin  // VV form
                                vs1_en = 1;
                                vs1_addr = vs1;
                                operand_v1 = vs1_dout_i;
                            end
                            `FUNCT3_IVX: begin  // VX form
                                rs1_en = 1;
                                rs1_addr = rs1;
                                operand_v1 = rs1_extended;
                            end
                            default: begin end
                        endcase
                    end

                    `FUNCT6_VMV_V_X: begin
                        if (funct3 == `FUNCT3_IVX) begin
                            valu_opcode = `VALU_OP_VMV_V_X;
                            rs1_en = 1;
                            rs1_addr = rs1;
                            operand_v1 = {{448{1'b0}}, rs1_dout_i};
                        end
                    end

                    `FUNCT6_VMIN: begin
                        valu_opcode = `VALU_OP_VMIN;
                        case (funct3)
                            `FUNCT3_IVV: begin
                                vs1_en = 1;
                                vs1_addr = vs1;
                                operand_v1 = vs1_dout_i;
                            end
                            `FUNCT3_IVX: begin
                                rs1_en = 1;
                                rs1_addr = rs1;
                                operand_v1 = rs1_extended;
                            end
                            default: begin end
                        endcase
                    end

                    `FUNCT6_VMAX: begin
                        valu_opcode = `VALU_OP_VMAX;
                        case (funct3)
                            `FUNCT3_IVV: begin
                                vs1_en = 1;
                                vs1_addr = vs1;
                                operand_v1 = vs1_dout_i;
                            end
                            `FUNCT3_IVX: begin
                                rs1_en = 1;
                                rs1_addr = rs1;
                                operand_v1 = rs1_extended;
                            end
                            3'b010: begin  // VREDMAX.VS
                                valu_opcode = `VALU_OP_VREDMAX_VS;
                                vs1_en = 1;
                                vs1_addr = vs1;
                                operand_v1 = vs1_dout_i;
                            end
                            default: begin end
                        endcase
                    end

                    `FUNCT6_VSRA: begin
                        valu_opcode = `VALU_OP_VSRA;
                        case (funct3)
                            `FUNCT3_IVV: begin
                                vs1_en = 1;
                                vs1_addr = vs1;
                                operand_v1 = vs1_dout_i;
                            end
                            `FUNCT3_IVX: begin
                                rs1_en = 1;
                                rs1_addr = rs1;
                                operand_v1 = rs1_extended;
                            end
                            `FUNCT3_IVI: begin
                                operand_v1 = imm_extended;
                            end
                            default: begin end
                        endcase
                    end

                    default: begin
                        // NOP
                    end
                endcase
            end

            `OPCODE_VL: begin  // VLE32.V
                if (funct3 == `WIDTH_VLE64 && funct6 == `FUNCT6_VLE64) begin
                    vmem_ren = 1;
                    rs1_en = 1;
                    rs1_addr = rs1;
                    vmem_addr = rs1_dout_i;
                    vid_wb_en = 1;
                    vid_wb_sel = 1;
                    vid_wb_addr = vd;
                end
            end

            `OPCODE_VS: begin  // VSE32.V
                if (funct3 == `WIDTH_VSE64 && funct6 == `FUNCT6_VSE64) begin
                    vmem_wen = 1;
                    rs1_en = 1;
                    rs1_addr = rs1;
                    vmem_addr = rs1_dout_i;
                    vs2_en = 1;
                    vs2_addr = vd;  // vs3 for store
                    vmem_din = vs2_dout_i;
                end
            end

            `OPCODE_VLX: begin  // VLX - 向量加载扩展
                // 指令格式: [31:29]=Len, [28:21]=Offset[7:0], [20]=Sign, [19:15]=rs1, [14:12]=Width, [11:7]=vd
                // bit 20独占表示符号/零扩展: 0=零扩展, 1=符号扩展
                vmem_ren = 1;
                vmem_is_vlx = 1;
                rs1_en = 1;
                rs1_addr = rs1;
                // 提取len, offset, width
                vmem_len = inst_i[31:29];  // len: 0-7表示1-8个元素
                vmem_width = funct3;       // width: 000=8bit, 001=16bit, 010=32bit, 011=64bit
                // offset是8位有符号数 [28:21]，需要符号扩展
                vmem_addr = rs1_dout_i + {{56{inst_i[28]}}, inst_i[28:21]};
                // 使用vmem_din传递符号/零扩展标志 (bit 20)
                vmem_din = {{`VLEN-1{1'b0}}, inst_i[20]};
                vid_wb_en = 1;
                vid_wb_sel = 1;  // 从mem获取数据
                vid_wb_addr = vd;
            end

            `OPCODE_VSX: begin  // VSX - 向量存储截断
                // 指令格式: [31:29]=Len, [28:21]=Offset[7:0], [24:20]=vs3, [19:15]=rs1, [14:12]=Width, [11:7]=unused
                vmem_wen = 1;
                vmem_is_vsx = 1;
                rs1_en = 1;
                rs1_addr = rs1;
                vs2_en = 1;
                vs2_addr = vs2;  // vs3在[24:20]
                vmem_din = vs2_dout_i;
                // 提取len, offset, width
                vmem_len = inst_i[31:29];  // len: 0-7表示1-8个元素
                vmem_width = funct3;       // width: 000=8bit, 001=16bit, 010=32bit, 011=64bit
                // offset是8位有符号数 [28:21]，需要符号扩展
                vmem_addr = rs1_dout_i + {{56{inst_i[28]}}, inst_i[28:21]};
            end

            default: begin
                // NOP
            end
        endcase
    end

    // ----------------------------
    // 输出赋值
    // ----------------------------
    assign rs1_en_o = rs1_en;
    assign rs1_addr_o = rs1_addr;
    assign vs1_en_o = vs1_en;
    assign vs1_addr_o = vs1_addr;
    assign vs2_en_o = vs2_en;
    assign vs2_addr_o = vs2_addr;
    assign valu_opcode_o = valu_opcode;
    assign operand_v1_o = operand_v1;
    assign operand_v2_o = operand_v2;
    assign vmem_ren_o = vmem_ren;
    assign vmem_wen_o = vmem_wen;
    assign vmem_addr_o = vmem_addr;
    assign vmem_din_o = vmem_din;
    assign vmem_width_o = vmem_width;
    assign vmem_len_o = vmem_len;
    assign vmem_is_vlx_o = vmem_is_vlx;
    assign vmem_is_vsx_o = vmem_is_vsx;
    assign vid_wb_en_o = vid_wb_en;
    assign vid_wb_sel_o = vid_wb_sel;
    assign vid_wb_addr_o = vid_wb_addr;

endmodule