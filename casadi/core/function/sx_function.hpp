/*
 *    This file is part of CasADi.
 *
 *    CasADi -- A symbolic framework for dynamic optimization.
 *    Copyright (C) 2010-2014 Joel Andersson, Joris Gillis, Moritz Diehl,
 *                            K.U. Leuven. All rights reserved.
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


#ifndef CASADI_SX_FUNCTION_HPP
#define CASADI_SX_FUNCTION_HPP

#include "function.hpp"
#ifdef USE_CXX11
#include <initializer_list>
#endif // USE_CXX11

namespace casadi {

/// \cond INTERNAL
#ifndef SWIG

  /** \brief  An atomic operation for the SXElement virtual machine */
  struct ScalarAtomic {
    int op;     /// Operator index
    int i0;
    union {
      double d;
      struct { int i1, i2; };
    };
  };

#endif // SWIG

/// \endcond

  /// Forward declaration of internal class
  class SXFunctionInternal;

  /// Forward declaration of MXFunction
  class MXFunction;

  /**   \brief Dynamically created function that can be expanded into a series of scalar operations.

        \author Joel Andersson
        \date 2010-2013
  */
  class CASADI_EXPORT SXFunction : public Function {

  public:
    /// Default constructor
    SXFunction();

    /// Expand an MXFunction
    explicit SXFunction(const MXFunction &f);

    /// Expand a Function
    explicit SXFunction(const Function &f);

    /// Expand a function
    SXFunction(const std::string& name, const Function &f, const Dict& opts=Dict());

    /** \brief Construct from vectors (new syntax, includes initialization) */
    SXFunction(const std::string& name, const std::vector<SX>& arg,
               const std::vector<SX>& res, const Dict& opts=Dict());

    /** \brief Construct from vectors (new syntax, includes initialization) */
    SXFunction(const std::string& name, const std::pair< SXDict, std::vector<std::string> >& arg,
               const std::vector<SX>& res, const Dict& opts=Dict());

    /** \brief Construct from vectors (new syntax, includes initialization) */
    SXFunction(const std::string& name, const std::vector<SX>& arg,
               const std::pair< SXDict, std::vector<std::string> >& res, const Dict& opts=Dict());

    /** \brief Construct from vectors (new syntax, includes initialization) */
    SXFunction(const std::string& name, const std::pair< SXDict, std::vector<std::string> >& arg,
               const std::pair< SXDict, std::vector<std::string> >& res, const Dict& opts=Dict());
#ifndef SWIG
#ifdef USE_CXX11
    /** \brief Construct from initializer lists (new syntax, includes initialization) */
    SXFunction(const std::string& name,
               std::initializer_list<SX> arg,
               std::initializer_list<SX> res,
               const Dict& opts=Dict());

    /** \brief Construct from vector & nitializer list (new syntax, includes initialization) */
    SXFunction(const std::string& name,
               std::vector<SX> arg,
               std::initializer_list<SX> res,
               const Dict& opts=Dict());

    /** \brief Construct from initializer list & vector (new syntax, includes initialization) */
    SXFunction(const std::string& name,
               std::initializer_list<SX> arg,
               std::vector<SX> res,
               const Dict& opts=Dict());
#endif // USE_CXX11
#endif // SWIG

/// \cond INTERNAL
    /// Access functions of the node
    SXFunctionInternal* operator->();

    /// Const access functions of the node
    const SXFunctionInternal* operator->() const;
/// \endcond

#ifdef WITH_DEPRECATED_FEATURES
    ///@{
    /** \brief Jacobian expression
     */
    SX jac(int iind=0, int oind=0, bool compact=false, bool symmetric=false);
    SX jac(const std::string& iname, int oind=0, bool compact=false, bool symmetric=false)
    { return jac(index_in(iname), oind, compact, symmetric); }
    SX jac(int iind, const std::string& oname, bool compact=false, bool symmetric=false)
    { return jac(iind, index_out(oname), compact, symmetric); }
    SX jac(const std::string& iname, const std::string& oname,
           bool compact=false, bool symmetric=false)
    { return jac(index_in(iname), index_out(oname), compact, symmetric); }
    ///@}

    ///@{
    /// Gradient expression
    SX grad(int iind=0, int oind=0);
    SX grad(const std::string& iname, int oind=0) { return grad(index_in(iname), oind); }
    SX grad(int iind, const std::string& oname) { return grad(iind, index_out(oname)); }
    SX grad(const std::string& iname, const std::string& oname)
    { return grad(index_in(iname), index_out(oname)); }
    ///@}

    ///@{
    /// Tangent expression
    SX tang(int iind=0, int oind=0);
    SX tang(const std::string& iname, int oind=0) { return tang(index_in(iname), oind); }
    SX tang(int iind, const std::string& oname) { return tang(iind, index_out(oname)); }
    SX tang(const std::string& iname, const std::string& oname)
    { return tang(index_in(iname), index_out(oname)); }
    ///@}

    ///@{
    /** \bried Hessian expression */
    SX hess(int iind=0, int oind=0);
    SX hess(const std::string& iname, int oind=0) { return hess(index_in(iname), oind); }
    SX hess(int iind, const std::string& oname) { return hess(iind, index_out(oname)); }
    SX hess(const std::string& iname, const std::string& oname) {
      return hess(index_in(iname), index_out(oname));
    }
    ///@}
#endif // WITH_DEPRECATED_FEATURES

/// \cond INTERNAL
#ifndef SWIG
    /** \brief Access the algorithm directly */
    const std::vector<ScalarAtomic>& algorithm() const;

    /** \brief Called from constructor */
    void construct(const std::string& name, const std::vector<SX>& arg,
                   const std::vector<SX>& res, const Dict& opts,
                   const std::vector<std::string>& ischeme=std::vector<std::string>(),
                   const std::vector<std::string>& oscheme=std::vector<std::string>());
#endif // SWIG
/// \endcond

    /** \brief Get the number of atomic operations */
    int getAlgorithmSize() const { return algorithm().size();}

    /** \brief Get the length of the work vector */
    int getWorkSize() const;

    /** \brief Get an atomic operation operator index */
    int getAtomicOperation(int k) const { return algorithm().at(k).op;}

    /** \brief Get the (integer) input arguments of an atomic operation */
    std::pair<int, int> getAtomicInput(int k) const {
      const ScalarAtomic& atomic = algorithm().at(k);
      return std::pair<int, int>(atomic.i1, atomic.i2);}

    /** \brief Get the floating point output argument of an atomic operation */
    double getAtomicInputReal(int k) const { return algorithm().at(k).d;}

    /** \brief Get the (integer) output argument of an atomic operation */
    int getAtomicOutput(int k) const { return algorithm().at(k).i0;}

    /** \brief Number of nodes in the algorithm */
    int countNodes() const;

    /** \brief Clear the function from its symbolic representation, to free up memory,
     * no symbolic evaluations are possible after this */
    void clearSymbolic();

    /** \brief Get the corresponding matrix type */
    typedef SX MatType;

    /// Check if a particular cast is allowed
    static bool testCast(const SharedObjectNode* ptr);
  };

} // namespace casadi

#endif // CASADI_SX_FUNCTION_HPP
