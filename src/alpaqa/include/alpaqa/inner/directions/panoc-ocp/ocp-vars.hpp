#pragma once

#include <alpaqa/config/config.hpp>
#include <alpaqa/problem/ocproblem.hpp>
#include <concepts>
#include <numeric>

namespace alpaqa {

template <class V, class Conf>
concept VectorRefLike =
    std::convertible_to<V, rvec<Conf>> || std::convertible_to<V, crvec<Conf>>;

template <Config Conf, VectorRefLike<Conf> V>
constexpr auto const_or_mut_rvec(V &&v) {
    if constexpr (Eigen::internal::is_lvalue<std::remove_reference_t<V>>::value)
        return rvec<Conf>{v};
    else
        return crvec<Conf>{v};
}

template <Config Conf>
struct OCPVariables {
    USING_ALPAQA_CONFIG(Conf);

    OCPVariables(
        /// nx, nu, nh, nc
        const std::array<index_t, 4> &sizes,
        /// nx, nh, nc
        const std::array<index_t, 3> &sizes_N,
        /// Horizon length
        length_t N)
        : N{N} {
        std::partial_sum(sizes.begin(), sizes.end(), indices.begin());
        std::partial_sum(sizes_N.begin(), sizes_N.end(), indices_N.begin());
    }
    OCPVariables(const TypeErasedControlProblem<config_t> &prob)
        : OCPVariables{
              {prob.get_nx(), prob.get_nu(), prob.get_nh(), prob.get_nc()},
              {prob.get_nx(), prob.get_nh_N(), prob.get_nc_N()},
              prob.get_N(),
          } {}

    enum Indices {
        i_u   = 0,
        i_h   = 1,
        i_c   = 2,
        i_h_N = 0,
        i_c_N = 1,
    };
    length_t N;
    std::array<index_t, 4> indices;
    std::array<index_t, 3> indices_N;
    length_t size(size_t i) const { return indices[i + 1] - indices[i]; }
    length_t size_N(size_t i) const { return indices_N[i + 1] - indices_N[i]; }
    length_t nx() const { return indices[0]; }
    length_t nu() const { return size(i_u); }
    length_t nxu() const { return nx() + nu(); }
    length_t nh() const { return size(i_h); }
    length_t nc() const { return size(i_c); }
    length_t nx_N() const { return indices_N[0]; }
    length_t nh_N() const { return size_N(i_h_N); }
    length_t nc_N() const { return size_N(i_c_N); }

    vec create() const { return vec(N * indices.back() + indices_N.back()); }
    auto xk(VectorRefLike<config_t> auto &&v, index_t t) const {
        return const_or_mut_rvec<config_t>(v.segment(t * indices.back(), nx()));
    }
    auto xuk(VectorRefLike<config_t> auto &&v, index_t t) const {
        return const_or_mut_rvec<config_t>(
            v.segment(t * indices.back(), nxu()));
    }
    auto uk(VectorRefLike<config_t> auto &&v, index_t t) const {
        assert(t < N);
        return const_or_mut_rvec<config_t>(
            v.segment(t * indices.back() + indices[0], nu()));
    }
    auto hk(VectorRefLike<config_t> auto &&v, index_t t) const {
        return const_or_mut_rvec<config_t>(v.segment(
            t * indices.back() + (t < N ? indices[i_h] : indices_N[i_h_N]),
            (t < N ? nh() : nh_N())));
    }
    auto ck(VectorRefLike<config_t> auto &&v, index_t t) const {
        return v.segment(t * indices.back() +
                             (t < N ? indices[i_c] : indices_N[i_c_N]),
                         (t < N ? nc() : nc_N()));
    }

    vec create_qr() const { return vec(N * nxu() + nx()); }
    auto qk(VectorRefLike<config_t> auto &&v, index_t t) const {
        assert(t <= N);
        return const_or_mut_rvec<config_t>(v.segment(t * nxu(), nx()));
    }
    auto rk(VectorRefLike<config_t> auto &&v, index_t t) const {
        assert(t < N);
        return const_or_mut_rvec<config_t>(v.segment(t * nxu() + nx(), nu()));
    }
    auto qrk(VectorRefLike<config_t> auto &&v, index_t t) const {
        assert(t < N);
        return const_or_mut_rvec<config_t>(v.segment(t * nxu(), nxu()));
    }

    mat create_AB() const { return mat(nx(), nxu() * N); }
    rmat ABk(rmat AB, index_t t) const {
        return AB.middleCols(t * nxu(), nxu());
    }
    rmat Ak(rmat AB, index_t t) const { return AB.middleCols(t * nxu(), nx()); }
    rmat Bk(rmat AB, index_t t) const {
        return AB.middleCols(t * nxu() + nx(), nu());
    }
};

template <Config Conf>
struct OCPEvaluator {
    USING_ALPAQA_CONFIG(Conf);
    using OCPVars = OCPVariables<config_t>;
    using Problem = TypeErasedControlProblem<config_t>;
    using Box     = alpaqa::Box<config_t>;
    const Problem *problem;
    OCPVars vars;
    OCPEvaluator(const Problem &problem) : problem{&problem}, vars{problem} {}

    length_t N() const { return vars.N; }

    /// @pre x0 and u initialized
    /// @post x, h and c updated
    /// @return @f$ V(u) =
    ///             \sum_{k=0}^{N-1} \ell(h_k(x_k, u_k)) + V_f(h_N(x_N)) @f$
    real_t forward(rvec storage, const Box &D, const Box &D_N, crvec μ,
                   crvec y) const {
        real_t V  = 0;
        auto N    = this->N();
        auto nc   = vars.nc();
        auto nc_N = vars.nc_N();
        for (index_t t = 0; t < N; ++t) {
            auto xk = vars.xk(storage, t);
            auto uk = vars.uk(storage, t);
            auto hk = vars.hk(storage, t);
            auto ck = vars.ck(storage, t);
            problem->eval_h(t, xk, uk, hk);
            V += problem->eval_l(t, hk);
            if (nc > 0) {
                problem->eval_constr(t, xk, ck);
                auto yk = y.segment(t * nc, nc);
                auto ζ  = ck + (real_t(1) / μ(t)) * yk;
                V += real_t(0.5) * μ(t) *
                     projecting_difference(ζ, D).squaredNorm();
            }
            problem->eval_f(t, xk, uk, vars.xk(storage, t + 1));
        }
        auto xN = vars.xk(storage, N);
        auto hN = vars.hk(storage, N);
        auto cN = vars.ck(storage, N);
        problem->eval_h_N(xN, hN);
        V += problem->eval_l_N(hN);
        if (nc_N > 0) {
            problem->eval_constr_N(xN, cN);
            auto yN = y.segment(N * nc, nc_N);
            auto ζ  = cN + (real_t(1) / μ(N)) * yN;
            V += real_t(0.5) * μ(N) *
                 projecting_difference(ζ, D_N).squaredNorm();
        }
        return V;
    }

    /// @pre x0 and u initialized
    /// @post x updated
    void forward_simulate(rvec storage) const {
        for (index_t t = 0; t < N(); ++t) {
            auto xk = vars.xk(storage, t);
            auto uk = vars.uk(storage, t);
            auto hk = vars.hk(storage, t);
            auto ck = vars.ck(storage, t);
            problem->eval_h(t, xk, uk, hk);
            if (vars.nc() > 0)
                problem->eval_constr(t, xk, ck);
            problem->eval_f(t, xk, uk, vars.xk(storage, t + 1));
        }
        auto xN = vars.xk(storage, N());
        auto hN = vars.hk(storage, N());
        auto cN = vars.ck(storage, N());
        problem->eval_h_N(xN, hN);
        if (vars.nc_N() > 0)
            problem->eval_constr_N(xN, cN);
    }

    /// @pre x0 and u initialized
    void forward_simulate(crvec u, rvec x) const {
        assert(u.size() == vars.N * vars.nu());
        assert(x.size() == vars.nx());
        for (index_t t = 0; t < N(); ++t) {
            auto uk = u.segment(t * vars.nu(), vars.nu());
            problem->eval_f(t, x, uk, x);
        }
    }

    /// @pre x, u, h and c initialized (i.e. forward was called)
    void backward(rvec storage, rvec g, rvec λ, rvec w, rvec v, const auto &qr,
                  const auto &q_N, const Box &D, const Box &D_N, crvec μ,
                  crvec y) const {
        auto N    = this->N();
        auto nc   = vars.nc();
        auto nc_N = vars.nc_N();
        auto nu   = vars.nu();
        auto nx   = vars.nx();
        assert((nc <= 0 && nc_N <= 0) || w.size() == nx);
        assert((nc <= 0 && nc_N <= 0) || v.size() == std::max(nc, nc_N));
        auto qN = q_N();
        auto xN = vars.xk(storage, N);
        auto hN = vars.hk(storage, N);
        auto vN = v.topRows(nc_N);
        auto vk = v.topRows(nc);
        // λ ← ∇h(x)·∇l(h)
        problem->eval_q_N(xN, hN, λ);
        // λ ← ∇h(x)·∇l(h) + ∇c(x)·μ·(c(x) + μ⁻¹y - Π(c(x) + μ⁻¹y; D))
        if (nc_N > 0) {
            auto cN = vars.ck(storage, N);
            auto yN = y.segment(N * nc, nc_N);
            auto ζ  = cN + (real_t(1) / μ(N)) * yN;
            vN      = μ(N) * projecting_difference(ζ, D_N);
            problem->eval_grad_constr_prod_N(xN, vN, w);
            λ += w;
        }
        qN = λ;
        for (index_t t = N; t-- > 0;) {
            auto gt    = g.segment(t * nu, nu);
            auto hk    = vars.hk(storage, t);
            auto xuk   = vars.xuk(storage, t);
            auto xk    = vars.xk(storage, t);
            auto uk    = vars.uk(storage, t);
            auto &&qrk = qr(t);
            auto &&qk  = qrk.topRows(nx);
            auto &&rk  = qrk.bottomRows(nu);
            // /q\ ← /Aᵀ\ λ
            // \r/   \Bᵀ/ λ
            problem->eval_grad_f_prod(t, xk, uk, λ, qrk);
            // λ ← Aᵀλ, ∇ψ ← Bᵀλ
            λ  = qk;
            gt = rk;
            // /q\ ← ∇h(x,u)·∇l(h)
            // \r/
            problem->eval_qr(t, xuk, hk, qrk);
            // q ← ∇h(x)·∇l(h) + ∇c(x)·μ·(c(x) + μ⁻¹y - Π(c(x) + μ⁻¹y; D))
            if (nc > 0) {
                auto ck = vars.ck(storage, t);
                auto yk = y.segment(t * nc, nc);
                auto ζ  = ck + (real_t(1) / μ(t)) * yk;
                vk      = μ(t) * projecting_difference(ζ, D);
                problem->eval_grad_constr_prod(t, xk, vk, w);
                qk += w;
            }
            // λ ← q + Aᵀλ, ∇ψ ← r + Bᵀλ
            λ += qk;
            gt += rk;
        }
    }
};

namespace detail {

template <Config Conf>
void assign_interleave_xu(const OCPVariables<Conf> &dim, crvec<Conf> u,
                          rvec<Conf> storage) {
    for (index_t<Conf> t = 0; t < dim.N; ++t)
        dim.uk(storage, t) = u.segment(t * dim.nu(), dim.nu());
}
template <Config Conf>
void assign_interleave_xu(const OCPVariables<Conf> &dim, crvec<Conf> x,
                          crvec<Conf> u, rvec<Conf> storage) {
    for (index_t<Conf> t = 0; t < dim.N; ++t) {
        dim.xk(storage, t) = x.segment(t * dim.nx(), dim.nx());
        dim.uk(storage, t) = u.segment(t * dim.nu(), dim.nu());
    }
    dim.xk(storage, dim.N) = x.segment(dim.N * dim.nx(), dim.nx());
}
template <Config Conf>
void assign_extract_u(const OCPVariables<Conf> &dim, crvec<Conf> storage,
                      rvec<Conf> u) {
    for (index_t<Conf> t = 0; t < dim.N; ++t)
        u.segment(t * dim.nu(), dim.nu()) = dim.uk(storage, t);
}
template <Config Conf>
void assign_extract_x(const OCPVariables<Conf> &dim, crvec<Conf> storage,
                      rvec<Conf> x) {
    for (index_t<Conf> t = 0; t < dim.N + 1; ++t)
        x.segment(t * dim.nx(), dim.nx()) =
            storage.segment(t * (dim.nx() + dim.nu()), dim.nx());
}

template <Config Conf>
vec<Conf> extract_u(const TypeErasedControlProblem<Conf> &problem,
                    crvec<Conf> xu) {
    OCPVariables<Conf> dim{problem};
    vec<Conf> u(dim.N * dim.nu());
    assign_extract_u(dim, xu, u);
    return u;
}
template <Config Conf>
vec<Conf> extract_x(const TypeErasedControlProblem<Conf> &problem,
                    crvec<Conf> xu) {
    OCPVariables<Conf> dim{problem};
    vec<Conf> x((dim.N + 1) * dim.nx());
    assign_extract_x(dim, xu, x);
    return x;
}

} // namespace detail

} // namespace alpaqa