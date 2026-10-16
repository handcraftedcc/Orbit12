#include "app/controller.hpp"

#include "domain/time.hpp"

#include <cmath>
#include <cstring>
#include <limits>

namespace orbit {
namespace {

constexpr std::int32_t kI32Min = (std::numeric_limits<std::int32_t>::min)();
constexpr std::int32_t kI32Max = (std::numeric_limits<std::int32_t>::max)();

int floor_mod(int a, int m) noexcept {
  int r = a % m;
  return r < 0 ? r + m : r;
}
bool same(const char* a, const char* b) noexcept {
  return a != nullptr && b != nullptr && std::strcmp(a, b) == 0;
}
void copy_text(char* out, std::size_t capacity, const char* text) noexcept {
  std::size_t n = std::strlen(text);
  if (n >= capacity) n = capacity - 1;
  std::memcpy(out, text, n);
  out[n] = '\0';
}
void swap_bytes(void* a, void* b, std::size_t n) noexcept {
  auto* pa = static_cast<unsigned char*>(a);
  auto* pb = static_cast<unsigned char*>(b);
  for (std::size_t i = 0; i < n; ++i) { const unsigned char t = pa[i]; pa[i] = pb[i]; pb[i] = t; }
}
std::int32_t clamp_i64(std::int64_t v) noexcept {
  if (v < kI32Min) return kI32Min;
  if (v > kI32Max) return kI32Max;
  return static_cast<std::int32_t>(v);
}

// Parm types that ParamSpec cannot express (see parms.py RateParmType /
// PatternParmType / PercentParmType; names are the C++ ParamSpec names).
bool is_rate_name(const char* n) noexcept { return same(n, "rate") || same(n, "interval_rate"); }
bool is_pattern_name(const char* n) noexcept {
  return same(n, "pattern") || same(n, "rhythm_pattern") || same(n, "rhythm");
}
bool is_percent_name(const char* n) noexcept {
  return same(n, "interval_mod") || same(n, "velocity_mod") || same(n, "gate_mod") ||
         same(n, "chop") || same(n, "note_chance") || same(n, "octave_chance") || same(n, "tilt");
}

// Saved-attribute schemas (source save_attrs), verified against the live
// attribute visitors in the T11b tests.
constexpr SceneAttributeSpec kMatrixAttrs[] = {
    {"notes", AttributeType::Integer, 12}, {"chords", AttributeType::Byte, 12},
    {"sevenths", AttributeType::Byte, 12}, {"ninths", AttributeType::Byte, 12},
    {"inversions", AttributeType::Byte, 12}, {"sus", AttributeType::Byte, 12},
    {"power", AttributeType::Byte, 12}, {"dim", AttributeType::Byte, 12}};
constexpr SceneAttributeSpec kChordWalkAttrs[] = {
    {"notes", AttributeType::Integer, 4}, {"sevenths", AttributeType::Byte, 4},
    {"sus", AttributeType::Byte, 4}, {"power", AttributeType::Byte, 4}};
constexpr SceneAttributeSpec kEuclidAttrs[] = {
    {"pulses", AttributeType::Byte, 6}, {"lengths", AttributeType::Byte, 6},
    {"rotations", AttributeType::Integer, 6}};
constexpr SceneAttributeSpec kArpWalkAttrs[] = {{"step_values", AttributeType::Integer, 8}};

struct AttrTable { const SceneAttributeSpec* specs; std::uint8_t count; };
AttrTable attrs_for(const char* id) noexcept {
  if (same(id, "matrix")) return {kMatrixAttrs, 8};
  if (same(id, "chord_walk")) return {kChordWalkAttrs, 4};
  if (same(id, "euclid")) return {kEuclidAttrs, 3};
  if (same(id, "arp_walk")) return {kArpWalkAttrs, 1};
  return {nullptr, 0};
}

ParamValue make_value(const ParamRow& r) noexcept {
  switch (r.spec_type) {
    case ParamType::Integer: return ParamValue::integer(r.value_i);
    case ParamType::Enumeration: return ParamValue(ParamType::Enumeration, ParamValue::Scalar(r.value_i));
    case ParamType::Boolean: return ParamValue::boolean(r.value_i != 0);
    case ParamType::Decimal: return ParamValue::decimal(r.value_f);
    case ParamType::Action: return ParamValue(ParamType::Action, ParamValue::Scalar(static_cast<std::int32_t>(0)));
    case ParamType::Text: break;
  }
  return ParamValue::integer(r.value_i);
}

ParamRow* row_by_name(SlotRecord& rec, const char* name) noexcept {
  for (std::uint8_t i = 0; i < rec.row_count; ++i)
    if (same(rec.rows[i].name, name)) return &rec.rows[i];
  return nullptr;
}

struct AttrCollect {
  SceneModule* module = nullptr;
  bool ok = true;
};
bool collect_attribute(void* ctx, AttributeDescriptor& d) noexcept {
  auto& c = *static_cast<AttrCollect*>(ctx);
  auto& m = *c.module;
  if (m.attribute_count >= kSceneMaxAttrsPerModule || d.element_count > kSceneMaxAttrValues ||
      d.name == nullptr || std::strlen(d.name) > kSceneMaxAttrNameChars || d.values == nullptr) {
    c.ok = false;
    return false;
  }
  SceneAttribute& a = m.attributes[m.attribute_count++];
  std::memset(a.name, 0, sizeof(a.name));
  copy_text(a.name, sizeof(a.name), d.name);
  a.type = d.type;
  a.count = d.element_count;
  for (std::uint16_t i = 0; i < d.element_count; ++i) {
    switch (d.type) {
      case AttributeType::Integer: a.values[i].integer = static_cast<const std::int32_t*>(d.values)[i]; break;
      case AttributeType::Byte: a.values[i].integer = static_cast<const std::uint8_t*>(d.values)[i]; break;
      case AttributeType::Decimal: a.values[i].decimal = static_cast<const float*>(d.values)[i]; break;
      case AttributeType::Boolean: a.values[i].boolean = static_cast<const bool*>(d.values)[i]; break;
    }
  }
  return true;
}

struct AttrRestore {
  const SceneAttribute* attr = nullptr;
  bool found = false;
  bool ok = false;
};
bool restore_attribute(void* ctx, AttributeDescriptor& d) noexcept {
  auto& r = *static_cast<AttrRestore*>(ctx);
  if (d.name == nullptr || !same(d.name, r.attr->name)) return true;
  r.found = true;
  if (d.type != r.attr->type || d.element_count != r.attr->count || d.values == nullptr) return false;
  for (std::uint16_t i = 0; i < d.element_count; ++i) {
    switch (d.type) {
      case AttributeType::Integer: static_cast<std::int32_t*>(d.values)[i] = r.attr->values[i].integer; break;
      case AttributeType::Byte: static_cast<std::uint8_t*>(d.values)[i] = static_cast<std::uint8_t>(r.attr->values[i].integer); break;
      case AttributeType::Decimal: static_cast<float*>(d.values)[i] = r.attr->values[i].decimal; break;
      case AttributeType::Boolean: static_cast<bool*>(d.values)[i] = r.attr->values[i].boolean; break;
    }
  }
  r.ok = true;
  return false;
}

}  // namespace

// ---------------------------------------------------------------------------
// Scene lifecycle bridge: static trampolines into private members.
// ---------------------------------------------------------------------------
struct SceneBridge {
  static Controller& c(void* ctx) noexcept { return *static_cast<Controller*>(ctx); }
  static bool is_stopped(void* x) noexcept { return c(x).scene_is_stopped(); }
  static void stop_release_drain(void* x) noexcept { c(x).scene_stop_release_drain(); }
  static std::uint32_t request_quiesce(void* x, std::uint32_t now, std::uint32_t timeout) noexcept {
    auto& s = c(x);
    if (s.maintenance_ != nullptr) return s.maintenance_->request(now, timeout);
    if (++s.local_quiesce_seq_ == 0) ++s.local_quiesce_seq_;
    return s.local_quiesce_seq_;
  }
  static bool quiesced(void* x, std::uint32_t seq) noexcept {
    auto& s = c(x);
    const bool done = s.maintenance_ != nullptr ? s.maintenance_->acknowledged(seq) : true;
    // Reads also rename/remove .tmp and .bak files during recovery, so the window opens
    // for every quiesced scene operation, not only writes.
    if (done) s.set_storage_window(true);
    return done;
  }
  static bool timed_out(void* x, std::uint32_t, std::uint32_t now) noexcept {
    auto& s = c(x);
    return s.maintenance_ != nullptr && s.maintenance_->timed_out(now);
  }
  static void cancel(void* x, std::uint32_t) noexcept {
    auto& s = c(x);
    s.set_storage_window(false);
    if (s.maintenance_ != nullptr) s.maintenance_->cancel();
  }
  static void resume(void* x, std::uint32_t seq) noexcept {
    auto& s = c(x);
    s.set_storage_window(false);
    if (s.maintenance_ != nullptr) s.maintenance_->resume(seq);
  }
  static bool capture(void* x, SceneDto& d) noexcept { return c(x).scene_capture(d); }
  static bool reset_defaults(void* x) noexcept { return c(x).scene_reset_defaults(); }
  static bool prepare(void* x) noexcept { return c(x).scene_prepare(); }
  static bool apply_globals(void* x, const GlobalState& g, std::uint8_t f) noexcept {
    return c(x).scene_apply_globals(g, f);
  }
  static bool construct(void* x, std::uint8_t slot, const char* id, std::uint16_t, bool unknown) noexcept {
    return c(x).scene_construct_module(slot, id, unknown);
  }
  static bool apply_param(void* x, std::uint8_t slot, const SceneParameter& p) noexcept {
    return c(x).scene_apply_parameter(slot, p);
  }
  static bool apply_attr(void* x, std::uint8_t slot, const SceneAttribute& a) noexcept {
    return c(x).scene_apply_attribute(slot, a);
  }
  static bool finish(void* x) noexcept { return c(x).scene_finish(); }
  static void rebuild(void* x) noexcept { c(x).scene_rebuild_ui(); }
  static bool resolve(void* x, std::uint8_t slot, const char* id, SceneModuleSchema& out) noexcept {
    return c(x).scene_resolve(slot, id, out);
  }
  static SceneLifecyclePort lifecycle(Controller* self) noexcept {
    SceneLifecyclePort p{};
    p.context = self;
    p.is_stopped = &is_stopped;
    p.stop_release_drain = &stop_release_drain;
    p.request_quiesce = &request_quiesce;
    p.quiesced = &quiesced;
    p.quiesce_timed_out = &timed_out;
    p.cancel_quiesce = &cancel;
    p.resume = &resume;
    p.capture = &capture;
    p.reset_defaults = &reset_defaults;
    p.prepare_scene = &prepare;
    p.apply_globals = &apply_globals;
    p.construct_module = &construct;
    p.apply_parameter = &apply_param;
    p.apply_attribute = &apply_attr;
    p.finish_scene = &finish;
    p.rebuild_ui = &rebuild;
    return p;
  }
  static SceneSchemaResolver schema(Controller* self) noexcept {
    SceneSchemaResolver r{};
    r.context = self;
    r.lookup = &resolve;
    return r;
  }
};

Controller::Controller(const ControllerPorts& ports) noexcept
    : transport_(global_),
      random_(ports.random),
      maintenance_(ports.maintenance),
      window_(ports.window),
      storage_(ports.storage),
      scene_(ports.storage, SceneBridge::lifecycle(this), SceneBridge::schema(this)) {
  if (random_.next == nullptr) {
    random_.ctx = this;
    random_.next = &Controller::random_thunk;
  }
  paging_.parm_count = 4;  // ParameterSection.parm_count initial value
  const RgbColor base = kKeyColorBase;
  for (auto& c : pixel_colors_) c = base;
  for (auto& c : pixel_temp_) c = base;
}

std::uint32_t Controller::random_thunk(void* ctx) noexcept {
  return static_cast<Controller*>(ctx)->next_random();
}
std::uint32_t Controller::next_random() noexcept {
  std::uint32_t x = rng_state_;
  x ^= x << 13; x ^= x >> 17; x ^= x << 5;
  rng_state_ = x;
  return x;
}

// ---------------------------------------------------------------------------
// Construction / slots
// ---------------------------------------------------------------------------
void Controller::reset_globals() noexcept {
  global_ = GlobalState{};  // State.reset_to_defaults
  tr_bpm_ = global_.bpm;
  tr_swing_ = global_.swing;
}

void Controller::init(TimeSample now) noexcept {
  now_ = now;
  reset_globals();
  (void)transport_.set_bpm(global_.bpm);
  (void)transport_.set_swing(global_.swing);
  transport_.set_mode(global_.transport_mode, now.us);
  brightness_ = 1.0f;
  install_stock_chain();
  build_system_rows();
  boot_ui_state();
  for (auto& c : pixel_colors_) c = kKeyColorBase;
  paint_pixels();
}

void Controller::install_stock_chain() noexcept {
  (void)install_input("note");
  for (std::uint8_t slot = kSlotP1; slot <= kSlotP6; ++slot) (void)install_processor(slot, "empty");
}

void Controller::build_system_rows() noexcept {
  // SET (Set.create_main_parms): rows read state/transport values at build time.
  SlotRecord& set = slots_[kSlotSet];
  set.id = "set";
  set.label = "SET";
  set.row_count = 5;
  ParamRow* r = set.rows;
  r[0] = ParamRow{}; r[0].label = "KEY"; r[0].name = "key"; r[0].kind = RowKind::Note;
  r[0].special = ParamSpecial::SetKey; r[0].value_i = global_.key; r[0].min_i = 0; r[0].max_i = 11;
  r[1] = ParamRow{}; r[1].label = "SCL"; r[1].name = "scale"; r[1].kind = RowKind::Enum;
  r[1].spec_type = ParamType::Enumeration; r[1].special = ParamSpecial::SetScale;
  r[1].options = music::kScaleNames.data(); r[1].option_count = static_cast<std::uint8_t>(music::kScaleNames.size());
  r[1].value_i = global_.scale; r[1].min_i = 0; r[1].max_i = 20;
  r[2] = ParamRow{}; r[2].label = "BPM"; r[2].name = "bpm"; r[2].kind = RowKind::Integer;
  r[2].special = ParamSpecial::SetBpm; r[2].bounded = true; r[2].min_i = 1; r[2].max_i = 300;
  r[2].value_i = tr_bpm_;
  r[3] = ParamRow{}; r[3].label = "SWNG"; r[3].name = "swing"; r[3].kind = RowKind::Percent;
  r[3].spec_type = ParamType::Decimal; r[3].special = ParamSpecial::SetSwing; r[3].bounded = true;
  r[3].min_f = 0.0f; r[3].max_f = 1.0f; r[3].step_f = 0.05f; r[3].value_f = tr_swing_;
  r[4] = ParamRow{}; r[4].label = "TRMDE"; r[4].name = "transport_mode"; r[4].kind = RowKind::Enum;
  r[4].spec_type = ParamType::Enumeration; r[4].special = ParamSpecial::SetTransportMode;
  r[4].options = kTransportModeOptions; r[4].option_count = 2; r[4].min_i = 0; r[4].max_i = 1;
  r[4].value_i = static_cast<std::int32_t>(global_.transport_mode);

  // OUT (Output.create_main_parms)
  SlotRecord& out = slots_[kSlotOut];
  out.id = "output";
  out.label = "OUT";
  out.row_count = 2;
  out.rows[0] = ParamRow{}; out.rows[0].label = "OUTCH"; out.rows[0].name = "out_ch";
  out.rows[0].kind = RowKind::Integer; out.rows[0].special = ParamSpecial::OutChannel;
  out.rows[0].bounded = true; out.rows[0].min_i = 1; out.rows[0].max_i = 16;
  out.rows[0].value_i = static_cast<std::int32_t>(out.out_channel) + 1;
  out.rows[1] = ParamRow{}; out.rows[1].label = "PRNT"; out.rows[1].name = "print";
  out.rows[1].kind = RowKind::Boolean; out.rows[1].spec_type = ParamType::Boolean;
  out.rows[1].special = ParamSpecial::OutPrint; out.rows[1].min_i = 0; out.rows[1].max_i = 1;
  out.rows[1].value_i = 0;

  build_settings_rows(slots_[kSlotSettings]);
}

void Controller::build_settings_rows(SlotRecord& rec) const noexcept {
  rec.id = "settings";
  rec.label = "STTNGS";
  rec.row_count = 5;  // RAM (no-op print button) removed: user-requested, see known-quirks.md
  ParamRow* r = rec.rows;
  r[0] = ParamRow{}; r[0].label = "SCENE"; r[0].kind = RowKind::Enum; r[0].spec_type = ParamType::Enumeration;
  r[0].special = ParamSpecial::SettingsScene; r[0].options = kSceneOptions; r[0].option_count = 10;
  r[0].max_i = 9; r[0].value_i = scene_.active_scene();
  r[1] = ParamRow{}; r[1].label = "SAVSCN"; r[1].kind = RowKind::Button; r[1].spec_type = ParamType::Action;
  r[1].special = ParamSpecial::SettingsSave;
  r[2] = ParamRow{}; r[2].label = "CPYSCN"; r[2].kind = RowKind::Enum; r[2].spec_type = ParamType::Enumeration;
  r[2].special = ParamSpecial::SettingsCopy; r[2].options = kCopySceneOptions; r[2].option_count = 11; r[2].max_i = 10;
  r[3] = ParamRow{}; r[3].label = "RSTSCN"; r[3].kind = RowKind::Enum; r[3].spec_type = ParamType::Enumeration;
  r[3].special = ParamSpecial::SettingsReset; r[3].options = kResetOptions; r[3].option_count = 2; r[3].max_i = 1;
  r[4] = ParamRow{}; r[4].label = "KEYBRI"; r[4].kind = RowKind::Decimal; r[4].spec_type = ParamType::Decimal;
  r[4].special = ParamSpecial::SettingsBrightness; r[4].bounded = true; r[4].min_f = 0.0f; r[4].max_f = 1.0f;
  r[4].step_f = 0.05f; r[4].value_f = brightness_;
}

void Controller::init_row_from_spec(ParamRow& row, const ParamSpec& spec, std::uint8_t index,
                                    const char* module_id, const GlobalState& g) noexcept {
  row = ParamRow{};
  row.label = spec.display_label != nullptr ? spec.display_label : spec.name;
  row.index = index;
  row.spec_type = spec.type;
  row.binding = spec.global_binding;
  row.options = spec.options;
  row.option_count = spec.option_count;
  const bool persisted = (spec.flags & ParamPersist) != 0 && (spec.flags & ParamUiOnly) == 0 &&
                         spec.type != ParamType::Action && spec.type != ParamType::Text;
  row.name = persisted ? spec.name : nullptr;
  switch (spec.type) {
    case ParamType::Integer:
      row.kind = is_rate_name(spec.name) ? RowKind::Rate
                 : (is_pattern_name(spec.name) ? RowKind::Pattern : RowKind::Integer);
      row.value_i = spec.default_value.value.integer;
      row.min_i = spec.minimum.value.integer;
      row.max_i = spec.maximum.value.integer;
      row.step_i = spec.step.value.integer < 1 ? 1 : spec.step.value.integer;
      row.bounded = !(row.min_i == kI32Min && row.max_i == kI32Max);
      if (spec.global_binding == GlobalBinding::Octave) row.value_i = g.octave;
      else if (spec.global_binding == GlobalBinding::KeyOffset) row.value_i = g.key_offset;
      break;
    case ParamType::Enumeration:
      row.kind = RowKind::Enum;
      row.value_i = spec.default_value.value.integer;
      row.min_i = spec.minimum.value.integer;
      row.max_i = spec.maximum.value.integer;
      if (row.option_count == 0) row.option_count = static_cast<std::uint8_t>(row.max_i - row.min_i + 1);
      break;
    case ParamType::Boolean:
      row.kind = RowKind::Boolean;
      row.value_i = spec.default_value.value.boolean ? 1 : 0;
      row.min_i = 0; row.max_i = 1;
      break;
    case ParamType::Decimal:
      row.kind = is_percent_name(spec.name) ? RowKind::Percent : RowKind::Decimal;
      row.value_f = spec.default_value.value.decimal;
      row.min_f = spec.minimum.value.decimal;
      row.max_f = spec.maximum.value.decimal;
      row.step_f = spec.step.value.decimal;
      row.bounded = true;
      break;
    case ParamType::Action:
    case ParamType::Text:
      row.kind = RowKind::Button;
      row.spec_type = ParamType::Action;
      break;
  }
  if (same(module_id, "matrix") && same(spec.name, "reset")) row.special = ParamSpecial::MatrixReset;
  if (same(module_id, "empty")) row.special = ParamSpecial::EmptyPick;
}

void Controller::build_module_rows(std::uint8_t slot, const ParamSpec* params, std::uint8_t count,
                                   bool routing, bool input_slot) noexcept {
  SlotRecord& rec = slots_[slot];
  rec.row_count = 0;
  for (std::uint8_t i = 0; i < count && rec.row_count < kMaxParamRows; ++i)
    init_row_from_spec(rec.rows[rec.row_count++], params[i], i, rec.id, global_);
  if (!routing) return;
  auto add = [&](const char* label, const char* name, ParamSpecial special, RowKind kind,
                 ParamType type, const char* const* options, std::uint8_t option_count,
                 std::int32_t value, std::int32_t lo, std::int32_t hi, bool bounded) {
    if (rec.row_count >= kMaxParamRows) return;
    ParamRow& r = rec.rows[rec.row_count++];
    r = ParamRow{};
    r.label = label; r.name = name; r.special = special; r.kind = kind; r.spec_type = type;
    r.options = options; r.option_count = option_count; r.value_i = value; r.min_i = lo; r.max_i = hi;
    r.bounded = bounded;
  };
  if (!input_slot)
    add("SRC", "source_mode", ParamSpecial::RouteSource, RowKind::Enum, ParamType::Enumeration,
        kSourceOptions, 7, 0, 0, 6, false);
  if (input_slot)
    add("OP", "operation_mode", ParamSpecial::RouteOperation, RowKind::Enum, ParamType::Enumeration,
        kInputOperationOptions, 3, 0, 0, 2, false);
  else
    add("OP", "operation_mode", ParamSpecial::RouteOperation, RowKind::Enum, ParamType::Enumeration,
        kOperationOptions, 5, 0, 0, 4, false);
  add("OUTCH", "out_channel", ParamSpecial::RouteChannel, RowKind::Integer, ParamType::Integer,
      nullptr, 0, 1, 1, 16, true);
}

bool Controller::install_input(const char* id) noexcept {
  const InputDescriptor* desc = find_input_module(id);
  if (desc == nullptr) { desc = find_input_module("note"); id = "note"; }
  if (desc == nullptr) return false;
  if (input_desc_ != nullptr && input_desc_->destroy != nullptr) input_desc_->destroy(input_state_);
  input_desc_ = desc;
  desc->construct(input_state_);
  if (same(id, "chord_walk")) {
    std::int32_t request = 0;  // ChordWalk.__init__ sets the global key_offset to -2
    if (chord_walk_take_key_offset_request(input_state_, request)) global_.key_offset = request;
  }
  SlotRecord& rec = slots_[kSlotIn];
  rec.id = desc->id;
  rec.label = module_label_for(kSlotIn, desc->id);
  rec.source_mode = 0; rec.operation_mode = 0; rec.out_channel = 0;
  build_module_rows(kSlotIn, desc->params, desc->param_count, true, true);
  return true;
}

bool Controller::install_processor(std::uint8_t slot, const char* id) noexcept {
  if (slot < kSlotP1 || slot > kSlotP6) return false;
  const ModuleDescriptor* desc = find_module(id);
  if (desc == nullptr) { desc = find_module("empty"); id = "empty"; }
  if (desc == nullptr) return false;
  const std::uint8_t index = static_cast<std::uint8_t>(slot - kSlotP1);
  if (!chain_.set_module(index, desc)) return false;
  SlotRecord& rec = slots_[slot];
  rec.id = desc->id;
  rec.label = module_label_for(slot, desc->id);
  rec.source_mode = 0; rec.operation_mode = 0; rec.out_channel = 0;
  build_module_rows(slot, desc->params, desc->param_count, !same(desc->id, "empty"), false);
  configure_chain(slot);
  seed_running_flags(slot);
  return true;
}

void Controller::seed_running_flags(std::uint8_t slot) noexcept {
  if (!transport_.running()) return;
  ModuleState* state = proc_state(slot);
  const char* id = slots_[slot].id;
  if (state == nullptr) return;
  // Source __init__: was_transport_running = transport.running (decision W2).
  if (same(id, "wander")) state->storage.wander.was_running = true;
  else if (same(id, "arp_walk")) state->storage.arp_walk.was_running = true;
  else if (same(id, "arp")) state->storage.arp.was_running = true;
  else if (same(id, "euclid")) state->storage.euclid.was_running = true;
}

void Controller::configure_chain(std::uint8_t slot) noexcept {
  if (slot < kSlotP1 || slot > kSlotP6) return;
  const SlotRecord& rec = slots_[slot];
  // Module.process_super: a source that is not earlier in the chain is ignored
  // (the slot then reads the previous stream); the chain rejects such taps.
  std::uint8_t source = rec.source_mode;
  if (source > 1 && source >= slot) source = 0;
  std::uint8_t operation = rec.operation_mode;
  if (operation > 4) operation = 0;
  (void)chain_.configure(static_cast<std::uint8_t>(slot - kSlotP1), static_cast<ChainSource>(source),
                         static_cast<ChainOperation>(operation), rec.out_channel);
}

const ParamRow* Controller::find_row(std::uint8_t slot_index, const char* name) const noexcept {
  if (slot_index >= kSlotCount) return nullptr;
  const SlotRecord& rec = slots_[slot_index];
  for (std::uint8_t i = 0; i < rec.row_count; ++i)
    if (same(rec.rows[i].name, name) || same(rec.rows[i].label, name)) return &rec.rows[i];
  return nullptr;
}

// ---------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------
ParamRow* Controller::active_row() noexcept {
  SlotRecord& rec = slots_[active_chain_];
  if (active_parm_ < 0 || active_parm_ >= rec.row_count) return nullptr;
  return &rec.rows[active_parm_];
}

void Controller::edit_row(ParamRow& row, int delta) noexcept {
  row.armed = false;
  switch (row.kind) {
    case RowKind::Integer: {
      std::int64_t v = static_cast<std::int64_t>(row.value_i) + static_cast<std::int64_t>(delta) * row.step_i;
      if (row.bounded) {
        if (v < row.min_i) v = row.min_i;
        if (v > row.max_i) v = row.max_i;
      }
      row.value_i = clamp_i64(v);
      break;
    }
    case RowKind::Decimal:
    case RowKind::Percent: {
      double x = static_cast<double>(row.value_f) + static_cast<double>(delta) * static_cast<double>(row.step_f);
      if (row.bounded) {
        if (x < static_cast<double>(row.min_f)) x = static_cast<double>(row.min_f);
        if (x > static_cast<double>(row.max_f)) x = static_cast<double>(row.max_f);
      }
      if (x != 0.0) x = std::nearbyint(x * 100.0) / 100.0;  // round(value, 2)
      row.value_f = static_cast<float>(x);
      if (x != std::floor(x) || row.step_f != std::floor(row.step_f)) row.float_text = true;
      break;
    }
    case RowKind::Boolean:
      row.value_i = floor_mod(row.value_i + delta, 2);
      break;
    case RowKind::Enum: {
      const int count = row.option_count != 0 ? row.option_count : 1;
      row.value_i = floor_mod(row.value_i + delta, count);
      break;
    }
    case RowKind::Rate:
      row.value_i = floor_mod(row.value_i + delta, static_cast<int>(music::kRateMidiTicks.size()));
      break;
    case RowKind::Pattern:
      row.value_i = floor_mod(row.value_i + delta, static_cast<int>(music::kPatternBits.size()));
      break;
    case RowKind::Note: {
      int v = row.value_i + delta;
      row.value_i = v < 0 ? 0 : (v > 11 ? 11 : v);
      break;
    }
    case RowKind::Button:
      break;
  }
}

void Controller::set_row_value_clamped(ParamRow& row, std::int32_t i, float f) noexcept {
  row.armed = false;
  if (row.kind == RowKind::Decimal || row.kind == RowKind::Percent) {
    if (row.bounded) {
      if (f < row.min_f) f = row.min_f;
      if (f > row.max_f) f = row.max_f;
    }
    row.value_f = f;
    row.float_text = true;  // parse_parm_value: float(text)
  } else {
    if (row.bounded) {
      if (i < row.min_i) i = row.min_i;
      if (i > row.max_i) i = row.max_i;
    }
    row.value_i = i;
  }
}

void Controller::release_all(std::int16_t channel, bool deviation) noexcept {
  ++diag_.release_calls;
  if (deviation) ++diag_.deviation_releases;
  (void)output_.all_notes_off(channel);
}

void Controller::apply_global_binding(const ParamRow& row) noexcept {
  if (row.binding == GlobalBinding::Octave) global_.octave = row.value_i;
  else if (row.binding == GlobalBinding::KeyOffset) global_.key_offset = row.value_i;
}

bool Controller::push_module_param(std::uint8_t slot, std::uint8_t row_index) noexcept {
  SlotRecord& rec = slots_[slot];
  ParamRow& row = rec.rows[row_index];
  const ParamValue value = make_value(row);
  std::uint8_t effects = EffectNone;
  bool ok = false;
  if (slot == kSlotIn) {
    ok = input_desc_ != nullptr && input_desc_->set_param != nullptr &&
         input_desc_->set_param(input_state_, row.index, value, global_, effects);
  } else {
    const ModuleDescriptor* desc = proc_desc(slot);
    ModuleState* state = proc_state(slot);
    ok = desc != nullptr && state != nullptr && desc->set_param != nullptr &&
         desc->set_param(*state, row.index, value, global_, effects);
  }
  if (!ok) { ++diag_.param_rejected; return false; }
  if (row.binding != GlobalBinding::None) {
    apply_global_binding(row);
    effects = static_cast<std::uint8_t>(effects | EffectReleaseNotes);  // set_octave/set_key_offset
  }
  apply_effects(slot, effects);
  if (slot == kSlotIn || same(rec.id, "euclid") || same(rec.id, "arp_walk")) sync_linked(slot);
  return true;
}

void Controller::apply_effects(std::uint8_t slot, std::uint8_t effects) noexcept {
  if ((effects & EffectReleaseNotes) != 0) release_all();
  if ((effects & EffectRepaint) != 0) queue_ui(UiCb::RebuildParm);
  if ((effects & EffectResetModule) != 0) {
    if (slot == kSlotIn) { if (input_desc_ != nullptr) input_desc_->reset(input_state_, ResetReason::ParameterChange); }
    else if (const ModuleDescriptor* d = proc_desc(slot)) d->reset(*proc_state(slot), ResetReason::ParameterChange);
  }
}

void Controller::sync_linked(std::uint8_t slot) noexcept {
  SlotRecord& rec = slots_[slot];
  auto set_i = [&](const char* name, std::int32_t v) {
    if (ParamRow* r = row_by_name(rec, name)) { r->value_i = v; r->armed = false; }
  };
  const char* id = rec.id;
  if (slot == kSlotIn) {
    if (same(id, "matrix")) {
      const MatrixInputState& s = input_state_.storage.matrix;
      const std::uint8_t pad = s.pad < 12 ? s.pad : 0;
      set_i("pad", pad); set_i("note", s.notes[pad]); set_i("chord", s.chords[pad] ? 1 : 0);
      set_i("seventh", s.sevenths[pad] ? 1 : 0); set_i("ninth", s.ninths[pad] ? 1 : 0);
      set_i("inversion", s.inversions[pad]); set_i("sus", s.sus[pad]); set_i("dim", s.dim[pad] ? 1 : 0);
      set_i("power", s.power[pad] ? 1 : 0); set_i("bass", s.bass_mode); set_i("spread", s.spread_mode);
      if (ParamRow* r = row_by_name(rec, "reset")) { r->value_i = s.reset_value; r->armed = s.reset_armed; }
    } else if (same(id, "chord_walk")) {
      const ChordWalkInputState& s = input_state_.storage.chord_walk;
      const std::uint8_t pad = s.pad < 4 ? s.pad : 0;
      set_i("pad", pad); set_i("note", s.notes[pad]); set_i("seventh", s.sevenths[pad] ? 1 : 0);
      set_i("sus", s.sus[pad]); set_i("power", s.power[pad] ? 1 : 0);
      set_i("preview_chord", s.preview_chord ? 1 : 0);
    } else if (same(id, "chords")) {
      const ChordInputState& s = input_state_.storage.chords;
      const std::uint8_t pad = s.modifier_pad < 6 ? s.modifier_pad : 0;
      set_i("modifier_select", pad + 1); set_i("modifier_action", s.modifier_actions[pad]);
    }
    return;
  }
  ModuleState* state = proc_state(slot);
  if (state == nullptr) return;
  if (same(id, "euclid")) {
    const EuclidModuleState& s = state->storage.euclid;
    const std::uint8_t lane = s.lane < kEuclidLanes ? s.lane : 0;
    set_i("lane", lane); set_i("pulses", s.pulses[lane]); set_i("length", s.lengths[lane]);
    set_i("rotation", s.rotations[lane]);
  } else if (same(id, "arp_walk")) {
    const ArpWalkModuleState& s = state->storage.arp_walk;
    const std::uint8_t sel = s.step_select < 8 ? s.step_select : 0;
    set_i("step_length", s.step_length); set_i("step_select", sel); set_i("step_value", s.step_values[sel]);
  }
}

void Controller::set_out_channel_value(std::uint8_t slot, std::int32_t one_based) noexcept {
  SlotRecord& rec = slots_[slot];
  const std::int32_t channel = one_based - 1;
  if (channel < 0 || channel > 15) return;
  if (slot == kSlotOut) {
    if (channel == rec.out_channel) return;  // Output.set_out_ch: unchanged -> nothing
    release_all(rec.out_channel);
  }
  rec.out_channel = static_cast<std::uint8_t>(channel);
}

void Controller::run_edit_callback(std::uint8_t slot, std::uint8_t row_index) noexcept {
  SlotRecord& rec = slots_[slot];
  ParamRow& row = rec.rows[row_index];
  switch (row.special) {
    case ParamSpecial::None:
    case ParamSpecial::MatrixReset:
      (void)push_module_param(slot, row_index);
      break;
    case ParamSpecial::SetKey:
      global_.key = static_cast<std::uint8_t>(row.value_i);
      release_all();
      break;
    case ParamSpecial::SetScale:
      global_.scale = static_cast<std::uint8_t>(row.value_i);
      release_all();
      break;
    case ParamSpecial::SetBpm:
      if (transport_.set_bpm(static_cast<std::uint16_t>(row.value_i))) tr_bpm_ = static_cast<std::uint16_t>(row.value_i);
      break;
    case ParamSpecial::SetSwing:
      if (transport_.set_swing(row.value_f)) { tr_swing_ = row.value_f; global_.swing = row.value_f; }
      break;
    case ParamSpecial::OutChannel:
      set_out_channel_value(slot, row.value_i);
      break;
    case ParamSpecial::RouteSource:
      rec.source_mode = static_cast<std::uint8_t>(row.value_i);
      configure_chain(slot);
      break;
    case ParamSpecial::RouteOperation:
      rec.operation_mode = static_cast<std::uint8_t>(row.value_i);
      configure_chain(slot);
      break;
    case ParamSpecial::RouteChannel:
      set_out_channel_value(slot, row.value_i);  // Module.set_out_channel: no release
      configure_chain(slot);
      break;
    case ParamSpecial::SettingsBrightness:
      brightness_ = row.value_f;
      paint_pixels();
      user_settings_dirty_ = true;
      break;
    case ParamSpecial::SetTransportMode:  // applied by the exit callback
    case ParamSpecial::OutPrint:
    case ParamSpecial::SettingsScene:
    case ParamSpecial::SettingsSave:
    case ParamSpecial::SettingsCopy:
    case ParamSpecial::SettingsReset:
    case ParamSpecial::EmptyPick:
      break;
  }
}

void Controller::run_exit_callback(std::uint8_t slot, std::uint8_t row_index) noexcept {
  SlotRecord& rec = slots_[slot];
  ParamRow& row = rec.rows[row_index];
  switch (row.special) {
    case ParamSpecial::SetTransportMode:
      // Set.set_transport_mode: state mode, clock off, reset. No module stop in the source.
      global_.transport_mode = static_cast<TransportMode>(row.value_i != 0 ? 1 : 0);
      transport_.set_mode(global_.transport_mode, now_.us);
      break;
    case ParamSpecial::SettingsScene:
      (void)request_scene_switch(static_cast<std::uint8_t>(row.value_i));
      break;
    case ParamSpecial::SettingsCopy:
      if (row.value_i > 0) (void)request_scene_copy(static_cast<std::uint8_t>(row.value_i - 1));
      break;
    case ParamSpecial::SettingsReset:
      if (row.value_i == 1) (void)request_scene_reset();
      else { row.value_i = 0; row.armed = false; queue_ui(UiCb::RebuildParm); }
      break;
    case ParamSpecial::MatrixReset: {
      // Matrix.reset_layout: nonzero applies a preset; the control always returns to 0.
      if (slot == kSlotIn && same(rec.id, "matrix")) {
        std::uint8_t effects = EffectNone;
        finish_matrix_reset(input_state_, effects);
        row.value_i = 0;
        row.armed = false;
        apply_effects(slot, effects);
        sync_linked(slot);
        queue_ui(UiCb::RebuildParm);
      }
      break;
    }
    default:
      break;
  }
}

Controller::EnterResult Controller::run_enter_callback(std::uint8_t slot, std::uint8_t row_index) noexcept {
  SlotRecord& rec = slots_[slot];
  ParamRow& row = rec.rows[row_index];
  switch (row.special) {
    case ParamSpecial::SettingsSave:
      (void)request_scene_save();
      break;
    case ParamSpecial::SettingsReset:
      row.armed = true;  // display "SUR?"
      queue_ui(UiCb::RebuildParm);
      break;
    case ParamSpecial::MatrixReset:
      if (slot == kSlotIn && same(rec.id, "matrix")) matrix_arm_reset(input_state_);
      row.armed = true;
      queue_ui(UiCb::RebuildParm);
      break;
    case ParamSpecial::EmptyPick:
      enter_module_selection();
      break;
    case ParamSpecial::None:
      if (row.kind == RowKind::Button) (void)push_module_param(slot, row_index);  // e.g. arp_walk RAND
      break;
    default:
      break;
  }
  return row.kind == RowKind::Button ? EnterResult::ReturnToSelection : EnterResult::StayInEdit;
}

// ---------------------------------------------------------------------------
// UI queue (only the callbacks that carry paging history are modelled)
// ---------------------------------------------------------------------------
void Controller::queue_ui(UiCb cb) noexcept {
  ui_dirty_ = true;
  for (std::uint8_t i = 0; i < ui_queue_count_; ++i)
    if (ui_queue_[i] == cb) return;  // add_to_ui_queue de-duplicates
  if (ui_queue_count_ < sizeof(ui_queue_) / sizeof(ui_queue_[0])) ui_queue_[ui_queue_count_++] = cb;
}

void Controller::flush_ui_queue() noexcept {
  const std::uint8_t count = slots_[active_chain_].row_count;
  for (std::uint8_t i = 0; i < ui_queue_count_; ++i) {
    if (ui_queue_[i] == UiCb::RebuildParm) paging_rebuild(paging_, active_parm_, count);
    else paging_update_selection(paging_, active_parm_, count);
  }
  ui_queue_count_ = 0;
}

// ---------------------------------------------------------------------------
// Navigation (Orion.py)
// ---------------------------------------------------------------------------
void Controller::state_move_active_chain(int delta) noexcept {
  active_chain_ = static_cast<std::uint8_t>(floor_mod(static_cast<int>(active_chain_) + delta, kSlotCount));
  active_parm_ = 0;
}

void Controller::move_active_chain_element(int delta) noexcept {
  ui_dirty_ = true;
  if (chain_swap_mode_ == 2) {  // swap instead of move
    if (active_chain_ < kSlotP1 || active_chain_ > kSlotP6) { disable_chain_swap_mode(); return; }
    std::uint8_t active = active_chain_;
    const int direction = delta < 0 ? -1 : 1;
    const int steps = delta < 0 ? -delta : delta;
    for (int i = 0; i < steps; ++i) {
      if (direction == 1 && active >= kSlotP6) break;
      if (direction == -1 && active <= kSlotP1) break;
      swap_modules(active, static_cast<std::uint8_t>(active + direction));
      active = static_cast<std::uint8_t>(active + direction);
      state_move_active_chain(direction);
    }
    stop_all_modules();
  } else {
    state_move_active_chain(delta);
    active_parm_ = 0;
    queue_ui(UiCb::RebuildParm);
  }
}

void Controller::swap_modules(std::uint8_t a, std::uint8_t b) noexcept {
  (void)chain_.swap_slots(static_cast<std::uint8_t>(a - kSlotP1), static_cast<std::uint8_t>(b - kSlotP1));
  swap_bytes(&slots_[a], &slots_[b], sizeof(SlotRecord));
  configure_chain(a);
  configure_chain(b);
}

void Controller::move_active_parm_element(int delta) noexcept {
  ui_dirty_ = true;
  active_parm_ = move_parm_index(active_parm_, delta, slots_[active_chain_].row_count,
                                 is_static_slot(active_chain_));
  if (active_parm_ < 0) return;  // cursor_change_state only
  queue_ui(UiCb::UpdateParmSelection);
}

void Controller::enter_parm_edit() noexcept {
  ui_dirty_ = true;
  if (active_parm_ == -2) { section_ = UiSection::Chain; return; }
  if (active_parm_ == -1) { enter_module_selection(); return; }
  section_ = UiSection::ParmEdit;
  ParamRow* row = active_row();
  if (row == nullptr) { enter_chain_view(); return; }
  const EnterResult result = run_enter_callback(active_chain_, static_cast<std::uint8_t>(active_parm_));
  if (section_ != UiSection::ParmEdit) return;
  if (result == EnterResult::ReturnToSelection) section_ = UiSection::ParmSelection;
}

void Controller::exit_parm_edit() noexcept {
  ui_dirty_ = true;
  ParamRow* row = active_row();
  if (row == nullptr) { enter_chain_view(); return; }
  run_exit_callback(active_chain_, static_cast<std::uint8_t>(active_parm_));
  if (section_ != UiSection::ParmEdit) return;
  section_ = UiSection::ParmSelection;
}

void Controller::edit_parm(int delta) noexcept {
  ui_dirty_ = true;
  ParamRow* row = active_row();
  if (row == nullptr) { enter_chain_view(); return; }
  const std::int32_t old_i = row->value_i;
  const float old_f = row->value_f;
  edit_row(*row, delta);
  const std::uint8_t slot = active_chain_;
  const std::uint8_t index = static_cast<std::uint8_t>(active_parm_);
  // Module parameters are validated by set_param; a refusal keeps the old value.
  if (row->special == ParamSpecial::None || row->special == ParamSpecial::MatrixReset) {
    if (!push_module_param(slot, index)) { row->value_i = old_i; row->value_f = old_f; }
  } else {
    run_edit_callback(slot, index);
  }
}

void Controller::enter_parm_selection() noexcept {
  ui_dirty_ = true;
  if (same(slots_[active_chain_].id, "empty")) { enter_module_selection(); return; }
  section_ = UiSection::ParmSelection;
  active_parm_ = 0;
  queue_ui(UiCb::RebuildParm);
}

void Controller::enter_chain_view() noexcept {
  ui_dirty_ = true;
  section_ = UiSection::Chain;
  active_parm_ = 0;
}

void Controller::jump_to_slot_parm_selection(std::uint8_t slot) noexcept {
  ui_dirty_ = true;
  if (section_ == UiSection::ParmEdit) exit_parm_edit();
  else if (section_ == UiSection::ModuleSelection) exit_module_selection();
  if (chain_swap_mode_ != 0 || chain_mode_ == UiChainMode::Swap) disable_chain_swap_mode();
  if (active_chain_ != slot) {
    active_chain_ = slot;
    active_parm_ = 0;
  }
  enter_parm_selection();
}

void Controller::enter_module_selection() noexcept {
  ui_dirty_ = true;
  if (is_static_slot(active_chain_)) return;
  section_ = UiSection::ModuleSelection;
  const char* id = slots_[active_chain_].id;
  selector_index_ = 0;
  if (active_chain_ == kSlotIn) {
    for (std::uint8_t i = 0; i < input_selector_count(); ++i)
      if (same(input_selector(i).id, id)) selector_index_ = i;
  } else {
    for (std::uint8_t i = 0; i < processor_selector_count(); ++i)
      if (same(processor_selector(i).id, id)) selector_index_ = i;
  }
}

void Controller::move_module_selection(int delta) noexcept {
  ui_dirty_ = true;
  const int count = active_chain_ == kSlotIn ? input_selector_count() : processor_selector_count();
  selector_index_ = static_cast<std::uint8_t>(floor_mod(static_cast<int>(selector_index_) + delta, count));
}

bool Controller::apply_module_selection() noexcept {
  const SelectorEntry& selected = active_chain_ == kSlotIn ? input_selector(selector_index_)
                                                           : processor_selector(selector_index_);
  if (same(slots_[active_chain_].id, selected.id)) return true;  // unchanged
  // DEVIATION (safety): the source leaves notes of the removed module sounding.
  release_all(-1, true);
  bool loaded;
  if (active_chain_ == kSlotIn) {
    loaded = install_input(selected.id) && same(slots_[kSlotIn].id, selected.id);
  } else {
    loaded = install_processor(active_chain_, selected.id) && same(slots_[active_chain_].id, selected.id);
  }
  active_parm_ = 0;
  return loaded;
}

void Controller::exit_module_selection() noexcept {
  ui_dirty_ = true;
  const bool loaded = apply_module_selection();
  if (!loaded) { section_ = UiSection::Chain; return; }
  if (same(slots_[active_chain_].id, "empty")) section_ = UiSection::Chain;
  else enter_parm_selection();
}

void Controller::enable_chain_swap_mode() noexcept {
  ui_dirty_ = true;
  if (active_chain_ < kSlotP1 || active_chain_ > kSlotP6) { disable_chain_swap_mode(); return; }
  chain_mode_ = UiChainMode::Swap;
}

void Controller::disable_chain_swap_mode() noexcept {
  ui_dirty_ = true;
  chain_mode_ = UiChainMode::Select;
  hold_active_ = false;
  chain_swap_mode_ = 0;
}

// ---------------------------------------------------------------------------
// Pixels (neo_pixels.py). Brightness arithmetic mirrors the source, including
// the held/release paths that multiply brightness_multiplier twice.
// ---------------------------------------------------------------------------
void Controller::paint_pixels() noexcept {
  ui_dirty_ = true;
  for (std::uint8_t i = 0; i < kKeys; ++i)
    pixel_out_[i] = combine_color_and_brightness(pixel_colors_[i], kKeyBrightnessDefault * brightness_);
}
void Controller::paint_pixel(std::uint8_t pad, double brightness) noexcept {
  ui_dirty_ = true;
  pixel_out_[pad] = combine_color_and_brightness(pixel_colors_[pad], brightness * brightness_);
}
void Controller::set_key_colors(const RgbColor* colors) noexcept {
  for (std::uint8_t i = 0; i < kKeys; ++i) pixel_colors_[i] = colors[i];
  paint_pixels();
}
void Controller::set_held_pixel(std::uint8_t pad) noexcept {
  const std::uint16_t bit = static_cast<std::uint16_t>(1U << pad);
  if ((pixel_held_ & bit) != 0) return;
  pixel_held_ = static_cast<std::uint16_t>(pixel_held_ | bit);
  paint_pixel(pad, kKeyBrightnessHeld * brightness_);
}
void Controller::release_held_pixel(std::uint8_t pad) noexcept {
  const std::uint16_t bit = static_cast<std::uint16_t>(1U << pad);
  if ((pixel_held_ & bit) == 0) return;
  pixel_held_ = static_cast<std::uint16_t>(pixel_held_ & ~bit);
  paint_pixel(pad, kKeyBrightnessDefault * brightness_);
}
void Controller::set_nav_state_colors(bool update) noexcept {
  if (!update) for (std::uint8_t i = 0; i < kKeys; ++i) pixel_temp_[i] = pixel_colors_[i];
  nav_colors_active_ = true;
  set_key_colors(nav_keys_state_ == 0 ? kNavNoteColors : kNavParmColors);
}
void Controller::exit_nav_state_colors() noexcept {
  nav_colors_active_ = false;
  set_key_colors(pixel_temp_);
}
void Controller::apply_view_colors(const InputView& view) noexcept {
  RgbColor colors[kKeys];
  for (std::uint8_t i = 0; i < kKeys; ++i) colors[i] = view.pad_colors[i];
  if (nav_colors_active_) {  // restored by exit_nav_state_colors
    for (std::uint8_t i = 0; i < kKeys; ++i) pixel_temp_[i] = colors[i];
    return;
  }
  bool changed = false;
  for (std::uint8_t i = 0; i < kKeys; ++i)
    if (colors[i].red != pixel_colors_[i].red || colors[i].green != pixel_colors_[i].green ||
        colors[i].blue != pixel_colors_[i].blue) changed = true;
  if (changed) set_key_colors(colors);
}

// ---------------------------------------------------------------------------
// Input events
// ---------------------------------------------------------------------------
void Controller::handle_event(const PhysicalEvent& e) noexcept {
  if (scene_.phase() == SceneServicePhase::Failed) {
    // Recovery gesture (T14): a press of the encoder knob re-applies the committed
    // scene in place. Every other event is dropped while the scene is failed.
    if (e.kind == InputKind::EncoderSwitch && e.value != 0) (void)request_scene_recovery();
    ++diag_.events_busy;
    return;
  }
  if (scene_.busy()) { ++diag_.events_busy; return; }
  ++activity_;
  ui_dirty_ = true;
  event_ms_ = e.at_ms;
  switch (e.kind) {
    case InputKind::KeyEdge:
      if (e.key >= kKeys) return;
      if (e.value != 0) on_key_down(e.key); else on_key_up(e.key);
      break;
    case InputKind::EncoderSwitch:
      if (e.value != 0) on_encoder_down(); else on_encoder_up();
      break;
    case InputKind::EncoderDelta:
      on_encoder_delta(e.value);
      break;
    case InputKind::Resync:
      on_resync(static_cast<std::uint16_t>(e.value));
      break;
  }
}

void Controller::on_encoder_delta(int delta) noexcept {
  if (delta == 0) return;
  switch (section_) {
    case UiSection::Chain: move_active_chain_element(delta); break;
    case UiSection::ModuleSelection: move_module_selection(delta); break;
    case UiSection::ParmSelection: move_active_parm_element(delta); break;
    case UiSection::ParmEdit: edit_parm(delta); break;
  }
}

void Controller::on_encoder_down() noexcept {
  if (encoder_down_) return;
  encoder_down_ = true;
  hold_active_ = true;
  hold_start_ms_ = event_ms_;
  consumed_ = 0;  // a press is a modifier or a selection; decided by key use
  set_nav_state_colors(false);
}

void Controller::on_encoder_up() noexcept {
  if (!encoder_down_) return;  // release without a press: gated/recovered state
  encoder_down_ = false;
  if (consumed_ == 1) {  // consumed by a key combination
    consumed_ = 0;
    release_all();  // only; modules keep their state (Orion.py:759-764)
    if (chain_swap_mode_ == 1) disable_chain_swap_mode();
  } else {
    switch (section_) {
      case UiSection::Chain:
        if (chain_swap_mode_ == 1) {
          enable_chain_swap_mode();
          chain_swap_mode_ = 2;
        } else if (chain_swap_mode_ == 2) {
          disable_chain_swap_mode();
        } else {
          chain_swap_mode_ = 0;
          hold_active_ = false;
          enter_parm_selection();
        }
        break;
      case UiSection::ModuleSelection: exit_module_selection(); break;
      case UiSection::ParmSelection: enter_parm_edit(); break;
      case UiSection::ParmEdit: exit_parm_edit(); break;
    }
  }
  exit_nav_state_colors();
  // IN.color_pixels(): the latest input colors replace the restored ones.
  RgbColor colors[kKeys];
  for (std::uint8_t i = 0; i < kKeys; ++i) colors[i] = input_view_.pad_colors[i];
  set_key_colors(colors);
}

void Controller::on_key_down(std::uint8_t key) noexcept {
  consumed_ = 1;  // any key press marks the encoder press as consumed
  if (chain_swap_mode_ == 1) disable_chain_swap_mode();
  if (encoder_down_) {
    shortcut(key);
  } else {
    KeyEvent ke{};
    ke.key = key; ke.down = true; ke.at_ms = event_ms_;
    (void)frame_.keys.append(ke);
    held_keys_ = static_cast<std::uint16_t>(held_keys_ | (1U << key));
    if (!transport_.running() && global_.transport_mode == TransportMode::Internal) start_clock();
  }
  set_held_pixel(key);
}

void Controller::on_key_up(std::uint8_t key) noexcept {
  if (encoder_down_) consumed_ = 1;
  // Every release reaches the input module whatever the encoder state; its
  // relationship table decides whether an off is owed.
  KeyEvent ke{};
  ke.key = key; ke.down = false; ke.at_ms = event_ms_;
  if (frame_.keys.append(ke) != PushResult::Added) {
    // Cannot happen with <=64 events per pass; never drop a release silently.
    release_all();
    ++diag_.emergency_releases;
  }
  held_keys_ = static_cast<std::uint16_t>(held_keys_ & ~(1U << key));
  release_held_pixel(key);
}

void Controller::shortcut(std::uint8_t key) noexcept {
  switch (key + 1) {  // physical KEY number (pressed[] index in the source)
    case 2: start_clock(); break;
    case 1: stop_clock(); break;
    case 4: enter_module_selection(); break;
    case 5: enter_chain_view(); break;
    case 7:
      nav_keys_state_ = static_cast<std::uint8_t>((nav_keys_state_ + 1) % 2);
      set_nav_state_colors(true);
      break;
    // DEVIATION (user-requested, see known-quirks.md "Intentional deviations"): the source
    // jumps to IN (key 3) / OUT (key 6) only. Pressing again cycles IN -> SET -> IN and
    // OUT -> STTNGS -> OUT. The test uses active_chain_, which section exits never change.
    case 3: jump_to_slot_parm_selection(active_chain_ == kSlotIn ? kSlotSet : kSlotIn); break;
    case 6: jump_to_slot_parm_selection(active_chain_ == kSlotOut ? kSlotSettings : kSlotOut); break;
    case 9:
      if (section_ == UiSection::Chain) {
        if (chain_mode_ == UiChainMode::Swap) disable_chain_swap_mode();
        enter_parm_selection();
      } else if (section_ == UiSection::ModuleSelection) {
        exit_module_selection();
      } else if (section_ == UiSection::ParmSelection) {
        enter_parm_edit();
      } else {
        exit_parm_edit();
      }
      break;
    default: break;
  }
  if (nav_keys_state_ == 0) nav_notes_adjust(key);
  if (nav_keys_state_ == 1) nav_parms(key);
}

void Controller::nav_notes_adjust(std::uint8_t key) noexcept {
  switch (key + 1) {
    case 8: ++global_.octave; break;
    case 11: --global_.octave; break;
    case 10: --global_.key_offset; break;
    case 12: ++global_.key_offset; break;
    default: break;
  }
  // The source mirrors only while IN is active, so a scene saved afterwards held stale values
  // (masked in Python by the restart on scene change). Always mirror.
  sync_input_global_rows();
}

void Controller::sync_input_global_rows() noexcept {
  SlotRecord& rec = slots_[kSlotIn];
  if (ParamRow* r = row_by_name(rec, "octave")) set_row_value_clamped(*r, global_.octave, 0.0f);
  if (ParamRow* r = row_by_name(rec, "key_offset")) set_row_value_clamped(*r, global_.key_offset, 0.0f);
  if (active_chain_ == kSlotIn) queue_ui(UiCb::RebuildParm);
}

void Controller::nav_parms(std::uint8_t key) noexcept {
  const int n = key + 1;
  const bool up = n == 8, down = n == 11, left = n == 10, right = n == 12;
  if (!up && !down && !left && !right) return;
  switch (section_) {
    case UiSection::Chain:
      if (up || down) {
        if (chain_mode_ == UiChainMode::Swap) disable_chain_swap_mode();
        section_ = UiSection::ParmSelection;
        move_active_parm_element(up ? -1 : 1);
      } else {
        move_active_chain_element(left ? -1 : 1);
      }
      break;
    case UiSection::ModuleSelection:
      move_module_selection((up || left) ? -1 : 1);
      break;
    case UiSection::ParmSelection:
      if (up || down) {
        move_active_parm_element(up ? -1 : 1);
      } else {
        active_parm_ = 0;
        move_active_chain_element(left ? -1 : 1);
      }
      break;
    case UiSection::ParmEdit:
      edit_parm((up || right) ? 1 : -1);
      break;
  }
}

void Controller::tick_timers(std::uint32_t now_ms) noexcept {
  // Holding the knob in chain view arms swap mode.
  if (section_ == UiSection::Chain && hold_active_ && chain_swap_mode_ == 0 && encoder_down_ &&
      elapsed(now_ms, hold_start_ms_) >= kSwapHoldMs) {
    chain_swap_mode_ = 1;
    enable_chain_swap_mode();
  }
}

void Controller::on_resync(std::uint16_t mask) noexcept {
  // Authoritative physical key mask: any key the input module still believes
  // is held but is physically up gets its release now.
  const std::uint16_t stale = static_cast<std::uint16_t>(held_keys_ & ~mask & PhysicalEventBridge::kAllKeysMask);
  for (std::uint8_t k = 0; k < kKeys; ++k) {
    if ((stale & (1U << k)) == 0) continue;
    ++diag_.resync_releases;
    KeyEvent ke{};
    ke.key = k; ke.down = false; ke.at_ms = event_ms_;
    (void)frame_.keys.append(ke);
    release_held_pixel(k);
  }
  held_keys_ = static_cast<std::uint16_t>(held_keys_ & mask);
}

void Controller::emergency_release(PhysicalEventBridge* bridge) noexcept {
  (void)bridge;
  ++diag_.emergency_releases;
  ui_dirty_ = true;
  release_all();
  if (input_desc_ != nullptr) input_desc_->reset(input_state_, ResetReason::Panic);
  chain_.reset(ResetReason::Panic);
  frame_.keys.clear();
  held_keys_ = 0;
  pixel_held_ = 0;
  if (encoder_down_ && nav_colors_active_) exit_nav_state_colors();
  encoder_down_ = false;
  consumed_ = 0;
  hold_active_ = false;
  if (chain_swap_mode_ != 0) disable_chain_swap_mode();
  paint_pixels();
}

PollResult Controller::poll(PhysicalEventBridge& bridge, const PollArgs& args) noexcept {
  now_ = args.now;
  PollResult result{};
  if (maintenance_hold_) {
    // Maintenance: no musical input, transport or scene work. Only pending releases
    // drain, so the engine is silent while the CDC tool owns the storage window.
    output_.set_connected(args.midi_connected);
    if (args.port != nullptr) result.packets_sent = output_.flush(*args.port);
    return result;
  }
  if (epoch_pending_) {
    epoch_pending_ = false;
    ++epoch_;
    (void)bridge.change_epoch(epoch_);
    encoder_down_ = false; consumed_ = 0; hold_active_ = false; held_keys_ = 0; pixel_held_ = 0;
    frame_.keys.clear();
    ui_dirty_ = true;
  }
  if (bridge.faulted()) {
    bridge.acknowledge_fault(bridge.state().fault_generation);  // gates keys until all-up Resync
    emergency_release(&bridge);
  }
  for (std::size_t n = 0; n < PhysicalEventBridge::kCapacity; ++n) {
    PhysicalEvent e{};
    if (!bridge.try_pop(e)) break;
    const bool was_waiting = bridge.awaiting_resync();
    const bool accepted = bridge.accept_for_musical_input(e);
    if (!was_waiting && bridge.awaiting_resync()) emergency_release(&bridge);  // sequence gap
    if (!accepted) { ++diag_.events_gated; continue; }
    handle_event(e);
    ++result.events_handled;
  }
  const PollResult stepped = step(args);
  result.midi_consumed = stepped.midi_consumed;
  result.packets_sent = stepped.packets_sent;
  return result;
}

// ---------------------------------------------------------------------------
// Engine
// ---------------------------------------------------------------------------
void Controller::emit_realtime(const RealtimeEvents& events, bool clocks_too) noexcept {
  for (std::uint16_t i = 0; i < events.count; ++i) {
    switch (events.items[i].kind) {
      case RealtimeKind::Start: (void)output_.queue_start(); break;
      case RealtimeKind::Stop: (void)output_.queue_stop(); break;
      case RealtimeKind::Clock: if (clocks_too) (void)output_.queue_clock(1); break;
      case RealtimeKind::Continue: break;
    }
  }
}

void Controller::start_clock() noexcept {
  ui_dirty_ = true;
  RealtimeEvents events{};
  transport_.start(now_.us, &events);
  emit_realtime(events, true);  // Start then the leading TimingClock, sent immediately
}

void Controller::stop_clock() noexcept {
  ui_dirty_ = true;
  RealtimeEvents events{};
  transport_.stop(now_.us, &events);
  emit_realtime(events, true);
  stop_all_modules();  // Transport.clock_stop -> state.stop_all_modules
}

void Controller::stop_all_modules() noexcept {
  if (input_desc_ != nullptr) input_desc_->reset(input_state_, ResetReason::Stop);
  chain_.reset(ResetReason::Stop);
  release_all();  // sources never release what stop() drops
}

void Controller::panic() noexcept {
  release_all();
  if (input_desc_ != nullptr) input_desc_->reset(input_state_, ResetReason::Panic);
  chain_.reset(ResetReason::Panic);
  held_keys_ = 0;
}

void Controller::recovery_reset() noexcept {
  ++diag_.chain_recoveries;
  transport_.stop(now_.us);
  if (input_desc_ != nullptr) input_desc_->reset(input_state_, ResetReason::Panic);
  chain_.reset(ResetReason::Panic);
  frame_.keys.clear();
  ui_dirty_ = true;
}

void Controller::refresh_follow_flags() noexcept {
  // Source follow checks: active chain is the input slot and the UI is in
  // parameter selection/edit. Re-asserted every pass because reset clears them.
  const bool follow = active_chain_ == kSlotIn &&
                      (section_ == UiSection::ParmSelection || section_ == UiSection::ParmEdit);
  const char* id = slots_[kSlotIn].id;
  if (same(id, "chords")) set_chords_modifier_follow(input_state_, follow);
  else if (same(id, "matrix")) set_matrix_pad_follow(input_state_, follow);
  else if (same(id, "chord_walk")) set_chord_walk_pad_follow(input_state_, follow);
}

void Controller::process_pass(const PollArgs&, const TransportUpdate& update) noexcept {
  if (input_desc_ == nullptr) return;
  ProcessContext ctx{now_, global_, update.view, random_};
  refresh_follow_flags();
  NoteBatch in_batch{};
  InputView view{};
  input_desc_->process(input_state_, ctx, frame_, in_batch, view);  // every pass: owed releases drain
  frame_.keys.clear();
  input_view_ = view;
  apply_view_colors(view);

  // Followed pad/selector changes inside process (source select_pad_from_input etc.).
  if (slots_[kSlotIn].row_count != 0) {
    const char* id = slots_[kSlotIn].id;
    bool changed = false;
    if (same(id, "matrix")) { const ParamRow* r = find_row(kSlotIn, "pad"); changed = r && r->value_i != input_state_.storage.matrix.pad; }
    else if (same(id, "chord_walk")) { const ParamRow* r = find_row(kSlotIn, "pad"); changed = r && r->value_i != input_state_.storage.chord_walk.pad; }
    else if (same(id, "chords")) { const ParamRow* r = find_row(kSlotIn, "modifier_select"); changed = r && r->value_i != input_state_.storage.chords.modifier_pad + 1; }
    if (changed) { sync_linked(kSlotIn); queue_ui(UiCb::RebuildParm); }
  }

  collector_.clear();
  bool lost = in_batch.overflowed();
  const SlotRecord& in_rec = slots_[kSlotIn];
  NoteBatch empty{};
  const NoteBatch* chain_input = &in_batch;
  if (in_rec.operation_mode == 1) {
    (void)collector_.append(in_batch, in_rec.out_channel);  // OUT+N
  } else if (in_rec.operation_mode == 2) {
    (void)collector_.append(in_batch, in_rec.out_channel);  // ONLY
    chain_input = &empty;
  }
  chain_.process(ctx, *chain_input, collector_);
  if (!chain_.recovery_required()) (void)collector_.append(chain_.final_stream(), slots_[kSlotOut].out_channel);
  if (lost) collector_.fault();
  const bool needs_recovery = lost || collector_.overflowed() || chain_.recovery_required();
  (void)collector_.drain(output_);
  if (needs_recovery) recovery_reset();
  emit_realtime(update.output, true);  // clocks follow the notes, as process_midi_clock_out
}

PollResult Controller::step(const PollArgs& args) noexcept {
  now_ = args.now;
  PollResult result{};
  output_.set_connected(args.midi_connected);
  tick_timers(now_.ms);
  if (!scene_.busy()) {
    const bool was_running = transport_.running();
    const TransportUpdate update = transport_.update(now_, args.midi_in, args.midi_in_count);
    result.midi_consumed = update.consumed_messages;
    if (global_.transport_mode == TransportMode::External && was_running && !transport_.running())
      stop_all_modules();  // external Stop -> clock_stop(send_out=False)
    process_pass(args, update);
  } else {
    frame_.keys.clear();
  }
  if (output_.recovering()) {
    if (!recovery_handled_) { recovery_reset(); recovery_handled_ = true; }
  }
  if (args.port != nullptr) {
    result.packets_sent = output_.flush(*args.port);
  }
  if (output_.recovering()) (void)output_.request_restart();
  else recovery_handled_ = false;
  service_settings_save();
  (void)scene_.poll(now_.ms);
  note_scene_result();
  return result;
}

void Controller::note_scene_result() noexcept {
  // Failed entries (failed rollback or failed recovery) are counted on entry, because
  // Failed never returns to idle with a failing status.
  const std::uint32_t failures = scene_.failure_count();
  if (failures != failures_seen_) {
    diag_.scene_failures += failures - failures_seen_;
    failures_seen_ = failures;
    ui_dirty_ = true;
  }
  const bool busy = scene_.busy();
  if (scene_was_busy_ && !busy) {
    last_scene_status_ = scene_.status();
    const bool ok = last_scene_status_ == SceneServiceStatus::Succeeded ||
                    last_scene_status_ == SceneServiceStatus::SucceededWithBackup;
    // First boot without scene files is the normal stock state, not a failure.
    const bool first_boot = boot_stage_ == BootStage::Scene &&
                            scene_.codec_result() == SceneResult::Missing;
    if (!ok && !first_boot) ++diag_.scene_failures;
    // Settings saves do not change live state and must not drop held keys.
    if (settings_op_ != SettingsOp::Save) epoch_pending_ = true;  // fresh all-up resync
    ui_dirty_ = true;
    // The SCENE row edits value_i; a switch that did not commit must show the active index again.
    build_settings_rows(slots_[kSlotSettings]);
    queue_ui(UiCb::RebuildParm);
    finish_scene_operation(ok);
  }
  scene_was_busy_ = scene_.busy();
}

void Controller::finish_scene_operation(bool ok) noexcept {
  if (settings_op_ == SettingsOp::Load) {
    if (ok) {
      brightness_ = scene_.user_settings().key_brightness;  // KEYBRI applies at boot
      build_settings_rows(slots_[kSlotSettings]);
      paint_pixels();
    } else {
      ++diag_.settings_load_failures;
    }
  } else if (settings_op_ == SettingsOp::Save && !ok) {
    ++diag_.settings_save_failures;
  }
  settings_op_ = SettingsOp::None;

  if (boot_stage_ == BootStage::Settings) {
    boot_stage_ = BootStage::Scene;
    if (!scene_.request(SceneOperation::LoadActive)) {
      boot_stage_ = BootStage::Idle;
      ++diag_.boot_default_fallbacks;
      (void)scene_reset_defaults();
    }
  } else if (boot_stage_ == BootStage::Scene) {
    boot_stage_ = BootStage::Idle;
    if (!ok) {
      // A failed boot load may have applied part of a scene. Restore the stock defaults in
      // memory (no storage write), as the first-boot state.
      ++diag_.boot_default_fallbacks;
      (void)scene_reset_defaults();
    }
  }
}

bool Controller::begin_boot() noexcept {
  if (boot_stage_ != BootStage::Idle || scene_.busy()) return false;
  boot_stage_ = BootStage::Settings;
  settings_op_ = SettingsOp::Load;
  if (!scene_.request(SceneOperation::LoadUserSettings)) {
    settings_op_ = SettingsOp::None;
    boot_stage_ = BootStage::Scene;
    if (!scene_.request(SceneOperation::LoadActive)) {
      boot_stage_ = BootStage::Idle;
      ++diag_.boot_default_fallbacks;
      (void)scene_reset_defaults();
      return false;
    }
  }
  return true;
}

bool Controller::editing_brightness() const noexcept {
  if (section_ != UiSection::ParmEdit || active_chain_ != kSlotSettings) return false;
  if (active_parm_ < 0 || active_parm_ >= slots_[kSlotSettings].row_count) return false;
  return slots_[kSlotSettings].rows[active_parm_].special == ParamSpecial::SettingsBrightness;
}

void Controller::service_settings_save() noexcept {
  // KEYBRI is saved only after the user leaves its edit, and only while the transport is
  // stopped (SaveUserSettings stops and quiesces). Edits never save per encoder tick.
  if (user_settings_dirty_ && !editing_brightness()) settings_save_pending_ = true;
  if (!settings_save_pending_ || !user_settings_dirty_) return;
  if (scene_.busy() || transport_.running() || boot_stage_ != BootStage::Idle) return;
  UserSettings settings;
  settings.key_brightness = brightness_;
  if (!scene_.set_user_settings(settings)) return;
  if (!scene_.request(SceneOperation::SaveUserSettings)) return;
  settings_op_ = SettingsOp::Save;
  settings_save_pending_ = false;
  user_settings_dirty_ = false;
  ++diag_.settings_saves;
}

bool Controller::enter_maintenance() noexcept {
  if (maintenance_hold_ || scene_.busy() || boot_stage_ != BootStage::Idle) return false;
  scene_stop_release_drain();  // stop_all_modules (releases through Output) and transport stop
  maintenance_hold_ = true;
  ui_dirty_ = true;
  return true;
}

void Controller::leave_maintenance() noexcept {
  if (!maintenance_hold_) return;
  maintenance_hold_ = false;
  epoch_pending_ = true;  // resume: discard physical events from before the hold, resync
  ui_dirty_ = true;
}

SceneSchemaResolver Controller::scene_schema() noexcept { return SceneBridge::schema(this); }

void Controller::set_storage_window(bool open) noexcept {
  if (window_.set_open != nullptr) window_.set_open(window_.context, open);
}

void Controller::publish_failure_pixels(UiModel& m) const noexcept {
  if (!scene_failed()) return;
  // Scene Failed: every pad shows solid red, the only failure indicator the current
  // screen can carry without a renderer change. Recovery clears it (busy -> idle). The
  // indicator is not scaled by KEYBRI, so it stays visible at brightness 0.
  const RgbColor red{255, 0, 0};
  const RgbColor shown = combine_color_and_brightness(red, kKeyBrightnessHeld);
  for (auto& pad : m.pixels) pad = shown;
}

// ---------------------------------------------------------------------------
// Scene requests
// ---------------------------------------------------------------------------
bool Controller::request_load_active() noexcept { return scene_.request(SceneOperation::LoadActive); }
bool Controller::request_scene_save() noexcept {
  const bool ok = scene_.request(SceneOperation::Save);
  if (!ok) ++diag_.scene_requests_ignored;
  return ok;
}
bool Controller::request_scene_switch(std::uint8_t scene) noexcept {
  if (scene == scene_.active_scene()) return true;  // switch_scene: same scene is a no-op
  const bool ok = scene_.request(SceneOperation::Switch, scene);
  if (!ok) ++diag_.scene_requests_ignored;
  return ok;
}
bool Controller::request_scene_copy(std::uint8_t scene) noexcept {
  const bool ok = scene_.request(SceneOperation::CopyCurrent, scene);
  if (!ok) ++diag_.scene_requests_ignored;
  return ok;
}
bool Controller::request_scene_reset() noexcept {
  const bool ok = scene_.request(SceneOperation::Reset);
  if (!ok) ++diag_.scene_requests_ignored;
  return ok;
}
bool Controller::request_scene_recovery() noexcept {
  const bool ok = scene_.acknowledge_recovery();
  if (!ok) ++diag_.scene_requests_ignored;
  return ok;
}

// ---------------------------------------------------------------------------
// Scene lifecycle callbacks
// ---------------------------------------------------------------------------
bool Controller::scene_is_stopped() const noexcept {
  return !transport_.running() &&
         (!output_.connected() || (output_.pending() == 0 && output_.owed_count() == 0));
}

void Controller::scene_stop_release_drain() noexcept {
  stop_all_modules();
  transport_.stop(now_.us);  // reload_app: running = 0 (no MIDI Stop in the source)
  frame_.keys.clear();
}

bool Controller::scene_capture(SceneDto& dto) noexcept {
  dto.globals = global_;
  dto.global_fields = static_cast<std::uint8_t>(SceneKey | SceneScale | SceneOctave | SceneKeyOffset |
                                                SceneBpm | SceneSwing | SceneTransportMode);
  for (std::uint8_t slot = 0; slot < kSlotCount; ++slot) {
    SlotRecord& rec = slots_[slot];
    SceneModule& m = dto.modules[slot];
    m.present = true;
    m.unknown_module = false;
    std::memset(m.id, 0, sizeof(m.id));
    if (std::strlen(rec.id) > kSceneMaxModuleIdChars) return false;
    copy_text(m.id, sizeof(m.id), rec.id);
    m.version = 1;
    m.parameter_count = 0;
    m.attribute_count = 0;
    if (slot == kSlotIn && input_desc_ != nullptr) m.version = input_desc_->version;
    else if (slot >= kSlotP1 && slot <= kSlotP6 && proc_desc(slot) != nullptr) m.version = proc_desc(slot)->version;
    for (std::uint8_t i = 0; i < rec.row_count; ++i) {
      const ParamRow& r = rec.rows[i];
      if (r.name == nullptr) continue;
      if (m.parameter_count >= kSceneMaxParamsPerModule || std::strlen(r.name) > kSceneMaxFieldNameChars) return false;
      SceneParameter& p = m.parameters[m.parameter_count++];
      std::memset(p.name, 0, sizeof(p.name));
      copy_text(p.name, sizeof(p.name), r.name);
      if (r.kind == RowKind::Decimal || r.kind == RowKind::Percent) {
        p.kind = SceneValueKind::Decimal; p.value.decimal = r.value_f;
      } else if (r.kind == RowKind::Boolean) {
        p.kind = SceneValueKind::Boolean; p.value.boolean = r.value_i != 0;
      } else {
        p.kind = SceneValueKind::Integer; p.value.integer = r.value_i;
      }
    }
    AttrCollect collect{&m, true};
    AttributeVisitor visitor{&collect, &collect_attribute};
    if (slot == kSlotIn && input_desc_ != nullptr && input_desc_->attributes != nullptr)
      input_desc_->attributes(input_state_, visitor);
    else if (slot >= kSlotP1 && slot <= kSlotP6 && proc_desc(slot) != nullptr && proc_desc(slot)->attributes != nullptr)
      proc_desc(slot)->attributes(*proc_state(slot), visitor);
    if (!collect.ok) return false;
  }
  return true;
}

bool Controller::scene_prepare() noexcept {
  // restore_stock_state_values + create_stock_chain_modules
  reset_globals();
  (void)transport_.set_bpm(global_.bpm);
  (void)transport_.set_swing(global_.swing);
  transport_.set_mode(global_.transport_mode, now_.us);
  slots_[kSlotOut].out_channel = 0;
  install_stock_chain();
  build_system_rows();
  return true;
}

bool Controller::scene_reset_defaults() noexcept {
  const bool ok = scene_prepare();
  boot_ui_state();
  return ok;
}

bool Controller::scene_apply_globals(const GlobalState& g, std::uint8_t fields) noexcept {
  if ((fields & SceneKey) != 0) global_.key = g.key;
  if ((fields & SceneScale) != 0) global_.scale = g.scale;
  if ((fields & SceneOctave) != 0) global_.octave = g.octave;
  if ((fields & SceneKeyOffset) != 0) global_.key_offset = g.key_offset;
  if ((fields & SceneBpm) != 0) global_.bpm = g.bpm;
  if ((fields & SceneSwing) != 0) global_.swing = g.swing;
  if ((fields & SceneTransportMode) != 0) global_.transport_mode = g.transport_mode;
  // SET parameters are created lazily after the S records in the source, so
  // their initial values reflect the loaded state (bpm/swing come from the transport).
  build_system_rows();
  return true;
}

bool Controller::scene_construct_module(std::uint8_t slot, const char* id, bool unknown) noexcept {
  if (slot >= kSlotCount) return false;
  if (slot == kSlotIn) return install_input(unknown ? "note" : id);
  if (slot >= kSlotP1 && slot <= kSlotP6) return install_processor(slot, unknown ? "empty" : id);
  return true;  // SET / OUT / SETTINGS are fixed
}

bool Controller::scene_apply_parameter(std::uint8_t slot, const SceneParameter& p) noexcept {
  if (slot >= kSlotCount) return false;
  SlotRecord& rec = slots_[slot];
  std::uint8_t index = 0xFF;
  for (std::uint8_t i = 0; i < rec.row_count; ++i)
    if (same(rec.rows[i].name, p.name)) { index = i; break; }
  if (index == 0xFF) return true;  // unknown parameters are tolerated
  ParamRow& row = rec.rows[index];
  std::int32_t iv = 0;
  float fv = 0.0f;
  switch (p.kind) {
    case SceneValueKind::Integer: iv = p.value.integer; fv = static_cast<float>(p.value.integer); break;
    case SceneValueKind::Decimal: fv = p.value.decimal; iv = static_cast<std::int32_t>(p.value.decimal); break;
    case SceneValueKind::Boolean: iv = p.value.boolean ? 1 : 0; break;
  }
  set_row_value_clamped(row, iv, fv);          // Parm.set_value
  run_edit_callback(slot, index);             // then edit callback
  run_exit_callback(slot, index);             // then exit callback
  return true;
}

bool Controller::scene_apply_attribute(std::uint8_t slot, const SceneAttribute& a) noexcept {
  AttrRestore restore{&a, false, false};
  AttributeVisitor visitor{&restore, &restore_attribute};
  if (slot == kSlotIn && input_desc_ != nullptr && input_desc_->attributes != nullptr)
    input_desc_->attributes(input_state_, visitor);
  else if (slot >= kSlotP1 && slot <= kSlotP6 && proc_desc(slot) != nullptr && proc_desc(slot)->attributes != nullptr)
    proc_desc(slot)->attributes(*proc_state(slot), visitor);
  else return true;
  return !restore.found || restore.ok;  // unknown attribute names are tolerated
}

bool Controller::scene_finish() noexcept {
  sync_linked(kSlotIn);
  for (std::uint8_t slot = kSlotP1; slot <= kSlotP6; ++slot) {
    sync_linked(slot);
    configure_chain(slot);
  }
  return true;
}

void Controller::boot_ui_state() noexcept {
  // Reboot emulation: Orion.__init__ state after load_active_scene.
  section_ = UiSection::Chain;
  chain_mode_ = UiChainMode::Select;
  chain_swap_mode_ = 0;
  active_chain_ = kSlotIn;
  active_parm_ = 0;
  selector_index_ = 0;
  nav_keys_state_ = 0;
  hold_active_ = false;
  encoder_down_ = false;
  consumed_ = 0;
  nav_colors_active_ = false;
  pixel_held_ = 0;
  held_keys_ = 0;
  frame_.keys.clear();
  paging_ = ParmPaging{};
  paging_.parm_count = 4;
  ui_queue_count_ = 0;
  build_settings_rows(slots_[kSlotSettings]);
  queue_ui(UiCb::RebuildParm);
  ui_dirty_ = true;
}

void Controller::scene_rebuild_ui() noexcept {
  boot_ui_state();
  paint_pixels();
}

bool Controller::scene_resolve(std::uint8_t slot, const char* id, SceneModuleSchema& out) noexcept {
  if (id == nullptr) return false;
  std::uint8_t count = 0;
  const ParamSpec* params = nullptr;
  std::uint8_t param_count = 0;
  SceneModuleKind kind = SceneModuleKind::Processor;
  std::uint16_t version = 1;
  bool routing = true;
  bool input_slot = false;
  if (slot == kSlotIn) {
    const InputDescriptor* d = find_input_module(id);
    if (d == nullptr) return false;
    params = d->params; param_count = d->param_count; version = d->version;
    kind = SceneModuleKind::Input; input_slot = true;
  } else if (slot >= kSlotP1 && slot <= kSlotP6) {
    const ModuleDescriptor* d = find_module(id);
    if (d == nullptr) return false;
    params = d->params; param_count = d->param_count; version = d->version;
    routing = !same(d->id, "empty");
  } else {
    return false;  // fixed schemas are provided by the codec
  }
  if (param_count + 3 > kMaxParamRows) return false;
  for (std::uint8_t i = 0; i < param_count; ++i) schema_params_[count++] = params[i];
  if (routing) {
    if (!input_slot) schema_params_[count++] = scene_enum_spec("source_mode", 0, 0, 6);
    schema_params_[count++] = scene_enum_spec("operation_mode", 0, 0, input_slot ? 2 : 4);
    schema_params_[count++] = scene_integer_spec("out_channel", 1, 1, 16);
  }
  const AttrTable attrs = attrs_for(id);
  out.id = id;
  out.version = version;
  out.kind = kind;
  out.params = schema_params_;
  out.param_count = count;
  out.attrs = attrs.specs;
  out.attr_count = attrs.count;
  // Return the descriptor's own id string so the schema never points at the caller's buffer.
  if (slot == kSlotIn) out.id = find_input_module(id)->id;
  else out.id = find_module(id)->id;
  return true;
}

// ---------------------------------------------------------------------------
// UI model
// ---------------------------------------------------------------------------
const UiModel& Controller::model() noexcept {
  flush_ui_queue();
  UiModel& m = model_;
  m.section = section_;
  m.chain_mode = chain_mode_;
  m.chain_swap_mode = chain_swap_mode_;
  m.active_chain = active_chain_;
  m.active_parm = active_parm_;
  const SlotRecord& active = slots_[active_chain_];
  m.parm_count = active.row_count;
  m.scroll_offset = paging_.scroll_offset;
  m.highlight_row = static_cast<std::int8_t>(static_cast<int>(active_parm_) - static_cast<int>(paging_.scroll_offset));
  std::uint8_t rows = 0;
  for (std::uint8_t i = 0; i < kParmsPerPage; ++i) {
    UiRow& ui = m.rows[i];
    ui = UiRow{};
    const std::uint8_t index = static_cast<std::uint8_t>(paging_.scroll_offset + i);
    if (index >= active.row_count) continue;
    const ParamRow& r = active.rows[index];
    ui.label = r.label;
    ui.kind = r.kind;
    ui.ivalue = r.value_i;
    ui.fvalue = r.value_f;
    ui.float_text = r.float_text;
    ui.options = r.options;
    ui.option_count = r.option_count;
    ui.armed = r.armed;
    ui.present = true;
    ++rows;
  }
  m.row_count = rows;
  for (std::uint8_t i = 0; i < kSlotCount; ++i) {
    m.slots[i].label = slots_[i].label;
    m.slots[i].empty = same(slots_[i].id, "empty");
  }
  m.selector_index = selector_index_;
  m.selector_label = active_chain_ == kSlotIn ? input_selector(selector_index_).label
                                              : processor_selector(selector_index_).label;
  m.nav_keys_state = nav_keys_state_;
  m.transport_running = transport_.running();
  m.transport_mode = global_.transport_mode;
  m.key = global_.key;
  m.scale = global_.scale;
  m.octave = global_.octave;
  m.key_custom_text = same(slots_[kSlotIn].id, "drum") ? "DRUM" : nullptr;
  m.active_scene = scene_.active_scene();
  m.key_brightness = brightness_;
  for (std::uint8_t i = 0; i < kKeys; ++i) m.pixels[i] = pixel_out_[i];
  m.encoder_down = encoder_down_;
  m.activity = activity_;
  m.recovery = output_.recovering() || scene_failed();
  m.busy = scene_.busy();
  publish_failure_pixels(m);
  model_valid_ = true;
  return m;
}

bool Controller::publish_ui(UiSnapshotQueue<UiModel>& queue, std::uint32_t epoch, std::uint32_t now_ms) noexcept {
  if (!ui_dirty_ && published_once_) return false;
  if (published_once_ && elapsed(now_ms, last_publish_ms_) < 33U) return false;  // <=30 Hz
  UiSnapshot<UiModel> snap{};
  snap.revision = revision_ + 1U;
  snap.epoch = epoch;
  snap.model = model();
  if (!queue.try_push(snap)) return false;  // full: retry next frame with the newest state
  ++revision_;
  ui_dirty_ = false;
  published_once_ = true;
  last_publish_ms_ = now_ms;
  return true;
}

}  // namespace orbit
