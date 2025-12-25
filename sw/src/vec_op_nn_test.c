#include <am.h>
#include <klib.h>
#include "vec_op.h"

// ==========================================
// 1. 地址定义 (必须与 gen_data.py 和 仿真器加载地址一致)
// ==========================================
// 假设 data.bin 被加载到了 0x80800000
#define ADDR_BASE           0x80800000

#define ADDR_INPUT          (ADDR_BASE + 0x00000)
#define ADDR_WCONV1         (ADDR_BASE + 0x01000)
#define ADDR_SCONV1         (ADDR_BASE + 0x02000)
#define ADDR_WFC1           (ADDR_BASE + 0x03000)
#define ADDR_SFC1           (ADDR_BASE + 0x08000)
#define ADDR_WFC2           (ADDR_BASE + 0x09000)
#define ADDR_BFC2           (ADDR_BASE + 0x10000)
#define ADDR_SOFTMAX_LUT    (ADDR_BASE + 0x11000)

// ==========================================
// 2. 静态缓冲区 (Static Buffers)
// ==========================================
// 放在 .bss 段，防止栈溢出
// 所有缓冲区8字节对齐以支持向量指令

// Im2Col Buffer: [Kh*Kw*Cin, H_out*W_out] = [54, 144]
// Size: 54 * 144 = 7776 bytes
static int8_t col_buf[54 * 144] __attribute__((aligned(8)));

// Conv Output Transposed (NHWC): [H_out, W_out, Cout]
// 用于喂给 MaxPool
static int16_t conv_out_nhwc[12 * 12 * 4] __attribute__((aligned(8)));

// MaxPool Output (NHWC): [H/2, W/2, Cout] = [6, 6, 4]
static int16_t pool_out[6 * 6 * 4] __attribute__((aligned(8)));

// FC1 Output: 60
static int32_t fc1_out[60] __attribute__((aligned(8)));

// FC2 Output: 10
static int32_t fc2_out[10] __attribute__((aligned(8)));

// Softmax Output: 10
static int32_t softmax_out[10] __attribute__((aligned(8)));

// ==========================================
// 4. Main Inference Function (Vector Version)
// ==========================================

int main() {
    printf("\n=== RISC-V Neural Network Inference (VECTOR VERSION) ===\n");
    // ------------------------------------------
    // Layer 1: Conv2D + Scale + Clip
    // ------------------------------------------
    // Config: In(6,14,14), Out(4,12,12), Kernel(3x3)
    int Cin = 6, Hin = 14, Win = 14;
    int K = 3, Cout = 4;
    int H_out = 12, W_out = 12; // (14-3+1)
    
    printf("1. Executing Conv2D (Vector)...\n");
    
    // 1.1 Im2Col (Input -> Col Buffer) - VECTOR VERSION
    im2col_input_int8_vec((int8_t*)ADDR_INPUT, col_buf, Cin, Hin, Win, K);

    // 1.2 MatMul (Weights x ColBuffer) - VECTOR VERSION
    // Weight: [Cout, K*K*Cin] = [4, 54]
    // ColBuf: [K*K*Cin, H_out*W_out] = [54, 144]
    // Output: [4, 144] (NCHW format)
    int16_t *conv_scale_ptr = (int16_t*)ADDR_SCONV1;
    int32_t conv_scale = (int32_t)(*conv_scale_ptr);

    int M = H_out * W_out;  // 144
    int N_patches = Cout;   // 4
    int K_dim = K * K * Cin;   // 54

    matmul_int8_scale_clip_vec(
        col_buf,              
        (int8_t*)ADDR_WCONV1, 
        conv_out_nhwc,        
        M, N_patches, K_dim, 
        conv_scale
    );

    // ------------------------------------------
    // Layer 2: MaxPool 2x2
    // ------------------------------------------
    // Input: [12, 12, 4] (NHWC) -> Output: [6, 6, 4]
    printf("2. Executing MaxPool (Vector)...\n");
    maxpool_int16_vec(conv_out_nhwc, pool_out, Cout, H_out, W_out);

    // ------------------------------------------
    // Layer 3: Flatten & Transpose
    // ------------------------------------------
    // pool_out: [6, 6, 4] (NHWC)
    // conv_out_nchw: [4, 6, 6] (NCHW) - buffer is large enough (4*12*12)
    transpose_NHWC_to_NCHW_vec(pool_out, conv_out_nhwc, Cout, 6, 6);

    // ------------------------------------------
    // Layer 4: Fully Connected 1
    // ------------------------------------------
    // Input: 144. Output: 60.
    // Weight shape in memory: [144, 60] (Transposed by gen_data)
    // Calculation: Input_Row(1, 144) x Weight(144, 60) = Output(1, 60)
    printf("3. Executing FC1 (Vector)...\n");

    int32_t *fc1_scale_ptr = (int32_t*)ADDR_SFC1;
    int fc1_in_features = 144;
    int fc1_out_features = 60;

    matmul_int16_scale_clip_vec(
        conv_out_nhwc,       // Matrix A (Input Vector treated as 1xK) - NOW NCHW
        (int16_t*)ADDR_WFC1, // Matrix B (Weights)
        fc1_out,             // Matrix C
        1,                   // M = 1 row
        fc1_out_features,    // N = 60 cols
        fc1_in_features,     // K = 144 shared dim
        *fc1_scale_ptr
    );

    // 4.1 ReLU - VECTOR VERSION
    relu_int32_vec(fc1_out, fc1_out_features);

    // ------------------------------------------
    // Layer 5: Fully Connected 2
    // ------------------------------------------
    // Input: 60. Output: 10.
    // Weight shape: [60, 10] (Transposed)
    printf("4. Executing FC2 (Vector)...\n");
    
    int fc2_in_features = 60;
    int fc2_out_features = 10;

    // MatMul (Scale = 0 means no division) - VECTOR VERSION
    matmul_int32_scale_clip_vec(
        fc1_out,             // Input
        (int32_t*)ADDR_WFC2, // Weight
        fc2_out,             // Output
        1, 
        fc2_out_features, 
        fc2_in_features, 
        0
    );

    // Bias Add - VECTOR VERSION
    matadd_int32_vec(fc2_out, (int32_t*)ADDR_BFC2, fc2_out, fc2_out_features);

    printf("\n=== FC2 Output (pre-Softmax) - VECTOR ===\n");
    for (int i = 0; i < fc2_out_features; i++) {
        printf("FC2[%d]: %d\n", i, fc2_out[i]);
    }

    // ------------------------------------------
    // Layer 6: Softmax
    // ------------------------------------------
    printf("5. Executing Softmax (Vector)...\n");
    softmax_hw_vec(fc2_out, softmax_out, (int32_t*)ADDR_SOFTMAX_LUT, fc2_out_features);

    // ------------------------------------------
    // 结果打印
    // ------------------------------------------
    printf("\n=== Softmax Result - VECTOR ===\n");
    for (int i = 0; i < 10; i++) {
        printf("Class %d: %d\n", i, softmax_out[i]);
    }
    
    // 找出预测类别
    int max_idx = 0;
    int32_t max_val = softmax_out[0];
    for (int i = 1; i < 10; i++) {
        if (softmax_out[i] > max_val) {
            max_val = softmax_out[i];
            max_idx = i;
        }
    }
    
    printf("\n=== Prediction ===\n");
    printf("Predicted Class: %d (Confidence: %d)\n", max_idx, max_val);
    printf("==================\n");

    return 0;
}
