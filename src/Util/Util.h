#pragma once

#include <functional>
#include <memory>
#include <optional>
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
