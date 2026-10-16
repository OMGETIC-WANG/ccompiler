#pragma once

#include <functional>
#include <memory>
#include <utility>

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

  const T *get() const { return _ptr; }

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

#define __VA_ARGS_COUNT_N(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12,   \
                          _13, _14, _15, _16, N, ...)                          \
  N

#define __VA_ARGS_COUNT(...)                                                   \
  __VA_ARGS_COUNT_N(__VA_ARGS__ __VA_OPT__(, ) 16, 15, 14, 13, 12, 11, 10, 9,  \
                    8, 7, 6, 5, 4, 3, 2, 1, 0)
