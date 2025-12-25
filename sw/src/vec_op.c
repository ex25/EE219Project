#include "vec_op.h"
#include "scale_op.h"
#include "rvv_vec.h"
#include <am.h>
#include <klib.h> 
#include <stdint.h> 

// ==========================================
// 向量化实现
// ==========================================

void im2col_weight_int8_vec(const int8_t *weight, int8_t *col_buf, 
                            int N, int C, int K) {
    // 权重预处理：直接使用向量加载和存储
    size_t total_size = (size_t)N * K * K * C;
    
    if (weight == col_buf) return;
    
    // 使用向量指令批量拷贝
    size_t i = 0;
    
    // 每次处理 8 个 int8 元素
    while (i + 8 <= total_size) {
        SET_X(x5, (uintptr_t)&weight[i]);
        SET_X(x6, (uintptr_t)&col_buf[i]);
        
        // 加载 8 个 int8 (零扩展到 64-bit)
        vlx(v1, x5, 0, 0, 8, 0);  // width=0 (8bit), num=8, is_signed=0
        
        // 存储 8 个 int8 (从 64-bit 截断)
        vsx(v1, x6, 0, 0, 8);  // width=0 (8bit), num=8
        
        i += 8;
    }
    
    // 处理剩余元素
    while (i < total_size) {
        col_buf[i] = weight[i];
        i++;
    }
}

void im2col_input_int8_vec(const int8_t *img, int8_t *col_buf, 
                           int C, int H, int W, int K) {
    int S = 1; 
    int P = 0; 
    
    int H_out = (H + 2 * P - K) / S + 1;
    int W_out = (W + 2 * P - K) / S + 1;
    
    int kernel_dim = K * K * C;
    int patch_idx = 0;

    // 遍历每一个滑动窗口
    for (int h_out = 0; h_out < H_out; ++h_out) {
        for (int w_out = 0; w_out < W_out; ++w_out) {
            
            int8_t* patch_ptr = col_buf + patch_idx * kernel_dim;
            
            int h_in_start = h_out * S - P;
            int w_in_start = w_out * S - P;
            
            // 遍历卷积核
            for (int kh = 0; kh < K; ++kh) {
                for (int kw = 0; kw < K; ++kw) {
                    int h_in = h_in_start + kh;
                    int w_in = w_in_start + kw;
                    
                    if (h_in >= 0 && h_in < H && w_in >= 0 && w_in < W) {
                        int pixel_offset = (h_in * W + w_in) * C;
                        const int8_t* src = img + pixel_offset;
                        
                        // 使用向量指令拷贝 C 个通道
                        int c = 0;
                        while (c + 8 <= C) {
                            SET_X(x5, (uintptr_t)&src[c]);
                            SET_X(x6, (uintptr_t)&patch_ptr[c]);
                            
                            vlx(v1, x5, 0, 0, 8, 0);
                            vsx(v1, x6, 0, 0, 8);
                            
                            c += 8;
                        }
                        while (c < C) {
                            patch_ptr[c] = src[c];
                            c++;
                        }
                    } else {
                        // Padding 填 0
                        memset(patch_ptr, 0, C * sizeof(int8_t));
                    }
                    patch_ptr += C;
                }
            }
            patch_idx++;
        }
    }
}

// Int8 * Int8 -> Int32 -> Scale -> Clip -> Int16 (向量化 - 优化版)
void matmul_int8_scale_clip_vec(const int8_t *A, const int8_t *B, int16_t *C, 
                                int M, int N, int K, int scale) {
    // 优化策略：
    // 1. B列预收集：一次性收集整列，减少重复访问
    // 2. 对齐检查外提：减少分支判断
    // 3. 固定地址寄存器重用：减少SET_X调用
    // 4. 向量累加：减少归约次数
    
    int8_t b_col_buf[256] __attribute__((aligned(64)));  // 最大支持K=256
    int64_t sum_result[8] __attribute__((aligned(64)));
    int64_t vacc[8] __attribute__((aligned(64))) = {0};  // 向量累加器
    
    // 固定地址寄存器设置（循环外）
    SET_X(x7, (uintptr_t)sum_result);
    SET_X(x9, (uintptr_t)vacc);
    
    for (int m = 0; m < M; ++m) {
        const int8_t *a_row = &A[m * K];
        
        // 检查这一行的对齐情况（提到外层）
        uintptr_t a_row_addr = (uintptr_t)a_row;
        int a_row_aligned = ((a_row_addr & 0x7) == 0);
        
        for (int n = 0; n < N; ++n) {
            // 优化1: B列预收集到连续缓冲区
            for (int k = 0; k < K; k++) {
                b_col_buf[k] = B[k * N + n];
            }
            
            int32_t sum = 0;
            int k = 0;
            
            // 向量化内积计算，每次处理 8 个元素
            while (k + 8 <= K) {
                // 优化2: 对齐检查外提
                if (a_row_aligned) {
                    SET_X(x5, (uintptr_t)&a_row[k]);
                    vlx(v1, x5, 0, 0, 8, 1);
                } else {
                    // 不对齐路径：使用缓冲区
                    int8_t a_buf[8] __attribute__((aligned(64)));
                    for (int i = 0; i < 8; i++) a_buf[i] = a_row[k + i];
                    SET_X(x5, (uintptr_t)a_buf);
                    vlx(v1, x5, 0, 0, 8, 1);
                }
                
                // B已预收集，连续访问
                SET_X(x6, (uintptr_t)&b_col_buf[k]);
                vlx(v2, x6, 0, 0, 8, 1);
                
                // 向量乘法
                vmul_vv(v3, v1, v2);
                
                // 归约求和
                vmv_v_x(v4, x0);
                vredsum_vs(v4, v3, v4);
                
                // 写回内存（x7已在循环外设置）
                vsx(v4, x7, 0, 3, 1);
                
                sum += (int32_t)sum_result[0];
                
                k += 8;
            }
            
            // 标量处理剩余元素
            while (k < K) {
                sum += (int32_t)a_row[k] * (int32_t)b_col_buf[k];
                k++;
            }
            
            // Scale
            if (scale != 0) {
                sum = sum / scale;
            }

            // Clip
            int32_t val = CLAMP(sum, 0, 32767);
            
            C[m * N + n] = (int16_t)val;
        }
    }
}

void matmul_int16_scale_clip_vec(const int16_t *A, const int16_t *B, int32_t *C, 
                                 int M, int N, int K, int scale) {
    // 临时缓冲区
    int16_t b_col[8] __attribute__((aligned(64)));
    int64_t result[8] __attribute__((aligned(64)));
    
    for (int m = 0; m < M; ++m) {
        for (int n = 0; n < N; ++n) {
            
            int64_t sum = 0;
            int k = 0;
            
            // 向量化：每次处理 8 个元素
            while (k + 8 <= K) {
                // 收集 B 的列元素
                for (int i = 0; i < 8; i++) {
                    b_col[i] = B[(k + i) * N + n];
                }
                
                // 加载 A 的 8 个 int16 (符号扩展到 64-bit)
                SET_X(x5, (uintptr_t)&A[m * K + k]);
                vlx(v1, x5, 0, 1, 8, 1);  // width=1 (16bit), is_signed=1
                
                // 加载 B 的 8 个 int16 (符号扩展到 64-bit)
                SET_X(x6, (uintptr_t)b_col);
                vlx(v2, x6, 0, 1, 8, 1);  // width=1 (16bit), is_signed=1
                
                // 向量乘法
                vmul_vv(v3, v1, v2);
                
                // 归约求和
                vmv_v_x(v4, x0);  // v4 = 0
                vredsum_vs(v4, v3, v4);
                
                // 写回内存并读取
                SET_X(x7, (uintptr_t)result);
                vsx(v4, x7, 0, 3, 1);  // width=3(64bit)
                sum += result[0];
                
                k += 8;
            }
            
            // 处理剩余元素
            while (k < K) {
                sum += (int64_t)A[m * K + k] * (int64_t)B[k * N + n];
                k++;
            }
            
            // Scale
            if (scale != 0) {
                sum = sum / scale;
            }

            // Clip
            int64_t max_int32 = 2147483647LL;
            int64_t min_int32 = -2147483648LL;
            
            if (sum > max_int32) sum = max_int32;
            if (sum < min_int32) sum = min_int32;

            C[m * N + n] = (int32_t)sum;
        }
    }
}

void matmul_int32_scale_clip_vec(const int32_t *A, const int32_t *B, int32_t *C, 
                                 int M, int N, int K, int scale) {
    // 临时缓冲区
    int32_t b_col[8] __attribute__((aligned(64)));
    int64_t result[8] __attribute__((aligned(64)));
    
    for (int m = 0; m < M; ++m) {
        for (int n = 0; n < N; ++n) {
            
            int64_t sum = 0;
            int k = 0;
            
            // 向量化：每次处理 8 个元素
            while (k + 8 <= K) {
                // 收集 B 的列元素
                for (int i = 0; i < 8; i++) {
                    b_col[i] = B[(k + i) * N + n];
                }
                
                // 加载 A 的 8 个 int32 (符号扩展到 64-bit)
                SET_X(x5, (uintptr_t)&A[m * K + k]);
                vlx(v1, x5, 0, 2, 8, 1);  // width=2 (32bit), is_signed=1
                
                // 加载 B 的 8 个 int32 (符号扩展到 64-bit)
                SET_X(x6, (uintptr_t)b_col);
                vlx(v2, x6, 0, 2, 8, 1);  // width=2 (32bit), is_signed=1
                
                // 向量乘法
                vmul_vv(v3, v1, v2);
                
                // 归约求和
                vmv_v_x(v4, x0);  // v4 = 0
                vredsum_vs(v4, v3, v4);
                
                // 写回内存并读取
                SET_X(x7, (uintptr_t)result);
                vsx(v4, x7, 0, 3, 1);  // width=3(64bit)
                sum += result[0];
                
                k += 8;
            }
            
            // 处理剩余元素
            while (k < K) {
                sum += (int64_t)A[m * K + k] * (int64_t)B[k * N + n];
                k++;
            }
            
            // Scale
            if (scale != 0) {
                sum = sum / scale;
            }
            
            // Clip
            int64_t max_int32 = 2147483647LL;
            int64_t min_int32 = -2147483648LL;
            if (sum > max_int32) sum = max_int32;
            if (sum < min_int32) sum = min_int32;

            C[m * N + n] = (int32_t)sum;
        }
    }
}

void matadd_int32_vec(const int32_t *A, const int32_t *B, int32_t *C, int len) {
    int i = 0;
    
    // 每次处理 8 个 int32 元素
    while (i + 8 <= len) {
        SET_X(x5, (uintptr_t)&A[i]);
        SET_X(x6, (uintptr_t)&B[i]);
        SET_X(x7, (uintptr_t)&C[i]);
        
        // 加载 8 个 int32
        vlx(v1, x5, 0, 2, 8, 1);  // width=2 (32bit)
        vlx(v2, x6, 0, 2, 8, 1);
        
        // 向量加法
        vadd_vv(v3, v1, v2);
        
        // 存储结果
        vsx(v3, x7, 0, 2, 8);  // width=2 (32bit)
        
        i += 8;
    }
    
    // 处理剩余元素
    while (i < len) {
        C[i] = A[i] + B[i];
        i++;
    }
}

void relu_int16_vec(int16_t *data, int len) {
    int i = 0;
    
    // 向量化 ReLU
    while (i + 8 <= len) {
        SET_X(x5, (uintptr_t)&data[i]);
        
        // 加载 8 个 int16 (符号扩展到 64-bit)
        vlx(v1, x5, 0, 1, 8, 1);  // width=1 (16bit)
        
        // v2 = 0
        vmv_v_x(v2, x0);  // x0 = 0
        
        // v3 = max(v1, v2)
        vmax_vv(v3, v1, v2);
        
        // 存储结果 (截断回 16-bit)
        vsx(v3, x5, 0, 1, 8);  // width=1 (16bit)
        
        i += 8;
    }
    
    // 处理剩余元素
    while (i < len) {
        if (data[i] < 0) data[i] = 0;
        i++;
    }
}

void relu_int32_vec(int32_t *data, int len) {
    int i = 0;
    
    // 向量化 ReLU
    while (i + 8 <= len) {
        SET_X(x5, (uintptr_t)&data[i]);
        
        // 加载 8 个 int32
        vlx(v1, x5, 0, 2, 8, 1);  // width=2 (32bit)
        
        // v2 = 0
        vmv_v_x(v2, x0);
        
        // v3 = max(v1, v2)
        vmax_vv(v3, v1, v2);
        
        // 存储结果
        vsx(v3, x5, 0, 2, 8);  // width=2 (32bit)
        
        i += 8;
    }
    
    // 处理剩余元素
    while (i < len) {
        if (data[i] < 0) data[i] = 0;
        i++;
    }
}

void maxpool_int16_vec(const int16_t *src, int16_t *dst, int C, int H, int W) {
    int H_out = H / 2;
    int W_out = W / 2;
    
    // NHWC Layout: H -> W -> C
    for (int h = 0; h < H_out; ++h) {
        for (int w = 0; w < W_out; ++w) {
            int c = 0;
            
            // 向量化通道维度
            while (c + 8 <= C) {
                // 2x2 窗口的 4 个位置
                int idx00 = ((h * 2 + 0) * W + (w * 2 + 0)) * C + c;
                int idx01 = ((h * 2 + 0) * W + (w * 2 + 1)) * C + c;
                int idx10 = ((h * 2 + 1) * W + (w * 2 + 0)) * C + c;
                int idx11 = ((h * 2 + 1) * W + (w * 2 + 1)) * C + c;
                
                SET_X(x5, (uintptr_t)&src[idx00]);
                SET_X(x6, (uintptr_t)&src[idx01]);
                SET_X(x7, (uintptr_t)&src[idx10]);
                SET_X(x8, (uintptr_t)&src[idx11]);
                
                // 加载 4 个位置的 8 个通道
                vlx(v1, x5, 0, 1, 8, 1);
                vlx(v2, x6, 0, 1, 8, 1);
                vlx(v3, x7, 0, 1, 8, 1);
                vlx(v4, x8, 0, 1, 8, 1);
                
                // 计算 max
                vmax_vv(v5, v1, v2);
                vmax_vv(v6, v3, v4);
                vmax_vv(v7, v5, v6);
                
                // 存储结果
                int dst_idx = (h * W_out + w) * C + c;
                SET_X(x9, (uintptr_t)&dst[dst_idx]);
                vsx(v7, x9, 0, 1, 8);
                
                c += 8;
            }
            
            // 处理剩余通道
            while (c < C) {
                int idx00 = ((h * 2 + 0) * W + (w * 2 + 0)) * C + c;
                int idx01 = ((h * 2 + 0) * W + (w * 2 + 1)) * C + c;
                int idx10 = ((h * 2 + 1) * W + (w * 2 + 0)) * C + c;
                int idx11 = ((h * 2 + 1) * W + (w * 2 + 1)) * C + c;

                int16_t max_val = src[idx00];
                if (src[idx01] > max_val) max_val = src[idx01];
                if (src[idx10] > max_val) max_val = src[idx10];
                if (src[idx11] > max_val) max_val = src[idx11];
                
                int dst_idx = (h * W_out + w) * C + c;
                dst[dst_idx] = max_val;
                c++;
            }
        }
    }
}

void transpose_NHWC_to_NCHW_vec(const int16_t *src, int16_t *dst, int C, int H, int W) {
    // 由于转置涉及非连续访问，向量化收益有限
    // 使用标量实现
    for (int c = 0; c < C; c++) {
        for (int h = 0; h < H; h++) {
            for (int w = 0; w < W; w++) {
                int src_idx = (h * W + w) * C + c;
                int dst_idx = c * (H * W) + h * W + w;
                dst[dst_idx] = src[src_idx];
            }
        }
    }
}

void flatten_int16_vec(const int16_t *src, int16_t *dst, int len) {
    if (src == dst) return;
    
    int i = 0;
    
    // 向量化拷贝
    while (i + 8 <= len) {
        SET_X(x5, (uintptr_t)&src[i]);
        SET_X(x6, (uintptr_t)&dst[i]);
        
        vlx(v1, x5, 0, 1, 8, 1);  // 加载 8 个 int16
        vsx(v1, x6, 0, 1, 8);     // 存储 8 个 int16
        
        i += 8;
    }
    
    // 处理剩余元素
    while (i < len) {
        dst[i] = src[i];
        i++;
    }
}

void softmax_hw_vec(const int32_t *src, int32_t *dst, const int32_t *lut, int len) {
    // Softmax 涉及复杂的查表、除法等操作，向量化收益有限
    // 且对于小规模数据（如分类任务的10个类别），标量实现已足够高效
    // 因此直接调用标量版本
    softmax_hw(src, dst, lut, len);
}

void transpose_int8_vec(const int8_t *src, int8_t *dst, int M, int N) {
    // 矩阵转置：src[M, N] -> dst[N, M]
    // src[m][n] -> dst[n][m]
    // 对于小规模矩阵，标量实现已足够高效
    // 向量化转置需要处理非连续访问，收益有限
    for (int m = 0; m < M; ++m) {
        for (int n = 0; n < N; ++n) {
            dst[n * M + m] = src[m * N + n];
        }
    }
}
