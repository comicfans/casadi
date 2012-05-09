/*
 *    This file is part of CasADi.
 *
 *    CasADi -- A symbolic framework for dynamic optimization.
 *    Copyright (C) 2010 by Joel Andersson, Moritz Diehl, K.U.Leuven. All rights reserved.
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

#ifndef QPOASES_INTERNAL_HPP
#define QPOASES_INTERNAL_HPP

#include "casadi/fx/qp_solver_internal.hpp"

#include <qpOASES.hpp>

namespace CasADi{
namespace Interfaces {

  /** \brief Internal class for QPOasesSolver
   * 
      @copydoc QPSolver_doc
   * */
class QPOasesInternal : public QPSolverInternal {
  friend class QPOasesSolver;
public:
  /** \brief  Constructor */
  explicit QPOasesInternal();

  /** \brief  Clone */
  virtual QPOasesInternal* clone() const;
  
  /** \brief  Create a new Solver */
  explicit QPOasesInternal(const CRSSparsity& H, const CRSSparsity& A);

  /** \brief  Destructor */
  virtual ~QPOasesInternal();

  /** \brief  Initialize */
  virtual void init();
  
  virtual void evaluate(int nfdir, int nadir);
  
  protected:
    
    /// QP Solver
    union{
      qpOASES::QProblemB *qp_;
    };
    	
    /// Number of working set recalculations
    int max_nWSR_;
    
    /// CPUtime for initialisation
    double max_cputime_;
    
    /// Throw error
    static void qpoases_error(const std::string& module, int flag);
    
    /// Has qpOASES been called once?
    bool called_once_;
    
    /// Dense data for H and A
    std::vector<double> h_data_;
    std::vector<double> a_data_;
    
    /// Temporary vector holding the dual solution
    std::vector<double> dual_;
    
    /// Get qpOASES error message
    static std::string getErrorMessage(int flag);
};


} // namespace Interfaces
} // namespace CasADi

#endif //QPOASES_INTERNAL_HPP

