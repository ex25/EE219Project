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

    output                     vid_wb_en_o,
    output                     vid_wb_sel_o,
    output                     vid_wb_split_o,
    output  [`VREG_ADDR_BUS]   vid_wb_addr_o
);

    // ----------------------------
    // 32位指令码拆解，基于RVV OP-V
    // ----------------------------
    wire [5:0] funct6;
    wire       vm;
    wire [4:0] rs2;
    wire [4:0] rs1;
    wire [2:0] funct3;
    wire [4:0] vd;      // also vs3 for store in this simplified design
    wire [6:0] opcode;

    assign {funct6, vm} = inst_i[31:25];
    assign rs2          = inst_i[24:20];
    assign rs1          = inst_i[19:15];
    assign funct3       = inst_i[14:12];
    assign vd           = inst_i[11:7];
    assign opcode       = inst_i[6:0];

    // ----------------------------
    // 指令识别
    // ----------------------------
    wire inst_vle32_v;
    wire inst_vse32_v;
    wire inst_vadd_vv;
    wire inst_vmul_vv;

    assign inst_vle32_v = (opcode == `OPCODE_VL) & (funct3 == `WIDTH_VLE32) & (funct6 == `FUNCT6_VLE32);
    assign inst_vse32_v = (opcode == `OPCODE_VS) & (funct3 == `WIDTH_VSE32) & (funct6 == `FUNCT6_VSE32);
    assign inst_vadd_vv = (opcode == `OPCODE_VV) & (funct3 == `FUNCT3_IVV)  & (funct6 == `FUNCT6_VADD);
    assign inst_vmul_vv = (opcode == `OPCODE_VV) & (funct3 == `FUNCT3_IVV)  & (funct6 == `FUNCT6_VMUL);

    // ----------------------------
    // 如果需要计算，会被分配对应的简化代码，否则进入NOP表示是读写操作，不用计算。
    // ----------------------------
    assign valu_opcode_o =
        (rst)                              ? `VALU_OP_NOP :
        (inst_vadd_vv)                     ? `VALU_OP_ADD :
        (inst_vmul_vv)                     ? `VALU_OP_MUL :
                                             `VALU_OP_NOP; // load/store -> NOP for ALU

    // ----------------------------
    // 读标量寄存器获取地址（用于vle和vse）
    // ----------------------------
    assign rs1_en_o   = (!rst) & (inst_vle32_v | inst_vse32_v);
    assign rs1_addr_o = (rs1_en_o) ? rs1 : {`SREG_ADDR_WIDTH{1'b0}};

    // ----------------------------
    // 读向量寄存器
    //   - vv: vs1 = rs1, vs2 = rs2
    //   - store 则用vd作为目标地址。
    // ----------------------------
    assign vs1_en_o   = (!rst) & (inst_vadd_vv | inst_vmul_vv);
    assign vs1_addr_o = (vs1_en_o) ? rs1 : {`VREG_ADDR_WIDTH{1'b0}};

    assign vs2_en_o   = (!rst) & (inst_vadd_vv | inst_vmul_vv | inst_vse32_v);
    assign vs2_addr_o = (!rst && inst_vse32_v) ? vd  :
                        (vs2_en_o)             ? rs2 :{`VREG_ADDR_WIDTH{1'b0}};

    // ----------------------------
    // 计算操作的两个参数送去execute
    // ----------------------------
    assign operand_v1_o = (vs1_en_o) ? vs1_dout_i : {`VREG_WIDTH{1'b0}};
    assign operand_v2_o = (vs2_en_o) ? vs2_dout_i : {`VREG_WIDTH{1'b0}};

    // ----------------------------
    // 内存操作
    // ----------------------------
    assign vmem_ren_o  = (!rst) & inst_vle32_v;
    assign vmem_wen_o  = (!rst) & inst_vse32_v;
    assign vmem_addr_o = (rs1_en_o) ? rs1_dout_i : {`VMEM_ADDR_WIDTH{1'b0}};
    assign vmem_din_o  = (inst_vse32_v) ? vs2_dout_i : {`VLEN{1'b0}};

    // ----------------------------
    // 写回（寄存器操作）
    // ----------------------------
    assign vid_wb_en_o     = (!rst) & (inst_vle32_v | inst_vadd_vv | inst_vmul_vv);
    assign vid_wb_sel_o    = (!rst) & inst_vle32_v;     // 1: 来自内存读出, 0: 来自计算
    assign vid_wb_split_o  = 1'b0;                      
    assign vid_wb_addr_o   = (vid_wb_en_o) ? vd : {`VREG_ADDR_WIDTH{1'b0}};

endmodule