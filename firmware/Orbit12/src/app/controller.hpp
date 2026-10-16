#pragma once

// T11b core-0 controller. Host-compilable: no Arduino includes, no heap, no
// RTTI. Owns the authoritative musical/UI state: global key/scale/octave,
// transport, output stage, input module, six processor slots, parameter
// shadows, UI focus, scene actions and the SETTINGS/SET/OUT slot controls.
//
// Event flow (core 0): PhysicalEventBridge -> handle_event (sequential, in
// order) -> step (transport, input, chain, output) -> flush. UI snapshots are
// copied out by publish_ui(). Stale-epoch events are rejected by the bridge;
// after a scene transition the controller advances the epoch and the UI is
// resynced by the next snapshot.
//
// Oracle: circuitpython-source/src/apps/Orion/{Orion.py,core/state.py,
// core/ui.py,core/scenes.py,core/_modules/*.py}. Deviations are listed in
// docs-facing comments marked DEVIATION and in the T11b status entry.

#include <cstddef>
#include <cstdint>

#include "app/bridge.hpp"
#include "app/ui_model.hpp"
#include "domain/chain.hpp"
#include "domain/modules/chords_input.hpp"
#include "domain/modules/input_registry.hpp"
#include "domain/modules/matrix_input.hpp"
#include "domain/modules/registry.hpp"
#include "domain/modules/walk_inputs.hpp"
#include "domain/output.hpp"
#include "domain/transport.hpp"
#include "persistence/scene_service.hpp"

namespace orbit {

constexpr std::uint8_t kMaxParamRows = 20;
constexpr std::uint32_t kSwapHoldMs = 1000;

// Opens/closes the storage write window (LittleFS refuses writes outside it). The
// controller opens it when a quiesce is acknowledged and closes it on resume/cancel.
struct StorageWindowPort {
  void* context = nullptr;
  void (*set_open)(void* context, bool open) = nullptr;
};

struct ControllerPorts {
  SceneStoragePort storage{};              // unset: scene operations are rejected
  MaintenanceHandshake* maintenance = nullptr;  // null: single-threaded, immediate quiesce
  StorageWindowPort window{};              // null set_open: no window gate (host tests)
  RandomSource random{};                   // null next: internal xorshift
};

struct PollArgs {
  TimeSample now{};
  const MidiInput* midi_in = nullptr;      // external clock/transport input, oldest first
  std::size_t midi_in_count = 0;
  const MidiPort* port = nullptr;          // null: nothing is flushed
  bool midi_connected = true;
};

struct PollResult {
  std::uint16_t midi_consumed = 0;         // messages the caller may drop from its queue
  std::uint16_t events_handled = 0;
  std::uint16_t packets_sent = 0;
};

struct ControllerDiagnostics {
  std::uint32_t emergency_releases = 0;    // bridge fault / gap / resync recovery
  std::uint32_t events_gated = 0;          // dropped by epoch or awaiting-resync gate
  std::uint32_t events_busy = 0;           // dropped during a scene operation
  std::uint32_t resync_releases = 0;       // key-ups synthesized from an authoritative mask
  std::uint32_t param_rejected = 0;        // set_param refused a validated value
  std::uint32_t chain_recoveries = 0;      // collector/chain overflow recovery
  std::uint32_t scene_failures = 0;
  std::uint32_t scene_requests_ignored = 0;
  std::uint32_t release_calls = 0;         // Output::all_notes_off invocations (source calls + deviations)
  std::uint32_t deviation_releases = 0;    // calls the source does not make (safety)
  std::uint32_t boot_default_fallbacks = 0;  // boot load failed: stock defaults applied in memory
  std::uint32_t settings_saves = 0;        // SaveUserSettings requests accepted
  std::uint32_t settings_load_failures = 0;
  std::uint32_t settings_save_failures = 0;
};

enum class ParamSpecial : std::uint8_t {
  None,  // module descriptor parameter (index = descriptor index)
  SetKey, SetScale, SetBpm, SetSwing, SetTransportMode,
  OutChannel, OutPrint,
  SettingsScene, SettingsSave, SettingsCopy, SettingsReset, SettingsBrightness,
  RouteSource, RouteOperation, RouteChannel,
  EmptyPick, MatrixReset
};

struct ParamRow {
  const char* label = "";
  const char* name = nullptr;            // persisted name; null: not persisted
  RowKind kind = RowKind::Integer;
  ParamSpecial special = ParamSpecial::None;
  ParamType spec_type = ParamType::Integer;  // tag used when calling set_param
  std::uint8_t index = 0;                // descriptor parameter index
  GlobalBinding binding = GlobalBinding::None;
  bool bounded = false;
  bool armed = false;
  bool float_text = false;  // Python value type: float after scene restore or a fractional edit
  const char* const* options = nullptr;
  std::uint8_t option_count = 0;
  std::int32_t value_i = 0, min_i = 0, max_i = 0, step_i = 1;
  float value_f = 0.0f, min_f = 0.0f, max_f = 0.0f, step_f = 1.0f;
};

struct SlotRecord {
  const char* id = "empty";
  const char* label = "EMPTY";
  ParamRow rows[kMaxParamRows]{};
  std::uint8_t row_count = 0;
  std::uint8_t source_mode = 0;     // SRC row value (0 PREV, 1 IN, 2..6 S1..S5)
  std::uint8_t operation_mode = 0;  // OP row value
  std::uint8_t out_channel = 0;     // zero-based
};

class Controller {
 public:
  explicit Controller(const ControllerPorts& ports = {}) noexcept;
  Controller(const Controller&) = delete;
  Controller& operator=(const Controller&) = delete;

  // Stock state: SET/OUT/SETTINGS rows, Note input, six Empty slots, defaults.
  void init(TimeSample now) noexcept;

  // One core-0 pass: consume <=64 ordered bridge events, then step().
  PollResult poll(PhysicalEventBridge& bridge, const PollArgs& args) noexcept;
  // Event handling alone (tests and the bridge consumer use this).
  void handle_event(const PhysicalEvent& event) noexcept;
  // Transport, input, chain, output, scene service. Call every pass.
  PollResult step(const PollArgs& args) noexcept;

  // Publishes a snapshot when the model changed; at most one per 33 ms.
  bool publish_ui(UiSnapshotQueue<UiModel>& queue, std::uint32_t epoch, std::uint32_t now_ms) noexcept;
  const UiModel& model() noexcept;  // flushes the pending source UI queue

  // Scene actions (SETTINGS slot calls these; also usable by the app).
  bool request_load_active() noexcept;
  bool request_scene_save() noexcept;
  bool request_scene_switch(std::uint8_t scene) noexcept;
  bool request_scene_copy(std::uint8_t scene) noexcept;
  bool request_scene_reset() noexcept;
  // Scene Failed phase (rollback after a failed switch also failed): re-applies the
  // committed scene in place. False when the service is not Failed. Never resets the MCU.
  bool request_scene_recovery() noexcept;

  // Boot (core 0, once, after both cores are ready): loads user settings (KEYBRI), then the
  // active scene. Nonblocking: progress happens in step(). A failed or missing scene leaves the
  // stock defaults in place. Nothing is written except through the normal scene protocol.
  bool begin_boot() noexcept;
  bool boot_complete() const noexcept { return boot_stage_ == BootStage::Idle; }

  // Maintenance hold: stop and release everything, then ignore musical input until
  // leave_maintenance(). enter returns false while a scene operation runs.
  bool enter_maintenance() noexcept;
  void leave_maintenance() noexcept;
  bool maintenance_hold() const noexcept { return maintenance_hold_; }
  // True once the transport is stopped and every MIDI packet has been accepted or cleared.
  bool is_stopped() const noexcept { return scene_is_stopped(); }
  SceneService& scene_service() noexcept { return scene_; }
  SceneSchemaResolver scene_schema() noexcept;
  bool scene_failed() const noexcept { return scene_.phase() == SceneServicePhase::Failed; }

  // Safety entry points.
  void panic() noexcept;   // all notes off + reset every module (Panic)

  // Introspection.
  const GlobalState& global() const noexcept { return global_; }
  const Transport& transport() const noexcept { return transport_; }
  const Output& output() const noexcept { return output_; }
  Output& output() noexcept { return output_; }
  const Chain& chain() const noexcept { return chain_; }
  Chain& chain() noexcept { return chain_; }
  const InputState& input_state() const noexcept { return input_state_; }
  InputState& input_state() noexcept { return input_state_; }
  const SceneService& scene_service() const noexcept { return scene_; }
  UiSection section() const noexcept { return section_; }
  std::uint8_t active_chain() const noexcept { return active_chain_; }
  std::int8_t active_parm() const noexcept { return active_parm_; }
  std::uint8_t nav_keys_state() const noexcept { return nav_keys_state_; }
  std::uint8_t chain_swap_mode() const noexcept { return chain_swap_mode_; }
  UiChainMode chain_mode() const noexcept { return chain_mode_; }
  bool encoder_down() const noexcept { return encoder_down_; }
  std::uint16_t held_keys() const noexcept { return held_keys_; }
  std::uint8_t selector_index() const noexcept { return selector_index_; }
  const SlotRecord& slot(std::uint8_t index) const noexcept { return slots_[index < kSlotCount ? index : 0]; }
  const ParamRow* find_row(std::uint8_t slot_index, const char* name) const noexcept;
  const char* slot_module_id(std::uint8_t slot_index) const noexcept { return slot(slot_index).id; }
  const ControllerDiagnostics& diagnostics() const noexcept { return diag_; }
  std::uint8_t active_scene() const noexcept { return scene_.active_scene(); }
  float key_brightness() const noexcept { return brightness_; }
  bool take_user_settings_dirty() noexcept { const bool d = user_settings_dirty_; user_settings_dirty_ = false; return d; }
  SceneServiceStatus last_scene_status() const noexcept { return last_scene_status_; }
  bool scene_busy() const noexcept { return scene_.busy(); }
  std::uint32_t epoch() const noexcept { return epoch_; }

 private:
  enum class UiCb : std::uint8_t { RebuildParm, UpdateParmSelection };
  enum class EnterResult : std::uint8_t { StayInEdit, ReturnToSelection };

  // ---- construction / slots ----
  void reset_globals() noexcept;
  void build_system_rows() noexcept;
  void build_settings_rows(SlotRecord& rec) const noexcept;
  void build_module_rows(std::uint8_t slot, const ParamSpec* params, std::uint8_t count,
                         bool routing, bool input_slot) noexcept;
  bool install_input(const char* id) noexcept;
  bool install_processor(std::uint8_t slot, const char* id) noexcept;
  void install_stock_chain() noexcept;
  void configure_chain(std::uint8_t slot) noexcept;
  void seed_running_flags(std::uint8_t slot) noexcept;
  ModuleState* proc_state(std::uint8_t slot) noexcept { return chain_.state(static_cast<std::uint8_t>(slot - kSlotP1)); }
  const ModuleDescriptor* proc_desc(std::uint8_t slot) const noexcept { return chain_.descriptor(static_cast<std::uint8_t>(slot - kSlotP1)); }

  // ---- parameters ----
  static void init_row_from_spec(ParamRow& row, const ParamSpec& spec, std::uint8_t index,
                                 const char* module_id, const GlobalState& g) noexcept;
  ParamRow* active_row() noexcept;
  void edit_row(ParamRow& row, int delta) noexcept;
  void set_row_value_clamped(ParamRow& row, std::int32_t i, float f) noexcept;
  void run_edit_callback(std::uint8_t slot, std::uint8_t row_index) noexcept;
  void run_exit_callback(std::uint8_t slot, std::uint8_t row_index) noexcept;
  EnterResult run_enter_callback(std::uint8_t slot, std::uint8_t row_index) noexcept;
  bool push_module_param(std::uint8_t slot, std::uint8_t row_index) noexcept;
  void apply_effects(std::uint8_t slot, std::uint8_t effects) noexcept;
  void sync_linked(std::uint8_t slot) noexcept;
  void apply_global_binding(const ParamRow& row) noexcept;
  void release_all(std::int16_t channel = -1, bool deviation = false) noexcept;
  void set_out_channel_value(std::uint8_t slot, std::int32_t channel_one_based) noexcept;

  // ---- navigation (Orion.py) ----
  void move_active_chain_element(int delta) noexcept;
  void state_move_active_chain(int delta) noexcept;
  void move_active_parm_element(int delta) noexcept;
  void enter_parm_edit() noexcept;
  void exit_parm_edit() noexcept;
  void edit_parm(int delta) noexcept;
  void enter_parm_selection() noexcept;
  void enter_chain_view() noexcept;
  void jump_to_slot_parm_selection(std::uint8_t slot) noexcept;
  void enter_module_selection() noexcept;
  void move_module_selection(int delta) noexcept;
  void exit_module_selection() noexcept;
  bool apply_module_selection() noexcept;  // false: load failed; true: loaded or unchanged
  void enable_chain_swap_mode() noexcept;
  void disable_chain_swap_mode() noexcept;
  void swap_modules(std::uint8_t a, std::uint8_t b) noexcept;
  void queue_ui(UiCb cb) noexcept;
  void flush_ui_queue() noexcept;
  std::uint8_t parm_count_of(std::uint8_t slot) const noexcept { return slots_[slot].row_count; }

  // ---- input events ----
  void on_encoder_delta(int delta) noexcept;
  void on_encoder_down() noexcept;
  void on_encoder_up() noexcept;
  void on_key_down(std::uint8_t key) noexcept;
  void on_key_up(std::uint8_t key) noexcept;
  void shortcut(std::uint8_t key) noexcept;
  void nav_notes_adjust(std::uint8_t key) noexcept;
  void nav_parms(std::uint8_t key) noexcept;
  void sync_input_global_rows() noexcept;
  void tick_timers(std::uint32_t now_ms) noexcept;
  void on_resync(std::uint16_t mask) noexcept;
  void emergency_release(PhysicalEventBridge* bridge) noexcept;

  // ---- engine ----
  void start_clock() noexcept;
  void stop_clock() noexcept;
  void emit_realtime(const RealtimeEvents& events, bool clocks_too) noexcept;
  void stop_all_modules() noexcept;
  void recovery_reset() noexcept;
  void process_pass(const PollArgs& args, const TransportUpdate& update) noexcept;
  void refresh_follow_flags() noexcept;
  std::uint32_t next_random() noexcept;
  static std::uint32_t random_thunk(void* ctx) noexcept;

  // ---- pixels (neo_pixels.py) ----
  void paint_pixels() noexcept;
  void paint_pixel(std::uint8_t pad, double brightness) noexcept;
  void set_key_colors(const RgbColor* colors) noexcept;
  void set_held_pixel(std::uint8_t pad) noexcept;
  void release_held_pixel(std::uint8_t pad) noexcept;
  void set_nav_state_colors(bool update) noexcept;
  void exit_nav_state_colors() noexcept;
  void apply_view_colors(const InputView& view) noexcept;

  // ---- scene lifecycle (SceneLifecyclePort / SceneSchemaResolver) ----
  friend struct SceneBridge;
  bool scene_is_stopped() const noexcept;
  void scene_stop_release_drain() noexcept;
  bool scene_capture(SceneDto& dto) noexcept;
  bool scene_reset_defaults() noexcept;
  bool scene_prepare() noexcept;
  bool scene_apply_globals(const GlobalState& g, std::uint8_t fields) noexcept;
  bool scene_construct_module(std::uint8_t slot, const char* id, bool unknown) noexcept;
  bool scene_apply_parameter(std::uint8_t slot, const SceneParameter& p) noexcept;
  bool scene_apply_attribute(std::uint8_t slot, const SceneAttribute& a) noexcept;
  bool scene_finish() noexcept;
  void scene_rebuild_ui() noexcept;
  bool scene_resolve(std::uint8_t slot, const char* id, SceneModuleSchema& out) noexcept;
  void boot_ui_state() noexcept;
  void note_scene_result() noexcept;

  // ---- state ----
  GlobalState global_{};
  Transport transport_;
  Output output_{};
  Chain chain_{};
  OutputCollector collector_{};
  InputState input_state_{};
  const InputDescriptor* input_desc_ = nullptr;
  InputView input_view_{};
  InputFrame frame_{};
  RandomSource random_{};
  std::uint32_t rng_state_ = 0x2545F491U;
  SlotRecord slots_[kSlotCount]{};
  ParamSpec schema_params_[kMaxParamRows]{};  // resolver scratch (one schema live at a time)
  TimeSample now_{};
  ControllerDiagnostics diag_{};
  UiModel model_{};
  bool model_valid_ = false;
  bool ui_dirty_ = true;
  std::uint32_t revision_ = 0;
  std::uint32_t last_publish_ms_ = 0;
  bool published_once_ = false;

  // UI focus (state.py)
  UiSection section_ = UiSection::Chain;
  UiChainMode chain_mode_ = UiChainMode::Select;
  std::uint8_t active_chain_ = kSlotIn;
  std::int8_t active_parm_ = 0;
  std::uint8_t selector_index_ = 0;
  std::uint8_t nav_keys_state_ = 0;
  ParmPaging paging_{};
  UiCb ui_queue_[4]{};
  std::uint8_t ui_queue_count_ = 0;

  // Orion.py loop state
  std::uint8_t chain_swap_mode_ = 0;  // 0 off, 1 pending, 2 active
  bool encoder_down_ = false;
  std::uint8_t consumed_ = 0;         // encoder_press_consumed: 0 none/0, 1 consumed
  bool hold_active_ = false;          // knob_hold_down_start is not None
  std::uint32_t hold_start_ms_ = 0;
  std::uint16_t held_keys_ = 0;       // note keys currently sent to the input module
  std::uint32_t activity_ = 0;
  std::uint32_t event_ms_ = 0;       // timestamp of the event being handled
  std::uint16_t tr_bpm_ = 120;       // value last given to Transport (SET bpm row source)
  float tr_swing_ = 0.0f;

  // bridge gating
  bool gated_ = false;
  bool epoch_pending_ = false;
  std::uint32_t epoch_ = 0;
  bool recovery_handled_ = false;

  // pixels
  RgbColor pixel_colors_[kKeys]{};
  RgbColor pixel_temp_[kKeys]{};
  RgbColor pixel_out_[kKeys]{};
  std::uint16_t pixel_held_ = 0;
  bool nav_colors_active_ = false;
  float brightness_ = 1.0f;
  bool user_settings_dirty_ = false;

  // scene plumbing
  enum class BootStage : std::uint8_t { Idle, Settings, Scene };
  enum class SettingsOp : std::uint8_t { None, Load, Save };
  void set_storage_window(bool open) noexcept;
  void finish_scene_operation(bool ok) noexcept;
  void service_settings_save() noexcept;
  bool editing_brightness() const noexcept;
  void publish_failure_pixels(UiModel& model) const noexcept;
  MaintenanceHandshake* maintenance_ = nullptr;
  StorageWindowPort window_{};
  std::uint32_t local_quiesce_seq_ = 0;
  SceneStoragePort storage_{};
  SceneService scene_;
  SceneServiceStatus last_scene_status_ = SceneServiceStatus::Idle;
  bool scene_was_busy_ = false;
  std::uint32_t failures_seen_ = 0;        // SceneService::failure_count() last folded in
  BootStage boot_stage_ = BootStage::Idle;
  SettingsOp settings_op_ = SettingsOp::None;
  bool settings_save_pending_ = false;     // KEYBRI left its page; save once stopped
  bool maintenance_hold_ = false;
};

}  // namespace orbit
