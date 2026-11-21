#pragma once

#include <cassert>
#include <compare>
#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

inline size_t GetUniqueId() {
  static size_t id = 0;
  return id++;
}

template <class Container, class T, class Comp = std::equal_to<>>
bool Contains(const Container &c, const T &t, Comp comp = Comp{}) {
  for (const auto &x : c) {
    if (comp(x, t)) {
      return true;
    }
  }
  return false;
}

template <typename T> class MayOwnPtr {
public:
  MayOwnPtr() : _ptr(nullptr), _own(false) {}

  MayOwnPtr(T *ptr, bool own) : _ptr(ptr), _own(own) {}

  MayOwnPtr(const T *ptr) : _ptr(ptr), _own(false) {}

  MayOwnPtr(const std::unique_ptr<T> &p) : _ptr(p.get()), _own(false) {}

  MayOwnPtr(std::unique_ptr<T> &&p) : _ptr(p.release()), _own(true) {}

  MayOwnPtr(const MayOwnPtr &) = delete;

  MayOwnPtr(MayOwnPtr &&rhs) noexcept
      : _ptr(std::exchange(rhs._ptr, nullptr)),
        _own(std::exchange(rhs._own, false)) {}

  MayOwnPtr &operator=(const MayOwnPtr &) = delete;

  MayOwnPtr &operator=(MayOwnPtr &&rhs) noexcept {
    if (&rhs == this) {
      return *this;
    }

    if (_own) {
      delete _ptr;
      _ptr = nullptr;
    }
    _ptr = std::exchange(rhs._ptr, nullptr);
    _own = std::exchange(rhs._own, false);
    return *this;
  }

  ~MayOwnPtr() noexcept {
    if (_own) {
      delete _ptr;
      _ptr = nullptr;
    }
  }

  T *get() const { return _ptr; }

  bool own() const { return _own; }

  T &operator*() const { return *_ptr; }
  T *operator->() const { return _ptr; }
  explicit operator bool() const noexcept { return _ptr != nullptr; }

private:
  T *_ptr;
  bool _own;
};

template <typename T, typename Deleter = std::default_delete<T>>
class OptUnique;

namespace detail {

template <typename T> struct is_optional_unnullable : std::false_type {};

template <typename T, typename D>
struct is_optional_unnullable<OptUnique<T, D>> : std::true_type {};

template <typename T>
inline constexpr bool is_optional_unnullable_v =
    is_optional_unnullable<std::decay_t<T>>::value;

} // namespace detail

template <typename T, typename Deleter> class OptUnique {
public:
  using value_type = std::unique_ptr<T, Deleter>;
  using pointer = typename value_type::pointer;
  using element_type = T;
  using deleter_type = Deleter;

  OptUnique() noexcept = default;

  OptUnique(std::nullopt_t) noexcept : ptr_(nullptr) {}

  OptUnique(const OptUnique &) = delete;
  OptUnique(OptUnique &&) noexcept = default;

  template <typename U, typename E>
    requires(!(std::is_same_v<U, T> && std::is_same_v<E, Deleter>) &&
             std::is_constructible_v<value_type, std::unique_ptr<U, E> &&>)
  explicit(!std::is_convertible_v<std::unique_ptr<U, E> &&, value_type>)
      OptUnique(OptUnique<U, E> &&other) noexcept
      : ptr_(std::move(other.ptr_)) {}

  template <typename... Args>
    requires std::is_constructible_v<value_type, Args...>
  explicit OptUnique(std::in_place_t, Args &&...args)
      : ptr_(std::forward<Args>(args)...) {
    assert(ptr_ != nullptr && "OptUnique cannot hold a null pointer");
  }

  template <typename U = value_type>
    requires(!detail::is_optional_unnullable_v<U> &&
             !std::is_same_v<std::decay_t<U>, std::in_place_t> &&
             !std::is_same_v<std::decay_t<U>, std::nullopt_t> &&
             std::is_constructible_v<value_type, U &&>)
  explicit(!std::is_convertible_v<U &&, value_type>) OptUnique(U &&value)
      : ptr_(std::forward<U>(value)) {
    assert(ptr_ != nullptr && "OptUnique cannot hold a null pointer");
  }

  ~OptUnique() = default;

  OptUnique &operator=(std::nullopt_t) noexcept {
    ptr_.reset();
    return *this;
  }

  OptUnique &operator=(const OptUnique &) = delete;
  OptUnique &operator=(OptUnique &&) noexcept = default;

  template <typename U, typename E>
    requires(!(std::is_same_v<U, T> && std::is_same_v<E, Deleter>) &&
             std::is_constructible_v<value_type, std::unique_ptr<U, E> &&> &&
             std::is_assignable_v<value_type &, std::unique_ptr<U, E> &&>)
  OptUnique &operator=(OptUnique<U, E> &&other) noexcept {
    ptr_ = std::move(other.ptr_);
    return *this;
  }

  template <typename U = value_type>
    requires(!detail::is_optional_unnullable_v<U> &&
             !std::is_same_v<std::decay_t<U>, std::nullopt_t> &&
             std::is_constructible_v<value_type, U &&> &&
             std::is_assignable_v<value_type &, U &&>)
  OptUnique &operator=(U &&value) {
    value_type tmp(std::forward<U>(value));
    assert(tmp != nullptr && "OptUnique cannot hold a null pointer");
    ptr_ = std::move(tmp);
    return *this;
  }

  const value_type *operator->() const noexcept {
    assert(has_value());
    return &ptr_;
  }

  value_type *operator->() noexcept {
    assert(has_value());
    return &ptr_;
  }

  const value_type &operator*() const & noexcept {
    assert(has_value());
    return ptr_;
  }

  value_type &operator*() & noexcept {
    assert(has_value());
    return ptr_;
  }

  const value_type &&operator*() const && noexcept {
    assert(has_value());
    return std::move(ptr_);
  }

  value_type &&operator*() && noexcept {
    assert(has_value());
    return std::move(ptr_);
  }

  explicit operator bool() const noexcept { return has_value(); }

  bool has_value() const noexcept { return ptr_ != nullptr; }

  value_type &value() & {
    if (!has_value()) {
      throw std::bad_optional_access();
    }
    return ptr_;
  }

  const value_type &value() const & {
    if (!has_value()) {
      throw std::bad_optional_access();
    }
    return ptr_;
  }

  value_type &&value() && {
    if (!has_value()) {
      throw std::bad_optional_access();
    }
    return std::move(ptr_);
  }

  const value_type &&value() const && {
    if (!has_value()) {
      throw std::bad_optional_access();
    }
    return std::move(ptr_);
  }

  template <typename U>
    requires std::is_convertible_v<U &&, value_type>
  value_type value_or(U &&default_value) && {
    return has_value() ? std::move(ptr_)
                       : value_type(std::forward<U>(default_value));
  }

  // --- 修改器 ---

  void swap(OptUnique &other) noexcept { ptr_.swap(other.ptr_); }

  void reset() noexcept { ptr_.reset(); }

  template <typename... Args>
    requires std::is_constructible_v<value_type, Args...>
  value_type &emplace(Args &&...args) {
    value_type tmp(std::forward<Args>(args)...);
    assert(tmp != nullptr && "OptUnique cannot hold a null pointer");
    ptr_ = std::move(tmp);
    return ptr_;
  }

private:
  template <typename U, typename E> friend class OptUnique;

  value_type ptr_;
};

template <typename T, typename Deleter>
void swap(OptUnique<T, Deleter> &lhs, OptUnique<T, Deleter> &rhs) noexcept {
  lhs.swap(rhs);
}

template <typename T1, typename D1, typename T2, typename D2>
  requires requires(const std::unique_ptr<T1, D1> &a,
                    const std::unique_ptr<T2, D2> &b) {
    { a == b } -> std::convertible_to<bool>;
  }
bool operator==(const OptUnique<T1, D1> &lhs, const OptUnique<T2, D2> &rhs) {
  return lhs.has_value() == rhs.has_value() &&
         (!lhs.has_value() || *lhs == *rhs);
}

template <typename T1, typename D1, typename T2, typename D2>
  requires requires(const std::unique_ptr<T1, D1> &a,
                    const std::unique_ptr<T2, D2> &b) { a <=> b; }
auto operator<=>(const OptUnique<T1, D1> &lhs, const OptUnique<T2, D2> &rhs)
    -> std::compare_three_way_result_t<std::unique_ptr<T1, D1>,
                                       std::unique_ptr<T2, D2>> {
  if (lhs.has_value() && rhs.has_value()) {
    return *lhs <=> *rhs;
  }
  return lhs.has_value() <=> rhs.has_value();
}

template <typename T, typename D>
bool operator==(const OptUnique<T, D> &opt, std::nullopt_t) noexcept {
  return !opt.has_value();
}

template <typename T, typename D>
std::strong_ordering operator<=>(const OptUnique<T, D> &opt,
                                 std::nullopt_t) noexcept {
  return opt.has_value() <=> false;
}

template <typename T, typename D, typename U>
  requires(!detail::is_optional_unnullable_v<U> &&
           !std::is_same_v<std::decay_t<U>, std::nullopt_t> &&
           requires(const std::unique_ptr<T, D> &p, const U &v) {
             { p == v } -> std::convertible_to<bool>;
           })
bool operator==(const OptUnique<T, D> &opt, const U &val) {
  return opt.has_value() ? (*opt == val) : false;
}

template <typename T, typename D, typename U>
  requires(!detail::is_optional_unnullable_v<U> &&
           !std::is_same_v<std::decay_t<U>, std::nullopt_t> &&
           requires(const std::unique_ptr<T, D> &p, const U &v) { p <=> v; })
auto operator<=>(const OptUnique<T, D> &opt, const U &val)
    -> std::compare_three_way_result_t<std::unique_ptr<T, D>, U> {
  return opt.has_value() ? (*opt <=> val) : std::strong_ordering::less;
}

namespace std {
template <typename T, typename Deleter> struct hash<OptUnique<T, Deleter>> {
  std::size_t operator()(const OptUnique<T, Deleter> &opt) const noexcept {
    return opt.has_value() ? std::hash<std::unique_ptr<T, Deleter>>{}(*opt) : 0;
  }
};
} // namespace std

template <class T> using RefPtr = T *;

template <class T> struct is_unique_ptr_impl : std::false_type {};

template <class T, class Deleter>
struct is_unique_ptr_impl<std::unique_ptr<T, Deleter>> : std::true_type {};

template <class T> using is_unique_ptr = is_unique_ptr_impl<std::decay_t<T>>;

template <class T>
inline constexpr bool is_unique_ptr_v = is_unique_ptr<T>::value;

// Propagate an optional failure, otherwise move its value into VAR.
#define TRY_ASSIGN(VAR, EXPR)                                                  \
  do {                                                                         \
    auto &&__try_result = (EXPR);                                              \
    if (!__try_result.has_value()) {                                           \
      return std::nullopt;                                                     \
    }                                                                          \
    VAR = std::move(*__try_result);                                            \
  } while (0)

#define CCOMPILER_TRY_CONCAT_IMPL(LEFT, RIGHT) LEFT##RIGHT
#define CCOMPILER_TRY_CONCAT(LEFT, RIGHT) CCOMPILER_TRY_CONCAT_IMPL(LEFT, RIGHT)

// Declare VAR from the value of EXPR if present.
#define TRY_ASSIGN_NEW_IMPL(VAR, EXPR, ID)                                     \
  auto &&CCOMPILER_TRY_CONCAT(__try_result_, ID) = (EXPR);                     \
  if (!CCOMPILER_TRY_CONCAT(__try_result_, ID).has_value()) {                  \
    return std::nullopt;                                                       \
  }                                                                            \
  auto VAR = std::move(*CCOMPILER_TRY_CONCAT(__try_result_, ID))

#define TRY_ASSIGN_NEW(VAR, EXPR) TRY_ASSIGN_NEW_IMPL(VAR, EXPR, __COUNTER__)

// Propagate an optional failure while discarding its successful value.
#define TRY(EXPR)                                                              \
  do {                                                                         \
    auto &&__try_result = (EXPR);                                              \
    if (!__try_result.has_value()) {                                           \
      return std::nullopt;                                                     \
    }                                                                          \
  } while (0)

inline std::optional<std::monostate> Succeeded() { return std::monostate{}; }

#define __VA_ARGS_COUNT_N(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12,   \
                          _13, _14, _15, _16, N, ...)                          \
  N

#define __VA_ARGS_COUNT(...)                                                   \
  __VA_ARGS_COUNT_N(__VA_ARGS__ __VA_OPT__(, ) 16, 15, 14, 13, 12, 11, 10, 9,  \
                    8, 7, 6, 5, 4, 3, 2, 1, 0)

#define __FOR_EACH_IMPL_0(action)
#define __FOR_EACH_IMPL_1(action, x) action(x)
#define __FOR_EACH_IMPL_2(action, x, ...)                                      \
  action(x) __FOR_EACH_IMPL_1(action, __VA_ARGS__)
#define __FOR_EACH_IMPL_3(action, x, ...)                                      \
  action(x) __FOR_EACH_IMPL_2(action, __VA_ARGS__)
#define __FOR_EACH_IMPL_4(action, x, ...)                                      \
  action(x) __FOR_EACH_IMPL_3(action, __VA_ARGS__)
#define __FOR_EACH_IMPL_5(action, x, ...)                                      \
  action(x) __FOR_EACH_IMPL_4(action, __VA_ARGS__)
#define __FOR_EACH_IMPL_6(action, x, ...)                                      \
  action(x) __FOR_EACH_IMPL_5(action, __VA_ARGS__)
#define __FOR_EACH_IMPL_7(action, x, ...)                                      \
  action(x) __FOR_EACH_IMPL_6(action, __VA_ARGS__)
#define __FOR_EACH_IMPL_8(action, x, ...)                                      \
  action(x) __FOR_EACH_IMPL_7(action, __VA_ARGS__)
#define __FOR_EACH_IMPL_9(action, x, ...)                                      \
  action(x) __FOR_EACH_IMPL_8(action, __VA_ARGS__)
#define __FOR_EACH_IMPL_10(action, x, ...)                                     \
  action(x) __FOR_EACH_IMPL_9(action, __VA_ARGS__)
#define __FOR_EACH_IMPL_11(action, x, ...)                                     \
  action(x) __FOR_EACH_IMPL_10(action, __VA_ARGS__)
#define __FOR_EACH_IMPL_12(action, x, ...)                                     \
  action(x) __FOR_EACH_IMPL_11(action, __VA_ARGS__)
#define __FOR_EACH_IMPL_13(action, x, ...)                                     \
  action(x) __FOR_EACH_IMPL_12(action, __VA_ARGS__)
#define __FOR_EACH_IMPL_14(action, x, ...)                                     \
  action(x) __FOR_EACH_IMPL_13(action, __VA_ARGS__)
#define __FOR_EACH_IMPL_15(action, x, ...)                                     \
  action(x) __FOR_EACH_IMPL_14(action, __VA_ARGS__)
#define __FOR_EACH_IMPL_16(action, x, ...)                                     \
  action(x) __FOR_EACH_IMPL_15(action, __VA_ARGS__)
#define __FOR_EACH_IMPL_17(action, x, ...)                                     \
  action(x) __FOR_EACH_IMPL_16(action, __VA_ARGS__)
#define __FOR_EACH_IMPL_18(action, x, ...)                                     \
  action(x) __FOR_EACH_IMPL_17(action, __VA_ARGS__)
#define __FOR_EACH_IMPL_19(action, x, ...)                                     \
  action(x) __FOR_EACH_IMPL_18(action, __VA_ARGS__)
#define __FOR_EACH_IMPL_20(action, x, ...)                                     \
  action(x) __FOR_EACH_IMPL_19(action, __VA_ARGS__)

#define __GET_FOR_EACH_IMPL(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11,  \
                            _12, _13, _14, _15, _16, _17, _18, _19, _20, name, \
                            ...)                                               \
  name

#define FOR_EACH(action, ...)                                                  \
  __GET_FOR_EACH_IMPL(                                                         \
      _0 __VA_OPT__(, ) __VA_ARGS__, __FOR_EACH_IMPL_20, __FOR_EACH_IMPL_19,   \
      __FOR_EACH_IMPL_18, __FOR_EACH_IMPL_17, __FOR_EACH_IMPL_16,              \
      __FOR_EACH_IMPL_15, __FOR_EACH_IMPL_14, __FOR_EACH_IMPL_13,              \
      __FOR_EACH_IMPL_12, __FOR_EACH_IMPL_11, __FOR_EACH_IMPL_10,              \
      __FOR_EACH_IMPL_9, __FOR_EACH_IMPL_8, __FOR_EACH_IMPL_7,                 \
      __FOR_EACH_IMPL_6, __FOR_EACH_IMPL_5, __FOR_EACH_IMPL_4,                 \
      __FOR_EACH_IMPL_3, __FOR_EACH_IMPL_2, __FOR_EACH_IMPL_1,                 \
      __FOR_EACH_IMPL_0)(action __VA_OPT__(, ) __VA_ARGS__)
