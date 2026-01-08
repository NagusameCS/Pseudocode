/*
 * C Benchmark Suite - Baseline Reference
 * Compile: gcc -O3 -march=native -o bench_c bench_c.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#define BILLION 1000000000LL
#define HUNDRED_MILLION 100000000LL
#define TEN_MILLION 10000000LL
#define MILLION 1000000LL

static inline int64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

/* 1. Empty loop */
void bench_empty_loop(void) {
    int64_t start = now_ns();
    volatile int64_t x = 0;
    for (int64_t i = 0; i < BILLION; i++) {
        x += 1;
    }
    int64_t end = now_ns();
    printf("1. Empty loop (1e9):      %8.2f ms  (x=%lld)\n", (end - start) / 1e6, (long long)x);
}

/* 2. Function call cost */
static inline int64_t f_inline(int64_t x) { return x + 1; }
__attribute__((noinline)) int64_t f_noinline(int64_t x) { return x + 1; }

void bench_function_call(void) {
    int64_t start = now_ns();
    int64_t x = 0;
    for (int64_t i = 0; i < HUNDRED_MILLION; i++) {
        x = f_noinline(x);
    }
    int64_t end = now_ns();
    printf("2. Function call (1e8):   %8.2f ms  (x=%lld)\n", (end - start) / 1e6, (long long)x);
}

/* 3. Integer arithmetic */
void bench_int_arith(void) {
    int64_t start = now_ns();
    int64_t x = 1;
    for (int64_t i = 0; i < BILLION; i++) {
        x = x * 3 + 7;
    }
    int64_t end = now_ns();
    printf("3. Int arithmetic (1e9):  %8.2f ms  (x=%lld)\n", (end - start) / 1e6, (long long)x);
}

/* 4. Array traversal */
void bench_array_read(void) {
    int64_t* arr = malloc(TEN_MILLION * sizeof(int64_t));
    for (int64_t i = 0; i < TEN_MILLION; i++) arr[i] = i;
    
    int64_t start = now_ns();
    int64_t sum = 0;
    for (int64_t i = 0; i < TEN_MILLION; i++) {
        sum += arr[i];
    }
    int64_t end = now_ns();
    printf("4. Array read (1e7):      %8.2f ms  (sum=%lld)\n", (end - start) / 1e6, (long long)sum);
    free(arr);
}

/* 5. Array write */
void bench_array_write(void) {
    int64_t* arr = malloc(TEN_MILLION * sizeof(int64_t));
    
    int64_t start = now_ns();
    for (int64_t i = 0; i < TEN_MILLION; i++) {
        arr[i] = i;
    }
    int64_t end = now_ns();
    printf("5. Array write (1e7):     %8.2f ms  (arr[0]=%lld)\n", (end - start) / 1e6, (long long)arr[0]);
    free(arr);
}

/* 6. Struct access */
typedef struct { int64_t x; int64_t y; } Point;

void bench_struct_access(void) {
    Point p = {1, 2};
    
    int64_t start = now_ns();
    for (int64_t i = 0; i < BILLION; i++) {
        p.x += 1;
    }
    int64_t end = now_ns();
    printf("6. Struct access (1e9):   %8.2f ms  (p.x=%lld)\n", (end - start) / 1e6, (long long)p.x);
}

/* 7. Branch-heavy code */
void bench_branching(void) {
    int64_t start = now_ns();
    int64_t x = 0;
    for (int64_t i = 0; i < BILLION; i++) {
        if ((i & 1) == 0) {
            x += 1;
        } else {
            x -= 1;
        }
    }
    int64_t end = now_ns();
    printf("7. Branching (1e9):       %8.2f ms  (x=%lld)\n", (end - start) / 1e6, (long long)x);
}

/* 8. Allocation stress */
void bench_allocation(void) {
    int64_t start = now_ns();
    for (int64_t i = 0; i < TEN_MILLION; i++) {
        Point* p = malloc(sizeof(Point));
        p->x = i;
        p->y = i + 1;
        free(p);  /* Immediate free to avoid OOM */
    }
    int64_t end = now_ns();
    printf("8. Allocation (1e7):      %8.2f ms\n", (end - start) / 1e6);
}

/* 9. String concatenation - C uses buffer approach */
void bench_string_concat(void) {
    int64_t start = now_ns();
    char* s = malloc(MILLION + 1);
    for (int64_t i = 0; i < MILLION; i++) {
        s[i] = 'a';
    }
    s[MILLION] = '\0';
    int64_t end = now_ns();
    printf("9. String build (1e6):    %8.2f ms  (len=%zu)\n", (end - start) / 1e6, strlen(s));
    free(s);
}

/* 10. Hash map - simple open addressing */
#define HASH_SIZE 16777216
typedef struct { int64_t key; int64_t value; int used; } Entry;

void bench_hashmap(void) {
    Entry* map = calloc(HASH_SIZE, sizeof(Entry));
    
    int64_t start = now_ns();
    /* Insert */
    for (int64_t i = 0; i < TEN_MILLION; i++) {
        uint64_t h = (i * 2654435761ULL) & (HASH_SIZE - 1);
        while (map[h].used) h = (h + 1) & (HASH_SIZE - 1);
        map[h].key = i;
        map[h].value = i;
        map[h].used = 1;
    }
    /* Lookup */
    int64_t x = 0;
    for (int64_t i = 0; i < TEN_MILLION; i++) {
        uint64_t h = (i * 2654435761ULL) & (HASH_SIZE - 1);
        while (map[h].key != i) h = (h + 1) & (HASH_SIZE - 1);
        x += map[h].value;
    }
    int64_t end = now_ns();
    printf("10. HashMap (1e7):        %8.2f ms  (x=%lld)\n", (end - start) / 1e6, (long long)x);
    free(map);
}

/* 11. Recursion */
int64_t recurse(int64_t n) {
    if (n == 0) return 0;
    return recurse(n - 1) + 1;
}

void bench_recursion(void) {
    int64_t start = now_ns();
    int64_t result = 0;
    for (int i = 0; i < 1000; i++) {
        result += recurse(10000);
    }
    int64_t end = now_ns();
    printf("11. Recursion (1k*10k):   %8.2f ms  (result=%lld)\n", (end - start) / 1e6, (long long)result);
}

/* 12. Mixed workload */
void bench_mixed(void) {
    int64_t* arr = calloc(TEN_MILLION, sizeof(int64_t));
    
    int64_t start = now_ns();
    for (int64_t i = 1; i < TEN_MILLION; i++) {
        if (i % 3 == 0) {
            arr[i] = i * 2;
        } else {
            arr[i] = arr[i - 1] + 1;
        }
    }
    int64_t end = now_ns();
    printf("12. Mixed (1e7):          %8.2f ms  (arr[last]=%lld)\n", (end - start) / 1e6, (long long)arr[TEN_MILLION-1]);
    free(arr);
}

int main(void) {
    printf("=== C Benchmark Suite ===\n\n");
    
    bench_empty_loop();
    bench_function_call();
    bench_int_arith();
    bench_array_read();
    bench_array_write();
    bench_struct_access();
    bench_branching();
    bench_allocation();
    bench_string_concat();
    bench_hashmap();
    bench_recursion();
    bench_mixed();
    
    printf("\n=== Done ===\n");
    return 0;
}
