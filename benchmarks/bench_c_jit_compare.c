/* Direct comparison with Pseudocode JIT benchmarks */
#include <stdio.h>
#include <time.h>
#include <stdint.h>

#define ITERATIONS 100000000LL

static inline int64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

int main(void) {
    printf("=== C Baseline (1e8 iterations) ===\n\n");
    
    // 1. Inc loop: x = x + 1
    {
        volatile int64_t x = 0;
        int64_t start = now_ns();
        for (int64_t i = 0; i < ITERATIONS; i++) {
            x = x + 1;
        }
        int64_t end = now_ns();
        printf("Inc loop:    %8.2f ms  (x=%lld)\n", (end - start) / 1e6, (long long)x);
    }
    
    // 2. Arith loop: x = x * 3 + 7
    {
        volatile int64_t x = 0;
        int64_t start = now_ns();
        for (int64_t i = 0; i < ITERATIONS; i++) {
            x = x * 3 + 7;
        }
        int64_t end = now_ns();
        printf("Arith loop:  %8.2f ms  (x=%lld)\n", (end - start) / 1e6, (long long)x);
    }
    
    // 3. Branch loop: if i&1 then x += 1 else x -= 1
    {
        volatile int64_t x = 0;
        int64_t start = now_ns();
        for (int64_t i = 0; i < ITERATIONS; i++) {
            if ((i & 1) == 0) {
                x = x + 1;
            } else {
                x = x - 1;
            }
        }
        int64_t end = now_ns();
        printf("Branch loop: %8.2f ms  (x=%lld)\n", (end - start) / 1e6, (long long)x);
    }
    
    printf("\n=== Done ===\n");
    return 0;
}
