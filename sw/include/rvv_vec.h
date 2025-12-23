#ifndef __RVV_OPS_H__
#define __RVV_OPS_H__

#include <stdint.h>

// ============================
// 字符串化：把 token x5 变成 "x5"
// ============================
#define _STR2(x) #x
#define _STR(x)  _STR2(x)

// ============================
// token -> 编号：XID(x5)=5, VID(v8)=8
// ============================
#define XID(x) XID_##x
#define VID(v) VID_##v

#define XID_x0  0
#define XID_x1  1
#define XID_x2  2
#define XID_x3  3
#define XID_x4  4
#define XID_x5  5
#define XID_x6  6
#define XID_x7  7
#define XID_x8  8
#define XID_x9  9
#define XID_x10 10
#define XID_x11 11
#define XID_x12 12
#define XID_x13 13
#define XID_x14 14
#define XID_x15 15
#define XID_x16 16
#define XID_x17 17
#define XID_x18 18
#define XID_x19 19
#define XID_x20 20
#define XID_x21 21
#define XID_x22 22
#define XID_x23 23
#define XID_x24 24
#define XID_x25 25
#define XID_x26 26
#define XID_x27 27
#define XID_x28 28
#define XID_x29 29
#define XID_x30 30
#define XID_x31 31

#define VID_v0  0
#define VID_v1  1
#define VID_v2  2
#define VID_v3  3
#define VID_v4  4
#define VID_v5  5
#define VID_v6  6
#define VID_v7  7
#define VID_v8  8
#define VID_v9  9
#define VID_v10 10
#define VID_v11 11
#define VID_v12 12
#define VID_v13 13
#define VID_v14 14
#define VID_v15 15
#define VID_v16 16
#define VID_v17 17
#define VID_v18 18
#define VID_v19 19
#define VID_v20 20
#define VID_v21 21
#define VID_v22 22
#define VID_v23 23
#define VID_v24 24
#define VID_v25 25
#define VID_v26 26
#define VID_v27 27
#define VID_v28 28
#define VID_v29 29
#define VID_v30 30
#define VID_v31 31

// ============================
// 你的 v_defines.v 对应常量（必须一致）
// ============================
#define OPCODE_VL   0x07u
#define OPCODE_VS   0x27u
#define OPCODE_VV   0x57u

#define FUNCT3_IVV  0x0u
#define WIDTH_VLE32 0x6u
#define WIDTH_VSE32 0x6u

#define FUNCT6_VADD 0x00u
#define FUNCT6_VMUL 0x24u
#define FUNCT6_VLE32 0x00u
#define FUNCT6_VSE32 0x00u

#define VM_BIT      1u

// ============================
// 指令编码：与你 decode 拆域一致
// [31:25]={funct6,vm} [24:20]=rs2 [19:15]=rs1 [14:12]=funct3 [11:7]=vd [6:0]=opcode
// ============================
#define ENCODE_RVV(funct6, vm, rs2, rs1, funct3, vd, opcode) ( \
  (((uint32_t)((funct6) & 0x3fu)) << 26) | \
  (((uint32_t)((vm)     & 0x01u)) << 25) | \
  (((uint32_t)((rs2)    & 0x1fu)) << 20) | \
  (((uint32_t)((rs1)    & 0x1fu)) << 15) | \
  (((uint32_t)((funct3) & 0x07u)) << 12) | \
  (((uint32_t)((vd)     & 0x1fu)) <<  7) | \
  (((uint32_t)((opcode) & 0x7fu))      ) )

// ============================
// 把一个 C 值放进指定标量寄存器：SET_X(x5, value)
// 这一步是“让 x5 里真的有地址值”，后面 vle/vse 才能用 rs1=x5 去取地址
// ============================
#define SET_X(xreg, value_expr) do { \
  register uintptr_t __tmp asm(_STR(xreg)) = (uintptr_t)(value_expr); \
  asm volatile("" : : "r"(__tmp) : "memory"); \
} while (0)

// ============================
// 发 32-bit 指令（.word），要求 inst 是编译期常量
// ============================
#define EMIT_WORD(inst_const) asm volatile(".word %0" : : "i"(inst_const) : "memory")

// ============================
// 你要的 API：vle32(vd, xrs1) / vse32(vs3, xrs1) / vadd_vv(vd, vs1, vs2) / vmul_vv(...)
// ============================

// vle32: vd 字段=目标向量寄存器编号；rs1 字段=地址所在标量寄存器编号
#define vle32(vd, xrs1) do { \
  const uint32_t __inst = ENCODE_RVV(FUNCT6_VLE32, VM_BIT, 0, XID(xrs1), WIDTH_VLE32, VID(vd), OPCODE_VL); \
  EMIT_WORD(__inst); \
} while (0)

// vse32: 你当前 decode 用 “vd 字段当作 store 数据源寄存器编号”
// 所以这里第一个参数写“要存的向量寄存器”
#define vse32(vs3, xrs1) do { \
  const uint32_t __inst = ENCODE_RVV(FUNCT6_VSE32, VM_BIT, 0, XID(xrs1), WIDTH_VSE32, VID(vs3), OPCODE_VS); \
  EMIT_WORD(__inst); \
} while (0)

#define vadd_vv(vd, vs1, vs2) do { \
  const uint32_t __inst = ENCODE_RVV(FUNCT6_VADD, VM_BIT, VID(vs2), VID(vs1), FUNCT3_IVV, VID(vd), OPCODE_VV); \
  EMIT_WORD(__inst); \
} while (0)

#define vmul_vv(vd, vs1, vs2) do { \
  const uint32_t __inst = ENCODE_RVV(FUNCT6_VMUL, VM_BIT, VID(vs2), VID(vs1), FUNCT3_IVV, VID(vd), OPCODE_VV); \
  EMIT_WORD(__inst); \
} while (0)

#endif
