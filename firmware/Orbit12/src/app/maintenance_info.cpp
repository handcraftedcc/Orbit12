#include "maintenance_info.hpp"

#include <cstring>

namespace orbit {
namespace {

// Small bounded writer: every append either fits completely or marks the writer overflowed.
class Writer {
 public:
  Writer(char* out, std::size_t capacity) noexcept : out_(out), capacity_(capacity) {}
  void text(const char* s) noexcept {
    const std::size_t n = std::strlen(s);
    if (n > capacity_ - length_ || overflow_) { overflow_ = true; return; }
    std::memcpy(out_ + length_, s, n);
    length_ += n;
  }
  void unsigned_value(std::uint32_t value) noexcept {
    char digits[12];
    std::size_t n = 0;
    do {
      digits[n++] = static_cast<char>('0' + value % 10U);
      value /= 10U;
    } while (value != 0);
    while (n != 0) ch(digits[--n]);
  }
  void ch(char c) noexcept {
    if (length_ + 1 > capacity_) { overflow_ = true; return; }
    out_[length_++] = c;
  }
  bool overflowed() const noexcept { return overflow_; }
  std::size_t length() const noexcept { return length_; }

 private:
  char* out_;
  std::size_t capacity_;
  std::size_t length_ = 0;
  bool overflow_ = false;
};

}  // namespace

std::size_t encode_info(const InfoFields& fields, char* out, std::size_t capacity) noexcept {
  if (out == nullptr || capacity == 0) return 0;
  Writer w(out, capacity);
  w.text("INFO fw=");
  w.text(fields.firmware);
  w.text(" sha=");
  w.text(fields.git_sha);
  w.text(" api=");
  w.unsigned_value(fields.engine_api);
  w.text(" board=");
  w.text(fields.board);
  w.ch('\n');

  w.text("MODULES count=");
  w.unsigned_value(static_cast<std::uint32_t>(fields.core_module_count));
  w.text(" core=");
  for (std::size_t i = 0; i < fields.core_module_count; ++i) {
    if (i != 0) w.ch(',');
    w.text(fields.core_modules[i].id);
  }
  w.text(" user=");   // user modules: none yet
  w.ch('\n');

  w.text("STORAGE mounted=");
  w.ch(fields.storage_mounted ? '1' : '0');
  w.text(" scenes=");
  w.unsigned_value(static_cast<std::uint32_t>(fields.scenes));
  w.text(" names_ok=");
  w.ch(fields.names_loaded ? '1' : '0');
  w.ch('\n');

  w.text("STATE maint=");
  w.ch(fields.maintenance ? '1' : '0');
  w.text(" transport=");
  w.ch(fields.transport_running ? '1' : '0');
  w.ch('\n');

  if (w.overflowed()) return 0;
  return w.length();
}

}  // namespace orbit
