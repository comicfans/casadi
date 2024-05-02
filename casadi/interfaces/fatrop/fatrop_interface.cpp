/*
 *    This file is part of CasADi.
 *
 *    CasADi -- A symbolic framework for dynamic optimization.
 *    Copyright (C) 2010-2023 Joel Andersson, Joris Gillis, Moritz Diehl,
 *                            KU Leuven. All rights reserved.
 *    Copyright (C) 2011-2014 Greg Horn
 *
 *    CasADi is free software; you can redistribute it and/or
 *    modify it under the terms of the GNU Lesser General Public
 *    License as published by the Free Software Foundation; either
 *    version 3 of the License, or (at your option) any later version.
 *
 *    CasADi is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *    Lesser General Public License for more details.
 *
 *    You should have received a copy of the GNU Lesser General Public
 *    License along with CasADi; if not, write to the Free Software
 *    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */



#include "fatrop_interface.hpp"
#include "casadi/core/casadi_misc.hpp"
#include "../../core/global_options.hpp"
#include "../../core/casadi_interrupt.hpp"
#include "../../core/convexify.hpp"

#include <ctime>
#include <stdlib.h>
#include <iostream>
#include <iomanip>
#include <chrono>

#include <fatrop_runtime_str.h>

namespace casadi {
  extern "C"
  int CASADI_NLPSOL_FATROP_EXPORT
  casadi_register_nlpsol_fatrop(Nlpsol::Plugin* plugin) {
    plugin->creator = FatropInterface::creator;
    plugin->name = "fatrop";
    plugin->doc = FatropInterface::meta_doc.c_str();
    plugin->version = CASADI_VERSION;
    plugin->options = &FatropInterface::options_;
    plugin->deserialize = &FatropInterface::deserialize;
    return 0;
  }

  extern "C"
  void CASADI_NLPSOL_FATROP_EXPORT casadi_load_nlpsol_fatrop() {
    Nlpsol::registerPlugin(casadi_register_nlpsol_fatrop);
  }

  FatropInterface::FatropInterface(const std::string& name, const Function& nlp)
    : Nlpsol(name, nlp) {
  }

  FatropInterface::~FatropInterface() {
    clear_mem();
  }

  Sparsity FatropInterface::blocksparsity(casadi_int rows, casadi_int cols,
      const std::vector<casadi_ocp_block>& blocks, bool eye) {
    DM r(rows, cols);
    for (auto && b : blocks) {
      if (eye) {
        r(range(b.offset_r, b.offset_r+b.rows),
          range(b.offset_c, b.offset_c+b.cols)) = DM::eye(b.rows);
        casadi_assert_dev(b.rows==b.cols);
      } else {
        r(range(b.offset_r, b.offset_r+b.rows),
        range(b.offset_c, b.offset_c+b.cols)) = DM::zeros(b.rows, b.cols);
      }
    }
    return r.sparsity();
  }

  const Options FatropInterface::options_
  = {{&Nlpsol::options_},
     {{"N",
       {OT_INT,
        "OCP horizon"}},
      {"nx",
       {OT_INTVECTOR,
        "Number of states, length N+1"}},
      {"nu",
       {OT_INTVECTOR,
        "Number of controls, length N"}},
      {"ng",
       {OT_INTVECTOR,
        "Number of non-dynamic constraints, length N+1"}},
      {"fatrop",
       {OT_DICT,
        "Options to be passed to fatrop"}},
      {"convexify_strategy",
       {OT_STRING,
        "NONE|regularize|eigen-reflect|eigen-clip. "
        "Strategy to convexify the Lagrange Hessian before passing it to the solver."}},
      {"convexify_margin",
       {OT_DOUBLE,
        "When using a convexification strategy, make sure that "
        "the smallest eigenvalue is at least this (default: 1e-7)."}},
      {"fatrop",
       {OT_DICT,
        "Options to be passed to fatrop"
      }}
     }
  };

  void FatropInterface::init(const Dict& opts) {
    // Call the init method of the base class
    Nlpsol::init(opts);

    casadi_int struct_cnt=0;

    // Default options
    std::string convexify_strategy = "none";
    double convexify_margin = 1e-7;
    casadi_int max_iter_eig = 200;

    calc_g_ = true;
    calc_f_ = true;

    // Read options
    for (auto&& op : opts) {
      if (op.first=="N") {
        N_ = op.second;
        struct_cnt++;
      } else if (op.first=="nx") {
        nxs_ = op.second;
        struct_cnt++;
      } else if (op.first=="nu") {
        nus_ = op.second;
        struct_cnt++;
      } else if (op.first=="ng") {
        ngs_ = op.second;
        struct_cnt++;
      } else if (op.first=="convexify_strategy") {
        convexify_strategy = op.second.to_string();
      } else if (op.first=="convexify_margin") {
        convexify_margin = op.second;
      } else if (op.first=="max_iter_eig") {
        max_iter_eig = op.second;
      } else if (op.first=="fatrop") {
        opts_ = op.second;
      }
    }

    // Do we need second order derivatives?
    exact_hessian_ = true;
    auto hessian_approximation = opts_.find("hessian_approximation");
    if (hessian_approximation!=opts_.end()) {
      exact_hessian_ = hessian_approximation->second == "exact";
    }

    // Setup NLP functions
    create_function("nlp_f", {"x", "p"}, {"f"});
    create_function("nlp_g", {"x", "p"}, {"g"});
    if (!has_function("nlp_grad_f")) {
      create_function("nlp_grad_f", {"x", "p"}, {"grad:f:x"});
    }
    if (!has_function("nlp_jac_g")) {
      create_function("nlp_jac_g", {"x", "p"}, {"g", "jac:g:x"});
    }
    jacg_sp_ = get_function("nlp_jac_g").sparsity_out(1);

    convexify_ = false;

    // Allocate temporary work vectors
    if (exact_hessian_) {
      if (!has_function("nlp_hess_l")) {
        create_function("nlp_hess_l", {"x", "p", "lam:f", "lam:g"},
                        {"grad:gamma:x", "hess:gamma:x:x"}, {{"gamma", {"f", "g"}}});
      }
      hesslag_sp_ = get_function("nlp_hess_l").sparsity_out(1);
      casadi_assert(hesslag_sp_.is_symmetric(), "Hessian must be symmetric");
      if (convexify_strategy!="none") {
        convexify_ = true;
        Dict opts;
        opts["strategy"] = convexify_strategy;
        opts["margin"] = convexify_margin;
        opts["max_iter_eig"] = max_iter_eig;
        opts["verbose"] = verbose_;
        hesslag_sp_ = Convexify::setup(convexify_data_, hesslag_sp_, opts);
      }
    }

    bool detect_structure = struct_cnt==0;
    casadi_assert(struct_cnt==0 || struct_cnt==4,
      "You must either set all of N, nx, nu, ng; "
      "or set none at all (automatic detection).");

    const std::vector<casadi_int>& nx = nxs_;
    const std::vector<casadi_int>& ng = ngs_;
    const std::vector<casadi_int>& nu = nus_;

    Sparsity lamg_csp_, lam_ulsp_, lam_uusp_, lam_xlsp_, lam_xusp_, lam_clsp_;

    const Sparsity& A_ = jacg_sp_;
    casadi_int na_ = A_.size1(); //TODO(jgillis): replace with ng

    if (detect_structure) {
      /* General strategy: look for the xk+1 diagonal part in A
      */

      // Find the right-most column for each row in A -> A_skyline
      // Find the second-to-right-most column -> A_skyline2
      // Find the left-most column -> A_bottomline
      Sparsity AT = A_.T();
      std::vector<casadi_int> A_skyline;
      std::vector<casadi_int> A_skyline2;
      std::vector<casadi_int> A_bottomline;
      for (casadi_int i=0;i<AT.size2();++i) {
        casadi_int pivot = AT.colind()[i+1];
        A_bottomline.push_back(AT.row()[AT.colind()[i]]);
        if (pivot>AT.colind()[i]) {
          A_skyline.push_back(AT.row()[pivot-1]);
          if (pivot>AT.colind()[i]+1) {
            A_skyline2.push_back(AT.row()[pivot-2]);
          } else {
            A_skyline2.push_back(-1);
          }
        } else {
          A_skyline.push_back(-1);
          A_skyline2.push_back(-1);
        }
      }

      /*
      Loop over the right-most columns of A:
      they form the diagonal part due to xk+1 in gap constraints.
      detect when the diagonal pattern is broken -> new stage
      */
      casadi_int pivot = 0; // Current right-most element
      casadi_int start_pivot = pivot; // First right-most element that started the stage
      casadi_int cg = 0; // Counter for non-gap-closing constraints
      for (casadi_int i=0;i<na_;++i) { // Loop over all rows
        bool commit = false; // Set true to jump to the stage
        if (A_skyline[i]>pivot+1) { // Jump to a diagonal in the future
          nus_.push_back(A_skyline[i]-pivot-1); // Size of jump equals number of states
          commit = true;
        } else if (A_skyline[i]==pivot+1) { // Walking the diagonal
          if (A_skyline2[i]<start_pivot) { // Free of below-diagonal entries?
            pivot++;
          } else {
            nus_.push_back(0); // We cannot but conclude that we arrived at a new stage
            commit = true;
          }
        } else { // non-gap-closing constraint detected
          cg++;
        }

        if (commit) {
          nxs_.push_back(pivot-start_pivot+1);
          ngs_.push_back(cg); cg=0;
          start_pivot = A_skyline[i];
          pivot = A_skyline[i];
        }
      }
      nxs_.push_back(pivot-start_pivot+1);

      // Correction for k==0
      nxs_[0] = A_skyline[0];
      nus_[0] = 0;
      ngs_.erase(ngs_.begin());
      casadi_int cN=0;
      for (casadi_int i=na_-1;i>=0;--i) {
        if (A_bottomline[i]<start_pivot) break;
        cN++;
      }
      ngs_.push_back(cg-cN);
      ngs_.push_back(cN);

      N_ = nus_.size();
      nus_.push_back(0);

      if (N_>1) {
        if (nus_[0]==0 && nxs_[1]+nus_[1]==nxs_[0]) {
          nxs_[0] = nxs_[1];
          nus_[0] = nus_[1];
        }
      }
    }

    if (verbose_) {
      casadi_message("Using structure: N " + str(N_) + ", nx " + str(nx) + ", "
            "nu " + str(nu) + ", ng " + str(ng) + ".");
    }

    /* Disassemble A input into:
       A B I
       C D
           A B I
           C D
               C
    */
    casadi_int offset_r = 0, offset_c = 0;
    for (casadi_int k=0;k<N_;++k) { // Loop over blocks
      AB_blocks.push_back({offset_r,        offset_c,            nx[k+1], nx[k]+nu[k]});
      CD_blocks.push_back({offset_r+nx[k+1], offset_c,           ng[k], nx[k]+nu[k]});
      offset_c+= nx[k]+nu[k];
      if (k+1<N_)
        I_blocks.push_back({offset_r, offset_c, nx[k+1], nx[k+1]});
        // TODO(jgillis) actually use these
        // test5.py versus tesst6.py
        //   test5 changes behaviour when piping stdout to file -> memory corruption
        //   logs are ever so slightly different
      else
        I_blocks.push_back({offset_r, offset_c, nx[k+1], nx[k+1]});
      offset_r+= nx[k+1]+ng[k];
    }
    CD_blocks.push_back({offset_r, offset_c,           ng[N_], nx[N_]});

    casadi_int offset = 0;
    AB_offsets_.push_back(0);
    for (auto e : AB_blocks) {
      offset += e.rows*e.cols;
      AB_offsets_.push_back(offset);
    }
    offset = 0;
    CD_offsets_.push_back(0);
    for (auto e : CD_blocks) {
      offset += e.rows*e.cols;
      CD_offsets_.push_back(offset);
    }

    ABsp_ = blocksparsity(na_, nx_, AB_blocks);
    CDsp_ = blocksparsity(na_, nx_, CD_blocks);
    Isp_ = blocksparsity(na_, nx_, I_blocks, true);

    Sparsity total = ABsp_ + CDsp_ + Isp_;

    casadi_assert((A_ + total).nnz() == total.nnz(),
      "HPIPM: specified structure of A does not correspond to what the interface can handle. "
      "Structure is: N " + str(N_) + ", nx " + str(nx) + ", nu " + str(nu) + ", "
      "ng " + str(ng) + ".");
    casadi_assert_dev(total.nnz() == ABsp_.nnz() + CDsp_.nnz() + Isp_.nnz());

    /* Disassemble H input into:
       Q S'
       S R
           Q S'
           S R

       Multiply by 2
    */
    offset = 0;
    for (casadi_int k=0;k<N_+1;++k) { // Loop over blocks
      RSQ_blocks.push_back({offset, offset,       nx[k]+nu[k], nx[k]+nu[k]});
      offset+= nx[k]+nu[k];
    }
    RSQsp_ = blocksparsity(nx_, nx_, RSQ_blocks);

    offset = 0;
    RSQ_offsets_.push_back(0);
    for (auto e : RSQ_blocks) {
      offset += e.rows*e.cols;
      RSQ_offsets_.push_back(offset);
    }

    set_fatrop_prob();

    // Allocate memory
    casadi_int sz_arg, sz_res, sz_w, sz_iw;
    casadi_fatrop_work(&p_, &sz_arg, &sz_res, &sz_iw, &sz_w);

    alloc_arg(sz_arg, true);
    alloc_res(sz_res, true);
    alloc_iw(sz_iw, true);
    alloc_w(sz_w, true);
  }

  int FatropInterface::init_mem(void* mem) const {
    if (Nlpsol::init_mem(mem)) return 1;
    if (!mem) return 1;
    auto m = static_cast<FatropMemory*>(mem);
    fatrop_init_mem(&m->d);

    return 0;
  }

  void FatropInterface::free_mem(void* mem) const {
    auto m = static_cast<FatropMemory*>(mem);
    fatrop_free_mem(&m->d);
    delete static_cast<FatropMemory*>(mem);
  }

  /** \brief Set the (persistent) work vectors */
  void FatropInterface::set_work(void* mem, const double**& arg, double**& res,
                                casadi_int*& iw, double*& w) const {
    auto m = static_cast<FatropMemory*>(mem);

    // Set work in base classes
    Nlpsol::set_work(mem, arg, res, iw, w);

    m->d.prob = &p_;
    m->d.nlp = &m->d_nlp;

    casadi_fatrop_init(&m->d, &arg, &res, &iw, &w);

    m->d.nlp->oracle->m = static_cast<void*>(m);

    // options
  }

  int FatropInterface::solve(void* mem) const {
    auto m = static_cast<FatropMemory*>(mem);

    casadi_fatrop_presolve(&m->d);

    for (const auto& kv : opts_) {
      switch (fatrop_ocp_c_option_type(kv.first.c_str())) {
        case 0:
          fatrop_ocp_c_set_option_double(m->d.solver, kv.first.c_str(), kv.second);
          break;
        case 1:
          fatrop_ocp_c_set_option_int(m->d.solver, kv.first.c_str(), kv.second.to_int());
          break;
        case 2:
          fatrop_ocp_c_set_option_bool(m->d.solver, kv.first.c_str(), kv.second.to_bool());
          break;
        case 3:
          {
            std::string s = kv.second.to_string();
            fatrop_ocp_c_set_option_string(m->d.solver, kv.first.c_str(), s.c_str());
          }
          break;
        case -1:
          casadi_error("Fatrop option not supported: " + kv.first);
        default:
          casadi_error("Unknown option type.");
      }
    }

    casadi_fatrop_solve(&m->d);

    m->success = m->d.success;
    m->unified_return_status = static_cast<UnifiedReturnStatus>(m->d.unified_return_status);

    return 0;
  }

  Dict FatropInterface::get_stats(void* mem) const {
    Dict stats = Nlpsol::get_stats(mem);
    auto m = static_cast<FatropMemory*>(mem);
    Dict fatrop;
    fatrop["compute_sd_time"] = m->d.stats.compute_sd_time;
    fatrop["duinf_time"] = m->d.stats.duinf_time;
    fatrop["eval_hess_time"] = m->d.stats.eval_hess_time;
    fatrop["eval_jac_time"] = m->d.stats.eval_jac_time;
    fatrop["eval_cv_time"] = m->d.stats.eval_cv_time;
    fatrop["eval_grad_time"] = m->d.stats.eval_grad_time;
    fatrop["eval_obj_time"] = m->d.stats.eval_obj_time;
    fatrop["initialization_time"] = m->d.stats.initialization_time;
    fatrop["time_total"] = m->d.stats.time_total;
    fatrop["eval_hess_count"] = m->d.stats.eval_hess_count;
    fatrop["eval_jac_count"] = m->d.stats.eval_jac_count;
    fatrop["eval_cv_count"] = m->d.stats.eval_cv_count;
    fatrop["eval_grad_count"] = m->d.stats.eval_grad_count;
    fatrop["eval_obj_count"] = m->d.stats.eval_obj_count;
    fatrop["iterations_count"] = m->d.stats.iterations_count;
    fatrop["return_flag"] = m->d.stats.return_flag;
    stats["fatrop"] = fatrop;
    stats["iter_count"]  =m->d.stats.iterations_count;
    return stats;
  }

  void FatropInterface::codegen_init_mem(CodeGenerator& g) const {
    g << "fatrop_init_mem(&" + codegen_mem(g) + ");\n";
    g << "return 0;\n";
  }

  void FatropInterface::codegen_free_mem(CodeGenerator& g) const {
    g << "fatrop_free_mem(&" + codegen_mem(g) + ");\n";
  }

void FatropInterface::codegen_declarations(CodeGenerator& g) const {
  Nlpsol::codegen_declarations(g);
  g.add_auxiliary(CodeGenerator::AUX_NLP);
  g.add_auxiliary(CodeGenerator::AUX_MAX);
  g.add_auxiliary(CodeGenerator::AUX_COPY);
  g.add_auxiliary(CodeGenerator::AUX_PROJECT);
  g.add_auxiliary(CodeGenerator::AUX_SCAL);
  g.add_auxiliary(CodeGenerator::AUX_SPARSITY);
  g.add_auxiliary(CodeGenerator::AUX_ORACLE_CALLBACK);
  g.add_auxiliary(CodeGenerator::AUX_OCP_BLOCK);
  g.add_auxiliary(CodeGenerator::AUX_DENSIFY);
  g.add_auxiliary(CodeGenerator::AUX_SPARSIFY);
  g.add_auxiliary(CodeGenerator::AUX_INF);
  g.add_auxiliary(CodeGenerator::AUX_SCALED_COPY);
  g.add_auxiliary(CodeGenerator::AUX_AXPY);
  g.add_dependency(get_function("nlp_f"));
  g.add_dependency(get_function("nlp_grad_f"));
  g.add_dependency(get_function("nlp_g"));
  g.add_dependency(get_function("nlp_jac_g"));
  if (exact_hessian_) {
    g.add_dependency(get_function("nlp_hess_l"));
  }
  g.add_include("fatrop/ocp/OCPCInterface.h");

}

void FatropInterface::codegen_body(CodeGenerator& g) const {
  codegen_body_enter(g);
  g.auxiliaries << g.sanitize_source(fatrop_runtime_str, {"casadi_real"});

  g.local("d", "struct casadi_fatrop_data*");
  g.init_local("d", "&" + codegen_mem(g));
  g.local("p", "struct casadi_fatrop_prob");
  set_fatrop_prob(g);

  g << "casadi_fatrop_init(d, &arg, &res, &iw, &w);\n";
  g << "casadi_oracle_init(d->nlp->oracle, &arg, &res, &iw, &w);\n";
  g << "casadi_fatrop_presolve(d);\n";

  for (const auto& kv : opts_) {
    switch (fatrop_ocp_c_option_type(kv.first.c_str())) { 
      case 0:
        g << "fatrop_ocp_c_set_option_double(d->solver, \"" + kv.first + "\", "
              + str(kv.second) + ");\n";
        break;
      case 1:
        g << "fatrop_ocp_c_set_option_int(d->solver, \"" + kv.first + "\", "
              + str(kv.second.to_int()) + ");\n";
        break;
      case 2:
        g << "fatrop_ocp_c_set_option_bool(d->solver, \"" + kv.first + "\", "
              + str(int(kv.second.to_bool())) + ");\n";
        break;
      case 3:
        {
          std::string s = kv.second.to_string();
          g << "fatrop_ocp_c_set_option_bool(d->solver, \"" + kv.first + "\", \""
              + s + "\");\n";
        }
        break;
      case -1:
        casadi_error("Fatrop option not supported: " + kv.first);
      default:
        casadi_error("Unknown option type.");
    }
  }

  // Options
  g << "casadi_fatrop_solve(d);\n";

  codegen_body_exit(g);

  if (error_on_fail_) {
    g << "return d->unified_return_status;\n";
  } else {
    g << "return 0;\n";
  }
}

std::vector<casadi_int> fatrop_blocks_pack(const std::vector<casadi_ocp_block>& blocks) {
  size_t N = blocks.size();
  std::vector<casadi_int> ret(4*N);
  casadi_int* r = get_ptr(ret);
  for (casadi_int i=0;i<N;++i) {
    *r++ = blocks[i].offset_r;
    *r++ = blocks[i].offset_c;
    *r++ = blocks[i].rows;
    *r++ = blocks[i].cols;
  }
  return ret;
}



void FatropInterface::set_fatrop_prob() {
  p_.nlp = &p_nlp_;
  p_.nx  = get_ptr(nxs_);
  p_.nu  = get_ptr(nus_);
  p_.ABsp = ABsp_;
  p_.AB_offsets = get_ptr(AB_offsets_);
  p_.CDsp = CDsp_;
  p_.CD_offsets = get_ptr(CD_offsets_);
  p_.RSQsp = RSQsp_;
  p_.RSQ_offsets = get_ptr(RSQ_offsets_);
  p_.Isp = Isp_;
  p_.I_offsets = get_ptr(I_offsets_);

  p_.AB = get_ptr(AB_blocks);
  p_.CD = get_ptr(CD_blocks);
  p_.RSQ = get_ptr(RSQ_blocks);
  p_.I = get_ptr(I_blocks);
  p_.N = N_;

  p_.sp_a = jacg_sp_;
  p_.sp_h = hesslag_sp_;
  get_function("nlp_hess_l").save("nlp_hess_l.casadi");
  p_.nlp_hess_l = OracleCallback("nlp_hess_l", this);
  p_.nlp_jac_g = OracleCallback("nlp_jac_g", this);
  p_.nlp_grad_f = OracleCallback("nlp_grad_f", this);
  p_.nlp_f = OracleCallback("nlp_f", this);
  p_.nlp_g = OracleCallback("nlp_g", this);

  casadi_fatrop_setup(&p_);
}

  void codegen_unpack_block(CodeGenerator& g, const std::string& name,
      const std::vector<casadi_ocp_block>& blocks) {
    std::string n = "block_" + name + "[" + str(blocks.size()) + "]";
    g.local(n, "static struct casadi_ocp_block");
    g << "p." << name << " = block_" + name + ";\n";
    g << "casadi_unpack_ocp_blocks(" << blocks.size()
    << ", p." << name
    << ", " << g.constant(fatrop_blocks_pack(blocks)) << ");\n";
  }

void FatropInterface::set_fatrop_prob(CodeGenerator& g) const {
  if (jacg_sp_.size1()>0 && jacg_sp_.nnz()==0) {
    casadi_error("Empty sparsity pattern not supported in FATROP C interface");
  }
  g << "d->nlp = &d_nlp;\n";
  g << "d->prob = &p;\n";
  g << "p.nlp = &p_nlp;\n";

  g << "p.nx = " << g.constant(nxs_) << ";\n";
  g << "p.nu = " << g.constant(nus_) << ";\n";
  g << "p.ABsp = " << g.sparsity(ABsp_) << ";\n";
  g << "p.AB_offsets = " << g.constant(AB_offsets_) << ";\n";
  g << "p.CDsp = " << g.sparsity(CDsp_) << ";\n";
  g << "p.CD_offsets = " << g.constant(CD_offsets_) << ";\n";
  g << "p.RSQsp = " << g.sparsity(RSQsp_) << ";\n";
  g << "p.RSQ_offsets = " << g.constant(RSQ_offsets_) << ";\n";
  g << "p.Isp = " << g.sparsity(Isp_) << ";\n";
  g << "p.I_offsets = " << g.constant(I_offsets_) << ";\n";

  codegen_unpack_block(g, "AB", AB_blocks);
  codegen_unpack_block(g, "CD", CD_blocks);
  codegen_unpack_block(g, "RSQ", RSQ_blocks);
  codegen_unpack_block(g, "I", I_blocks);
  g << "p.N = " << N_ << ";\n";

  g.setup_callback("p.nlp_jac_g", get_function("nlp_jac_g"));
  g.setup_callback("p.nlp_grad_f", get_function("nlp_grad_f"));
  g.setup_callback("p.nlp_f", get_function("nlp_f"));
  g.setup_callback("p.nlp_g", get_function("nlp_g"));
  g.setup_callback("p.nlp_hess_l", get_function("nlp_hess_l"));

  g << "p.sp_a = " << g.sparsity(jacg_sp_) << ";\n";
  if (exact_hessian_) {
    g << "p.sp_h = " << g.sparsity(hesslag_sp_) << ";\n";
  } else {
    g << "p.sp_h = 0;\n";
  }

  g << "casadi_fatrop_setup(&p);\n";

}

} // namespace casadi
