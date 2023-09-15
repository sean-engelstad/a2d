#ifndef A2D_MAT_VEC_MULT_H
#define A2D_MAT_VEC_MULT_H

#include <type_traits>

#include "a2ddefs.h"
#include "a2dmat.h"
#include "a2dstack.h"
#include "a2dtest.h"
#include "ad/core/a2dmatveccore.h"

namespace A2D {

template <typename T, int N, int M>
KOKKOS_FUNCTION void MatVecMult(const Mat<T, N, M>& A, const Vec<T, M>& x,
                                Vec<T, N>& y) {
  MatVecCore<T, N, M>(get_data(A), get_data(x), get_data(y));
}

template <MatOp op, typename T, int N, int M, int K, int P>
KOKKOS_FUNCTION void MatVecMult(const Mat<T, N, M>& A, const Vec<T, K>& x,
                                Vec<T, P>& y) {
  static_assert(((op == MatOp::NORMAL && (M == K && N == P)) ||
                 (op == MatOp::TRANSPOSE && (M == P && N == K))),
                "Matrix and vector dimensions must agree");
  MatVecCore<T, N, M, op>(get_data(A), get_data(x), get_data(y));
}

template <MatOp op, class Atype, class xtype, class ytype>
class MatVecMultExpr {
 public:
  static constexpr MatOp not_op =
      conditional_value<MatOp, op == MatOp::NORMAL, MatOp::TRANSPOSE,
                        MatOp::NORMAL>::value;

  // Extract the numeric type to use
  typedef typename get_object_numeric_type<ytype>::type T;

  // Extract the dimensions of the matrices
  static constexpr int N = get_matrix_rows<Atype>::size;
  static constexpr int M = get_matrix_columns<Atype>::size;
  static constexpr int K = get_vec_size<xtype>::size;
  static constexpr int P = get_vec_size<ytype>::size;

  // Get the types of the matrices
  static constexpr ADiffType adA = get_diff_type<Atype>::diff_type;
  static constexpr ADiffType adx = get_diff_type<xtype>::diff_type;

  // Get the differentiation order from the output
  static constexpr ADorder order = get_diff_order<ytype>::order;

  KOKKOS_FUNCTION MatVecMultExpr(Atype& A, xtype& x, ytype& y)
      : A(A), x(x), y(y) {
    static_assert(((op == MatOp::NORMAL && (M == K && N == P)) ||
                   (op == MatOp::TRANSPOSE && (M == P && N == K))),
                  "Matrix and vector dimensions must agree");
  }

  KOKKOS_FUNCTION void eval() {
    MatVecCore<T, N, M, op>(get_data(A), get_data(x), get_data(y));
  }

  template <ADorder forder>
  KOKKOS_FUNCTION void forward() {
    static_assert(
        !(order == ADorder::FIRST and forder == ADorder::SECOND),
        "Can't perform second order forward with first order objects");
    constexpr ADseed seed = conditional_value<ADseed, forder == ADorder::FIRST,
                                              ADseed::b, ADseed::p>::value;

    if constexpr (adA == ADiffType::ACTIVE && adx == ADiffType::ACTIVE) {
      constexpr bool additive = true;
      MatVecCore<T, N, M, op>(GetSeed<seed>::get_data(A), get_data(x),
                              GetSeed<seed>::get_data(y));
      MatVecCore<T, N, M, op, additive>(get_data(A), GetSeed<seed>::get_data(x),
                                        GetSeed<seed>::get_data(y));

    } else if constexpr (adA == ADiffType::ACTIVE) {
      MatVecCore<T, N, M, op>(GetSeed<seed>::get_data(A), get_data(x),
                              GetSeed<seed>::get_data(y));
    } else if constexpr (adx == ADiffType::ACTIVE) {
      MatVecCore<T, N, M, op>(get_data(A), GetSeed<seed>::get_data(x),
                              GetSeed<seed>::get_data(y));
    }
  }

  KOKKOS_FUNCTION void reverse() {
    constexpr bool additive = true;
    if constexpr (adA == ADiffType::ACTIVE) {
      if constexpr (op == MatOp::NORMAL) {
        VecOuterCore<T, N, M, additive>(GetSeed<ADseed::b>::get_data(y),
                                        get_data(x),
                                        GetSeed<ADseed::b>::get_data(A));
      } else {
        VecOuterCore<T, N, M, additive>(get_data(x),
                                        GetSeed<ADseed::b>::get_data(y),
                                        GetSeed<ADseed::b>::get_data(A));
      }
    }
    if constexpr (adx == ADiffType::ACTIVE) {
      MatVecCore<T, N, M, not_op, additive>(get_data(A),
                                            GetSeed<ADseed::b>::get_data(y),
                                            GetSeed<ADseed::b>::get_data(x));
    }
  }

  KOKKOS_FUNCTION void hreverse() {
    constexpr bool additive = true;
    if constexpr (adA == ADiffType::ACTIVE) {
      if constexpr (op == MatOp::NORMAL) {
        VecOuterCore<T, N, M, additive>(GetSeed<ADseed::h>::get_data(y),
                                        get_data(x),
                                        GetSeed<ADseed::h>::get_data(A));
      } else {
        VecOuterCore<T, N, M, additive>(get_data(x),
                                        GetSeed<ADseed::h>::get_data(y),
                                        GetSeed<ADseed::h>::get_data(A));
      }
    }
    if constexpr (adx == ADiffType::ACTIVE) {
      MatVecCore<T, N, M, not_op, additive>(get_data(A),
                                            GetSeed<ADseed::h>::get_data(y),
                                            GetSeed<ADseed::h>::get_data(x));
    }
    if constexpr (adA == ADiffType::ACTIVE && adx == ADiffType::ACTIVE) {
      if constexpr (op == MatOp::NORMAL) {
        VecOuterCore<T, N, M, additive>(GetSeed<ADseed::b>::get_data(y),
                                        GetSeed<ADseed::p>::get_data(x),
                                        GetSeed<ADseed::h>::get_data(A));
      } else {
        VecOuterCore<T, N, M, additive>(GetSeed<ADseed::p>::get_data(x),
                                        GetSeed<ADseed::b>::get_data(y),
                                        GetSeed<ADseed::h>::get_data(A));
      }

      MatVecCore<T, N, M, not_op, additive>(GetSeed<ADseed::p>::get_data(A),
                                            GetSeed<ADseed::b>::get_data(y),
                                            GetSeed<ADseed::h>::get_data(x));
    }
  }

 private:
  Atype& A;
  xtype& x;
  ytype& y;
};

template <class Atype, class xtype, class ytype>
KOKKOS_FUNCTION auto MatVecMult(ADObj<Atype>& A, ADObj<xtype>& x,
                                ADObj<ytype>& y) {
  return MatVecMultExpr<MatOp::NORMAL, ADObj<Atype>, ADObj<xtype>,
                        ADObj<ytype>>(A, x, y);
}
template <class Atype, class xtype, class ytype>
KOKKOS_FUNCTION auto MatVecMult(A2DObj<Atype>& A, A2DObj<xtype>& x,
                                A2DObj<ytype>& y) {
  return MatVecMultExpr<MatOp::NORMAL, A2DObj<Atype>, A2DObj<xtype>,
                        A2DObj<ytype>>(A, x, y);
}
template <MatOp op, class Atype, class xtype, class ytype>
KOKKOS_FUNCTION auto MatVecMult(ADObj<Atype>& A, ADObj<xtype>& x,
                                ADObj<ytype>& y) {
  return MatVecMultExpr<op, ADObj<Atype>, ADObj<xtype>, ADObj<ytype>>(A, x, y);
}
template <MatOp op, class Atype, class xtype, class ytype>
KOKKOS_FUNCTION auto MatVecMult(A2DObj<Atype>& A, A2DObj<xtype>& x,
                                A2DObj<ytype>& y) {
  return MatVecMultExpr<op, A2DObj<Atype>, A2DObj<xtype>, A2DObj<ytype>>(A, x,
                                                                         y);
}

namespace Test {

template <MatOp op, typename T, int N, int M, int K, int P>
class MatVecMultTest : public A2DTest<T, Vec<T, P>, Mat<T, N, M>, Vec<T, K>> {
 public:
  using Input = VarTuple<T, Mat<T, N, M>, Vec<T, K>>;
  using Output = VarTuple<T, Vec<T, P>>;

  // Assemble a string to describe the test
  std::string name() {
    std::stringstream s;
    s << "MatVecMult<";
    if (op == MatOp::NORMAL) {
      s << "N,";
    } else {
      s << "T,";
    }
    s << N << "," << M << "," << K << "," << P << ">";
    return s.str();
  }

  // Evaluate the matrix-matrix product
  Output eval(const Input& X) {
    Mat<T, N, M> A;
    Vec<T, K> x;
    Vec<T, P> y;

    X.get_values(A, x);
    MatVecMult<op>(A, x, y);
    return MakeVarTuple<T>(y);
  }

  // Compute the derivative
  void deriv(const Output& seed, const Input& X, Input& g) {
    ADObj<Mat<T, N, M>> A;
    ADObj<Vec<T, K>> x;
    ADObj<Vec<T, P>> y;

    X.get_values(A.value(), x.value());
    auto stack = MakeStack(MatVecMult<op>(A, x, y));
    seed.get_values(y.bvalue());
    stack.reverse();
    g.set_values(A.bvalue(), x.bvalue());
  }

  // Compute the second-derivative
  void hprod(const Output& seed, const Output& hval, const Input& X,
             const Input& p, Input& h) {
    A2DObj<Mat<T, N, M>> A;
    A2DObj<Vec<T, K>> x;
    A2DObj<Vec<T, P>> y;

    X.get_values(A.value(), x.value());
    p.get_values(A.pvalue(), x.pvalue());
    auto stack = MakeStack(MatVecMult<op>(A, x, y));
    seed.get_values(y.bvalue());
    hval.get_values(y.hvalue());
    stack.reverse();
    stack.hforward();
    stack.hreverse();
    h.set_values(A.hvalue(), x.hvalue());
  }
};

template <typename T, int N, int M>
bool MatVecMultTestHelper(bool component = false, bool write_output = true) {
  const MatOp NORMAL = MatOp::NORMAL;
  const MatOp TRANSPOSE = MatOp::TRANSPOSE;
  using Tc = std::complex<T>;

  bool passed = true;
  MatVecMultTest<NORMAL, Tc, N, M, M, N> test1;
  passed = passed && Run(test1, component, write_output);

  MatVecMultTest<TRANSPOSE, Tc, M, N, M, N> test2;
  passed = passed && Run(test2, component, write_output);

  return passed;
}

bool MatVecMultTestAll(bool component = false, bool write_output = true) {
  bool passed = true;
  passed =
      passed && MatVecMultTestHelper<double, 3, 3>(component, write_output);
  passed =
      passed && MatVecMultTestHelper<double, 2, 4>(component, write_output);
  passed =
      passed && MatVecMultTestHelper<double, 5, 3>(component, write_output);

  return passed;
}

}  // namespace Test

}  // namespace A2D

#endif  // A2D_MAT_VEC_MULT_H