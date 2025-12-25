#ifndef NN_OPS_H
#define NN_OPS_H

#include <stdint.h>
#include <stddef.h>

// ==========================================
// 宏定义与工具
// ==========================================
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define CLAMP(x, min, max) (MAX(MIN(x, max), min))

// Softmax 查找表大小 (需与 gen_data.py 一致)
#define LUT_SIZE 256

/**
 * @brief 权重预处理 (Flatten)
 * 对应 gen_data.py 中的 NHWC 权重 [N, K, K, C]
 * 在这里只是将其视为线性内存，准备由 MatMul 读取
 */
void im2col_weight_int8(const int8_t *weight, int8_t *col_buf, 
                        int N, int C, int K);

/**
 * @brief 输入图片重排 (NHWC 格式)
 * 将输入图片展开为列向量矩阵，以便进行卷积矩阵乘法
 * Input Layout: [Batch, Height, Width, Channel]
 * Output Layout: [ (Kh * Kw * C), (H_out * W_out) ] (逻辑上的矩阵B)
 * @param img     输入图片指针
 * @param col_buf 输出 buffer 指针
 * @param C       Input Channels
 * @param H       Input Height
 * @param W       Input Width
 * @param K       Kernel size (假设为正方形卷积核 KxK)
 */
void im2col_input_int8(const int8_t *img, int8_t *col_buf, 
                       int C, int H, int W, int K);

/**
 * @brief 通用矩阵乘法: C = A * B
 * A 维度: [M, K]
 * B 维度: [K, N]
 * C 维度: [M, N]
 */
void matmul_int8_scale_clip(const int8_t *A, const int8_t *B, int16_t *C, int M, int N, int K, int scale);
void matmul_int16_scale_clip(const int16_t *A, const int16_t *B, int32_t *C, int M, int N, int K, int scale);
void matmul_int32_scale_clip(const int32_t *A, const int32_t *B, int32_t *C, int M, int N, int K, int scale);

/**
 * @brief 矩阵元素级加法 (通常用于 Bias Add)
 * C = A + B
 * @param len 元素总数
 */
void matadd_int32(const int32_t *A, const int32_t *B, int32_t *C, int len);

// ReLU: max(0, x)
void relu_int16(int16_t *data, int len);
void relu_int32(int32_t *data, int len);

/**
 * @brief MaxPool 2x2 (NHWC 格式)
 * Input:  [H, W, C]
 * Output: [H/2, W/2, C]
 */
void maxpool_int16(const int16_t *src, int16_t *dst, int C, int H, int W);

/**
 * @brief Transpose NHWC to NCHW
 * Input:  [H, W, C]
 * Output: [C, H, W]
 */
void transpose_NHWC_to_NCHW(const int16_t *src, int16_t *dst, int C, int H, int W);

/**
 * @brief Flatten (逻辑展平)
 * 将多维数组拷贝为一维连续数组
 */
void flatten_int16(const int16_t *src, int16_t *dst, int len);

/**
 * @brief 硬件风格定点 Softmax
 * @param src 输入 (int32)
 * @param dst 输出 (int32, Q16.16格式)
 * @param lut 指向 Exp Lookup Table 的指针
 * @param len 向量长度 (类别数)
 */
void softmax_hw(const int32_t *src, int32_t *dst, const int32_t *lut, int len);

/**
 * @brief 矩阵转置 (int8)
 * @param src 源矩阵 [M, N]
 * @param dst 目标矩阵 [N, M]
 * @param M 源矩阵行数
 * @param N 源矩阵列数
 */
void transpose_int8(const int8_t *src, int8_t *dst, int M, int N);

#endif // NN_OPS_H