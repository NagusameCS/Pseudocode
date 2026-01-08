#!/usr/bin/env python3
"""
Python Benchmark Suite - Comparison Reference
"""

import time

BILLION = 1_000_000_000
HUNDRED_MILLION = 100_000_000
TEN_MILLION = 10_000_000
MILLION = 1_000_000

def now_ms():
    return time.perf_counter() * 1000

# 1. Empty loop (reduced for Python)
def bench_empty_loop():
    N = 100_000_000  # 1e8 for Python (1e9 too slow)
    start = now_ms()
    x = 0
    for i in range(N):
        x += 1
    end = now_ms()
    print(f"1. Empty loop (1e8):      {end - start:8.2f} ms  (x={x})")

# 2. Function call cost
def f(x):
    return x + 1

def bench_function_call():
    N = 10_000_000  # 1e7 for Python
    start = now_ms()
    x = 0
    for i in range(N):
        x = f(x)
    end = now_ms()
    print(f"2. Function call (1e7):   {end - start:8.2f} ms  (x={x})")

# 3. Integer arithmetic
def bench_int_arith():
    N = 100_000_000  # 1e8 for Python
    start = now_ms()
    x = 1
    for i in range(N):
        x = (x * 3 + 7) & 0xFFFFFFFFFFFFFFFF  # Keep in 64-bit
    end = now_ms()
    print(f"3. Int arithmetic (1e8):  {end - start:8.2f} ms  (x={x})")

# 4. Array traversal
def bench_array_read():
    arr = list(range(TEN_MILLION))
    start = now_ms()
    s = 0
    for i in range(len(arr)):
        s += arr[i]
    end = now_ms()
    print(f"4. Array read (1e7):      {end - start:8.2f} ms  (sum={s})")

# 5. Array write
def bench_array_write():
    arr = [0] * TEN_MILLION
    start = now_ms()
    for i in range(len(arr)):
        arr[i] = i
    end = now_ms()
    print(f"5. Array write (1e7):     {end - start:8.2f} ms  (arr[0]={arr[0]})")

# 6. Object access
class Point:
    __slots__ = ['x', 'y']
    def __init__(self, x, y):
        self.x = x
        self.y = y

def bench_object_access():
    N = 100_000_000  # 1e8 for Python
    p = Point(1, 2)
    start = now_ms()
    for i in range(N):
        p.x += 1
    end = now_ms()
    print(f"6. Object access (1e8):   {end - start:8.2f} ms  (p.x={p.x})")

# 7. Branching
def bench_branching():
    N = 100_000_000  # 1e8 for Python
    start = now_ms()
    x = 0
    for i in range(N):
        if (i & 1) == 0:
            x += 1
        else:
            x -= 1
    end = now_ms()
    print(f"7. Branching (1e8):       {end - start:8.2f} ms  (x={x})")

# 8. Allocation
def bench_allocation():
    start = now_ms()
    for i in range(TEN_MILLION):
        p = Point(i, i + 1)
    end = now_ms()
    print(f"8. Allocation (1e7):      {end - start:8.2f} ms")

# 9. String concatenation
def bench_string_concat():
    N = 100_000  # 1e5 for Python (quadratic)
    start = now_ms()
    s = ""
    for i in range(N):
        s = s + "a"
    end = now_ms()
    print(f"9. String concat (1e5):   {end - start:8.2f} ms  (len={len(s)})")

# 10. HashMap
def bench_hashmap():
    start = now_ms()
    m = {}
    for i in range(TEN_MILLION):
        m[i] = i
    x = 0
    for i in range(TEN_MILLION):
        x += m[i]
    end = now_ms()
    print(f"10. HashMap (1e7):        {end - start:8.2f} ms  (x={x})")

# 11. Recursion
import sys
sys.setrecursionlimit(20000)

def recurse(n):
    if n == 0:
        return 0
    return recurse(n - 1) + 1

def bench_recursion():
    start = now_ms()
    result = 0
    for i in range(100):  # Reduced iterations
        result += recurse(5000)  # Reduced depth
    end = now_ms()
    print(f"11. Recursion (100*5k):   {end - start:8.2f} ms  (result={result})")

# 12. Mixed workload
def bench_mixed():
    arr = [0] * TEN_MILLION
    start = now_ms()
    for i in range(1, TEN_MILLION):
        if i % 3 == 0:
            arr[i] = i * 2
        else:
            arr[i] = arr[i - 1] + 1
    end = now_ms()
    print(f"12. Mixed (1e7):          {end - start:8.2f} ms  (arr[last]={arr[-1]})")

if __name__ == "__main__":
    print("=== Python Benchmark Suite ===\n")
    
    bench_empty_loop()
    bench_function_call()
    bench_int_arith()
    bench_array_read()
    bench_array_write()
    bench_object_access()
    bench_branching()
    bench_allocation()
    bench_string_concat()
    bench_hashmap()
    bench_recursion()
    bench_mixed()
    
    print("\n=== Done ===")
