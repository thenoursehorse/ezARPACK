/*******************************************************************************
 *
 * This file is part of ezARPACK, an easy-to-use C++ wrapper for
 * the ARPACK-NG FORTRAN library.
 *
 * Copyright (C) 2016-2020 Igor Krivenko <igor.s.krivenko@gmail.com>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 ******************************************************************************/
/// @file ezarpack/worker_symmetric.hpp
/// @brief Specialization of `arpack_worker` class for the case of real
/// symmetric eigenproblems.
#pragma once

namespace ezarpack {

/// @brief Main worker class wrapping the Implicitly Restarted Lanczos
/// Method (IRLM) for real symmetric eigenproblems.
///
/// This specialization of `arpack_worker` calls ARPACK-NG routines `dsaupd()`
/// and `dseupd()` to compute approximations to a few eigenpairs of a linear
/// operator @f$ \hat O @f$ that is real and symmetric with respect to
/// a real positive semi-definite symmetric matrix @f$ \hat B @f$. In other
/// words,
/// @f[
///   \langle \mathbf{x},\hat O \mathbf{y} \rangle =
///   \langle \hat O \mathbf{x}, \mathbf{y} \rangle
/// @f]
/// for all vectors @f$ \mathbf{x} @f$, @f$ \mathbf{y} @f$ and with the scalar
/// product defined as
/// @f[
///   \langle \mathbf{x}, \mathbf{y} \rangle = \mathbf{x}^T \hat B \mathbf{y}.
/// @f]
/// A variant of the Lanczos algorithm is internally used instead of the
/// Arnoldi iteration for this class of problems.
///
/// @tparam Backend Tag type specifying what *storage backend* (matrix/vector
/// algebra library) must be used by `arpack_worker`. The storage backend
/// determines types of internally stored data arrays and input/output view
/// objects returned by methods of the class.
template<typename Backend> class arpack_worker<Symmetric, Backend> {

  using storage = storage_traits<Backend>;

public:
  /// @name Backend-specific array and view types

  /// @{

  /// One-dimensional data array (vector) of real numbers.
  using real_vector_t = typename storage::real_vector_type;
  /// Two-dimensional data array (matrix) of real numbers.
  using real_matrix_t = typename storage::real_matrix_type;
  /// One-dimensional data array (vector) of integers.
  using int_vector_t = typename storage::int_vector_type;

  /// Partial view (slice) of a real vector.
  using real_vector_view_t = typename storage::real_vector_view_type;
  /// Partial constant view (slice) of a real vector.
  using real_vector_const_view_t =
      typename storage::real_vector_const_view_type;
  /// Partial constant view (slice) of a real matrix.
  using real_matrix_const_view_t =
      typename storage::real_matrix_const_view_type;

  /// Storage-specific view type to expose real input vectors
  /// @f$ \mathbf{x} @f$. An argument of this type is passed as input to
  /// callable objects representing linear operators @f$ \hat O @f$ and
  /// @f$ \hat B @f$.
  using vector_const_view_t = real_vector_const_view_t;

  /// Storage-specific view type to expose real output vectors
  /// @f$ \mathbf{y} @f$. An argument of this type receives output from
  /// callable objects representing linear operators @f$ \hat O @f$ and
  /// @f$ \hat B @f$.
  using vector_view_t = real_vector_view_t;

  /// @}

private:
  int N;                      // Matrix size
  const char* which;          // WHICH parameter
  int nev = 0;                // Number of eigenvalues
  double tol;                 // Relative tolerance for Ritz value convergence
  real_vector_t resid;        // Residual vector
  real_vector_t workd;        // Working space
  int ncv = 0;                // Number of Lanczos vectors to be generated
  real_matrix_t v;            // Matrix with Lanczos basis vectors
  real_vector_t d;            // Ritz values
  int iparam[11];             // Various input/output parameters
  int ipntr[11];              // Starting locations in workd and workl
  int info = 0;               // !=0 to use resid, 0 otherwise
  int rvec;                   // RVEC parameter of dseupd
  int_vector_t select;        // SELECT parameter of dseupd
  bool Bx_available_ = false; // Has B*x already been computed?

public:
  /// Input parameters of the Implicitly Restarted Lanczos Method (IRLM).
  struct params_t {

    /// Number of eigenvalues (Ritz values) to compute.
    unsigned int n_eigenvalues;

    /// Categories of eigenvalues to compute.
    enum eigenvalues_select_t {
      Largest,           /**< Largest (algebraic) eigenvalues. */
      Smallest,          /**< Smallest (algebraic) eigenvalues. */
      LargestMagnitude,  /**< Largest eigenvalues in magnitude. */
      SmallestMagnitude, /**< Smallest eigenvalues in magnitude. */
      BothEnds           /**< Eigenvalues at both ends of the
                              spectrum. If @ref n_eigenvalues is odd,
                              compute one more from the high end
                              than from the low end. */
    };

    /// Which of the eigenvalues to compute?
    eigenvalues_select_t eigenvalues_select;

    /// Number of Lanczos vectors to be generated.
    /// `-1` stands for the default value `min(2*n_eigenvalues + 2, N)`,
    /// where `N` is the dimension of the problem.
    int ncv = -1;

    /// Compute Ritz vectors in addition to the eigenvalues?
    bool compute_eigenvectors;

    /// Use a randomly generated initial residual vector?
    bool random_residual_vector = true;

    /// Eigenvalue shift @f$ \sigma @f$ used if a spectral transformation is
    /// employed.
    double sigma = 0;

    /// Relative tolerance for Ritz value convergence. The default setting is
    /// machine precision.
    double tolerance = 0;

    /// Maximum number of IRLM iterations allowed.
    unsigned int max_iter = INT_MAX;

    /// Constructs an IRLM parameter object with given
    /// @ref n_eigenvalues, @ref eigenvalues_select and
    /// @ref compute_eigenvectors.
    /// The rest of the parameters are set to their defaults.
    params_t(unsigned int n_eigenvalues,
             eigenvalues_select_t eigenvalues_select,
             bool compute_eigenvectors)
        : n_eigenvalues(n_eigenvalues),
          eigenvalues_select(eigenvalues_select),
          compute_eigenvectors(compute_eigenvectors) {}
  };

  /// Constructs a worker object and allocates internal data buffers to be
  /// used by ARPACK-NG.
  /// @param N Dimension of the eigenproblem.
  arpack_worker(unsigned int N)
      : N(N),
        resid(storage::make_real_vector(N)),
        workd(storage::make_real_vector(3 * N)),
        v(storage::make_real_matrix(N, 0)),
        d(storage::make_real_vector(nev)),
        select(storage::make_int_vector(0)) {
    iparam[3] = 1;
  }

  ~arpack_worker() {
    storage::destroy(resid);
    storage::destroy(workd);
    storage::destroy(v);
    storage::destroy(d);
    storage::destroy(select);
  }

  arpack_worker(arpack_worker const&) = delete;
  arpack_worker(arpack_worker&&) noexcept = delete;

private:
  /// @internal Prepare values of input parameters and resize containers.
  void prepare(params_t const& params) {

    // Check n_eigenvalues
    nev = params.n_eigenvalues;
    int nev_min = params.eigenvalues_select == params_t::BothEnds ? 2 : 1;
    int nev_max = N - 1;

    if(nev < nev_min || nev > nev_max)
      throw ARPACK_WORKER_ERROR("n_eigenvalues must be within [" +
                                std::to_string(nev_min) + ";" +
                                std::to_string(nev_max) + "]");

    // Character codes for eigenvalues_select
    static const std::array<const char*, 5> wh = {"LA", "SA", "LM", "SM", "BE"};
    which = wh[int(params.eigenvalues_select)];

    // Check ncv
    ncv = params.ncv;
    if(ncv == -1)
      ncv = std::min(2 * int(params.n_eigenvalues) + 2, N);
    else if(ncv <= int(params.n_eigenvalues) || ncv > N)
      throw ARPACK_WORKER_ERROR("ncv must be within ]" +
                                std::to_string(params.n_eigenvalues) + ";" +
                                std::to_string(N) + "]");

    storage::resize(v, N, ncv);

    // Eigenvectors
    rvec = params.compute_eigenvectors;
    storage::resize(select, ncv);

    // Tolerance
    tol = std::max(.0, params.tolerance);

    // Use random initial residual vector?
    info = !params.random_residual_vector;

    iparam[2] = int(params.max_iter); // Max number of iterations
    if(iparam[2] <= 0)
      throw ARPACK_WORKER_ERROR(
          "Maximum number of Arnoldi update iterations must be positive");
  }

public:
  /// If this functor is used to provide shifts for implicit restart,
  /// then the default ARPACK-NG's shift strategy (Exact Shift Strategy)
  /// will be employed.
  /// @sa Paragraph 4.4.1 of ARPACK Users' Guide:
  /// Solution of Large Scale Eigenvalue Problems
  /// with Implicitly Restarted Arnoldi Methods (R. B. Lehoucq, D. C. Sorensen,
  /// C. Yang, SIAM, 1998),
  /// https://www.caam.rice.edu/software/ARPACK/UG/node50.html.
  struct exact_shifts_f {
    /// Trivial call operator. The actual shifts will be internally computed by
    /// ARPACK-NG.
    ///
    /// @param[in] ritz_values View of a real vector with current
    /// @ref params_t::ncv Ritz values.
    /// @param[in] ritz_bounds View of a real vector with current estimated
    /// error bounds of the Ritz values.
    /// @param[out] shifts Real vector view to receive the computed shifts.
    void operator()(real_vector_const_view_t ritz_values,
                    real_vector_const_view_t ritz_bounds,
                    real_vector_view_t shifts) {}
  };

  /// Solve a standard eigenproblem @f$ \hat A\mathbf{x} = \lambda\mathbf{x}@f$.
  ///
  /// @param a A callable object representing the linear operator
  /// @f$ \hat A @f$. It must take two arguments,
  /// @code
  /// a(vector_const_view_t from, vector_view_t to)
  /// @endcode
  /// `a` is expected to act on the vector view `from` and write the result into
  /// the vector view `to`, `to = a*from`. Given an instance `aw` of the
  /// arpack_worker< Symmetric, Backend > class, `from` is also indirectly
  /// accessible as
  /// @code
  /// aw.workspace_vector(aw.from_vector_n())
  /// @endcode
  /// and `to` is accessible as
  /// @code
  /// aw.workspace_vector(aw.to_vector_n())
  /// @endcode
  ///
  /// @param params Set of input parameters for the Implicitly Restarted
  /// Lanczos Method.
  ///
  /// @param shifts_f Functor that implements a shift selection strategy for the
  /// implicit restarts. For the expected signature of the functor, see
  /// @ref exact_shifts_f::operator()(). When this argument is omitted, the
  /// default @ref exact_shifts_f "\"Exact Shift Strategy\"" is used, which is
  /// the right choice in most cases.
  ///
  /// @throws ezarpack::ncv_insufficient No shifts could be applied during
  /// a cycle of the IRL iteration.
  /// @throws ezarpack::maxiter_reached Maximum number of IRL iterations has
  /// been reached. All possible eigenvalues of @f$ \hat O @f$ have been found.
  /// @throws std::runtime_error Invalid input parameters and other errors
  /// reported by ARPACK-NG routines `dsaupd()` and `dseupd()`.
  template<typename A, typename ShiftsF = exact_shifts_f>
  void operator()(A&& a, params_t const& params, ShiftsF shifts_f = {}) {

    prepare(params);

    iparam[0] = (std::is_same<ShiftsF, exact_shifts_f>::value ? 1 : 0);
    iparam[6] = 1; // Mode 1, standard eigenproblem

    const int workl_size = ncv * ncv + 8 * ncv;
    real_vector_t workl = storage::make_real_vector(workl_size);

    rci_flag ido = Init;
    Bx_available_ = false;
    do {
      f77::aupd<true>(ido, "I", N, which, nev, tol,
                      storage::get_data_ptr(resid), ncv,
                      storage::get_data_ptr(v), N, iparam, ipntr,
                      storage::get_data_ptr(workd),
                      storage::get_data_ptr(workl), workl_size, info);
      switch(ido) {
        case ApplyOpInit:
        case ApplyOp: {
          int from_pos = from_vector_n() * N;
          int to_pos = to_vector_n() * N;
          a(storage::make_vector_const_view(workd, from_pos, N),
            storage::make_vector_view(workd, to_pos, N));
        } break;
        case Shifts:
          shifts_f(storage::make_vector_const_view(workl, ipntr[5] - 1, ncv),
                   storage::make_vector_const_view(workl, ipntr[6] - 1, ncv),
                   storage::make_vector_view(workl, ipntr[10] - 1, iparam[7]));
          break;
        case Done: break;
        default: {
          storage::destroy(workl);
          throw ARPACK_WORKER_ERROR("Reverse communication interface error");
        }
      }
    } while(ido != Done);

    handle_aupd_error_codes(info, workl);

    storage::resize(d, nev);

    f77::eupd(rvec, "A", storage::get_data_ptr(select),
              storage::get_data_ptr(d), storage::get_data_ptr(v), N,
              params.sigma, "I", N, which, nev, tol,
              storage::get_data_ptr(resid), ncv, storage::get_data_ptr(v), N,
              iparam, ipntr, storage::get_data_ptr(workd),
              storage::get_data_ptr(workl), workl_size, info);

    storage::destroy(workl);

    handle_eupd_error_codes(info);
  }

  // clang-format off
  /// Computational modes for generalized eigenproblems.
  enum Mode : int {
    Invert = 2,
    /**< Regular inverse mode.

    Solve a generalized eigenproblem
    @f$ \hat A\mathbf{x} = \lambda \hat M\mathbf{x} @f$ by reduction to
    the canonical form with @f$ \hat O = \hat M^{-1} \hat A @f$ and
    @f$ \hat B = \hat M @f$, where @f$ \hat A @f$ is symmetric and
    @f$ \hat M @f$ is symmetric positive-definite.
    */
    ShiftAndInvert = 3,
    /**< Shift-and-Invert mode.

    Solve a generalized eigenproblem
    @f$ \hat A\mathbf{x} = \lambda \hat M\mathbf{x} @f$ by reduction to
    the canonical form with @f$ \hat O = (\hat A - \sigma\hat M)^{-1} \hat M @f$
    and @f$ \hat B = \hat M @f$, where @f$ \hat A @f$ is symmetric and
    @f$ \hat M @f$ is symmetric positive semi-definite.
    */
    Buckling = 4,
    /**< Buckling mode.

    Solve a generalized eigenproblem
    @f$ \hat K\mathbf{x} = \lambda \hat K_G\mathbf{x} @f$ by reduction to
    the canonical form with @f$ \hat O = (\hat K - \sigma\hat K_G)^{-1} \hat K @f$
    and @f$ \hat B = \hat K @f$, where @f$ \hat K @f$ is symmetric positive
    semi-definite and @f$ \hat K_G @f$ is symmetric indefinite.
    */
    Cayley = 5
    /**< Cayley mode.

    Solve a generalized eigenproblem
    @f$ \hat A\mathbf{x} = \lambda \hat M\mathbf{x} @f$ by reduction to
    the canonical form with
    @f$\hat O = (\hat A -\sigma\hat M)^{-1}(\hat A +\sigma M)@f$ and
    @f$ \hat B = \hat M @f$, where @f$ \hat A @f$ is symmetric and
    @f$ \hat M @f$ is symmetric positive semi-definite.
    */
  };
  // clang-format on

  /// Solve a generalized eigenproblem @f$ \hat A\mathbf{x} =
  /// \lambda\hat M\mathbf{x}@f$.
  ///
  /// Operators @f$ \hat O @f$ and @f$ \hat B @f$
  /// mentioned below are related to @f$ \hat A @f$ and @f$ \hat M @f$ via
  /// a @ref Mode -dependent spectral transformation.
  ///
  /// @param op A callable object representing the linear operator
  /// @f$ \hat O @f$. It must take two arguments,
  /// @code
  /// op(vector_view_t from, vector_view_t to)
  /// @endcode
  /// In all computational modes except for @ref Invert, `op` is expected to act
  /// on the vector view `from` and write the result into
  /// the vector view `to`, `to = op*from`. In the @ref Invert mode, however,
  /// `op` must do the following,
  /// `from = op*from`, `to = M^{-1}*from`.
  /// Given an instance `aw` of the arpack_worker< Symmetric, Backend > class,
  /// `from` is also indirectly accessible as
  /// @code
  /// aw.workspace_vector(aw.from_vector_n())
  /// @endcode
  /// and `to` is accessible as
  /// @code
  /// aw.workspace_vector(aw.to_vector_n())
  /// @endcode
  ///
  /// @param b A callable object representing the linear operator
  /// @f$ \hat B @f$. It must take two arguments,
  /// @code
  /// b(vector_const_view_t from, vector_view_t to)
  /// @endcode
  /// `b` is expected to act on the vector view `from` and write the result into
  /// the vector view `to`, `to = b*from`.
  ///
  /// @param mode @ref Mode "Computational mode" to be used.
  /// @param params Set of input parameters for the Implicitly Restarted
  /// Lanczos Method.
  ///
  /// @param shifts_f Functor that implements a shift selection strategy for the
  /// implicit restarts. For the expected signature of the functor, see
  /// @ref exact_shifts_f::operator()(). When this argument is omitted, the
  /// default @ref exact_shifts_f "\"Exact Shift Strategy\"" is used, which is
  /// the right choice in most cases.
  ///
  /// @throws ezarpack::ncv_insufficient No shifts could be applied during
  /// a cycle of the IRL iteration.
  /// @throws ezarpack::maxiter_reached Maximum number of IRL iterations has
  /// been reached. All possible eigenvalues of @f$ \hat O @f$ have been found.
  /// @throws std::runtime_error Invalid input parameters and other errors
  /// reported by ARPACK-NG routines `dsaupd()` and `dseupd()`.
  template<typename OP, typename B, typename ShiftsF = exact_shifts_f>
  void operator()(OP&& op,
                  B&& b,
                  Mode mode,
                  params_t const& params,
                  ShiftsF shifts_f = {}) {

    prepare(params);

    iparam[0] = (std::is_same<ShiftsF, exact_shifts_f>::value ? 1 : 0);
    iparam[6] = mode; // Modes 2-5, generalized eigenproblem

    const int workl_size = ncv * ncv + 8 * ncv;
    real_vector_t workl = storage::make_real_vector(workl_size);

    rci_flag ido = Init;
    Bx_available_ = false;
    do {
      f77::aupd<true>(ido, "G", N, which, nev, tol,
                      storage::get_data_ptr(resid), ncv,
                      storage::get_data_ptr(v), N, iparam, ipntr,
                      storage::get_data_ptr(workd),
                      storage::get_data_ptr(workl), workl_size, info);
      switch(ido) {
        case ApplyOpInit: {
          int from_pos = from_vector_n() * N;
          int to_pos = to_vector_n() * N;
          Bx_available_ = false;
          op(storage::make_vector_view(workd, from_pos, N),
             storage::make_vector_view(workd, to_pos, N));
        } break;
        case ApplyOp: {
          int from_pos = from_vector_n() * N;
          int to_pos = to_vector_n() * N;
          // B*x is available via Bx_vector()
          Bx_available_ = true;
          op(storage::make_vector_view(workd, from_pos, N),
             storage::make_vector_view(workd, to_pos, N));
        } break;
        case ApplyB: {
          int from_pos = from_vector_n() * N;
          int to_pos = to_vector_n() * N;
          b(storage::make_vector_const_view(workd, from_pos, N),
            storage::make_vector_view(workd, to_pos, N));
        } break;
        case Shifts:
          shifts_f(storage::make_vector_const_view(workl, ipntr[5] - 1, ncv),
                   storage::make_vector_const_view(workl, ipntr[6] - 1, ncv),
                   storage::make_vector_view(workl, ipntr[10] - 1, iparam[7]));
          break;
        case Done: break;
        default: {
          storage::destroy(workl);
          throw ARPACK_WORKER_ERROR("Reverse communication interface error");
        }
      }
    } while(ido != Done);

    handle_aupd_error_codes(info, workl);

    storage::resize(d, nev);
    double sigma = (mode != Invert) ? params.sigma : 0;

    f77::eupd(rvec, "A", storage::get_data_ptr(select),
              storage::get_data_ptr(d), storage::get_data_ptr(v), N, sigma, "G",
              N, which, nev, tol, storage::get_data_ptr(resid), ncv,
              storage::get_data_ptr(v), N, iparam, ipntr,
              storage::get_data_ptr(workd), storage::get_data_ptr(workl),
              workl_size, info);

    storage::destroy(workl);

    handle_eupd_error_codes(info);
  }

  /// Returns the index of the workspace vector, which is currently expected to
  /// be acted upon by linear operator @f$ \hat O @f$ or @f$ \hat B @f$.
  inline int from_vector_n() const { return (ipntr[0] - 1) / N; }

  /// Returns the index of the workspace vector, which is currently expected to
  /// receive result from application of linear operator @f$ \hat O @f$ or
  /// @f$ \hat B @f$.
  inline int to_vector_n() const { return (ipntr[1] - 1) / N; }

  /// Returns a view of a vector within ARPACK-NG's workspace array.
  ///
  /// @param n Index of the workspace vector. Valid values are 0, 1 and 2.
  /// @throws std::runtime_error Invalid index value.
  real_vector_view_t workspace_vector(int n) const {
    if(n < 0 || n > 2)
      throw ARPACK_WORKER_ERROR(
          "Valid indices of workspace vectors are 0, 1 and 2 (got " +
          std::to_string(n) + ")");
    return storage::make_vector_view(workd, n * N, N);
  }

  /// Returns a constant view of a list of @ref stats_t::n_converged
  /// eigenvalues.
  ///
  /// The values in the list are in ascending order.
  /// \note In the generalized eigenproblem @ref Mode "modes", this
  /// method always returns eigenvalues of the **original** problem.
  real_vector_const_view_t eigenvalues() const {
    return storage::make_vector_const_view(d, 0, iparam[4]);
  }

  /// Returns a constant view of a matrix, whose
  /// @ref stats_t::n_converged columns are converged Lanczos basis
  /// vectors (eigenvectors).
  real_matrix_const_view_t eigenvectors() const {
    return storage::make_matrix_const_view(v, N, iparam[4]);
  }

  /// Returns a view of the current residual vector.
  ///
  /// When params_t::random_residual_vector is set to `false`, the view returned
  /// by this accessor can be used to set the initial residual vector.
  real_vector_view_t residual_vector() {
    return storage::make_vector_view(resid);
  }

  /// Has @f$ \hat B\mathbf{x} @f$ already been computed at the current
  /// IRLM iteration?
  bool Bx_available() const { return Bx_available_; }

  /// Returns a constant view of the most recently computed vector
  /// @f$ \hat B\mathbf{x} @f$.
  real_vector_const_view_t Bx_vector() const {
    unsigned int n = ipntr[2] - 1;
    return storage::make_vector_const_view(workd, n, N);
  }

  /// Statistics regarding a completed IRLM run.
  struct stats_t {
    /// Number of Arnoldi update iterations taken.
    unsigned int n_iter;
    /// Number of "converged" Ritz values.
    unsigned int n_converged;
    /// Total number of @f$ \hat O \mathbf{x} @f$ operations.
    unsigned int n_op_x_operations;
    /// Total number of @f$ \hat B \mathbf{x} @f$ operations.
    unsigned int n_b_x_operations;
    /// Total number of steps of re-orthogonalization.
    unsigned int n_reorth_steps;
  };

  /// Returns computation statistics from the last IRLM run.
  stats_t stats() const {
    stats_t s;
    s.n_iter = iparam[2];
    s.n_converged = iparam[4];
    s.n_op_x_operations = iparam[8];
    s.n_b_x_operations = iparam[9];
    s.n_reorth_steps = iparam[10];
    return s;
  }

private:
  /// @internal Translate dsaupd's INFO codes into C++ exceptions.
  ///
  /// @param info dsaupd's INFO code.
  void handle_aupd_error_codes(int info, real_vector_t& workl) {
    if(info == 0) return;

    storage::destroy(workl);
    switch(info) {
      case 1: throw(maxiter_reached(iparam[2]));
      case 3: throw(ncv_insufficient(ncv));
      case -8:
        throw ARPACK_WORKER_ERROR(
            "Error in LAPACK tridiagonal eigenvalue calculation (dsteqr)");
      case -9: throw ARPACK_WORKER_ERROR("Starting vector is zero");
      case -13:
        throw ARPACK_WORKER_ERROR("n_eigenvalues = 1 is incompatible with "
                                  "eigenvalues_select = BothEnds");
      case -9999:
        throw ARPACK_WORKER_ERROR(
            "Could not build an Arnoldi factorization. "
            "The size of the current Arnoldi factorization is " +
            std::to_string(iparam[4]));
      default:
        throw ARPACK_WORKER_ERROR("dsaupd failed with error code " +
                                  std::to_string(info));
    }
  }

  /// @internal Translate dseupd's INFO codes into C++ exceptions.
  ///
  /// @param info dseupd's INFO code.
  void handle_eupd_error_codes(int info) {
    switch(info) {
      case 0: return;
      case -8:
        throw ARPACK_WORKER_ERROR(
            "Error in LAPACK tridiagonal eigenvalue calculation (dsteqr)");
      case -9: throw ARPACK_WORKER_ERROR("Starting vector is zero");
      case -12:
        throw ARPACK_WORKER_ERROR("n_eigenvalues = 1 is incompatible with "
                                  "eigenvalues_select = BothEnds");
      case -14:
        throw ARPACK_WORKER_ERROR(
            "dsaupd did not find any eigenvalues to sufficient accuracy");
      default:
        throw ARPACK_WORKER_ERROR("dseupd failed with error code " +
                                  std::to_string(info));
    }
  }
};

} // namespace ezarpack
