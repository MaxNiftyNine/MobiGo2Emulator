#pragma once

#include "common.hpp"

namespace mobigo::desktop {

// The launcher intentionally keeps its data model separate from SDL widgets.
// This makes configuration and library behavior testable on headless builders.
struct LibraryEntry {
  std::filesystem::path path;

  bool operator==(const LibraryEntry &) const = default;
};

struct FrontendConfig {
  static constexpr unsigned kSchemaVersion = 3;

  std::filesystem::path rom;
  std::filesystem::path spi;
  std::filesystem::path nand;
  std::filesystem::path last_directory;
  std::vector<LibraryEntry> library;

  int window_scale = 2;
  bool audio = true;
  bool show_speed = true;
  bool fullscreen = false;
  bool integer_scaling = true;
  InputBindings input_bindings;

  // Developer settings mirror the useful diagnostic and board-selection
  // CLI switches, but live behind the Advanced page in the launcher.
  std::string rom_endian = "le";
  uint64_t max_steps = 0;
  uint64_t open_window_at = 0;
  uint64_t trace_limit = 100000;
  uint64_t trace_transition_limit = 100000;
  uint64_t start_logging_at = 0;
  uint64_t trace_start_insn = 0;
  uint64_t dump_frame_interval = 0;
  uint32_t start_pc = 0;
  uint32_t trace_lo = 0;
  uint32_t trace_hi = 0;
  uint32_t dump_code_base = 0;
  uint32_t dump_code_words = 0;
  bool start_pc_set = false;
  bool trace_range = false;
  bool log = false;
  bool trace = false;
  bool trace_transitions = false;
  bool dump_memory_dma = false;
  bool rom_shadow_low = false;
  bool rom_fetch_mirror64 = false;
  bool allow_invalid_alu_nop = false;
  bool auto_power_wake = false;
  std::filesystem::path log_path = "emulator.log";
  std::filesystem::path dump_frame;
  std::filesystem::path dump_current_frame;
  std::filesystem::path dump_frame_dir;
  std::filesystem::path dump_memory;
  std::filesystem::path dump_code;
  uint32_t dump_memory_base = 0;
  uint32_t dump_memory_words = 0;
  uint32_t rom_base = 0x008000;
  uint16_t efuse0 = 0;
  uint16_t efuse1 = 0;
  uint16_t efuse2 = 0x0300;
  uint16_t gpio_a = 0x7fff;
  uint16_t gpio_b = 0xfffe;
  uint16_t gpio_c = 0xfeff;
  uint16_t gpio_d = 0xffff;
  uint16_t gpio_e = 0x0000;
  uint16_t battery_adc = 0x0500;
};

enum class FirmwareFileState {
  Ready,
  Missing,
  WrongSize,
};

struct FirmwareStatus {
  FirmwareFileState rom = FirmwareFileState::Missing;
  FirmwareFileState spi = FirmwareFileState::Missing;
  FirmwareFileState nand = FirmwareFileState::Missing;

  [[nodiscard]] bool ready() const {
    return rom == FirmwareFileState::Ready && spi == FirmwareFileState::Ready &&
           nand == FirmwareFileState::Ready;
  }
  [[nodiscard]] std::string message() const;
};

enum class LaunchAction {
  Quit,
  Boot,
};

struct LaunchRequest {
  LaunchAction action = LaunchAction::Quit;
  Options options;
};

enum class ValidationSection {
  Advanced,
  Diagnostics,
  Outputs,
};

struct LaunchValidationIssue {
  std::string message;
  ValidationSection section = ValidationSection::Diagnostics;
};

[[nodiscard]] bool should_open_launcher(int argc, bool explicitly_disabled);
[[nodiscard]] bool launcher_disabled_by_environment();
[[nodiscard]] std::filesystem::path user_data_directory();
[[nodiscard]] std::filesystem::path default_config_path();
[[nodiscard]] FrontendConfig config_from_defaults(const Options &defaults);
[[nodiscard]] FrontendConfig load_config(const std::filesystem::path &path,
                                         const FrontendConfig &fallback);
void save_config(const std::filesystem::path &path,
                 const FrontendConfig &config);
void remember_game(FrontendConfig &config, const LibraryEntry &entry,
                   size_t maximum_entries = 24);
[[nodiscard]] FirmwareStatus firmware_status(const FrontendConfig &config);
[[nodiscard]] std::optional<LaunchValidationIssue>
launch_validation_issue(const FrontendConfig &config);
[[nodiscard]] std::optional<std::string>
launch_validation_error(const FrontendConfig &config);
[[nodiscard]] Options
make_launch_options(const Options &defaults, const FrontendConfig &config,
                    const std::optional<LibraryEntry> &game);
[[nodiscard]] uint64_t parse_ui_unsigned(std::string_view text);

// Opens the SDL-only desktop launcher. It never mutates command-line options;
// main() calls it only for a genuine no-argument desktop launch.
[[nodiscard]] LaunchRequest run_launcher(const Options &packaged_defaults,
                                         const char *argv0);

} // namespace mobigo::desktop
