#include "scale_op.h"
#include <am.h>
#include <klib.h> 
#include <stdint.h> 

void im2col_weight_int8(const int8_t *weight, int8_t *col_buf, 
                        int N, int C, int K) {
    size_t size = (size_t)N * K * K * C * sizeof(int8_t);
    if (weight != col_buf) {
        memcpy(col_buf, weight, size);
    }
}

void im2col_input_int8(const int8_t *img, int8_t *col_buf, 
                       int C, int H, int W, int K) {
    int S = 1; // Stride
    int P = 0; // Padding
    
    int H_out = (H + 2 * P - K) / S + 1;
    int W_out = (W + 2 * P - K) / S + 1;
    
    int n_patches = H_out * W_out;      // 矩阵 B 的列数 (N)
    
    // 我们生成矩阵 B [K_dim, N_patches]
    // 遍历顺序：为了优化缓存，通常遍历输出 Patch
    
    for (int h_out = 0; h_out < H_out; ++h_out) {
        for (int w_out = 0; w_out < W_out; ++w_out) {
            
            // 当前 Patch 在矩阵 B 中的列索引
            int patch_col = h_out * W_out + w_out;
            
            // 原图中的起始坐标
            int h_in_start = h_out * S - P;
            int w_in_start = w_out * S - P;
            
            int matrix_row = 0; // 矩阵 B 的行索引 0 ~ K_dim-1

            // 卷积核循环 (K -> K -> C)
            for (int kh = 0; kh < K; ++kh) {
                for (int kw = 0; kw < K; ++kw) {
                    int h_in = h_in_start + kh;
                    int w_in = w_in_start + kw;
                    
                    // 边界检查 (处理 Padding)
                    if (h_in >= 0 && h_in < H && w_in >= 0 && w_in < W) {
                        // NHWC 寻址优化：C 是连续的
                        int pixel_offset = (h_in * W + w_in) * C;
                        
                        for (int c = 0; c < C; ++c) {
                            int8_t val = img[pixel_offset + c];
                            // B[matrix_row][patch_col] -> Row Major Storage
                            col_buf[matrix_row * n_patches + patch_col] = val;
                            matrix_row++;
                        }
                    } else {
                        // Padding 区域填 0
                        for (int c = 0; c < C; ++c) {
                            col_buf[matrix_row * n_patches + patch_col] = 0;
                            matrix_row++;
                        }
                    }
                }
            }
        }
    }
}

// Int8 * Int8 -> Int32 -> Scale -> Clip -> Int16
void matmul_int8_scale_clip(const int8_t *A, const int8_t *B, int16_t *C, 
                            int M, int N, int K, int scale) {
    for (int m = 0; m < M; ++m) {
        for (int n = 0; n < N; ++n) {
            
            // 1. 使用 int32 累加，防止溢出
            int32_t sum = 0;
            for (int k = 0; k < K; ++k) {
                sum += (int32_t)A[m * K + k] * (int32_t)B[k * N + n];
            }
            
            // 2. Scale (除法)
            if (scale != 0) {
                sum = sum / scale;
            }

            // 3. Clip (Saturate to int16 range)
            int32_t val = CLAMP(sum, 0, 32767);
            
            // 4. Cast & Store
            C[m * N + n] = (int16_t)val;
        }
    }
}

// Int8 * Int8 -> Int32 -> Scale -> Clip -> Int16 (Output NHWC)
void matmul_int8_scale_clip_nhwc(const int8_t *A, const int8_t *B, int16_t *C, 
                            int M, int N, int K, int scale) {
    // Output C is [N, M] (NHWC: Pixels x Channels)
    // A is [M, K]
    // B is [K, N]
    for (int n = 0; n < N; ++n) {
        for (int m = 0; m < M; ++m) {
            
            int32_t sum = 0;
            for (int k = 0; k < K; ++k) {
                sum += (int32_t)A[m * K + k] * (int32_t)B[k * N + n];
            }
            
            if (scale != 0) {
                sum = sum / scale;
            }

            int32_t val = CLAMP(sum, 0, 32767);
            
            C[n * M + m] = (int16_t)val;
        }
    }
}

// Int16 * Int16 -> Int32 -> Scale -> Int32
void matmul_int16_scale_clip(const int16_t *A, const int16_t *B, int32_t *C, 
                             int M, int N, int K, int scale) {
    for (int m = 0; m < M; ++m) {
        for (int n = 0; n < N; ++n) {
            
            // 1. Int16*Int16 可能超出 Int32，建议用 Int64 累加
            int64_t sum = 0;
            for (int k = 0; k < K; ++k) {
                sum += (int64_t)A[m * K + k] * (int64_t)B[k * N + n];
            }
            
            // 2. Scale
            if (scale != 0) {
                sum = sum / scale;
            }

            // 3. Clip (Output is int32)
            int64_t max_int32 = 2147483647LL;
            int64_t min_int32 = -2147483648LL;
            
            if (sum > max_int32) sum = max_int32;
            if (sum < min_int32) sum = min_int32;

            C[m * N + n] = (int32_t)sum;
        }
    }
}

// Int32 * Int32 -> Int64 -> Scale -> Int32
void matmul_int32_scale_clip(const int32_t *A, const int32_t *B, int32_t *C, 
                             int M, int N, int K, int scale) {
    for (int m = 0; m < M; ++m) {
        for (int n = 0; n < N; ++n) {
            
            // 1. 必须使用 Int64 累加
            int64_t sum = 0;
            for (int k = 0; k < K; ++k) {
                sum += (int64_t)A[m * K + k] * (int64_t)B[k * N + n];
            }
            
            // 2. Scale
            if (scale != 0) {
                sum = sum / scale;
            }
            
            // 3. Clip to Int32
            int64_t max_int32 = 2147483647LL;
            int64_t min_int32 = -2147483648LL;
            if (sum > max_int32) sum = max_int32;
            if (sum < min_int32) sum = min_int32;

            C[m * N + n] = (int32_t)sum;
        }
    }
}

void matadd_int32(const int32_t *A, const int32_t *B, int32_t *C, int len) {
    for (int i = 0; i < len; ++i) {
        C[i] = A[i] + B[i];
    }
}

void relu_int16(int16_t *data, int len) {
    for (int i = 0; i < len; ++i) {
        if (data[i] < 0) data[i] = 0;
    }
}

void relu_int32(int32_t *data, int len) {
    for (int i = 0; i < len; ++i) {
        if (data[i] < 0) data[i] = 0;
    }
}

void maxpool_int16(const int16_t *src, int16_t *dst, int C, int H, int W) {
    int H_out = H / 2;
    int W_out = W / 2;
    
    // NHWC Layout: H -> W -> C
    for (int h = 0; h < H_out; ++h) {
        for (int w = 0; w < W_out; ++w) {
            for (int c = 0; c < C; ++c) {
                // 2x2 窗口索引
                // Row 0
                int idx00 = ((h * 2 + 0) * W + (w * 2 + 0)) * C + c;
                int idx01 = ((h * 2 + 0) * W + (w * 2 + 1)) * C + c;
                // Row 1
                int idx10 = ((h * 2 + 1) * W + (w * 2 + 0)) * C + c;
                int idx11 = ((h * 2 + 1) * W + (w * 2 + 1)) * C + c;

                int16_t max_val = src[idx00];
                if (src[idx01] > max_val) max_val = src[idx01];
                if (src[idx10] > max_val) max_val = src[idx10];
                if (src[idx11] > max_val) max_val = src[idx11];
                
                // 输出索引
                int dst_idx = (h * W_out + w) * C + c;
                dst[dst_idx] = max_val;
            }
        }
    }
}

void transpose_NHWC_to_NCHW(const int16_t *src, int16_t *dst, int C, int H, int W) {
    for (int h = 0; h < H; h++) {
        for (int w = 0; w < W; w++) {
            for (int c = 0; c < C; c++) {
                // src index (NHWC): h * (W*C) + w * C + c
                // dst index (NCHW): c * (H*W) + h * W + w
                
                int src_idx = (h * W + w) * C + c;
                int dst_idx = c * (H * W) + h * W + w;
                
                dst[dst_idx] = src[src_idx];
            }
        }
    }
}

void flatten_int16(const int16_t *src, int16_t *dst, int len) {
    if (src != dst) {
        memcpy(dst, src, len * sizeof(int16_t));
    }
}

// 定义 Softmax 支持的最大类别数 (CIFAR-10只有10类，1024足够大且安全)
#define MAX_SOFTMAX_LEN 1024

// 使用静态缓冲区，避免栈溢出和动态分配
static int32_t x_shifted_buf[MAX_SOFTMAX_LEN];
static int32_t exp_vals_buf[MAX_SOFTMAX_LEN];

void softmax_hw(const int32_t *src, int32_t *dst, const int32_t *lut, int len) {
    // 安全检查：如果分类数量超过预设 Buffer 大小，直接报错并停止
    if (len > MAX_SOFTMAX_LEN) {
        printf("[Error] Softmax len exceeds MAX_SOFTMAX_LEN\n");
        halt(1); 
    }

    const int32_t Q = 16;
    const int32_t Q_16 = 1 << Q;
    const int32_t SAFE_SHIFT = 2;

    // 1. Clip & Shift & Find Max
    int32_t x_max = -2147483648; 

    for (int i = 0; i < len; ++i) {
        int32_t val = src[i];
        val = CLAMP(val, -32767, 32767);
        
        x_shifted_buf[i] = val << 16; // 写入静态 buffer
        if (x_shifted_buf[i] > x_max) x_max = x_shifted_buf[i];
    }

    // 2. Calculate Delta, Lookup, and Sum
    int32_t exp_sum = 0;

    for (int i = 0; i < len; ++i) {
        int32_t delta = x_shifted_buf[i] - x_max;
        
        // 限制 delta 范围以匹配 LUT
        int32_t lower_bound = -8 * Q_16;
        if (delta < lower_bound) delta = lower_bound;
        if (delta > 0) delta = 0; 

        // 计算 LUT 索引
        int64_t idx_num = (int64_t)(delta + 8 * Q_16) * (LUT_SIZE - 1);
        int32_t idx = (int32_t)(idx_num / (8 * Q_16));
        
        idx = CLAMP(idx, 0, LUT_SIZE - 1);
        
        exp_vals_buf[i] = lut[idx]; // 写入静态 buffer
        exp_sum += exp_vals_buf[i];
    }
    
    // 防止除以 0
    if (exp_sum <= 0) exp_sum = 1;

    // 3. Normalize (Division)
    int32_t exp_sum_shr = exp_sum >> SAFE_SHIFT;
    if (exp_sum_shr <= 0) exp_sum_shr = 1;

    for (int i = 0; i < len; ++i) {
        int32_t exp_delta_shr = exp_vals_buf[i] >> SAFE_SHIFT;
        int64_t num = (int64_t)exp_delta_shr * Q_16;
        dst[i] = (int32_t)(num / exp_sum_shr);
    }
}