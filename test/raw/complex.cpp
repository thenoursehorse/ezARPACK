/*******************************************************************************
 *
 * This file is part of ezARPACK, an easy-to-use C++ wrapper for
 * the ARPACK-NG FORTRAN library.
 *
 * Copyright (C) 2016-2023 Igor Krivenko <igor.s.krivenko@gmail.com>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 ******************************************************************************/

#include "common.hpp"

///////////////////
// Test invert() //
///////////////////
TEST_CASE("Complex matrix is inverted", "[invert_asymmetric]") {
  const int N = 100;
  const dcomplex diag_coeff_mean = 2.0;
  const int offdiag_offset = 3;
  const dcomplex offdiag_coeff_mean = 0;
  const dcomplex offdiag_coeff_diff(-0.01, 0.1);

  // Hermitian matrix A
  auto A = make_sparse_matrix<ezarpack::Complex>(
      N, diag_coeff_mean, offdiag_offset, offdiag_coeff_mean,
      offdiag_coeff_diff);

  auto invA = make_buffer<dcomplex>(N * N);
  invert(A.get(), invA.get(), N);

  auto id = make_buffer<dcomplex>(N * N);
  for(int i = 0; i < N; ++i) {
    for(int j = 0; j < N; ++j) {
      id[i + j * N] = i == j;
    }
  }

  auto prod = make_buffer<dcomplex>(N * N);

  // A * A^{-1}
  mm_prod(A.get(), invA.get(), prod.get(), N);
  CHECK_THAT(prod.get(), IsCloseTo(id.get(), N * N));

  // A^{-1} * A
  mm_prod(invA.get(), A.get(), prod.get(), N);
  CHECK_THAT(prod.get(), IsCloseTo(id.get(), N * N));
}

/////////////////////////////////////////
// Eigenproblems with complex matrices //
/////////////////////////////////////////

TEST_CASE("Complex eigenproblem is solved", "[solver_complex]") {

  using solver_t = arpack_solver<ezarpack::Complex, raw_storage>;

  const int N = 100;
  const dcomplex diag_coeff_mean = 2.0;
  const int offdiag_offset = 3;
  const dcomplex offdiag_coeff_mean = 0;
  const dcomplex offdiag_coeff_diff(-0.01, 0.1);
  const int nev = 8;

  const dcomplex sigma(0.1, 0.1);

  // Hermitian matrix A
  auto A = make_sparse_matrix<ezarpack::Complex>(
      N, diag_coeff_mean, offdiag_offset, offdiag_coeff_mean,
      offdiag_coeff_diff);
  // Inner product matrix
  auto M = make_inner_prod_matrix<ezarpack::Complex>(N);

  // Testing helper
  auto testing = make_testing_helper<solver_t>(A, M, N, nev);

  using vv_t = solver_t::vector_view_t;
  using vcv_t = solver_t::vector_const_view_t;

  SECTION("Standard eigenproblem") {
    auto Aop = [&](vcv_t in, vv_t out) { mv_prod(A.get(), in, out, N); };

    solver_t ar(N);
    testing.standard_eigenproblems(ar, Aop);
  }

  SECTION("Generalized eigenproblem: invert mode") {
    auto invM = make_buffer<dcomplex>(N * N);
    invert(M.get(), invM.get(), N);
    auto op_mat = make_buffer<dcomplex>(N * N);
    mm_prod(invM.get(), A.get(), op_mat.get(), N);

    auto op = [&](vcv_t in, vv_t out) { mv_prod(op_mat.get(), in, out, N); };
    auto Bop = [&](vcv_t in, vv_t out) { mv_prod(M.get(), in, out, N); };

    solver_t ar(N);
    testing.generalized_eigenproblems(ar, solver_t::Inverse, op, Bop);
  }

  SECTION("Generalized eigenproblem: Shift-and-Invert mode") {

    auto AmM = make_buffer<dcomplex>(N * N);
    for(int i = 0; i < N; ++i) {
      for(int j = 0; j < N; ++j) {
        AmM[i + j * N] = A[i + j * N] - sigma * M[i + j * N];
      }
    }
    auto invAmM = make_buffer<dcomplex>(N * N);
    invert(AmM.get(), invAmM.get(), N);
    auto op_mat = make_buffer<dcomplex>(N * N);
    mm_prod(invAmM.get(), M.get(), op_mat.get(), N);

    auto op = [&](vcv_t in, vv_t out) { mv_prod(op_mat.get(), in, out, N); };
    auto Bop = [&](vcv_t in, vv_t out) { mv_prod(M.get(), in, out, N); };

    solver_t ar(N);
    testing.generalized_eigenproblems(ar, solver_t::ShiftAndInvert, op, Bop,
                                      sigma);
  }

  SECTION("Indirect access to workspace vectors") {
    solver_t ar(N);

    auto Aop = [&](vcv_t, vv_t) {
      auto in = ar.workspace_vector(ar.in_vector_n());
      auto out = ar.workspace_vector(ar.out_vector_n());
      mv_prod(A.get(), in, out, N);
    };

    testing.standard_eigenproblems(ar, Aop);

    CHECK_THROWS(ar.workspace_vector(-1));
    CHECK_THROWS(ar.workspace_vector(3));
  }

  SECTION("Various compute_vectors") {
    solver_t ar(N);

    SECTION("Standard eigenproblem") {
      auto Aop = [&](vcv_t in, vv_t out) { mv_prod(A.get(), in, out, N); };

      testing.standard_compute_vectors(ar, Aop);
    }

    SECTION("Generalized eigenproblem: invert mode") {
      auto invM = make_buffer<dcomplex>(N * N);
      invert(M.get(), invM.get(), N);
      auto op_mat = make_buffer<dcomplex>(N * N);
      mm_prod(invM.get(), A.get(), op_mat.get(), N);

      auto op = [&](vcv_t in, vv_t out) { mv_prod(op_mat.get(), in, out, N); };
      auto Bop = [&](vcv_t in, vv_t out) { mv_prod(M.get(), in, out, N); };

      testing.generalized_compute_vectors(ar, op, Bop);
    }
  }
}
