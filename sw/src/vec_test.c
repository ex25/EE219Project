#include "trap.h"
#include "model.h"
#include "rvv_vec.h"
#include <stdint.h>

#define ADDR_VA   (ADDR_DATA + 0x000)  // A 向量 16*32bit
#define ADDR_VB   (ADDR_DATA + 0x100)  // B 向量 16*32bit
#define ADDR_VC   (ADDR_DATA + 0x200)  // 检查区（存回内存后用标量读验证）

int main() {
  volatile uint32_t *pa = (volatile uint32_t *)(uintptr_t)ADDR_VA;
  volatile uint32_t *pb = (volatile uint32_t *)(uintptr_t)ADDR_VB;
  volatile uint32_t *pc = (volatile uint32_t *)(uintptr_t)ADDR_VC;

  // 1) 初始化 A/B，并清空 C
  for (int i = 0; i < 16; i++) {
    pa[i] = (uint32_t)(0x10u + (uint32_t)i);
    pb[i] = (uint32_t)(0x20u + (uint32_t)i);
    pc[i] = 0;
  }

  // 2) vle：v1 <- A, v2 <- B（地址放在 x31）
  SET_X(x31, (uintptr_t)ADDR_VA);
  vle32(v1, x31);

  SET_X(x31, (uintptr_t)ADDR_VB);
  vle32(v2, x31);

  // 3) vadd：v3 = v1 + v2；vse 写回到 C
  vadd_vv(v3, v1, v2);
  SET_X(x31, (uintptr_t)ADDR_VC);
  vse32(v3, x31);

  // 4) 标量验证 add
  int ok_add = 1;
  for (int i = 0; i < 16; i++) {
    uint32_t expect = (uint32_t)(pa[i] + pb[i]);
    uint32_t got    = pc[i];
    if (got != expect) {
      ok_add = 0;
      printf("ADD mismatch[%d]: expect=0x%x got=0x%x\n", i, expect, got);
    }
  }
  check(ok_add);

  // 5) vmul：v4 = v1 * v2；vse 写回到 C
  vmul_vv(v4, v1, v2);
  SET_X(x31, (uintptr_t)ADDR_VC);
  vse32(v4, x31);

  // 6) 标量验证 mul（32-bit wrap）
  int ok_mul = 1;
  for (int i = 0; i < 16; i++) {
    uint32_t expect = (uint32_t)(pa[i] * pb[i]);
    uint32_t got    = pc[i];
    if (got != expect) {
      ok_mul = 0;
      printf("MUL mismatch[%d]: expect=0x%x got=0x%x\n", i, expect, got);
    }
  }
  check(ok_mul);

  printf("PASS\n");
  return 0;
}
