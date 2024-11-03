#ifndef A2D_ADSCALAR_H
#define A2D_ADSCALAR_H

#include <complex>
#include <type_traits>

#include "a2ddefs.h"

namespace A2D {

// Primary template for get_non_scalar_type
template <typename... Types>
struct get_non_scalar_type;

// Specialization for the case where there are only two types
template <typename First, typename Second>
struct get_non_scalar_type<First, Second> {
  using type = typename std::conditional<is_scalar_type<First>::value, Second,
                                         First>::type;
};

// Recursive case
template <typename First, typename Second, typename Third, typename... Rest>
struct get_non_scalar_type<First, Second, Third, Rest...> {
  using type = typename std::conditional<
      is_scalar_type<
          typename get_non_scalar_type<Second, Third, Rest...>::type>::value,
      First, typename get_non_scalar_type<Second, Third, Rest...>::type>::type;
};

// Helper alias template to simplify usage
template <typename... Types>
using get_non_scalar_type_t = typename get_non_scalar_type<Types...>::type;

template <class T, int N>
class ADScalar {
 public:
  ADScalar() {}
  template <typename R, typename = std::enable_if_t<is_scalar_type<R>::value>>

  ADScalar(const R value) : value(value), deriv{0.0} {}

  template <typename R, typename = std::enable_if_t<is_scalar_type<R>::value>>
  ADScalar(const R value, const T d[]) : value(value) {
    for (int i = 0; i < N; i++) {
      deriv[i] = d[i];
    }
  }

  ADScalar(const ADScalar<T, N> &r) : value(r.value) {
    for (int i = 0; i < N; i++) {
      deriv[i] = r.deriv[i];
    }
  }

  template <typename R, typename = std::enable_if_t<is_scalar_type<R>::value>>
  inline ADScalar<T, N> &operator=(const R &r) {
    value = r;
    for (int i = 0; i < N; i++) {
      deriv[i] = 0.0;
    }
    return *this;
  }

  // Comparison operators
  template <typename R, typename = std::enable_if_t<is_scalar_type<R>::value>>
  inline bool operator<(const R &rhs) const {
    return value < rhs;
  }
  template <typename R, typename = std::enable_if_t<is_scalar_type<R>::value>>
  inline bool operator<=(const R &rhs) const {
    return value <= rhs;
  }
  template <typename R, typename = std::enable_if_t<is_scalar_type<R>::value>>
  inline bool operator>(const R &rhs) const {
    return value > rhs;
  }
  template <typename R, typename = std::enable_if_t<is_scalar_type<R>::value>>
  inline bool operator>=(const R &rhs) const {
    return value >= rhs;
  }

  template <typename X, int M>
  inline bool operator<(const ADScalar<X, M> &rhs) const {
    return value < rhs.value;
  }
  template <typename X, int M>
  inline bool operator<=(const ADScalar<X, M> &rhs) const {
    return value <= rhs.value;
  }
  template <typename X, int M>
  inline bool operator>(const ADScalar<X, M> &rhs) const {
    return value > rhs.value;
  }
  template <typename X, int M>
  inline bool operator>=(const ADScalar<X, M> &rhs) const {
    return value >= rhs.value;
  }

  // Operator +=, -=, *=, /=
  inline ADScalar<T, N> &operator+=(const ADScalar<T, N> &r) {
    value += r.value;
    for (int i = 0; i < N; i++) {
      deriv[i] += r.deriv[i];
    }
    return *this;
  }
  template <class R, typename = std::enable_if_t<is_scalar_type<R>::value>>
  inline ADScalar<T, N> &operator+=(const R &r) {
    value += r;
    return *this;
  }
  inline ADScalar<T, N> &operator-=(const ADScalar<T, N> &r) {
    value -= r.value;
    for (int i = 0; i < N; i++) {
      deriv[i] -= r.deriv[i];
    }
    return *this;
  }
  template <class R, typename = std::enable_if_t<is_scalar_type<R>::value>>
  inline ADScalar<T, N> &operator-=(const R &r) {
    value -= r;
    return *this;
  }
  inline ADScalar<T, N> &operator*=(const ADScalar<T, N> &r) {
    for (int i = 0; i < N; i++) {
      deriv[i] = r.value * deriv[i] + value * r.deriv[i];
    }
    value *= r.value;
    return *this;
  }
  template <class R, typename = std::enable_if_t<is_scalar_type<R>::value>>
  inline ADScalar<T, N> &operator*=(const R &r) {
    value *= r;
    for (int i = 0; i < N; i++) {
      deriv[i] = r * deriv[i];
    }
    return *this;
  }

  inline ADScalar<T, N> &operator/=(const ADScalar<T, N> &r) {
    T inv = 1.0 / r.value;
    T inv2 = value * inv * inv;
    value *= inv;
    for (int i = 0; i < N; i++) {
      deriv[i] = inv * deriv[i] - inv2 * r.deriv[i];
    }
    return *this;
  }
  template <class R, typename = std::enable_if_t<is_scalar_type<R>::value>>
  inline ADScalar<T, N> &operator/=(const R &r) {
    T inv = 1.0 / r;
    value *= inv;
    for (int i = 0; i < N; i++) {
      deriv[i] = inv * deriv[i];
    }
    return *this;
  }

  //  private:
  T value;
  T deriv[N];
};

// Addition
template <class X, int M>
inline ADScalar<X, M> operator+(const ADScalar<X, M> &l,
                                const ADScalar<X, M> &r) {
  ADScalar<X, M> out(l.value + r.value);
  for (int i = 0; i < M; i++) {
    out.deriv[i] = l.deriv[i] + r.deriv[i];
  }
  return out;
}
template <class X, int M, class L,
          typename = std::enable_if_t<is_scalar_type<L>::value>>
inline ADScalar<X, M> operator+(const L &l, const ADScalar<X, M> &r) {
  return ADScalar<X, M>(r.value + l, r.deriv);
}
template <class X, int M, class R,
          typename = std::enable_if_t<is_scalar_type<R>::value>>
inline ADScalar<X, M> operator+(const ADScalar<X, M> &l, const R &r) {
  return ADScalar<X, M>(l.value + r, l.deriv);
}

// Subtraction
template <class X, int M>
inline ADScalar<X, M> operator-(const ADScalar<X, M> &l,
                                const ADScalar<X, M> &r) {
  ADScalar<X, M> out(l.value - r.value);
  for (int i = 0; i < M; i++) {
    out.deriv[i] = l.deriv[i] - r.deriv[i];
  }
  return out;
}
template <class X, int M, class L,
          typename = std::enable_if_t<is_scalar_type<L>::value>>
inline ADScalar<X, M> operator-(const L &l, const ADScalar<X, M> &r) {
  ADScalar<X, M> out(l - r.value);
  for (int i = 0; i < M; i++) {
    out.deriv[i] = -r.deriv[i];
  }
  return out;
}
template <class X, int M, class R,
          typename = std::enable_if_t<is_scalar_type<R>::value>>
inline ADScalar<X, M> operator-(const ADScalar<X, M> &l, const R &r) {
  return ADScalar<X, M>(l.value - r, l.deriv);
}

// Multiplication
template <class X, int M>
inline ADScalar<X, M> operator*(const ADScalar<X, M> &l,
                                const ADScalar<X, M> &r) {
  ADScalar<X, M> out(l.value * r.value);
  for (int i = 0; i < M; i++) {
    out.deriv[i] = r.value * l.deriv[i] + r.deriv[i] * l.value;
  }
  return out;
}
template <class X, int M, class L,
          typename = std::enable_if_t<is_scalar_type<L>::value>>
inline ADScalar<X, M> operator*(const L &l, const ADScalar<X, M> &r) {
  ADScalar<X, M> out(l * r.value);
  for (int i = 0; i < M; i++) {
    out.deriv[i] = r.deriv[i] * l;
  }
  return out;
}
template <class X, int M, class R,
          typename = std::enable_if_t<is_scalar_type<R>::value>>
inline ADScalar<X, M> operator*(const ADScalar<X, M> &l, const R &r) {
  ADScalar<X, M> out(l.value * r);
  for (int i = 0; i < M; i++) {
    out.deriv[i] = l.deriv[i] * r;
  }
  return out;
}

// Division
template <class X, int M>
inline ADScalar<X, M> operator/(const ADScalar<X, M> &l,
                                const ADScalar<X, M> &r) {
  X inv = 1.0 / r.value;
  X inv2 = l.value * inv * inv;
  ADScalar<X, M> out(inv * l.value);

  for (int i = 0; i < M; i++) {
    out.deriv[i] = inv * l.deriv[i] - inv2 * r.deriv[i];
  }
  return out;
}
template <class X, int M, class L,
          typename = std::enable_if_t<is_scalar_type<L>::value>>
inline ADScalar<X, M> operator/(const L &l, const ADScalar<X, M> &r) {
  X inv = 1.0 / r.value;
  X inv2 = l * inv * inv;
  ADScalar<X, M> out(inv * l);

  for (int i = 0; i < M; i++) {
    out.deriv[i] = -inv2 * r.deriv[i];
  }
  return out;
}
template <class X, int M, class R,
          typename = std::enable_if_t<is_scalar_type<R>::value>>
inline ADScalar<X, M> operator/(const ADScalar<X, M> &l, const R &r) {
  X inv = 1.0 / r;
  ADScalar<X, M> out(inv * l.value);

  for (int i = 0; i < M; i++) {
    out.deriv[i] = inv * l.deriv[i];
  }
  return out;
}

// fabs, sqrt
template <class X, int M>
inline ADScalar<X, M> fabs(const ADScalar<X, M> &r) {
  X scalar = 1.0;
  if (r.value < 0.0) {
    scalar = -1.0;
  }
  ADScalar<X, M> out(fabs(r.value));
  for (int i = 0; i < M; i++) {
    out.deriv[i] = scalar * r.deriv[i];
  }
  return out;
}

template <class X, int M>
inline ADScalar<X, M> sqrt(const ADScalar<X, M> &r) {
  X value = sqrt(r.value);
  ADScalar<X, M> out(value);
  X inv = 0.5 / value;
  for (int i = 0; i < M; i++) {
    out.deriv[i] = inv * r.deriv[i];
  }
  return out;
}

template <class X, int M, class R,
          typename = std::enable_if_t<is_scalar_type<R>::value>>
inline ADScalar<X, M> pow(const ADScalar<X, M> &r, const R &exponent) {
  X value = pow(r.value, exponent);
  ADScalar<X, M> out(value);
  X inv = exponent * value / r.value;
  for (int i = 0; i < M; i++) {
    out.deriv[i] = inv * r.deriv[i];
  }
  return out;
}

template <class X, int M>
inline ADScalar<X, M> exp(const ADScalar<X, M> &r) {
  X value = exp(r.value);
  ADScalar<X, M> out(value);
  for (int i = 0; i < M; i++) {
    out.deriv[i] = value * r.deriv[i];
  }
  return out;
}

template <class X, int M>
inline ADScalar<X, M> sin(const ADScalar<X, M> &r) {
  ADScalar<X, M> out(sin(r.value));
  X d = cos(r.value);
  for (int i = 0; i < M; i++) {
    out.deriv[i] = d * r.deriv[i];
  }
}

template <class X, int M>
inline ADScalar<X, M> cos(const ADScalar<X, M> &r) {
  ADScalar<X, M> out(cos(r.value));
  X d = -sin(r.value);
  for (int i = 0; i < M; i++) {
    out.deriv[i] = d * r.deriv[i];
  }
}

}  // namespace A2D

#endif  // A2D_ADSCALAR_H