#pragma once

#include <alpaqa/problem/box.hpp>
#include <alpaqa/util/check-dim.hpp>

namespace alpaqa {

/// @ingroup grp_Problems
template <Config Conf>
class BoxConstrProblem {
  public:
    USING_ALPAQA_CONFIG(Conf);
    using Box = alpaqa::Box<config_t>;

    /// Number of decision variables, dimension of x
    length_t n;
    /// Number of constraints, dimension of g(x) and z
    length_t m;

    /// Components of the constraint function with indices below this number are
    /// handled using a quadratic penalty method rather than using an
    /// augmented Lagrangian method. Specifically, the Lagrange multipliers for
    /// these components (which determine the shifts in ALM) are kept at zero.
    index_t penalty_alm_split = 0;

    BoxConstrProblem(length_t n, ///< Number of decision variables
                     length_t m) ///< Number of constraints
        : n{n}, m{m} {}

    BoxConstrProblem(Box C, Box D)
        : n{C.lowerbound.size()}, m{D.lowerbound.size()}, C{std::move(C)}, D{std::move(D)} {}

    BoxConstrProblem(const BoxConstrProblem &)                = default;
    BoxConstrProblem &operator=(const BoxConstrProblem &)     = default;
    BoxConstrProblem(BoxConstrProblem &&) noexcept            = default;
    BoxConstrProblem &operator=(BoxConstrProblem &&) noexcept = default;

    /// Constraints of the decision variables, @f$ x \in C @f$
    Box C{this->n};
    /// Other constraints, @f$ g(x) \in D @f$
    Box D{this->m};

    /// Number of decision variables, @ref n
    length_t get_n() const { return n; }
    /// Number of constraints, @ref m
    length_t get_m() const { return m; }

    /// @f$ \hat x = \Pi_C(x - \gamma\nabla\psi(x)) @f$
    /// @f$ p = \hat x - x @f$
    static real_t eval_proj_grad_step_box(const Box &C, real_t γ, crvec x, crvec grad_ψ, rvec x̂,
                                          rvec p) {
        using binary_real_f = real_t (*)(real_t, real_t);
        p                   = (-γ * grad_ψ)
                .binaryExpr(C.lowerbound - x, binary_real_f(std::fmax))
                .binaryExpr(C.upperbound - x, binary_real_f(std::fmin));
        x̂ = x + p;
        return real_t(0);
    }

    /// @see @ref TypeErasedProblem::eval_prox_grad_step
    real_t eval_prox_grad_step(real_t γ, crvec x, crvec grad_ψ, rvec x̂, rvec p) const {
        return eval_proj_grad_step_box(C, γ, x, grad_ψ, x̂, p);
    }

    /// @see @ref TypeErasedProblem::eval_proj_diff_g
    void eval_proj_diff_g(crvec z, rvec p) const { p = alpaqa::projecting_difference(z, D); }

    static void eval_proj_multipliers_box(const Box &D, rvec y, real_t M,
                                          index_t penalty_alm_split) {
        // If there's no lower bound, the multipliers can only be positive
        auto max_lb = [M](real_t y, real_t z_lb) {
            real_t y_lb = z_lb == -alpaqa::inf<config_t> ? 0 : -M;
            return std::max(y, y_lb);
        };
        // If there's no upper bound, the multipliers can only be negative
        auto min_ub = [M](real_t y, real_t z_ub) {
            real_t y_ub = z_ub == alpaqa::inf<config_t> ? 0 : M;
            return std::min(y, y_ub);
        };
        auto num_alm    = y.size() - penalty_alm_split;
        auto &&y_alm    = y.bottomRows(num_alm);
        auto &&z_alm_lb = D.lowerbound.bottomRows(num_alm);
        auto &&z_alm_ub = D.upperbound.bottomRows(num_alm);
        y_alm           = y_alm.binaryExpr(z_alm_lb, max_lb).binaryExpr(z_alm_ub, min_ub);
    }

    /// @see @ref TypeErasedProblem::eval_proj_multipliers
    void eval_proj_multipliers(rvec y, real_t M) const {
        eval_proj_multipliers_box(D, y, M, penalty_alm_split);
    }

    /// @see @ref TypeErasedProblem::get_box_C
    const Box &get_box_C() const { return C; }
    /// @see @ref TypeErasedProblem::get_box_D
    const Box &get_box_D() const { return D; }

    /// @see @ref TypeErasedProblem::check
    void check() const {
        util::check_dim_msg<config_t>(
            C.lowerbound, n,
            "Length of problem.C.lowerbound does not match problem size problem.n");
        util::check_dim_msg<config_t>(
            C.upperbound, n,
            "Length of problem.C.upperbound does not match problem size problem.n");
        util::check_dim_msg<config_t>(
            D.lowerbound, m,
            "Length of problem.D.lowerbound does not match problem size problem.m");
        util::check_dim_msg<config_t>(
            D.upperbound, m,
            "Length of problem.D.upperbound does not match problem size problem.m");
        if (penalty_alm_split < 0 || penalty_alm_split > m)
            throw std::invalid_argument("Invalid penalty_alm_split");
    }
};

} // namespace alpaqa
