#pragma once

#include "Util/EnumRefl.h"
#include <format>
#include <stdexcept>

// class Exception : public std::runtime_error {
// public:
//   Exception(const std::string &message)
//       : std::runtime_error(message), _stack_trace(std::stacktrace::current())
//       {}

//   Exception(const char *message)
//       : std::runtime_error(message), _stack_trace(std::stacktrace::current())
//       {}

//   std::string Format() const {
//     return std::format("{}\n---\n{}\n", what(), _stack_trace);
//   }

// private:
//   std::stacktrace _stack_trace;
// };

class Exception : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

template <> struct std::formatter<Exception> : std::formatter<char> {
  static auto format(const Exception &e, std::format_context &ctx) {
    return std::format_to(ctx.out(), "{}", e.what());
  }
};

template <typename T> void _AppendChunk(std::string &msg, const T &chunk) {
  if constexpr (std::convertible_to<T, std::string_view>) {
    msg += std::string_view(chunk);
  } else if constexpr (EnumRefl<T>::registered) {
    msg += EnumRefl<T>::GetName(chunk);
  } else {
    static_assert(sizeof(T) == 0, "Unsupported chunk type for Assert message");
  }
}

template <class... MsgChunk>
std::string _BuildMessage(const MsgChunk &...msg_chunks) {
  std::string msg;
  (_AppendChunk(msg, msg_chunks), ...);
  return msg;
}

template <class... MsgChunk>
void _Assert(bool cond, const MsgChunk &...msg_chunks) {
  if (!cond) {
    throw Exception(_BuildMessage(msg_chunks...));
  }
}

template <class... MsgChunk>
[[noreturn]] void _RaiseError(const MsgChunk &...msg_chunks) {
  throw Exception(_BuildMessage(msg_chunks...));
}

#define __STRINGIFY_IMPL(x) #x
#define __STRINGIFY(x) __STRINGIFY_IMPL(x)
#define __LINE_STR __STRINGIFY(__LINE__)

#define ASSERT(cond, ...)                                                      \
  _Assert(cond, #cond, " ", __FILE__, ":", __LINE_STR,                         \
          " failed: " __VA_OPT__(, ) __VA_ARGS__)

#define __ASSERT_COND(cond, a, b, ...)                                         \
  do {                                                                         \
    auto a_val = (a);                                                          \
    auto b_val = (b);                                                          \
    _Assert(a_val cond b_val, #a, " " #cond " ", #b, " ", __FILE__, ":",       \
            __LINE_STR, " failed: Expected ", b_val, " got ", a_val,           \
            " " __VA_OPT__(, ) __VA_ARGS__);                                   \
  } while (0)

#define ASSERT_EQ(a, b, ...) __ASSERT_COND(==, a, b, __VA_ARGS__)
#define ASSERT_NE(a, b, ...) __ASSERT_COND(!=, a, b, __VA_ARGS__)
#define ASSERT_LT(a, b, ...) __ASSERT_COND(<, a, b, __VA_ARGS__)
#define ASSERT_LE(a, b, ...) __ASSERT_COND(<=, a, b, __VA_ARGS__)
#define ASSERT_GT(a, b, ...) __ASSERT_COND(>, a, b, __VA_ARGS__)
#define ASSERT_GE(a, b, ...) __ASSERT_COND(>=, a, b, __VA_ARGS__)

#define RAISE_ERROR(...)                                                       \
  _RaiseError(__FILE__, ":", __LINE_STR, " failed: ", __VA_ARGS__)
