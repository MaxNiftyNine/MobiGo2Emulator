#include "desktop_frontend.hpp"

#include <charconv>
#include <limits>
#include <type_traits>

namespace mobigo::desktop {
namespace {

template <typename Integer>
Integer bounded(Integer value, Integer minimum, Integer maximum) {
  return std::clamp(value, minimum, maximum);
}

template <typename Unsigned>
bool parse_config_unsigned(std::string_view text, Unsigned &target) {
  static_assert(std::is_unsigned_v<Unsigned>);
  if (text.empty() || text.front() == '+' || text.front() == '-')
    return false;
  uint64_t value = 0;
  const auto parsed =
      std::from_chars(text.data(), text.data() + text.size(), value, 10);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
      value > std::numeric_limits<Unsigned>::max())
    return false;
  target = static_cast<Unsigned>(value);
  return true;
}

template <typename Unsigned>
void read_config_unsigned(std::istringstream &stream, Unsigned &target) {
  std::string token;
  if (!(stream >> token))
    return;
  Unsigned parsed{};
  if (parse_config_unsigned(token, parsed))
    target = parsed;
}

void read_config_int(std::istringstream &stream, int &target) {
  std::string token;
  if (!(stream >> token))
    return;
  unsigned int parsed = 0;
  if (parse_config_unsigned(token, parsed) &&
      parsed <= static_cast<unsigned int>(std::numeric_limits<int>::max()))
    target = static_cast<int>(parsed);
}

void read_config_bool(std::istringstream &stream, bool &target) {
  std::string token;
  if (!(stream >> token))
    return;
  unsigned int parsed = 0;
  if (parse_config_unsigned(token, parsed) && parsed <= 1)
    target = parsed != 0;
}

FirmwareFileState firmware_file_state(const std::filesystem::path &path,
                                      uintmax_t expected_size) {
  std::error_code error;
  if (!std::filesystem::is_regular_file(path, error) || error)
    return FirmwareFileState::Missing;
  const uintmax_t size = std::filesystem::file_size(path, error);
  if (error || size != expected_size)
    return FirmwareFileState::WrongSize;
  return FirmwareFileState::Ready;
}

std::string quoted_path(const std::filesystem::path &path) {
  std::ostringstream out;
  out << std::quoted(path_to_utf8(path));
  return out.str();
}

void sanitize(FrontendConfig &config) {
  config.window_scale = bounded(config.window_scale, 1, 6);
  config.battery_adc &= 0x0fff;
  config.start_pc &= kAddrMask;
  config.trace_lo &= kAddrMask;
  config.trace_hi = std::min(config.trace_hi, kWordCount);
  config.dump_memory_base &= kAddrMask;
  config.dump_code_base &= kAddrMask;
  if (config.rom_base > kAddrMask)
    config.rom_base = 0x008000;
  if (config.rom_endian != "le" && config.rom_endian != "be")
    config.rom_endian = "le";
  if (config.trace_range)
    config.trace = true;
  if (config.trace || config.trace_transitions)
    config.log = true;
  if (config.trace && config.trace_limit == 0)
    config.trace_limit = 100000;
  if (config.trace_transitions && config.trace_transition_limit == 0)
    config.trace_transition_limit = 100000;

  std::vector<LibraryEntry> cleaned;
  cleaned.reserve(config.library.size());
  for (const LibraryEntry &entry : config.library) {
    if (entry.path.empty())
      continue;
    const auto duplicate = std::find_if(cleaned.begin(), cleaned.end(),
                                        [&](const LibraryEntry &candidate) {
                                          return candidate.path == entry.path;
                                        });
    if (duplicate == cleaned.end())
      cleaned.push_back(entry);
    if (cleaned.size() == 24)
      break;
  }
  config.library = std::move(cleaned);
}

} // namespace

std::string FirmwareStatus::message() const {
  if (ready())
    return "Firmware ready";
  std::string details;
  auto append = [&](FirmwareFileState state, std::string_view name) {
    if (state == FirmwareFileState::Ready)
      return;
    if (!details.empty())
      details += ", ";
    details += name;
    details +=
        state == FirmwareFileState::Missing ? " missing" : " has wrong size";
  };
  append(rom, "internal ROM");
  append(spi, "SPI image");
  append(nand, "NAND image");
  return details;
}

bool should_open_launcher(int argc, bool explicitly_disabled) {
#ifdef __EMSCRIPTEN__
  (void)argc;
  (void)explicitly_disabled;
  return false;
#else
  return argc == 1 && !explicitly_disabled;
#endif
}

bool launcher_disabled_by_environment() {
  const char *value = std::getenv("MOBIGO2_NO_LAUNCHER");
  if (!value)
    return false;
  const std::string normalized = ascii_lower(value);
  return normalized == "1" || normalized == "true" || normalized == "yes";
}

std::filesystem::path user_data_directory() {
#ifdef __EMSCRIPTEN__
  return "/mobigo2";
#else
  char *raw = SDL_GetPrefPath("MobiGo2", "Emulator");
  if (!raw)
    return std::filesystem::current_path() / ".mobigo2";
  const std::filesystem::path result = path_from_utf8(raw);
  SDL_free(raw);
  return result;
#endif
}

std::filesystem::path default_config_path() {
  return user_data_directory() / "frontend.conf";
}

FrontendConfig config_from_defaults(const Options &defaults) {
  FrontendConfig config;
  config.rom = defaults.rom;
  config.spi = defaults.spi;
  config.nand = defaults.nand;
  config.audio = true;
  config.show_speed = true;
  config.input_bindings = defaults.input_bindings;
  config.rom_endian = defaults.rom_endian;
  config.max_steps = defaults.max_steps;
  config.open_window_at = defaults.open_window_at;
  config.trace_limit = defaults.trace_limit ? defaults.trace_limit : 100000;
  config.trace_transition_limit = defaults.trace_transition_limit
                                      ? defaults.trace_transition_limit
                                      : 100000;
  config.start_logging_at = defaults.start_logging_at;
  config.trace_start_insn = defaults.trace_start_insn;
  config.dump_frame_interval = defaults.dump_frame_interval;
  config.start_pc = defaults.start_pc;
  config.trace_lo = defaults.trace_lo;
  config.trace_hi = defaults.trace_hi;
  config.dump_code_base = defaults.dump_code_base;
  config.dump_code_words = defaults.dump_code_words;
  config.start_pc_set = defaults.start_pc_set;
  config.trace_range = defaults.trace_range;
  config.log = defaults.log;
  config.trace = defaults.trace;
  config.trace_transitions = defaults.trace_transitions;
  config.dump_memory_dma = defaults.dump_memory_dma;
  config.rom_shadow_low = defaults.rom_shadow_low;
  config.rom_fetch_mirror64 = defaults.rom_fetch_mirror64;
  config.allow_invalid_alu_nop = defaults.allow_invalid_alu_nop;
  config.auto_power_wake = defaults.auto_power_wake;
  config.log_path = defaults.log_path;
  config.dump_frame = defaults.dump_frame;
  config.dump_current_frame = defaults.dump_current_frame;
  config.dump_frame_dir = defaults.dump_frame_dir;
  config.dump_memory = defaults.dump_memory;
  config.dump_code = defaults.dump_code;
  config.dump_memory_base = defaults.dump_memory_base;
  config.dump_memory_words = defaults.dump_memory_words;
  config.rom_base = defaults.rom_base;
  config.efuse0 = defaults.efuse0;
  config.efuse1 = defaults.efuse1;
  config.efuse2 = defaults.efuse2;
  config.gpio_a = defaults.gpio_a;
  config.gpio_b = defaults.gpio_b;
  config.gpio_c = defaults.gpio_c;
  config.gpio_d = defaults.gpio_d;
  config.gpio_e = defaults.gpio_e;
  config.battery_adc = defaults.battery_adc;
  return config;
}

FrontendConfig load_config(const std::filesystem::path &path,
                           const FrontendConfig &fallback) {
  FrontendConfig config = fallback;
  std::ifstream input(path);
  if (!input)
    return config;

  std::string line;
  while (std::getline(input, line)) {
    std::istringstream stream(line);
    std::string key;
    stream >> key;
    if (key.empty() || key.front() == '#')
      continue;
    try {
      auto read_path = [&](std::filesystem::path &target) {
        std::string value;
        if (stream >> std::quoted(value))
          target = path_from_utf8(value);
      };
      for (size_t index = 0; index < kBindableControlCount; ++index) {
        if (key != kBindableControlConfigKeys[index])
          continue;
        std::string token;
        uint32_t value = 0;
        if (stream >> token && parse_config_unsigned(token, value))
          config.input_bindings.keys[index] = static_cast<SDL_Keycode>(value);
        key.clear();
        break;
      }
      if (key.empty())
        continue;
      if (key == "rom")
        read_path(config.rom);
      else if (key == "spi")
        read_path(config.spi);
      else if (key == "nand")
        read_path(config.nand);
      else if (key == "last_directory")
        read_path(config.last_directory);
      else if (key == "library") {
        std::string value;
        if (stream >> std::quoted(value)) {
          // Schema 1 stored `library <kind> <path>`. Preserve old cartridge
          // entries but intentionally discard its retired MBA launcher rows.
          if (value == "0" || value == "1") {
            std::string legacy_path;
            if (stream >> std::quoted(legacy_path) && value == "0")
              config.library.push_back({path_from_utf8(legacy_path)});
          } else {
            config.library.push_back({path_from_utf8(value)});
          }
        }
      }
      else if (key == "window_scale")
        read_config_int(stream, config.window_scale);
      else if (key == "audio")
        read_config_bool(stream, config.audio);
      else if (key == "show_speed")
        read_config_bool(stream, config.show_speed);
      else if (key == "fullscreen")
        read_config_bool(stream, config.fullscreen);
      else if (key == "integer_scaling")
        read_config_bool(stream, config.integer_scaling);
      else if (key == "rom_endian")
        stream >> config.rom_endian;
      else if (key == "max_steps")
        read_config_unsigned(stream, config.max_steps);
      else if (key == "open_window_at")
        read_config_unsigned(stream, config.open_window_at);
      else if (key == "trace_limit")
        read_config_unsigned(stream, config.trace_limit);
      else if (key == "trace_transition_limit")
        read_config_unsigned(stream, config.trace_transition_limit);
      else if (key == "start_logging_at")
        read_config_unsigned(stream, config.start_logging_at);
      else if (key == "trace_start_insn")
        read_config_unsigned(stream, config.trace_start_insn);
      else if (key == "dump_frame_interval")
        read_config_unsigned(stream, config.dump_frame_interval);
      else if (key == "start_pc")
        read_config_unsigned(stream, config.start_pc);
      else if (key == "trace_lo")
        read_config_unsigned(stream, config.trace_lo);
      else if (key == "trace_hi")
        read_config_unsigned(stream, config.trace_hi);
      else if (key == "dump_code_base")
        read_config_unsigned(stream, config.dump_code_base);
      else if (key == "dump_code_words")
        read_config_unsigned(stream, config.dump_code_words);
      else if (key == "start_pc_set")
        read_config_bool(stream, config.start_pc_set);
      else if (key == "trace_range")
        read_config_bool(stream, config.trace_range);
      else if (key == "log")
        read_config_bool(stream, config.log);
      else if (key == "trace")
        read_config_bool(stream, config.trace);
      else if (key == "trace_transitions")
        read_config_bool(stream, config.trace_transitions);
      else if (key == "dump_memory_dma")
        read_config_bool(stream, config.dump_memory_dma);
      else if (key == "rom_shadow_low")
        read_config_bool(stream, config.rom_shadow_low);
      else if (key == "rom_fetch_mirror64")
        read_config_bool(stream, config.rom_fetch_mirror64);
      else if (key == "allow_invalid_alu_nop")
        read_config_bool(stream, config.allow_invalid_alu_nop);
      else if (key == "auto_power_wake")
        read_config_bool(stream, config.auto_power_wake);
      else if (key == "log_path")
        read_path(config.log_path);
      else if (key == "dump_frame")
        read_path(config.dump_frame);
      else if (key == "dump_current_frame")
        read_path(config.dump_current_frame);
      else if (key == "dump_frame_dir")
        read_path(config.dump_frame_dir);
      else if (key == "dump_memory")
        read_path(config.dump_memory);
      else if (key == "dump_code")
        read_path(config.dump_code);
      else if (key == "dump_memory_base")
        read_config_unsigned(stream, config.dump_memory_base);
      else if (key == "dump_memory_words")
        read_config_unsigned(stream, config.dump_memory_words);
      else if (key == "rom_base")
        read_config_unsigned(stream, config.rom_base);
      else if (key == "efuse0")
        read_config_unsigned(stream, config.efuse0);
      else if (key == "efuse1")
        read_config_unsigned(stream, config.efuse1);
      else if (key == "efuse2")
        read_config_unsigned(stream, config.efuse2);
      else if (key == "gpio_a")
        read_config_unsigned(stream, config.gpio_a);
      else if (key == "gpio_b")
        read_config_unsigned(stream, config.gpio_b);
      else if (key == "gpio_c")
        read_config_unsigned(stream, config.gpio_c);
      else if (key == "gpio_d")
        read_config_unsigned(stream, config.gpio_d);
      else if (key == "gpio_e")
        read_config_unsigned(stream, config.gpio_e);
      else if (key == "battery_adc")
        read_config_unsigned(stream, config.battery_adc);
    } catch (const std::exception &) {
      // A single hand-edited or stale value must not discard the rest of
      // a user's library. Invalid fields retain their fallback value.
    }
  }
  sanitize(config);
  return config;
}

void save_config(const std::filesystem::path &path,
                 const FrontendConfig &input_config) {
  FrontendConfig config = input_config;
  sanitize(config);
  std::error_code error;
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), error);
    if (error)
      die("failed to create settings directory " +
          path_to_utf8(path.parent_path()) + ": " + error.message());
  }

  std::ostringstream output;
  output << "# MobiGo 2 Emulator desktop settings\n"
         << "schema " << FrontendConfig::kSchemaVersion << '\n'
         << "rom " << quoted_path(config.rom) << '\n'
         << "spi " << quoted_path(config.spi) << '\n'
         << "nand " << quoted_path(config.nand) << '\n'
         << "last_directory " << quoted_path(config.last_directory) << '\n'
         << "window_scale " << config.window_scale << '\n'
         << "audio " << config.audio << '\n'
         << "show_speed " << config.show_speed << '\n'
         << "fullscreen " << config.fullscreen << '\n'
         << "integer_scaling " << config.integer_scaling << '\n'
         << "rom_endian " << config.rom_endian << '\n'
         << "max_steps " << config.max_steps << '\n'
         << "open_window_at " << config.open_window_at << '\n'
         << "trace_limit " << config.trace_limit << '\n'
         << "trace_transition_limit " << config.trace_transition_limit << '\n'
         << "start_logging_at " << config.start_logging_at << '\n'
         << "trace_start_insn " << config.trace_start_insn << '\n'
         << "dump_frame_interval " << config.dump_frame_interval << '\n'
         << "start_pc " << config.start_pc << '\n'
         << "trace_lo " << config.trace_lo << '\n'
         << "trace_hi " << config.trace_hi << '\n'
         << "dump_code_base " << config.dump_code_base << '\n'
         << "dump_code_words " << config.dump_code_words << '\n'
         << "start_pc_set " << config.start_pc_set << '\n'
         << "trace_range " << config.trace_range << '\n'
         << "log " << config.log << '\n'
         << "trace " << config.trace << '\n'
         << "trace_transitions " << config.trace_transitions << '\n'
         << "dump_memory_dma " << config.dump_memory_dma << '\n'
         << "rom_shadow_low " << config.rom_shadow_low << '\n'
         << "rom_fetch_mirror64 " << config.rom_fetch_mirror64 << '\n'
         << "allow_invalid_alu_nop " << config.allow_invalid_alu_nop << '\n'
         << "auto_power_wake " << config.auto_power_wake << '\n'
         << "log_path " << quoted_path(config.log_path) << '\n'
         << "dump_frame " << quoted_path(config.dump_frame) << '\n'
         << "dump_current_frame " << quoted_path(config.dump_current_frame)
         << '\n'
         << "dump_frame_dir " << quoted_path(config.dump_frame_dir) << '\n'
         << "dump_memory " << quoted_path(config.dump_memory) << '\n'
         << "dump_code " << quoted_path(config.dump_code) << '\n'
         << "dump_memory_base " << config.dump_memory_base << '\n'
         << "dump_memory_words " << config.dump_memory_words << '\n'
         << "rom_base " << config.rom_base << '\n'
         << "efuse0 " << config.efuse0 << '\n'
         << "efuse1 " << config.efuse1 << '\n'
         << "efuse2 " << config.efuse2 << '\n'
         << "gpio_a " << config.gpio_a << '\n'
         << "gpio_b " << config.gpio_b << '\n'
         << "gpio_c " << config.gpio_c << '\n'
         << "gpio_d " << config.gpio_d << '\n'
         << "gpio_e " << config.gpio_e << '\n'
         << "battery_adc " << config.battery_adc << '\n';
  for (size_t index = 0; index < kBindableControlCount; ++index)
    output << kBindableControlConfigKeys[index] << ' '
           << static_cast<uint32_t>(config.input_bindings.keys[index]) << '\n';
  for (const LibraryEntry &entry : config.library)
    output << "library " << quoted_path(entry.path) << '\n';

  const std::string text = output.str();
  std::filesystem::path temporary = path;
  temporary += ".tmp";
  {
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    if (!file)
      die("failed to write settings " + path_to_utf8(temporary));
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file)
      die("failed while writing settings " + path_to_utf8(temporary));
  }
  replace_file_atomic(temporary, path, "settings");
}

void remember_game(FrontendConfig &config, const LibraryEntry &entry,
                   size_t maximum_entries) {
  config.library.erase(std::remove_if(config.library.begin(),
                                      config.library.end(),
                                      [&](const LibraryEntry &candidate) {
                                        return candidate.path == entry.path;
                                      }),
                       config.library.end());
  config.library.insert(config.library.begin(), entry);
  if (config.library.size() > maximum_entries)
    config.library.resize(maximum_entries);
}

FirmwareStatus firmware_status(const FrontendConfig &config) {
  return {
      firmware_file_state(config.rom, 131072),
      firmware_file_state(config.spi, 2097152),
      firmware_file_state(config.nand, 138412032),
  };
}

std::optional<LaunchValidationIssue>
launch_validation_issue(const FrontendConfig &config) {
  if ((config.log || config.trace || config.trace_transitions) &&
      config.log_path.empty())
    return LaunchValidationIssue{
        "Logging and tracing need a nonempty log output path",
        ValidationSection::Diagnostics};
  if (!config.dump_memory.empty() && config.dump_memory_words == 0)
    return LaunchValidationIssue{"Memory dump needs a nonzero word count",
                                 ValidationSection::Diagnostics};
  if (!config.dump_memory.empty() &&
      config.dump_memory_words > kWordCount - config.dump_memory_base)
    return LaunchValidationIssue{"Memory dump exceeds the 22-bit address space",
                                 ValidationSection::Diagnostics};
  if (!config.dump_code.empty() && config.dump_code_words == 0)
    return LaunchValidationIssue{"Code dump needs a nonzero word count",
                                 ValidationSection::Outputs};
  if (!config.dump_code.empty() &&
      config.dump_code_words > kWordCount - config.dump_code_base)
    return LaunchValidationIssue{"Code dump exceeds the 22-bit address space",
                                 ValidationSection::Outputs};
  if (!config.dump_frame_dir.empty() && config.dump_frame_interval == 0)
    return LaunchValidationIssue{
        "Frame sequence needs a nonzero instruction interval",
        ValidationSection::Outputs};
  if (config.trace_range && config.trace_lo >= config.trace_hi)
    return LaunchValidationIssue{
        "Trace PC high must be greater than low (high is exclusive)",
        ValidationSection::Outputs};
  return std::nullopt;
}

std::optional<std::string>
launch_validation_error(const FrontendConfig &config) {
  if (const auto issue = launch_validation_issue(config))
    return issue->message;
  return std::nullopt;
}

Options make_launch_options(const Options &defaults,
                            const FrontendConfig &config,
                            const std::optional<LibraryEntry> &game) {
  Options options = defaults;
  options.rom = config.rom;
  options.spi = config.spi;
  options.nand = config.nand;
  // Player-facing sessions target real MobiGo speed. Rendering stays at the
  // automatic safe rate so it cannot race ahead or starve guest execution.
  options.max_present_hz = 0;
  options.speed_percent = 100;
  options.audio = config.audio;
  options.vsync = true;
  options.realtime_cap = true;
  options.realtime_cap_explicit = true;
  options.show_speed = config.show_speed;
  options.input_bindings = config.input_bindings;
  options.window = true;
  options.window_scale = config.window_scale;
  options.fullscreen = config.fullscreen;
  options.integer_scaling = config.integer_scaling;
  options.rom_endian = config.rom_endian;
  options.max_steps = config.max_steps;
  options.open_window_at = config.open_window_at;
  options.trace_limit = config.trace_limit;
  options.trace_transition_limit = config.trace_transition_limit;
  options.start_logging_at = config.start_logging_at;
  options.trace_start_insn = config.trace_start_insn;
  options.dump_frame_interval = config.dump_frame_interval;
  options.start_pc = config.start_pc;
  options.trace_lo = config.trace_lo;
  options.trace_hi = config.trace_hi;
  options.dump_code_base = config.dump_code_base;
  options.dump_code_words = config.dump_code_words;
  options.start_pc_set = config.start_pc_set;
  options.trace_range = config.trace_range;
  options.log = config.log || config.trace || config.trace_transitions;
  options.trace = config.trace;
  options.trace_transitions = config.trace_transitions;
  options.dump_memory_dma = config.dump_memory_dma;
  options.rom_shadow_low = config.rom_shadow_low;
  options.rom_fetch_mirror64 = config.rom_fetch_mirror64;
  options.allow_invalid_alu_nop = config.allow_invalid_alu_nop;
  options.auto_power_wake = config.auto_power_wake;
  options.log_path = config.log_path;
  options.dump_frame = config.dump_frame;
  options.dump_current_frame = config.dump_current_frame;
  options.dump_frame_dir = config.dump_frame_dir;
  options.dump_memory = config.dump_memory;
  options.dump_code = config.dump_code;
  options.dump_memory_base = config.dump_memory_base;
  options.dump_memory_words = config.dump_memory_words;
  options.rom_base = config.rom_base;
  options.efuse0 = config.efuse0;
  options.efuse1 = config.efuse1;
  options.efuse2 = config.efuse2;
  options.gpio_a = config.gpio_a;
  options.gpio_b = config.gpio_b;
  options.gpio_c = config.gpio_c;
  options.gpio_d = config.gpio_d;
  options.gpio_e = config.gpio_e;
  options.battery_adc = config.battery_adc;
  options.cart.clear();
  if (game)
    options.cart = game->path;
  return options;
}

uint64_t parse_ui_unsigned(std::string_view text) {
  if (text.empty())
    die("Enter a decimal or 0x-prefixed value");
  if (text.front() == '+' || text.front() == '-')
    die("Unsigned values cannot have a sign");
  int base = 10;
  size_t prefix = 0;
  if (text.size() >= 2 && text[0] == '0' &&
      (text[1] == 'x' || text[1] == 'X')) {
    base = 16;
    prefix = 2;
    if (text.size() == prefix)
      die("A 0x value needs hexadecimal digits");
  }
  size_t consumed = 0;
  uint64_t result = 0;
  try {
    result = std::stoull(std::string(text.substr(prefix)), &consumed, base);
  } catch (const std::exception &) {
    die(base == 16 ? "Enter hexadecimal digits after 0x"
                   : "Enter decimal digits or a 0x value");
  }
  if (consumed != text.size() - prefix)
    die("The value contains non-numeric characters");
  return result;
}

} // namespace mobigo::desktop
