//最终封装模块，将decode,execute,mem,writeback,regfile连接起来，
`include "v_defines.v"

module v_rvcpu(
    input                       clk,
    input                       rst,
    input   [`VINST_BUS]        inst ,

    input   [`SREG_BUS]         vec_rs1_data,
	output            	        vec_rs1_r_ena,
	output  [`SREG_ADDR_BUS]   	vec_rs1_r_addr,

    output                      vram_r_ena,
    output  [`VRAM_ADDR_BUS]    vram_r_addr,
    input   [`VRAM_DATA_BUS]    vram_r_data,

    output                      vram_w_ena,
    output  [`VRAM_ADDR_BUS]    vram_w_addr,
    output  [`VRAM_DATA_BUS]    vram_w_data,
    output  [`VRAM_DATA_BUS]    vram_w_mask
);

    //========================================================
    // 1) Decode 输出（控制信号 + 操作数 + 内存/写回控制）
    //========================================================
    wire                    rs1_en;
    wire [`SREG_ADDR_BUS]   rs1_addr;

    wire                    vs1_en;
    wire [`VREG_ADDR_BUS]   vs1_addr;
    wire [`VREG_BUS]        vs1_dout;

    wire                    vs2_en;
    wire [`VREG_ADDR_BUS]   vs2_addr;
    wire [`VREG_BUS]        vs2_dout;

    wire [`ALU_OP_BUS]      valu_opcode;
    wire [`VREG_BUS]        operand_v1;
    wire [`VREG_BUS]        operand_v2;

    wire                    vmem_ren;
    wire                    vmem_wen;
    wire [`VMEM_ADDR_BUS]   vmem_addr;
    wire [`VMEM_DATA_BUS]   vmem_din;
    wire [2:0]              vmem_width;
    wire [2:0]              vmem_len;
    wire                    vmem_is_vlx;
    wire                    vmem_is_vsx;

    wire                    vid_wb_en;
    wire                    vid_wb_sel;    // 1: mem -> vreg, 0: alu -> vreg
    wire [`VREG_ADDR_BUS]   vid_wb_addr;

    v_inst_decode u_decode (
        .rst            (rst),
        .inst_i         (inst),

        // 标量 rs1
        .rs1_en_o       (rs1_en),
        .rs1_addr_o     (rs1_addr),
        .rs1_dout_i     (vec_rs1_data),

        // 向量寄存器读端口
        .vs1_en_o       (vs1_en),
        .vs1_addr_o     (vs1_addr),
        .vs1_dout_i     (vs1_dout),

        .vs2_en_o       (vs2_en),
        .vs2_addr_o     (vs2_addr),
        .vs2_dout_i     (vs2_dout),

        // 送 execute 的简化 opcode + 两个向量操作数
        .valu_opcode_o  (valu_opcode),
        .operand_v1_o   (operand_v1),
        .operand_v2_o   (operand_v2),

        // 送 mem 的向量内存访问信号
        .vmem_ren_o     (vmem_ren),
        .vmem_wen_o     (vmem_wen),
        .vmem_addr_o    (vmem_addr),
        .vmem_din_o     (vmem_din),
        .vmem_width_o   (vmem_width),
        .vmem_len_o     (vmem_len),
        .vmem_is_vlx_o  (vmem_is_vlx),
        .vmem_is_vsx_o  (vmem_is_vsx),

        // 送 writeback 的写回控制
        .vid_wb_en_o    (vid_wb_en),
        .vid_wb_sel_o   (vid_wb_sel),
        .vid_wb_addr_o  (vid_wb_addr)
    );

    //========================================================
    // 2) 对外连接：标量 rs1 读请求（当需要读取标量寄存器，会通过top发起标量读，再把值放到指定的rs1_addr）
    //========================================================
    assign vec_rs1_r_ena  = rs1_en;
    assign vec_rs1_r_addr = rs1_addr;

    //========================================================
    // 3) Execute：向量 ALU
    //========================================================
    wire [`VREG_BUS] valu_result;

    v_execute u_execute (
        .clk            (clk),
        .rst            (rst),
        .valu_opcode_i  (valu_opcode),
        .operand_v1_i   (operand_v1),
        .operand_v2_i   (operand_v2),
        .valu_result_o  (valu_result)
    );

    //========================================================
    // 4) Mem：直通连接
    //========================================================
    wire [`VMEM_DATA_BUS] vmem_dout;

    wire                  vram_ren_int;
    wire                  vram_wen_int;
    wire [`VRAM_ADDR_BUS] vram_addr_int;
    wire [`VRAM_DATA_BUS] vram_mask_int;
    wire [`VRAM_DATA_BUS] vram_din_int;

    v_mem u_mem (
        .clk             (clk),
        .rst             (rst),
        .vmem_ren_i      (vmem_ren),
        .vmem_wen_i      (vmem_wen),
        .vmem_addr_i     (vmem_addr),
        .vmem_din_i      (vmem_din),
        .vmem_width_i    (vmem_width),
        .vmem_len_i      (vmem_len),
        .vmem_is_vlx_i   (vmem_is_vlx),
        .vmem_is_vsx_i   (vmem_is_vsx),
        .vmem_dout_o     (vmem_dout),

        .vram_ren_o      (vram_ren_int),
        .vram_wen_o      (vram_wen_int),
        .vram_addr_o     (vram_addr_int),
        .vram_mask_o     (vram_mask_int),
        .vram_din_o      (vram_din_int),
        .vram_dout_i     (vram_r_data)
    );

    // 对外 VRAM 端口（读写共用地址）
    assign vram_r_ena  = vram_ren_int;
    assign vram_w_ena  = vram_wen_int;
    assign vram_r_addr = vram_addr_int;
    assign vram_w_addr = vram_addr_int;
    assign vram_w_data = vram_din_int;
    assign vram_w_mask = vram_mask_int;

    //========================================================
    // 5) Write-back：选择写回来源（ALU 或 Mem）
    //========================================================
    wire                  vwb_en;
    wire [`VREG_ADDR_BUS]  vwb_addr;
    wire [`VREG_BUS]       vwb_data;

    v_write_back u_wb (
        .vid_wb_en_i     (vid_wb_en),
        .vid_wb_sel_i    (vid_wb_sel),
        .vid_wb_addr_i   (vid_wb_addr),
        .valu_result_i   (valu_result),
        .vmem_result_i   (vmem_dout),

        .vwb_en_o        (vwb_en),
        .vwb_addr_o      (vwb_addr),
        .vwb_data_o      (vwb_data)
    );

    //========================================================
    // 6) Vector regfile：向量寄存器 v0~v31
    //========================================================
    v_regfile u_vregfile (
        .clk        (clk),
        .rst        (rst),

        .vwb_en_i   (vwb_en),
        .vwb_addr_i (vwb_addr),
        .vwb_data_i (vwb_data),

        .vs1_en_i   (vs1_en),
        .vs1_addr_i (vs1_addr),
        .vs1_data_o (vs1_dout),

        .vs2_en_i   (vs2_en),
        .vs2_addr_i (vs2_addr),
        .vs2_data_o (vs2_dout)
    );

endmodule
