/*
 * Pseudocode Language - Tensor and Matrix Operations
 * High-performance scientific computing with optional SIMD acceleration
 *
 * Copyright (c) 2026 NagusameCS
 * Licensed under the MIT License
 */

#define _GNU_SOURCE /* For M_PI */
#include "pseudo.h"
#include "tensor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Alias for memory allocation */
#define reallocate pseudo_realloc

/* SIMD support detection */
#if defined(__AVX2__)
#include <immintrin.h>
#define HAVE_AVX2 1
#elif defined(__SSE4_1__)
#include <smmintrin.h>
#define HAVE_SSE4 1
#endif

/* Forward declarations */
static void tensor_free_data(ObjTensor *tensor);

/* ============ Memory Allocation ============ */

static double *alloc_aligned(size_t count)
{
    /* Align to 32 bytes for AVX */
    void *ptr = NULL;
    if (posix_memalign(&ptr, 32, count * sizeof(double)) != 0)
    {
        return NULL;
    }
    return (double *)ptr;
}

/* ============ Tensor Creation ============ */

ObjTensor *tensor_create(VM *vm, uint32_t ndim, const uint32_t *shape)
{
    ObjTensor *tensor = (ObjTensor *)reallocate(vm, NULL, 0, sizeof(ObjTensor));
    tensor->obj.type = OBJ_TENSOR;
    tensor->obj.next = vm->objects;
    tensor->obj.marked = false;
    vm->objects = (Obj *)tensor;

    tensor->ndim = ndim;
    tensor->size = 1;

    for (uint32_t i = 0; i < ndim; i++)
    {
        tensor->shape[i] = shape[i];
        tensor->size *= shape[i];
    }

    /* Compute strides (row-major) */
    int64_t stride = 1;
    for (int i = ndim - 1; i >= 0; i--)
    {
        tensor->strides[i] = stride;
        stride *= shape[i];
    }

    tensor->data = alloc_aligned(tensor->size);
    tensor->owns_data = true;
    tensor->requires_grad = false;
    tensor->grad = NULL;

    return tensor;
}

ObjTensor *tensor_zeros(VM *vm, uint32_t ndim, const uint32_t *shape)
{
    ObjTensor *tensor = tensor_create(vm, ndim, shape);
    memset(tensor->data, 0, tensor->size * sizeof(double));
    return tensor;
}

ObjTensor *tensor_ones(VM *vm, uint32_t ndim, const uint32_t *shape)
{
    ObjTensor *tensor = tensor_create(vm, ndim, shape);
    for (uint32_t i = 0; i < tensor->size; i++)
    {
        tensor->data[i] = 1.0;
    }
    return tensor;
}

ObjTensor *tensor_rand(VM *vm, uint32_t ndim, const uint32_t *shape)
{
    ObjTensor *tensor = tensor_create(vm, ndim, shape);
    for (uint32_t i = 0; i < tensor->size; i++)
    {
        tensor->data[i] = (double)rand() / RAND_MAX;
    }
    return tensor;
}

ObjTensor *tensor_randn(VM *vm, uint32_t ndim, const uint32_t *shape)
{
    ObjTensor *tensor = tensor_create(vm, ndim, shape);
    /* Box-Muller transform for normal distribution */
    for (uint32_t i = 0; i < tensor->size; i += 2)
    {
        double u1 = (double)rand() / RAND_MAX;
        double u2 = (double)rand() / RAND_MAX;
        if (u1 < 1e-10)
            u1 = 1e-10;
        double z0 = sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
        double z1 = sqrt(-2.0 * log(u1)) * sin(2.0 * M_PI * u2);
        tensor->data[i] = z0;
        if (i + 1 < tensor->size)
            tensor->data[i + 1] = z1;
    }
    return tensor;
}

ObjTensor *tensor_arange(VM *vm, double start, double stop, double step)
{
    uint32_t count = (uint32_t)ceil((stop - start) / step);
    uint32_t shape[1] = {count};
    ObjTensor *tensor = tensor_create(vm, 1, shape);
    for (uint32_t i = 0; i < count; i++)
    {
        tensor->data[i] = start + i * step;
    }
    return tensor;
}

ObjTensor *tensor_linspace(VM *vm, double start, double stop, uint32_t num)
{
    uint32_t shape[1] = {num};
    ObjTensor *tensor = tensor_create(vm, 1, shape);
    double step = (stop - start) / (num - 1);
    for (uint32_t i = 0; i < num; i++)
    {
        tensor->data[i] = start + i * step;
    }
    return tensor;
}

ObjTensor *tensor_from_array(VM *vm, ObjArray *arr)
{
    uint32_t shape[1] = {arr->count};
    ObjTensor *tensor = tensor_create(vm, 1, shape);
    for (uint32_t i = 0; i < arr->count; i++)
    {
        Value v = arr->values[i];
        if (IS_NUM(v))
            tensor->data[i] = as_num(v);
        else if (IS_INT(v))
            tensor->data[i] = (double)as_int(v);
        else
            tensor->data[i] = 0.0;
    }
    return tensor;
}

/* ============ SIMD-Accelerated Operations ============ */

#ifdef HAVE_AVX2
/* AVX2 vectorized operations - 4 doubles at a time */

static void tensor_add_avx2(double *__restrict out,
                            const double *__restrict a,
                            const double *__restrict b,
                            size_t n)
{
    size_t i = 0;
    for (; i + 4 <= n; i += 4)
    {
        __m256d va = _mm256_load_pd(a + i);
        __m256d vb = _mm256_load_pd(b + i);
        __m256d vr = _mm256_add_pd(va, vb);
        _mm256_store_pd(out + i, vr);
    }
    for (; i < n; i++)
    {
        out[i] = a[i] + b[i];
    }
}

static void tensor_sub_avx2(double *__restrict out,
                            const double *__restrict a,
                            const double *__restrict b,
                            size_t n)
{
    size_t i = 0;
    for (; i + 4 <= n; i += 4)
    {
        __m256d va = _mm256_load_pd(a + i);
        __m256d vb = _mm256_load_pd(b + i);
        __m256d vr = _mm256_sub_pd(va, vb);
        _mm256_store_pd(out + i, vr);
    }
    for (; i < n; i++)
    {
        out[i] = a[i] - b[i];
    }
}

static void tensor_mul_avx2(double *__restrict out,
                            const double *__restrict a,
                            const double *__restrict b,
                            size_t n)
{
    size_t i = 0;
    for (; i + 4 <= n; i += 4)
    {
        __m256d va = _mm256_load_pd(a + i);
        __m256d vb = _mm256_load_pd(b + i);
        __m256d vr = _mm256_mul_pd(va, vb);
        _mm256_store_pd(out + i, vr);
    }
    for (; i < n; i++)
    {
        out[i] = a[i] * b[i];
    }
}

static double tensor_dot_avx2(const double *__restrict a,
                              const double *__restrict b,
                              size_t n)
{
    __m256d sum = _mm256_setzero_pd();
    size_t i = 0;
    for (; i + 4 <= n; i += 4)
    {
        __m256d va = _mm256_load_pd(a + i);
        __m256d vb = _mm256_load_pd(b + i);
        sum = _mm256_fmadd_pd(va, vb, sum); /* FMA: sum += a * b */
    }
    /* Horizontal sum */
    double result[4];
    _mm256_store_pd(result, sum);
    double total = result[0] + result[1] + result[2] + result[3];
    for (; i < n; i++)
    {
        total += a[i] * b[i];
    }
    return total;
}

static double tensor_sum_avx2(const double *__restrict a, size_t n)
{
    __m256d sum = _mm256_setzero_pd();
    size_t i = 0;
    for (; i + 4 <= n; i += 4)
    {
        __m256d va = _mm256_load_pd(a + i);
        sum = _mm256_add_pd(sum, va);
    }
    double result[4];
    _mm256_store_pd(result, sum);
    double total = result[0] + result[1] + result[2] + result[3];
    for (; i < n; i++)
    {
        total += a[i];
    }
    return total;
}

#endif /* HAVE_AVX2 */

/* Scalar fallback implementations */
static void tensor_add_scalar(double *out, const double *a, const double *b, size_t n)
{
    for (size_t i = 0; i < n; i++)
        out[i] = a[i] + b[i];
}

static void tensor_sub_scalar(double *out, const double *a, const double *b, size_t n)
{
    for (size_t i = 0; i < n; i++)
        out[i] = a[i] - b[i];
}

static void tensor_mul_scalar(double *out, const double *a, const double *b, size_t n)
{
    for (size_t i = 0; i < n; i++)
        out[i] = a[i] * b[i];
}

static double tensor_dot_scalar(const double *a, const double *b, size_t n)
{
    double sum = 0.0;
    for (size_t i = 0; i < n; i++)
        sum += a[i] * b[i];
    return sum;
}

static double tensor_sum_scalar(const double *a, size_t n)
{
    double sum = 0.0;
    for (size_t i = 0; i < n; i++)
        sum += a[i];
    return sum;
}

/* Dispatch to SIMD or scalar */
void tensor_add(double *out, const double *a, const double *b, size_t n)
{
#ifdef HAVE_AVX2
    tensor_add_avx2(out, a, b, n);
#else
    tensor_add_scalar(out, a, b, n);
#endif
}

void tensor_sub(double *out, const double *a, const double *b, size_t n)
{
#ifdef HAVE_AVX2
    tensor_sub_avx2(out, a, b, n);
#else
    tensor_sub_scalar(out, a, b, n);
#endif
}

void tensor_mul(double *out, const double *a, const double *b, size_t n)
{
#ifdef HAVE_AVX2
    tensor_mul_avx2(out, a, b, n);
#else
    tensor_mul_scalar(out, a, b, n);
#endif
}

double tensor_dot(const double *a, const double *b, size_t n)
{
#ifdef HAVE_AVX2
    return tensor_dot_avx2(a, b, n);
#else
    return tensor_dot_scalar(a, b, n);
#endif
}

double tensor_sum(const double *a, size_t n)
{
#ifdef HAVE_AVX2
    return tensor_sum_avx2(a, n);
#else
    return tensor_sum_scalar(a, n);
#endif
}

/* ============ Tensor Operations ============ */

ObjTensor *tensor_add_tensors(VM *vm, ObjTensor *a, ObjTensor *b)
{
    if (a->size != b->size)
        return NULL;
    ObjTensor *result = tensor_create(vm, a->ndim, a->shape);
    tensor_add(result->data, a->data, b->data, a->size);
    return result;
}

ObjTensor *tensor_sub_tensors(VM *vm, ObjTensor *a, ObjTensor *b)
{
    if (a->size != b->size)
        return NULL;
    ObjTensor *result = tensor_create(vm, a->ndim, a->shape);
    tensor_sub(result->data, a->data, b->data, a->size);
    return result;
}

ObjTensor *tensor_mul_tensors(VM *vm, ObjTensor *a, ObjTensor *b)
{
    if (a->size != b->size)
        return NULL;
    ObjTensor *result = tensor_create(vm, a->ndim, a->shape);
    tensor_mul(result->data, a->data, b->data, a->size);
    return result;
}

ObjTensor *tensor_div_tensors(VM *vm, ObjTensor *a, ObjTensor *b)
{
    if (a->size != b->size)
        return NULL;
    ObjTensor *result = tensor_create(vm, a->ndim, a->shape);
    for (size_t i = 0; i < a->size; i++)
    {
        result->data[i] = a->data[i] / b->data[i];
    }
    return result;
}

ObjTensor *tensor_scale(VM *vm, ObjTensor *a, double scalar)
{
    ObjTensor *result = tensor_create(vm, a->ndim, a->shape);
    for (size_t i = 0; i < a->size; i++)
    {
        result->data[i] = a->data[i] * scalar;
    }
    return result;
}

ObjTensor *tensor_neg(VM *vm, ObjTensor *a)
{
    ObjTensor *result = tensor_create(vm, a->ndim, a->shape);
    for (size_t i = 0; i < a->size; i++)
    {
        result->data[i] = -a->data[i];
    }
    return result;
}

ObjTensor *tensor_abs(VM *vm, ObjTensor *a)
{
    ObjTensor *result = tensor_create(vm, a->ndim, a->shape);
    for (size_t i = 0; i < a->size; i++)
    {
        result->data[i] = fabs(a->data[i]);
    }
    return result;
}

ObjTensor *tensor_sqrt_op(VM *vm, ObjTensor *a)
{
    ObjTensor *result = tensor_create(vm, a->ndim, a->shape);
    for (size_t i = 0; i < a->size; i++)
    {
        result->data[i] = sqrt(a->data[i]);
    }
    return result;
}

ObjTensor *tensor_exp_op(VM *vm, ObjTensor *a)
{
    ObjTensor *result = tensor_create(vm, a->ndim, a->shape);
    for (size_t i = 0; i < a->size; i++)
    {
        result->data[i] = exp(a->data[i]);
    }
    return result;
}

ObjTensor *tensor_log_op(VM *vm, ObjTensor *a)
{
    ObjTensor *result = tensor_create(vm, a->ndim, a->shape);
    for (size_t i = 0; i < a->size; i++)
    {
        result->data[i] = log(a->data[i]);
    }
    return result;
}

ObjTensor *tensor_pow_op(VM *vm, ObjTensor *a, double power)
{
    ObjTensor *result = tensor_create(vm, a->ndim, a->shape);
    for (size_t i = 0; i < a->size; i++)
    {
        result->data[i] = pow(a->data[i], power);
    }
    return result;
}

double tensor_sum_all(ObjTensor *a)
{
    return tensor_sum(a->data, a->size);
}

double tensor_mean_all(ObjTensor *a)
{
    return tensor_sum(a->data, a->size) / a->size;
}

double tensor_min_all(ObjTensor *a)
{
    double min = a->data[0];
    for (size_t i = 1; i < a->size; i++)
    {
        if (a->data[i] < min)
            min = a->data[i];
    }
    return min;
}

double tensor_max_all(ObjTensor *a)
{
    double max = a->data[0];
    for (size_t i = 1; i < a->size; i++)
    {
        if (a->data[i] > max)
            max = a->data[i];
    }
    return max;
}

double tensor_norm(ObjTensor *a)
{
    return sqrt(tensor_dot(a->data, a->data, a->size));
}

/* ============ Matrix Operations ============ */

ObjMatrix *matrix_create(VM *vm, uint32_t rows, uint32_t cols)
{
    ObjMatrix *mat = (ObjMatrix *)reallocate(vm, NULL, 0, sizeof(ObjMatrix));
    mat->obj.type = OBJ_MATRIX;
    mat->obj.next = vm->objects;
    mat->obj.marked = false;
    vm->objects = (Obj *)mat;

    mat->rows = rows;
    mat->cols = cols;
    mat->data = alloc_aligned(rows * cols);
    mat->owns_data = true;

    return mat;
}

ObjMatrix *matrix_zeros(VM *vm, uint32_t rows, uint32_t cols)
{
    ObjMatrix *mat = matrix_create(vm, rows, cols);
    memset(mat->data, 0, rows * cols * sizeof(double));
    return mat;
}

ObjMatrix *matrix_ones(VM *vm, uint32_t rows, uint32_t cols)
{
    ObjMatrix *mat = matrix_create(vm, rows, cols);
    for (uint32_t i = 0; i < rows * cols; i++)
    {
        mat->data[i] = 1.0;
    }
    return mat;
}

ObjMatrix *matrix_eye(VM *vm, uint32_t n)
{
    ObjMatrix *mat = matrix_zeros(vm, n, n);
    for (uint32_t i = 0; i < n; i++)
    {
        mat->data[i * n + i] = 1.0;
    }
    return mat;
}

ObjMatrix *matrix_rand(VM *vm, uint32_t rows, uint32_t cols)
{
    ObjMatrix *mat = matrix_create(vm, rows, cols);
    for (uint32_t i = 0; i < rows * cols; i++)
    {
        mat->data[i] = (double)rand() / RAND_MAX;
    }
    return mat;
}

ObjMatrix *matrix_from_array(VM *vm, ObjArray *arr)
{
    /* Assume arr is array of arrays (2D) */
    if (arr->count == 0)
        return matrix_zeros(vm, 0, 0);

    uint32_t rows = arr->count;
    uint32_t cols = 0;

    /* Get column count from first row */
    if (IS_ARRAY(arr->values[0]))
    {
        cols = AS_ARRAY(arr->values[0])->count;
    }

    ObjMatrix *mat = matrix_create(vm, rows, cols);

    for (uint32_t i = 0; i < rows; i++)
    {
        if (!IS_ARRAY(arr->values[i]))
            continue;
        ObjArray *row = AS_ARRAY(arr->values[i]);
        for (uint32_t j = 0; j < cols && j < row->count; j++)
        {
            Value v = row->values[j];
            if (IS_NUM(v))
                mat->data[i * cols + j] = as_num(v);
            else if (IS_INT(v))
                mat->data[i * cols + j] = (double)as_int(v);
            else
                mat->data[i * cols + j] = 0.0;
        }
    }

    return mat;
}

/* Matrix addition */
ObjMatrix *matrix_add(VM *vm, ObjMatrix *a, ObjMatrix *b)
{
    if (a->rows != b->rows || a->cols != b->cols)
        return NULL;
    ObjMatrix *result = matrix_create(vm, a->rows, a->cols);
    size_t n = a->rows * a->cols;
    tensor_add(result->data, a->data, b->data, n);
    return result;
}

/* Matrix subtraction */
ObjMatrix *matrix_sub(VM *vm, ObjMatrix *a, ObjMatrix *b)
{
    if (a->rows != b->rows || a->cols != b->cols)
        return NULL;
    ObjMatrix *result = matrix_create(vm, a->rows, a->cols);
    size_t n = a->rows * a->cols;
    tensor_sub(result->data, a->data, b->data, n);
    return result;
}

/* Matrix multiplication (A @ B) */
ObjMatrix *matrix_matmul(VM *vm, ObjMatrix *a, ObjMatrix *b)
{
    if (a->cols != b->rows)
        return NULL;

    ObjMatrix *result = matrix_zeros(vm, a->rows, b->cols);

    /* Naive O(n³) implementation - could use Strassen or link BLAS */
    for (uint32_t i = 0; i < a->rows; i++)
    {
        for (uint32_t k = 0; k < a->cols; k++)
        {
            double aik = a->data[i * a->cols + k];
            for (uint32_t j = 0; j < b->cols; j++)
            {
                result->data[i * b->cols + j] += aik * b->data[k * b->cols + j];
            }
        }
    }

    return result;
}

/* Matrix transpose */
ObjMatrix *matrix_transpose(VM *vm, ObjMatrix *a)
{
    ObjMatrix *result = matrix_create(vm, a->cols, a->rows);
    for (uint32_t i = 0; i < a->rows; i++)
    {
        for (uint32_t j = 0; j < a->cols; j++)
        {
            result->data[j * a->rows + i] = a->data[i * a->cols + j];
        }
    }
    return result;
}

/* Matrix trace */
double matrix_trace(ObjMatrix *a)
{
    uint32_t n = a->rows < a->cols ? a->rows : a->cols;
    double sum = 0.0;
    for (uint32_t i = 0; i < n; i++)
    {
        sum += a->data[i * a->cols + i];
    }
    return sum;
}

/* Matrix determinant (for small matrices) */
double matrix_det(ObjMatrix *a)
{
    if (a->rows != a->cols)
        return 0.0;
    uint32_t n = a->rows;

    if (n == 1)
        return a->data[0];
    if (n == 2)
        return a->data[0] * a->data[3] - a->data[1] * a->data[2];
    if (n == 3)
    {
        return a->data[0] * (a->data[4] * a->data[8] - a->data[5] * a->data[7]) - a->data[1] * (a->data[3] * a->data[8] - a->data[5] * a->data[6]) + a->data[2] * (a->data[3] * a->data[7] - a->data[4] * a->data[6]);
    }

    /* LU decomposition with partial pivoting for larger matrices */
    double *lu = malloc(n * n * sizeof(double));
    memcpy(lu, a->data, n * n * sizeof(double));
    double det = 1.0;

    for (uint32_t k = 0; k < n; k++)
    {
        /* Find pivot */
        uint32_t pivot_row = k;
        double max_val = fabs(lu[k * n + k]);
        for (uint32_t i = k + 1; i < n; i++)
        {
            if (fabs(lu[i * n + k]) > max_val)
            {
                max_val = fabs(lu[i * n + k]);
                pivot_row = i;
            }
        }

        if (max_val < 1e-15)
        {
            free(lu);
            return 0.0; /* Singular matrix */
        }

        /* Swap rows if needed */
        if (pivot_row != k)
        {
            det = -det;
            for (uint32_t j = 0; j < n; j++)
            {
                double tmp = lu[k * n + j];
                lu[k * n + j] = lu[pivot_row * n + j];
                lu[pivot_row * n + j] = tmp;
            }
        }

        det *= lu[k * n + k];

        /* Eliminate below pivot */
        for (uint32_t i = k + 1; i < n; i++)
        {
            double factor = lu[i * n + k] / lu[k * n + k];
            for (uint32_t j = k; j < n; j++)
            {
                lu[i * n + j] -= factor * lu[k * n + j];
            }
        }
    }

    free(lu);
    return det;
}

/* Matrix inverse (for small matrices) */
ObjMatrix *matrix_inverse(VM *vm, ObjMatrix *a)
{
    if (a->rows != a->cols)
        return NULL;
    uint32_t n = a->rows;

    if (n == 2)
    {
        double det = a->data[0] * a->data[3] - a->data[1] * a->data[2];
        if (fabs(det) < 1e-10)
            return NULL;

        ObjMatrix *inv = matrix_create(vm, 2, 2);
        inv->data[0] = a->data[3] / det;
        inv->data[1] = -a->data[1] / det;
        inv->data[2] = -a->data[2] / det;
        inv->data[3] = a->data[0] / det;
        return inv;
    }

    /* Gauss-Jordan elimination for larger matrices */
    ObjMatrix *aug = matrix_create(vm, n, 2 * n);

    /* Copy A to left half, I to right half */
    for (uint32_t i = 0; i < n; i++)
    {
        for (uint32_t j = 0; j < n; j++)
        {
            aug->data[i * 2 * n + j] = a->data[i * n + j];
            aug->data[i * 2 * n + n + j] = (i == j) ? 1.0 : 0.0;
        }
    }

    /* Forward elimination */
    for (uint32_t i = 0; i < n; i++)
    {
        /* Find pivot */
        double pivot = aug->data[i * 2 * n + i];
        if (fabs(pivot) < 1e-10)
            return NULL;

        /* Scale row */
        for (uint32_t j = 0; j < 2 * n; j++)
        {
            aug->data[i * 2 * n + j] /= pivot;
        }

        /* Eliminate column */
        for (uint32_t k = 0; k < n; k++)
        {
            if (k != i)
            {
                double factor = aug->data[k * 2 * n + i];
                for (uint32_t j = 0; j < 2 * n; j++)
                {
                    aug->data[k * 2 * n + j] -= factor * aug->data[i * 2 * n + j];
                }
            }
        }
    }

    /* Extract right half */
    ObjMatrix *inv = matrix_create(vm, n, n);
    for (uint32_t i = 0; i < n; i++)
    {
        for (uint32_t j = 0; j < n; j++)
        {
            inv->data[i * n + j] = aug->data[i * 2 * n + n + j];
        }
    }

    return inv;
}

/* Solve Ax = b */
ObjMatrix *matrix_solve(VM *vm, ObjMatrix *a, ObjMatrix *b)
{
    ObjMatrix *inv = matrix_inverse(vm, a);
    if (!inv)
        return NULL;
    return matrix_matmul(vm, inv, b);
}

/* ============ Neural Network Activations ============ */

ObjTensor *tensor_relu(VM *vm, ObjTensor *a)
{
    ObjTensor *result = tensor_create(vm, a->ndim, a->shape);
    for (size_t i = 0; i < a->size; i++)
    {
        result->data[i] = a->data[i] > 0 ? a->data[i] : 0;
    }
    return result;
}

ObjTensor *tensor_sigmoid(VM *vm, ObjTensor *a)
{
    ObjTensor *result = tensor_create(vm, a->ndim, a->shape);
    for (size_t i = 0; i < a->size; i++)
    {
        result->data[i] = 1.0 / (1.0 + exp(-a->data[i]));
    }
    return result;
}

ObjTensor *tensor_tanh_op(VM *vm, ObjTensor *a)
{
    ObjTensor *result = tensor_create(vm, a->ndim, a->shape);
    for (size_t i = 0; i < a->size; i++)
    {
        result->data[i] = tanh(a->data[i]);
    }
    return result;
}

ObjTensor *tensor_softmax(VM *vm, ObjTensor *a)
{
    ObjTensor *result = tensor_create(vm, a->ndim, a->shape);

    /* Find max for numerical stability */
    double max_val = tensor_max_all(a);

    /* Compute exp(x - max) and sum */
    double sum = 0.0;
    for (size_t i = 0; i < a->size; i++)
    {
        result->data[i] = exp(a->data[i] - max_val);
        sum += result->data[i];
    }

    /* Normalize */
    for (size_t i = 0; i < a->size; i++)
    {
        result->data[i] /= sum;
    }

    return result;
}

/* ============ Loss Functions ============ */

double tensor_mse_loss(ObjTensor *pred, ObjTensor *target)
{
    if (pred->size != target->size)
        return -1.0;
    double sum = 0.0;
    for (size_t i = 0; i < pred->size; i++)
    {
        double diff = pred->data[i] - target->data[i];
        sum += diff * diff;
    }
    return sum / pred->size;
}

double tensor_cross_entropy_loss(ObjTensor *pred, ObjTensor *target)
{
    if (pred->size != target->size)
        return -1.0;
    double sum = 0.0;
    for (size_t i = 0; i < pred->size; i++)
    {
        if (target->data[i] > 0)
        {
            sum -= target->data[i] * log(pred->data[i] + 1e-10);
        }
    }
    return sum;
}

/* ============ DataFrame Operations ============ */

ObjDataFrame *dataframe_create(VM *vm, uint32_t num_cols)
{
    ObjDataFrame *df = (ObjDataFrame *)reallocate(vm, NULL, 0, sizeof(ObjDataFrame));
    df->obj.type = OBJ_DATAFRAME;
    df->obj.next = vm->objects;
    df->obj.marked = false;
    vm->objects = (Obj *)df;

    df->num_rows = 0;
    df->num_cols = num_cols;
    df->column_names = (ObjString **)reallocate(vm, NULL, 0, num_cols * sizeof(ObjString *));
    df->columns = (ObjArray **)reallocate(vm, NULL, 0, num_cols * sizeof(ObjArray *));

    return df;
}

ObjDataFrame *dataframe_from_dict(VM *vm, ObjDict *dict)
{
    if (dict->count == 0)
        return dataframe_create(vm, 0);

    ObjDataFrame *df = dataframe_create(vm, dict->count);

    uint32_t col_idx = 0;
    for (uint32_t i = 0; i < dict->capacity; i++)
    {
        if (dict->keys[i] != NULL)
        {
            df->column_names[col_idx] = dict->keys[i];
            if (IS_ARRAY(dict->values[i]))
            {
                df->columns[col_idx] = AS_ARRAY(dict->values[i]);
                if (col_idx == 0)
                {
                    df->num_rows = df->columns[col_idx]->count;
                }
            }
            col_idx++;
        }
    }

    return df;
}

/* ============ Autograd ============ */

ObjGradTape *grad_tape_create(VM *vm)
{
    ObjGradTape *tape = (ObjGradTape *)reallocate(vm, NULL, 0, sizeof(ObjGradTape));
    tape->obj.type = OBJ_GRAD_TAPE;
    tape->obj.next = vm->objects;
    tape->obj.marked = false;
    vm->objects = (Obj *)tape;

    tape->capacity = 64;
    tape->count = 0;
    tape->entries = (GradTapeEntry *)reallocate(vm, NULL, 0,
                                                tape->capacity * sizeof(GradTapeEntry));
    tape->recording = false;

    return tape;
}

void grad_tape_record(ObjGradTape *tape, uint8_t op, ObjTensor *result,
                      ObjTensor *in1, ObjTensor *in2, ObjTensor *in3, double scalar)
{
    if (!tape->recording)
        return;

    if (tape->count >= tape->capacity)
    {
        tape->capacity *= 2;
        tape->entries = realloc(tape->entries, tape->capacity * sizeof(GradTapeEntry));
    }

    GradTapeEntry *entry = &tape->entries[tape->count++];
    entry->op = op;
    entry->result = result;
    entry->inputs[0] = in1;
    entry->inputs[1] = in2;
    entry->inputs[2] = in3;
    entry->scalar = scalar;
}

/* Backward pass - compute gradients */
void grad_tape_backward(VM *vm, ObjGradTape *tape, ObjTensor *loss)
{
    /* Initialize loss gradient to 1 */
    if (!loss->grad)
    {
        loss->grad = tensor_ones(vm, loss->ndim, loss->shape);
    }

    /* Traverse tape in reverse */
    for (int i = tape->count - 1; i >= 0; i--)
    {
        GradTapeEntry *entry = &tape->entries[i];
        ObjTensor *out_grad = entry->result->grad;
        if (!out_grad)
            continue;

        switch (entry->op)
        {
        case GRAD_OP_ADD:
        {
            /* d(a+b)/da = 1, d(a+b)/db = 1 */
            ObjTensor *in1 = entry->inputs[0];
            ObjTensor *in2 = entry->inputs[1];
            if (in1->requires_grad)
            {
                if (!in1->grad)
                    in1->grad = tensor_zeros(vm, in1->ndim, in1->shape);
                tensor_add(in1->grad->data, in1->grad->data, out_grad->data, in1->size);
            }
            if (in2->requires_grad)
            {
                if (!in2->grad)
                    in2->grad = tensor_zeros(vm, in2->ndim, in2->shape);
                tensor_add(in2->grad->data, in2->grad->data, out_grad->data, in2->size);
            }
            break;
        }
        case GRAD_OP_MUL:
        {
            /* d(a*b)/da = b, d(a*b)/db = a */
            ObjTensor *in1 = entry->inputs[0];
            ObjTensor *in2 = entry->inputs[1];
            if (in1->requires_grad)
            {
                if (!in1->grad)
                    in1->grad = tensor_zeros(vm, in1->ndim, in1->shape);
                for (size_t j = 0; j < in1->size; j++)
                {
                    in1->grad->data[j] += out_grad->data[j] * in2->data[j];
                }
            }
            if (in2->requires_grad)
            {
                if (!in2->grad)
                    in2->grad = tensor_zeros(vm, in2->ndim, in2->shape);
                for (size_t j = 0; j < in2->size; j++)
                {
                    in2->grad->data[j] += out_grad->data[j] * in1->data[j];
                }
            }
            break;
        }
        case GRAD_OP_RELU:
        {
            /* d(relu(x))/dx = 1 if x > 0, else 0 */
            ObjTensor *in1 = entry->inputs[0];
            if (in1->requires_grad)
            {
                if (!in1->grad)
                    in1->grad = tensor_zeros(vm, in1->ndim, in1->shape);
                for (size_t j = 0; j < in1->size; j++)
                {
                    if (in1->data[j] > 0)
                    {
                        in1->grad->data[j] += out_grad->data[j];
                    }
                }
            }
            break;
        }
        case GRAD_OP_SIGMOID:
        {
            /* d(sigmoid(x))/dx = sigmoid(x) * (1 - sigmoid(x)) */
            ObjTensor *in1 = entry->inputs[0];
            if (in1->requires_grad)
            {
                if (!in1->grad)
                    in1->grad = tensor_zeros(vm, in1->ndim, in1->shape);
                for (size_t j = 0; j < in1->size; j++)
                {
                    double s = entry->result->data[j];
                    in1->grad->data[j] += out_grad->data[j] * s * (1.0 - s);
                }
            }
            break;
        }
        case GRAD_OP_SUM:
        {
            /* d(sum(x))/dx = 1 for all elements */
            ObjTensor *in1 = entry->inputs[0];
            if (in1->requires_grad)
            {
                if (!in1->grad)
                    in1->grad = tensor_zeros(vm, in1->ndim, in1->shape);
                for (size_t j = 0; j < in1->size; j++)
                {
                    in1->grad->data[j] += out_grad->data[0];
                }
            }
            break;
        }
        default:
            break;
        }
    }
}

void grad_zero(ObjTensor *tensor)
{
    if (tensor->grad)
    {
        memset(tensor->grad->data, 0, tensor->grad->size * sizeof(double));
    }
}
