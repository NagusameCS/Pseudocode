/*
 * Pseudocode Language - Tensor and Matrix Operations Header
 *
 * Copyright (c) 2026 NagusameCS
 * Licensed under the MIT License
 */

#ifndef TENSOR_H
#define TENSOR_H

#include "pseudo.h"

/* Note: GradOp enum is defined in pseudo.h */

/* ============ Tensor Creation ============ */
ObjTensor* tensor_create(VM* vm, uint32_t ndim, const uint32_t* shape);
ObjTensor* tensor_zeros(VM* vm, uint32_t ndim, const uint32_t* shape);
ObjTensor* tensor_ones(VM* vm, uint32_t ndim, const uint32_t* shape);
ObjTensor* tensor_rand(VM* vm, uint32_t ndim, const uint32_t* shape);
ObjTensor* tensor_randn(VM* vm, uint32_t ndim, const uint32_t* shape);
ObjTensor* tensor_arange(VM* vm, double start, double stop, double step);
ObjTensor* tensor_linspace(VM* vm, double start, double stop, uint32_t num);
ObjTensor* tensor_from_array(VM* vm, ObjArray* arr);

/* ============ SIMD Operations ============ */
void tensor_add(double* out, const double* a, const double* b, size_t n);
void tensor_sub(double* out, const double* a, const double* b, size_t n);
void tensor_mul(double* out, const double* a, const double* b, size_t n);
double tensor_dot(const double* a, const double* b, size_t n);
double tensor_sum(const double* a, size_t n);

/* ============ Tensor Operations ============ */
ObjTensor* tensor_add_tensors(VM* vm, ObjTensor* a, ObjTensor* b);
ObjTensor* tensor_sub_tensors(VM* vm, ObjTensor* a, ObjTensor* b);
ObjTensor* tensor_mul_tensors(VM* vm, ObjTensor* a, ObjTensor* b);
ObjTensor* tensor_div_tensors(VM* vm, ObjTensor* a, ObjTensor* b);
ObjTensor* tensor_scale(VM* vm, ObjTensor* a, double scalar);
ObjTensor* tensor_neg(VM* vm, ObjTensor* a);
ObjTensor* tensor_abs(VM* vm, ObjTensor* a);
ObjTensor* tensor_sqrt_op(VM* vm, ObjTensor* a);
ObjTensor* tensor_exp_op(VM* vm, ObjTensor* a);
ObjTensor* tensor_log_op(VM* vm, ObjTensor* a);
ObjTensor* tensor_pow_op(VM* vm, ObjTensor* a, double power);

/* ============ Reductions ============ */
double tensor_sum_all(ObjTensor* a);
double tensor_mean_all(ObjTensor* a);
double tensor_min_all(ObjTensor* a);
double tensor_max_all(ObjTensor* a);
double tensor_norm(ObjTensor* a);

/* ============ Matrix Creation ============ */
ObjMatrix* matrix_create(VM* vm, uint32_t rows, uint32_t cols);
ObjMatrix* matrix_zeros(VM* vm, uint32_t rows, uint32_t cols);
ObjMatrix* matrix_ones(VM* vm, uint32_t rows, uint32_t cols);
ObjMatrix* matrix_eye(VM* vm, uint32_t n);
ObjMatrix* matrix_rand(VM* vm, uint32_t rows, uint32_t cols);
ObjMatrix* matrix_from_array(VM* vm, ObjArray* arr);

/* ============ Matrix Operations ============ */
ObjMatrix* matrix_add(VM* vm, ObjMatrix* a, ObjMatrix* b);
ObjMatrix* matrix_sub(VM* vm, ObjMatrix* a, ObjMatrix* b);
ObjMatrix* matrix_matmul(VM* vm, ObjMatrix* a, ObjMatrix* b);
ObjMatrix* matrix_transpose(VM* vm, ObjMatrix* a);
ObjMatrix* matrix_inverse(VM* vm, ObjMatrix* a);
ObjMatrix* matrix_solve(VM* vm, ObjMatrix* a, ObjMatrix* b);
double matrix_trace(ObjMatrix* a);
double matrix_det(ObjMatrix* a);

/* ============ Neural Network Activations ============ */
ObjTensor* tensor_relu(VM* vm, ObjTensor* a);
ObjTensor* tensor_sigmoid(VM* vm, ObjTensor* a);
ObjTensor* tensor_tanh_op(VM* vm, ObjTensor* a);
ObjTensor* tensor_softmax(VM* vm, ObjTensor* a);

/* ============ Loss Functions ============ */
double tensor_mse_loss(ObjTensor* pred, ObjTensor* target);
double tensor_cross_entropy_loss(ObjTensor* pred, ObjTensor* target);

/* ============ DataFrame ============ */
ObjDataFrame* dataframe_create(VM* vm, uint32_t num_cols);
ObjDataFrame* dataframe_from_dict(VM* vm, ObjDict* dict);

/* ============ Autograd ============ */
ObjGradTape* grad_tape_create(VM* vm);
void grad_tape_record(ObjGradTape* tape, uint8_t op, ObjTensor* result,
                      ObjTensor* in1, ObjTensor* in2, ObjTensor* in3, double scalar);
void grad_tape_backward(VM* vm, ObjGradTape* tape, ObjTensor* loss);
void grad_zero(ObjTensor* tensor);

#endif /* TENSOR_H */
