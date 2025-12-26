#include "rvv_systolic.h"
#include "rvv_vec.h"
#include <stdint.h>

// ==========================================
// 脉动阵列矩阵乘法示例
// ==========================================

/**
 * @brief 使用脉动阵列进行矩阵乘法：C = A * B
 * 
 * A: [M, K]
 * B: [K, N]  
 * C: [M, N]
 * 
 * 使用分块策略，支持大矩阵
 */
void matmul_systolic_int64(const int64_t *A, const int64_t *B, int64_t *C,
                           int M, int N, int K) {
    // 脉动阵列参数
    const int TILE_M = 8;  // ARRAY_SIZE_M_MAX
    const int TILE_K = 8;  // ARRAY_SIZE_K_MAX
    
    int64_t x_data[8] __attribute__((aligned(64)));
    int64_t w_data[8] __attribute__((aligned(64)));
    int64_t y_data[8] __attribute__((aligned(64)));
    
    // 分块处理：M维度
    for (int m0 = 0; m0 < M; m0 += TILE_M) {
        int m_size = (m0 + TILE_M <= M) ? TILE_M : (M - m0);
        
        // 分块处理：N维度
        for (int n0 = 0; n0 < N; n0 += TILE_K) {
            int n_size = (n0 + TILE_K <= N) ? TILE_K : (N - n0);
            
            // 配置脉动阵列
            vsa_config(m_size, K, n_size);
            
            // 分块处理：K维度（累加）
            for (int k0 = 0; k0 < K; k0 += TILE_K) {
                int k_size = (k0 + TILE_K <= K) ? TILE_K : (K - k0);
                
                // 决定是否累加
                int accumulate = (k0 > 0) ? 1 : 0;
                
                // 更新配置（如果K分块导致k_size变化）
                if (k_size != TILE_K) {
                    vsa_config(m_size, k_size, n_size);
                }
                
                // 加载数据到脉动阵列
                for (int kk = 0; kk < k_size; kk++) {
                    // 准备X数据：A的一列（从第k0+kk列）
                    for (int mm = 0; mm < m_size; mm++) {
                        x_data[mm] = A[(m0 + mm) * K + (k0 + kk)];
                    }
                    for (int mm = m_size; mm < 8; mm++) {
                        x_data[mm] = 0;  // 填充
                    }
                    
                    // 加载X到向量寄存器
                    SET_X(x5, (uintptr_t)x_data);
                    vlx(v1, x5, 0, 3, 8, 1);  // 加载8个int64
                    
                    // 写入脉动阵列X缓冲区
                    vsa_load_x(v1, kk);
                    
                    // 准备W数据：B的一行（第k0+kk行）
                    for (int nn = 0; nn < n_size; nn++) {
                        w_data[nn] = B[(k0 + kk) * N + (n0 + nn)];
                    }
                    for (int nn = n_size; nn < 8; nn++) {
                        w_data[nn] = 0;  // 填充
                    }
                    
                    // 加载W到向量寄存器
                    SET_X(x6, (uintptr_t)w_data);
                    vlx(v2, x6, 0, 3, 8, 1);
                    
                    // 写入脉动阵列W缓冲区
                    vsa_load_w(v2, kk);
                }
                
                // 启动计算
                vsa_start(accumulate);
                
                // 等待完成
                vsa_wait_done();
            }
            
            // 读取结果
            for (int mm = 0; mm < m_size; mm++) {
                // 读取第mm行
                vsa_read_y(v3, mm);
                
                // 存储到内存
                SET_X(x7, (uintptr_t)y_data);
                vsx(v3, x7, 0, 3, 8);
                
                // 写入结果矩阵C
                for (int nn = 0; nn < n_size; nn++) {
                    C[(m0 + mm) * N + (n0 + nn)] = y_data[nn];
                }
            }
        }
    }
}

/**
 * @brief 简单示例：小矩阵乘法（不需要分块）
 * 
 * 假设 M, N, K <= 8
 */
void matmul_systolic_simple(const int64_t *A, const int64_t *B, int64_t *C,
                            int M, int N, int K) {
    int64_t x_data[8] __attribute__((aligned(64)));
    int64_t w_data[8] __attribute__((aligned(64)));
    int64_t y_data[8] __attribute__((aligned(64)));
    
    // 1. 配置维度
    vsa_config(M, K, N);
    
    // 2. 加载数据
    for (int k = 0; k < K; k++) {
        // 准备X：A的第k列
        for (int m = 0; m < M; m++) {
            x_data[m] = A[m * K + k];
        }
        for (int m = M; m < 8; m++) {
            x_data[m] = 0;
        }
        
        SET_X(x5, (uintptr_t)x_data);
        vlx(v1, x5, 0, 3, 8, 1);
        vsa_load_x(v1, k);
        
        // 准备W：B的第k行
        for (int n = 0; n < N; n++) {
            w_data[n] = B[k * N + n];
        }
        for (int n = N; n < 8; n++) {
            w_data[n] = 0;
        }
        
        SET_X(x6, (uintptr_t)w_data);
        vlx(v2, x6, 0, 3, 8, 1);
        vsa_load_w(v2, k);
    }
    
    // 3. 启动计算（清零模式）
    vsa_start(0);
    
    // 4. 等待完成
    vsa_wait_done();
    
    // 5. 读取结果
    for (int m = 0; m < M; m++) {
        vsa_read_y(v3, m);
        
        SET_X(x7, (uintptr_t)y_data);
        vsx(v3, x7, 0, 3, 8);
        
        for (int n = 0; n < N; n++) {
            C[m * N + n] = y_data[n];
        }
    }
}

/**
 * @brief Output Stationary示例：多次累加
 * 
 * 展示如何利用累加模式进行分块K维度的计算
 */
void matmul_systolic_output_stationary(const int64_t *A, const int64_t *B, int64_t *C,
                                        int M, int N, int K) {
    const int K_BLOCK = 4;  // 每次处理4个K
    
    int64_t x_data[8] __attribute__((aligned(64)));
    int64_t w_data[8] __attribute__((aligned(64)));
    int64_t y_data[8] __attribute__((aligned(64)));
    
    vsa_config(M, K_BLOCK, N);
    
    // 分K次计算并累加
    for (int k0 = 0; k0 < K; k0 += K_BLOCK) {
        int k_size = (k0 + K_BLOCK <= K) ? K_BLOCK : (K - k0);
        
        // 加载这一块的数据
        for (int kk = 0; kk < k_size; kk++) {
            // X: A的列
            for (int m = 0; m < M; m++) {
                x_data[m] = A[m * K + (k0 + kk)];
            }
            for (int m = M; m < 8; m++) x_data[m] = 0;
            
            SET_X(x5, (uintptr_t)x_data);
            vlx(v1, x5, 0, 3, 8, 1);
            vsa_load_x(v1, kk);
            
            // W: B的行
            for (int n = 0; n < N; n++) {
                w_data[n] = B[(k0 + kk) * N + n];
            }
            for (int n = N; n < 8; n++) w_data[n] = 0;
            
            SET_X(x6, (uintptr_t)w_data);
            vlx(v2, x6, 0, 3, 8, 1);
            vsa_load_w(v2, kk);
        }
        
        // 第一块清零，后续累加
        int accumulate = (k0 > 0) ? 1 : 0;
        vsa_start(accumulate);
        vsa_wait_done();
    }
    
    // 读取最终结果
    for (int m = 0; m < M; m++) {
        vsa_read_y(v3, m);
        
        SET_X(x7, (uintptr_t)y_data);
        vsx(v3, x7, 0, 3, 8);
        
        for (int n = 0; n < N; n++) {
            C[m * N + n] = y_data[n];
        }
    }
}
