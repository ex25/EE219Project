//v_define,用于定义器件规模，尾款参数，vector指令的OPcode，以及后续解码后发给execute单元的简易
//给定的标量使用RV64，向量元素宽度也使用64bit。

`define VLEN            512
`define SEW             64
`define LMUL            1
`define VLMAX           (`VLEN/`SEW) * `LMUL

`define VINST_BUS       31:0
`define SREG_BUS        63:0
`define SREG_ADDR_BUS   4:0

`define VREG_WIDTH      `VLEN
`define VREG_BUS        `VLEN-1 : 0
`define VREG_ADDR_BUS   4  : 0

`define VMEM_ADDR_BUS   63 : 0
`define VMEM_DATA_BUS   `VLEN-1 : 0

`define VRAM_ADDR_BUS   63 : 0
`define VRAM_DATA_BUS   `VLEN-1 : 0

`define ALU_OP_BUS      7  : 0

//以上是原有代码。


`define SREG_ADDR_WIDTH  5
`define VREG_ADDR_WIDTH  5
`define VMEM_ADDR_WIDTH  64



// ============================================================
// 常规RVV OPCODE定义
// ============================================================

// RVV major opcodes
`define OPCODE_VL       7'b000_0111   // vector load
`define OPCODE_VS       7'b010_0111   // vector store
`define OPCODE_VEC      7'b101_0111   // vector ALU (OP-V)
`define OPCODE_VLX      7'b000_1011   // custom vector load (0x0B)
`define OPCODE_VSX      7'b010_1011   // custom vector store (0x2B)

// funct3 for vector integer ops / memory width
`define FUNCT3_IVV      3'b000        // OPIVV (vv form)
`define FUNCT3_IVI      3'b011        // OPIVI (vi form)
`define FUNCT3_IVX      3'b100        // OPIVX (vx form)
`define WIDTH_VLE64     3'b111        // VLE64 width
`define WIDTH_VSE64     3'b111        // VSE64 width

// Custom VLX/VSX width encoding
`define WIDTH_VLX_B     3'b000        // Byte (8b -> 64b)
`define WIDTH_VLX_H     3'b001        // Half (16b -> 64b)
`define WIDTH_VLX_W     3'b010        // Word (32b -> 64b)
`define WIDTH_VLX_D     3'b011        // Double (64b copy)
`define WIDTH_VSX_B     3'b000        // Byte (64b -> 8b)
`define WIDTH_VSX_H     3'b001        // Half (64b -> 16b)
`define WIDTH_VSX_W     3'b010        // Word (64b -> 32b)
`define WIDTH_VSX_D     3'b011        // Double (64b)

// funct6 for specific operations (OP-V)
`define FUNCT6_VADD     6'b00_0000
`define FUNCT6_VSUB     6'b00_0010
`define FUNCT6_VMUL     6'b10_0101
`define FUNCT6_VDIV     6'b10_0001
`define FUNCT6_VMV_V_X  6'b01_0111
`define FUNCT6_VMIN     6'b00_0101
`define FUNCT6_VMAX     6'b00_0111
`define FUNCT6_VSRA     6'b10_1001
`define FUNCT6_VREDSUM_VS 6'b00_0000
`define FUNCT6_VREDMAX_VS 6'b00_0111

// funct6 for vector load/store
`define FUNCT6_VLE64    6'b00_0000
`define FUNCT6_VSE64    6'b00_0000

// ============================================================
// ALU用简便指令码
// ============================================================
`define VALU_OP_NOP        8'h00
`define VALU_OP_VADD       8'h01
`define VALU_OP_VMUL       8'h02
`define VALU_OP_VSUB       8'h03
`define VALU_OP_VMIN       8'h04
`define VALU_OP_VMAX       8'h05
`define VALU_OP_VSRA       8'h06
`define VALU_OP_VREDSUM_VS 8'h07
`define VALU_OP_VREDMAX_VS 8'h08
`define VALU_OP_VMV_V_X    8'h09
`define VALU_OP_VDIV       8'h0A
`define VALU_OP_VLX        8'h0B
`define VALU_OP_VSX        8'h0C