#ifndef RVV_SYSTOLIC_H
#define RVV_SYSTOLIC_H

#include "rvv_vec.h"

// ==========================================
// 脉动阵列向量指令定义
// ==========================================

// 新增操作码（需要在硬件中定义）
#define OPCODE_VSA_CONFIG   0x5F  // 配置MNK
#define OPCODE_VSA_LOAD_X   0x6F  // 加载X到缓冲区
#define OPCODE_VSA_LOAD_W   0x7F  // 加载W到缓冲区
#define OPCODE_VSA_START    0x0B  // 启动计算
#define OPCODE_VSA_READ_Y   0x1B  // 读取结果
#define OPCODE_VSA_STATUS   0x2B  // 读取状态

/**
 * @brief 配置脉动阵列的M, N, K维度
 * vsa_config M, N, K
 * 
 * 编码格式: [31:23]=M[8:0], [22:14]=N[8:0], [13:5]=K[8:0]
 */
#define vsa_config(m, n, k) do { \
    uint64_t __m = (m); \
    uint64_t __n = (n); \
    uint64_t __k = (k); \
    uint64_t __inst = ((__m & 0x1FF) << 23) | ((__n & 0x1FF) << 14) | ((__k & 0x1FF) << 5) | OPCODE_VSA_CONFIG; \
    asm volatile (".word %0" : : "i"(__inst)); \
} while(0)

/**
 * @brief 加载向量寄存器数据到X缓冲区
 * vsa_load_x vs, idx
 * 
 * @param vs  源向量寄存器（包含ARRAY_SIZE_M_MAX个元素）
 * @param idx N维度索引 (0 to N-1)
 * 
 * 编码格式: [31:23]=idx[8:0], [22:20]=vs3[2:0], [6:0]=opcode
 */
#define vsa_load_x(vs, idx) do { \
    uint64_t __idx = (idx); \
    uint64_t __vs = VID(vs); \
    uint64_t __inst = ((__idx & 0x1FF) << 23) | ((__vs & 0x7) << 20) | OPCODE_VSA_LOAD_X; \
    asm volatile (".word %0" : : "i"(__inst)); \
} while(0)

/**
 * @brief 加载向量寄存器数据到W缓冲区  
 * vsa_load_w vs, idx
 * 
 * @param vs  源向量寄存器（包含ARRAY_SIZE_K_MAX个元素）
 * @param idx N维度索引 (0 to N-1)
 */
#define vsa_load_w(vs, idx) do { \
    uint64_t __idx = (idx); \
    uint64_t __vs = VID(vs); \
    uint64_t __inst = ((__idx & 0x1FF) << 23) | ((__vs & 0x7) << 20) | OPCODE_VSA_LOAD_W; \
    asm volatile (".word %0" : : "i"(__inst)); \
} while(0)

/**
 * @brief 启动脉动阵列计算
 * vsa_start accumulate
 * 
 * @param accum 1=累加模式（保留partial sum）, 0=清零后计算
 */
#define vsa_start(accum) do { \
    uint64_t __accum = (accum); \
    uint64_t __inst = ((__accum & 0x1) << 20) | OPCODE_VSA_START; \
    asm volatile (".word %0" : : "i"(__inst)); \
} while(0)

/**
 * @brief 从脉动阵列读取指定行的结果
 * vsa_read_y vd, row
 * 
 * @param vd  目标向量寄存器（接收ARRAY_SIZE_K_MAX个元素）
 * @param row 读取的行索引 (0 to M-1)
 */
#define vsa_read_y(vd, row) do { \
    uint64_t __row = (row); \
    uint64_t __vd = VID(vd); \
    uint64_t __inst = ((__row & 0x1FF) << 23) | ((__vd & 0x7) << 7) | OPCODE_VSA_READ_Y; \
    asm volatile (".word %0" : : "i"(__inst)); \
} while(0)

/**
 * @brief 读取脉动阵列状态
 * vsa_status vd
 * 
 * vd[0] = busy (1=正在计算, 0=空闲)
 * vd[1] = done (1=计算完成, 0=未完成)
 */
#define vsa_status(vd) do { \
    uint64_t __vd = VID(vd); \
    uint64_t __inst = ((__vd & 0x7) << 7) | OPCODE_VSA_STATUS; \
    asm volatile (".word %0" : : "i"(__inst)); \
} while(0)

// ==========================================
// 辅助函数：等待计算完成
// ==========================================

/**
 * @brief 轮询等待脉动阵列计算完成
 */
static inline void vsa_wait_done(void) {
    uint64_t status[8] __attribute__((aligned(64)));
    
    while (1) {
        vsa_status(v0);
        
        // 读取状态到内存
        SET_X(x5, (uintptr_t)status);
        vsx(v0, x5, 0, 3, 8);  // 存储64位
        
        // 检查done标志 (status[1])
        if (status[1] != 0) {
            break;
        }
    }
}

#endif // RVV_SYSTOLIC_H
