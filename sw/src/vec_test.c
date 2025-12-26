#include "trap.h"
#include "model.h"
#include "rvv_vec.h"
#include "vec_op.h"
#include "scale_op.h"
#include <am.h>
#include <klib.h>
#include <stdint.h>

// Base address for bare-metal test buffers
#define ADDR_BASE_U 0x80800000U
#define A_OFF  0x00000U   // Matrix A region (int8)
#define B_OFF  0x10000U   // Matrix B region (int8)
#define C_REF_OFF 0x20000U // Reference output (int16)
#define C_VEC_OFF 0x30000U // Vector output (int16)

// Run one matmul test: A(MxK) * B(KxN) -> C(MxN)
static int run_matmul_case(int M, int N, int K, int scale) {
    int8_t *A = (int8_t *)(uintptr_t)(ADDR_BASE_U + A_OFF);
    int8_t *B = (int8_t *)(uintptr_t)(ADDR_BASE_U + B_OFF);
    int16_t *C_ref = (int16_t *)(uintptr_t)(ADDR_BASE_U + C_REF_OFF);
    int16_t *C_vec = (int16_t *)(uintptr_t)(ADDR_BASE_U + C_VEC_OFF);

    int a_size = M * K;
    int b_size = K * N;
    int c_size = M * N;

    // initialize A and B with deterministic pattern
    for (int i = 0; i < a_size; ++i) A[i] = (int8_t)((i * 37 + 3) & 0xFF);
    for (int i = 0; i < b_size; ++i) B[i] = (int8_t)(((i * 17) - 5) & 0xFF);

    // initialize outputs with sentinels
    for (int i = 0; i < c_size; ++i) C_ref[i] = (int16_t)0x1234;
    for (int i = 0; i < c_size; ++i) C_vec[i] = (int16_t)0x4321;

    // Call reference scalar implementation
    matmul_int8_scale_clip(A, B, C_ref, M, N, K, scale);

    // Call vector implementation
    matmul_int8_scale_clip_vec(A, B, C_vec, M, N, K, scale);

    // Compare
    for (int i = 0; i < c_size; ++i) {
        if (C_ref[i] != C_vec[i]) {
            printf("[FAIL] matmul mismatch M=%d N=%d K=%d idx=%d ref=%d vec=%d\n", M, N, K, i, C_ref[i], C_vec[i]);
            return 1;
        }
    }
    printf("[PASS] matmul M=%d N=%d K=%d\n", M, N, K);
    return 0;
}

int main() {
    int failures = 0;

    // Deterministic matmul test cases (cover small/medium/edges)
    struct { int M,N,K,scale; } cases[] = {
        {1, 1, 1, 1},
        {1, 4, 3, 1},
        {4, 4, 3, 1},
        {5, 3, 7, 2},
        {8, 8, 8, 4},
        {9, 7, 5, 3},
        {12, 6, 9, 5},
        {3, 10, 2, 2},
        {16, 16, 16, 8},
        {15, 15, 15, 1},
    };

    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
        failures += run_matmul_case(cases[i].M, cases[i].N, cases[i].K, cases[i].scale);
    }

    // Randomized deterministic tests
    srand(20231225);
    for (int t = 0; t < 30; ++t) {
        int M = 1 + (rand() % 16);
        int N = 1 + (rand() % 16);
        int K = 1 + (rand() % 16);
        int scale = (rand() % 5);
        failures += run_matmul_case(M, N, K, scale);
    }

    if (failures == 0) printf("All matmul_int8_scale_clip_vec tests passed.\n");
    else printf("%d matmul_int8_scale_clip_vec test(s) failed.\n", failures);

    return failures;
}