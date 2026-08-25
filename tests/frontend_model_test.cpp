#include "desktop_frontend.hpp"
#include "ui_font.hpp"

#include <iostream>

using namespace mobigo;
using namespace mobigo::desktop;

namespace {

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

template <typename Function>
void require_throws(Function &&function, const char *message) {
  try {
    function();
  } catch (const std::runtime_error &) {
    return;
  }
  throw std::runtime_error(message);
}

void test_numeric_editor_parser() {
  require(parse_ui_unsigned("010") == 10,
          "leading-zero decimal was treated as octal");
  require(parse_ui_unsigned("090") == 90,
          "leading-zero decimal with 9 was rejected");
  require(parse_ui_unsigned("0x10") == 16 &&
              parse_ui_unsigned("0Xffff") == 65535,
          "explicit hexadecimal parsing failed");
  require_throws([] { (void)parse_ui_unsigned("+1"); },
                 "positive sign was accepted");
  require_throws([] { (void)parse_ui_unsigned("-1"); },
                 "negative sign was accepted");
  require_throws([] { (void)parse_ui_unsigned("0x"); },
                 "empty hexadecimal value was accepted");
}

void test_utf8_bitmap_text() {
  const std::string text = "Aé日本🙂B";
  require(ui_font::glyph_count(text) == 6,
          "UTF-8 glyph count used encoded byte length");
  require(ui_font::bitmap_glyphs(text) == "A????B",
          "unsupported Unicode codepoints produced multiple replacements");
  require(ui_font::width(text, 2) == 72,
          "UTF-8 bitmap width used encoded byte length");
  require(ui_font::width("AB\n日本", 1) == 12,
          "multiline UTF-8 width did not use the widest display line");
  require(ui_font::abbreviate("ABé日本CDEF", 7) == "ABé日...",
          "UTF-8 abbreviation split a codepoint or used byte length");
  require(ui_font::abbreviate("é日本A", 3) == "é日本",
          "short UTF-8 abbreviation split a codepoint");
}

void create_sized_file(const std::filesystem::path &path, uintmax_t size) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output)
    throw std::runtime_error("could not create test file");
  if (size != 0) {
    output.seekp(static_cast<std::streamoff>(size - 1));
    output.put('\0');
  }
}

void test_launcher_routing() {
#ifndef __EMSCRIPTEN__
  require(should_open_launcher(1, false),
          "no-argument desktop launch was not routed to GUI");
  require(!should_open_launcher(2, false), "CLI invocation was routed to GUI");
  require(!should_open_launcher(1, true),
          "explicit headless escape was ignored");
#endif
}

void test_config_round_trip(const std::filesystem::path &directory) {
  FrontendConfig source;
  source.rom = directory / path_from_utf8("firmware café 日本") /
               path_from_utf8("internal rom.bin");
  source.spi = directory / "spi.bin";
  source.nand = directory / "nand.bin";
  source.last_directory = directory / "game folder";
  source.show_speed = false;
  source.input_bindings.keys[static_cast<size_t>(BindableControl::Primary)] = SDLK_SPACE;
  source.window_scale = 4;
  source.audio = false;
  source.fullscreen = true;
  source.integer_scaling = false;
  source.rom_endian = "be";
  source.max_steps = 123456;
  source.log = true;
  source.log_path = directory / "logs" / "session log.txt";
  source.efuse2 = 0x3456;
  source.gpio_b = 0x1234;
  source.start_pc = 0x123456;
  source.start_pc_set = true;
  source.start_logging_at = 111;
  source.trace_start_insn = 222;
  source.trace_range = true;
  source.trace_lo = 0x100000;
  source.trace_hi = kWordCount;
  source.dump_current_frame = directory / "current.bmp";
  source.dump_frame_dir = directory / "frames";
  source.dump_frame_interval = 333;
  source.dump_code = directory / "code.bin";
  source.dump_code_base = 0x200000;
  source.dump_code_words = 0x100;
  remember_game(source, {directory / "first game.bin"});
  remember_game(source, {directory / "second game.bin"});
  remember_game(source, {directory / "first game.bin"});
  require(source.library.size() == 2 &&
              source.library.front().path.filename() == "first game.bin",
          "recent-game promotion or deduplication failed");

  const auto path = directory / "frontend.conf";
  save_config(path, source);
  const FrontendConfig loaded = load_config(path, {});
  require(loaded.rom == source.rom &&
              loaded.last_directory == source.last_directory,
          "quoted configuration paths did not round-trip");
  require(path_to_utf8(loaded.rom).find("café 日本") != std::string::npos,
          "non-ASCII configuration path lost its UTF-8 spelling");
  require(!loaded.show_speed &&
              loaded.input_bindings.keys[static_cast<size_t>(BindableControl::Primary)] == SDLK_SPACE,
          "speed display or custom control did not round-trip");
  require(loaded.window_scale == 4 &&
              !loaded.audio && loaded.fullscreen && !loaded.integer_scaling,
          "desktop settings did not round-trip");
  require(loaded.rom_endian == "be" &&
              loaded.max_steps == 123456 && loaded.log &&
              loaded.log_path == source.log_path,
          "advanced settings did not round-trip");
  require(loaded.efuse2 == 0x3456 && loaded.gpio_b == 0x1234,
          "board overrides did not round-trip");
  require(loaded.start_pc_set && loaded.start_pc == source.start_pc &&
              loaded.start_logging_at == source.start_logging_at &&
              loaded.trace_start_insn == source.trace_start_insn &&
              loaded.trace_range && loaded.trace_lo == source.trace_lo &&
              loaded.trace_hi == source.trace_hi,
          "output start/trace controls did not round-trip");
  require(loaded.dump_current_frame == source.dump_current_frame &&
              loaded.dump_frame_dir == source.dump_frame_dir &&
              loaded.dump_frame_interval == source.dump_frame_interval &&
              loaded.dump_code == source.dump_code &&
              loaded.dump_code_base == source.dump_code_base &&
              loaded.dump_code_words == source.dump_code_words,
          "output dump controls did not round-trip");
  const Options mapped = make_launch_options({}, loaded, std::nullopt);
  require(mapped.start_pc_set && mapped.start_pc == source.start_pc &&
              mapped.start_logging_at == source.start_logging_at &&
              mapped.trace_start_insn == source.trace_start_insn &&
              mapped.trace_range && mapped.trace_lo == source.trace_lo &&
              mapped.trace_hi == source.trace_hi &&
              mapped.dump_current_frame == source.dump_current_frame &&
              mapped.dump_frame_dir == source.dump_frame_dir &&
              mapped.dump_frame_interval == source.dump_frame_interval &&
              mapped.dump_code == source.dump_code &&
              mapped.dump_code_base == source.dump_code_base &&
              mapped.dump_code_words == source.dump_code_words,
          "output fields were lost while mapping GUI settings to Options");
  require(loaded.library == source.library,
          "game library did not round-trip in recent order");
  FrontendConfig replacement = source;
  replacement.audio = !source.audio;
  save_config(path, replacement);
  require(load_config(path, {}).audio == replacement.audio,
          "existing settings file was not atomically replaced");

  const auto atomic_file = directory / "atomic.bin";
  write_file_bytes_atomic(atomic_file, {1, 2, 3});
  write_file_bytes_atomic(atomic_file, {9, 8});
  require(read_file_bytes(atomic_file) == std::vector<uint8_t>({9, 8}),
          "existing data file was not atomically replaced");

  const auto original_directory = std::filesystem::current_path();
  try {
    std::filesystem::current_path(directory);
    save_config("relative.conf", source);
    require(load_config("relative.conf", {}).rom == source.rom,
            "relative configuration path did not save");
    std::filesystem::current_path(original_directory);
  } catch (...) {
    std::filesystem::current_path(original_directory);
    throw;
  }

  const auto malformed_path = directory / "malformed.conf";
  {
    std::ofstream malformed(malformed_path);
    malformed << "rom_base 4294967295\n";
  }
  require(load_config(malformed_path, {}).rom_base == 0x008000,
          "out-of-range ROM base did not recover to the safe default");

  const auto legacy_library_path = directory / "legacy-library.conf";
  {
    std::ofstream legacy(legacy_library_path);
    legacy << "schema 1\n"
           << "library 0 \"cartridge.bin\"\n"
           << "library 1 \"old-launcher.MBA\"\n";
  }
  const FrontendConfig migrated_library = load_config(legacy_library_path, {});
  require(migrated_library.library.size() == 1 &&
              migrated_library.library.front().path == "cartridge.bin",
          "legacy game library did not discard retired MBA launch entries");

  const auto overflow_path = directory / "overflow.conf";
  {
    std::ofstream overflow(overflow_path);
    overflow << "max_steps 18446744073709551616\n"
             << "trace_limit -1\n"
             << "window_scale 99999999999999999999\n"
             << "efuse0 65536\n"
             << "start_pc 4294967296\n"
             << "audio 2\n"
             << "library\n";
  }
  FrontendConfig checked_fallback;
  checked_fallback.max_steps = 77;
  checked_fallback.trace_limit = 1234;
  checked_fallback.window_scale = 3;
  checked_fallback.efuse0 = 0x4321;
  checked_fallback.start_pc = 0x123456;
  checked_fallback.audio = true;
  const FrontendConfig checked = load_config(overflow_path, checked_fallback);
  require(checked.max_steps == checked_fallback.max_steps &&
              checked.trace_limit == checked_fallback.trace_limit &&
              checked.window_scale == checked_fallback.window_scale &&
              checked.efuse0 == checked_fallback.efuse0 &&
              checked.start_pc == checked_fallback.start_pc &&
              checked.audio == checked_fallback.audio &&
              checked.library.empty(),
          "malformed numeric settings overwrote checked fallback values");
}

void test_firmware_validation(const std::filesystem::path &directory) {
  FrontendConfig config;
  config.rom = directory / "internalrom.bin";
  config.spi = directory / "spi.bin";
  config.nand = directory / "nand.bin";
  create_sized_file(config.rom, 131072);
  create_sized_file(config.spi, 2097152);
  create_sized_file(config.nand, 138412032);
  require(firmware_status(config).ready(),
          "exact firmware sizes were rejected");

  create_sized_file(config.spi, 1024);
  const FirmwareStatus wrong = firmware_status(config);
  require(wrong.spi == FirmwareFileState::WrongSize && !wrong.ready(),
          "wrong-sized firmware was reported ready");
  require(wrong.message().find("wrong size") != std::string::npos,
          "wrong-sized firmware did not produce a useful diagnostic");
  std::filesystem::remove(config.rom);
  require(firmware_status(config).rom == FirmwareFileState::Missing,
          "missing firmware was not distinguished from wrong size");
}

void test_launch_mapping(const std::filesystem::path &directory) {
  Options defaults;
  FrontendConfig config = config_from_defaults(defaults);
  config.rom = directory / "rom.bin";
  config.spi = directory / "spi.bin";
  config.nand = directory / "nand.bin";
  config.audio = true;
  config.window_scale = 3;
  config.fullscreen = true;
  config.integer_scaling = true;
  config.show_speed = false;
  config.input_bindings.keys[static_cast<size_t>(BindableControl::Primary)] = SDLK_SPACE;
  const LibraryEntry cart{directory / "game.bin"};
  const Options options = make_launch_options(defaults, config, cart);
  require(options.cart == cart.path,
          "cartridge launch was not mapped");
  require(options.speed_percent == 100 && options.realtime_cap &&
              options.realtime_cap_explicit && options.max_present_hz == 0 &&
              !options.vsync && !options.show_speed &&
              options.input_bindings.keys[static_cast<size_t>(BindableControl::Primary)] == SDLK_SPACE,
          "GUI real-time defaults or custom controls were lost");
  require(options.window_scale == 3 && options.fullscreen &&
              options.integer_scaling,
          "GUI presentation choices were lost");

  const Options system = make_launch_options(defaults, config, std::nullopt);
  require(system.cart.empty(), "system launch retained game-only state");
}

void test_launch_validation() {
  FrontendConfig config;
  config.log = true;
  config.log_path.clear();
  auto issue = launch_validation_issue(config);
  require(issue && issue->section == ValidationSection::Diagnostics,
          "empty enabled log path passed GUI validation");
  config.log_path = "emulator.log";
  require(!launch_validation_error(config), "valid log path was rejected");
  config.dump_memory = "memory.bin";
  config.dump_memory_words = 0;
  issue = launch_validation_issue(config);
  require(issue && issue->section == ValidationSection::Diagnostics,
          "zero-length enabled memory dump passed GUI validation");
  config.dump_memory_base = kAddrMask;
  config.dump_memory_words = 2;
  require(launch_validation_error(config).has_value(),
          "memory dump beyond address space passed GUI validation");
  config.dump_memory.clear();
  config.dump_code = "code.bin";
  config.dump_code_base = kAddrMask;
  config.dump_code_words = 2;
  issue = launch_validation_issue(config);
  require(issue && issue->section == ValidationSection::Outputs,
          "code dump beyond address space passed GUI validation");
  config.dump_code.clear();
  config.dump_frame_dir = "frames";
  config.dump_frame_interval = 0;
  issue = launch_validation_issue(config);
  require(issue && issue->section == ValidationSection::Outputs,
          "frame output validation did not route to Outputs");
  config.dump_frame_dir.clear();
  config.trace_range = true;
  config.trace_lo = 0x100;
  config.trace_hi = 0x100;
  issue = launch_validation_issue(config);
  require(issue && issue->section == ValidationSection::Outputs,
          "empty trace PC range passed GUI validation");
  config.trace_hi = kWordCount;
  require(!launch_validation_error(config),
          "exclusive full-address-space trace high was rejected");
}

} // namespace

int main() {
  const std::filesystem::path directory =
      std::filesystem::temp_directory_path() /
      ("mobigo2_frontend_test_" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  try {
    std::filesystem::create_directories(directory);
    test_launcher_routing();
    test_numeric_editor_parser();
    test_utf8_bitmap_text();
    test_config_round_trip(directory);
    test_firmware_validation(directory);
    test_launch_mapping(directory);
    test_launch_validation();
    std::filesystem::remove_all(directory);
    std::cout << "frontend model tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::filesystem::remove_all(directory);
    std::cerr << "frontend model test failed: " << error.what() << '\n';
    return 1;
  }
}
