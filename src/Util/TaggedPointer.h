#pragma once

#include "../Exception.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>

template <size_t size> struct UIntOfSize {
  using type = void;
};

#define DECLARE_UIntOfSize(size, type_)                                        \
  template <> struct UIntOfSize<size> {                                        \
    using type = type_;                                                        \
  }

DECLARE_UIntOfSize(1, uint8_t);
DECLARE_UIntOfSize(2, uint8_t);
DECLARE_UIntOfSize(4, uint8_t);
DECLARE_UIntOfSize(8, uint8_t);
DECLARE_UIntOfSize(16, uint8_t);
#undef DECLARE_UIntOfSize

template <size_t size> using UIntOfSize_t = typename UIntOfSize<size>::type;

template <size_t pointer_align = alignof(void *)> class TaggedVoidPointer {
  static_assert((pointer_align & (pointer_align - 1)) == 0,
                "pointer_align must be a power of two");

public:
  using TagType = UIntOfSize_t<pointer_align>;

  constexpr static uintptr_t pointer_mask =
      ~(static_cast<uintptr_t>(pointer_align) - 1);

  constexpr static uintptr_t tag_mask =
      static_cast<uintptr_t>(pointer_align) - 1;

  constexpr TaggedVoidPointer() noexcept = default;

  constexpr TaggedVoidPointer(std::nullptr_t) noexcept : _ptr(0) {}

  TaggedVoidPointer(void *ptr, TagType tag = 0) {
    uintptr_t p = reinterpret_cast<uintptr_t>(ptr);
    ASSERT((p & tag_mask) == 0, "Pointer is not aligned");
    ASSERT((static_cast<uintptr_t>(tag) & pointer_mask) == 0,
           "Tag out of range");
    ASSERT(p != 0 || tag == 0, "Null pointer cannot carry a tag");
    _ptr = p | (static_cast<uintptr_t>(tag) & tag_mask);
  }

  void *Get() const noexcept {
    return reinterpret_cast<void *>(_ptr & pointer_mask);
  }

  TagType GetTag() const noexcept {
    return static_cast<TagType>(_ptr & tag_mask);
  }

  void SetTag(TagType tag) noexcept {
    ASSERT((static_cast<uintptr_t>(tag) & pointer_mask) == 0,
           "Tag out of range");
    ASSERT(_ptr != 0 || tag == 0, "Null pointer cannot carry a tag");
    _ptr = (_ptr & pointer_mask) | (static_cast<uintptr_t>(tag) & tag_mask);
  }

  void Set(void *ptr) {
    uintptr_t p = reinterpret_cast<uintptr_t>(ptr);
    ASSERT((p & tag_mask) == 0, "Pointer is not aligned");
    ASSERT(p != 0 || (_ptr & tag_mask) == 0, "Null pointer cannot carry a tag");
    _ptr = p | (_ptr & tag_mask);
  }

  void Reset(void *ptr = nullptr, TagType tag = 0) {
    uintptr_t p = reinterpret_cast<uintptr_t>(ptr);
    ASSERT((p & tag_mask) == 0, "Pointer is not aligned");
    ASSERT((static_cast<uintptr_t>(tag) & pointer_mask) == 0,
           "Tag out of range");
    ASSERT(p != 0 || tag == 0, "Null pointer cannot carry a tag");
    _ptr = p | (static_cast<uintptr_t>(tag) & tag_mask);
  }

  uintptr_t GetRaw() const noexcept { return _ptr; }

protected:
  uintptr_t _ptr = 0;
};

template <typename T, size_t pointer_align = alignof(T)>
class TaggedPointer : private TaggedVoidPointer<pointer_align> {
  using Base = TaggedVoidPointer<pointer_align>;

  static_assert(
      std::is_void_v<T> || alignof(T) >= pointer_align,
      "pointer_align exceeds alignment of T; pointer may be misaligned");

public:
  using element_type = T;
  using pointer = T *;
  using TagType = typename Base::TagType;

  constexpr static size_t alignment = pointer_align;
  constexpr static uintptr_t pointer_mask = Base::pointer_mask;
  constexpr static uintptr_t tag_mask = Base::tag_mask;

  constexpr TaggedPointer() noexcept = default;
  constexpr TaggedPointer(std::nullptr_t) noexcept : Base(nullptr) {}

  TaggedPointer(pointer ptr, TagType tag = 0)
      : Base(const_cast<std::remove_cv_t<T> *>(ptr), tag) {}

  pointer Get() const noexcept { return static_cast<pointer>(Base::Get()); }

  template <typename U = T, typename = std::enable_if_t<!std::is_void_v<U>>>
  U &operator*() const noexcept {
    ASSERT(Get() != nullptr, "Dereferencing null TaggedPointer");
    return *Get();
  }

  pointer operator->() const noexcept {
    ASSERT(Get() != nullptr, "Accessing member of null TaggedPointer");
    return Get();
  }

  TagType GetTag() const noexcept { return Base::GetTag(); }
  void SetTag(TagType tag) noexcept { Base::SetTag(tag); }

  void Set(pointer ptr) { Base::Set(const_cast<std::remove_cv_t<T> *>(ptr)); }

  void reset(pointer ptr = nullptr, TagType tag = 0) {
    Base::Reset(const_cast<std::remove_cv_t<T> *>(ptr), tag);
  }

  explicit operator bool() const noexcept { return Get() != nullptr; }

  uintptr_t GetRaw() const noexcept { return Base::GetRaw(); }

  friend bool operator==(const TaggedPointer &lhs,
                         const TaggedPointer &rhs) noexcept {
    return lhs.GetRaw() == rhs.GetRaw();
  }

  friend bool operator!=(const TaggedPointer &lhs,
                         const TaggedPointer &rhs) noexcept {
    return !(lhs == rhs);
  }

  friend bool operator==(const TaggedPointer &lhs, std::nullptr_t) noexcept {
    return lhs.Get() == nullptr;
  }

  friend bool operator==(std::nullptr_t, const TaggedPointer &rhs) noexcept {
    return rhs.Get() == nullptr;
  }

  friend bool operator!=(const TaggedPointer &lhs, std::nullptr_t) noexcept {
    return !(lhs == nullptr);
  }

  friend bool operator!=(std::nullptr_t, const TaggedPointer &rhs) noexcept {
    return !(rhs == nullptr);
  }

  friend bool operator==(const TaggedPointer &lhs, const pointer rhs) noexcept {
    return lhs.Get() == rhs;
  }

  friend bool operator==(const pointer lhs, const TaggedPointer &rhs) noexcept {
    return lhs == rhs.Get();
  }

  friend bool operator!=(const TaggedPointer &lhs, const pointer rhs) noexcept {
    return lhs.Get() != rhs;
  }

  friend bool operator!=(const pointer lhs, const TaggedPointer &rhs) noexcept {
    return lhs != rhs.Get();
  }
};

namespace std {

template <typename T, size_t pointer_align>
struct hash<TaggedPointer<T, pointer_align>> {
  size_t operator()(const TaggedPointer<T, pointer_align> &ptr) const noexcept {
    return std::hash<uintptr_t>{}(ptr.GetRaw());
  }
};

} // namespace std
