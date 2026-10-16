// Module selector footer descriptions (app/module_descriptors.hpp).
// Checks that every registered input and processor module (including Empty) has a
// description, that descriptions fit the 17-cell footer field, use only characters
// the 6px font can draw, are unique, and that the selector labels resolve to the
// same module through ui_model's label table.
#include <cstdio>
#include <cstring>

#include "app/module_descriptors.hpp"
#include "app/ui_model.hpp"
#include "modules/common/input_registry.hpp"
#include "modules/common/registry.hpp"

namespace {
int g_failures = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #x); ++g_failures; } } while (false)

// Renderer font charset (app/renderer.cpp kFontChars, ui.py FONTCHARS). Lowercase and
// other bytes map to the blank tile, so they are not drawable.
const char kDrawable[] =
    " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,!?;:''()[]{}<>+-=*/\\_%&@#$^|~`";

bool drawable(char c) noexcept {
  for (const char* p = kDrawable; *p != '\0'; ++p) {
    if (*p == c) return true;
  }
  return false;
}

bool all_drawable(const char* text) noexcept {
  for (const char* p = text; *p != '\0'; ++p) {
    if (!drawable(*p)) return false;
  }
  return true;
}

bool has_id(const char* id) noexcept {
  for (const orbit::ModuleDescription& entry : orbit::kModuleDescriptions) {
    if (std::strcmp(entry.id, id) == 0) return true;
  }
  return false;
}

}  // namespace

int main() {
  // Every registry entry is covered by exactly one table entry, and the table has no extras.
  std::size_t registered = 0;
  for (std::uint8_t i = 0; i < orbit::module_count(); ++i) {
    const orbit::ModuleDescriptor* module = orbit::module_at(i);
    CHECK(module != nullptr && module->id != nullptr);
    if (module == nullptr || module->id == nullptr) continue;
    ++registered;
    const char* description = orbit::module_descriptor(module->id);
    CHECK(description != nullptr);
    if (description == nullptr) std::fprintf(stderr, "no descriptor for processor '%s'\n", module->id);
  }
  for (std::uint8_t i = 0; i < orbit::input_module_count(); ++i) {
    const orbit::InputDescriptor* input = orbit::input_module_at(i);
    CHECK(input != nullptr && input->id != nullptr);
    if (input == nullptr || input->id == nullptr) continue;
    ++registered;
    const char* description = orbit::module_descriptor(input->id);
    CHECK(description != nullptr);
    if (description == nullptr) std::fprintf(stderr, "no descriptor for input '%s'\n", input->id);
  }
  CHECK(registered == orbit::kModuleDescriptionCount);
  CHECK(orbit::module_count() + orbit::input_module_count() == orbit::kModuleDescriptionCount);
  CHECK(orbit::find_module("empty") != nullptr);  // Empty is part of the processor registry

  // Table entries name registered modules, and every registered module has an entry.
  for (const orbit::ModuleDescription& entry : orbit::kModuleDescriptions) {
    const bool is_processor = orbit::find_module(entry.id) != nullptr;
    const bool is_input = orbit::find_input_module(entry.id) != nullptr;
    CHECK(is_processor != is_input);  // exactly one registry owns each id
  }

  // Length and font drawability.
  for (const orbit::ModuleDescription& entry : orbit::kModuleDescriptions) {
    const std::size_t length = std::strlen(entry.description);
    CHECK(length > 0);
    CHECK(length <= orbit::kModuleDescriptionMaxLength);
    CHECK(all_drawable(entry.description));
    if (length > orbit::kModuleDescriptionMaxLength || !all_drawable(entry.description)) {
      std::fprintf(stderr, "bad description for '%s': '%s'\n", entry.id, entry.description);
    }
  }

  // Uniqueness of descriptions and of ids.
  for (std::size_t i = 0; i < orbit::kModuleDescriptionCount; ++i) {
    for (std::size_t j = i + 1; j < orbit::kModuleDescriptionCount; ++j) {
      const auto& a = orbit::kModuleDescriptions[i];
      const auto& b = orbit::kModuleDescriptions[j];
      CHECK(std::strcmp(a.description, b.description) != 0);
      CHECK(std::strcmp(a.id, b.id) != 0);
    }
  }

  // Selector labels resolve to the same module the id does.
  for (std::uint8_t i = 0; i < orbit::module_count(); ++i) {
    const orbit::ModuleDescriptor* module = orbit::module_at(i);
    if (module == nullptr) continue;
    const char* label = orbit::module_label_for(orbit::kSlotP1, module->id);
    CHECK(has_id(module->id));
    const char* by_label = orbit::module_descriptor(label);
    // Labels shared with an input (CHRD) are ambiguous and must not resolve.
    if (std::strcmp(label, "CHRD") == 0) {
      CHECK(by_label == nullptr);
    } else {
      CHECK(by_label != nullptr);
      CHECK(by_label == orbit::module_descriptor(module->id));
    }
  }
  for (std::uint8_t i = 0; i < orbit::input_module_count(); ++i) {
    const orbit::InputDescriptor* input = orbit::input_module_at(i);
    if (input == nullptr) continue;
    const char* label = orbit::module_label_for(orbit::kSlotIn, input->id);
    const char* by_label = orbit::module_descriptor(label);
    if (std::strcmp(label, "CHRD") == 0) {
      CHECK(by_label == nullptr);
    } else {
      CHECK(by_label == orbit::module_descriptor(input->id));
    }
  }

  // Lookup contract: unknown and null keys return nullptr.
  CHECK(orbit::module_descriptor(nullptr) == nullptr);
  CHECK(orbit::module_descriptor("") == nullptr);
  CHECK(orbit::module_descriptor("phraser") == nullptr);
  CHECK(orbit::module_descriptor("no_such_module") == nullptr);
  CHECK(orbit::module_descriptor("ARP") != nullptr);
  CHECK(std::strcmp(orbit::module_descriptor("arp"), "ARPEGGIATES NOTES") == 0);
  CHECK(orbit::module_descriptor("arp") == orbit::module_descriptor("ARP"));

  if (g_failures == 0) {
    std::printf("t15_module_descriptors: all checks passed (%zu modules)\n", registered);
    return 0;
  }
  std::fprintf(stderr, "t15_module_descriptors: %d failure(s)\n", g_failures);
  return 1;
}
