//v_define,用于定义器件规模，尾款参数，vector指令的OPcode，以及后续解码后发给execute单元的简易
//给定的标量使用RV64，我们向量沿用Lab4的32bit。

`define VLEN            512
`define SEW             32
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
`define OPCODE_VV       7'b101_0111   // vector ALU (OP-V)

// funct3 for vector integer ops / memory width
`define FUNCT3_IVV      3'b000        // OPIVV (vv form)
`define WIDTH_VLE32     3'b110        // 110表示32bit
`define WIDTH_VSE32     3'b110        // 

// funct6 for specific operations (OP-V)
`define FUNCT6_VADD     6'b00_0000
`define FUNCT6_VMUL     6'b10_0100

// funct6 for vector load/store base patterns (simplified)
`define FUNCT6_VLE32    6'b00_0000
`define FUNCT6_VSE32    6'b00_0000



// ============================================================
// ALU用简便指令码
// ============================================================
`define VALU_OP_NOP     8'h00
`define VALU_OP_ADD     8'h01
`define VALU_OP_MUL     8'h02