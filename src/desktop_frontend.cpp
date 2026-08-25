#include "desktop_frontend.hpp"

#include "ui_font.hpp"

#include <system_error>
#include <SDL_ttf.h>

#ifdef _WIN32
#include <commdlg.h>
#endif

namespace mobigo::desktop {

#ifdef __EMSCRIPTEN__

LaunchRequest run_launcher(const Options &packaged_defaults, const char *) {
  return {LaunchAction::Boot, packaged_defaults};
}

#else
namespace {

constexpr int kWindowWidth = 1024;
constexpr int kWindowHeight = 680;

constexpr SDL_Color kBackground{40, 40, 40, 255};
constexpr SDL_Color kToolbar{43, 43, 43, 255};
constexpr SDL_Color kPanel{48, 48, 48, 255};
constexpr SDL_Color kPanelHover{62, 62, 62, 255};
constexpr SDL_Color kAccent{86, 173, 188, 255};
constexpr SDL_Color kAccentHover{113, 198, 211, 255};
constexpr SDL_Color kText{229, 229, 229, 255};
constexpr SDL_Color kMuted{159, 159, 159, 255};
constexpr SDL_Color kBorder{91, 91, 91, 255};
constexpr SDL_Color kDanger{248, 112, 112, 255};
constexpr SDL_Color kWarning{245, 185, 75, 255};

std::array<TTF_Font *, 3> g_ui_fonts{};

int ui_font_index(int scale) {
  return std::clamp(scale, 1, 3) - 1;
}

TTF_Font *ui_font_for_scale(int scale) {
  return g_ui_fonts[ui_font_index(scale)];
}

int system_text_width(std::string_view text, int scale) {
  if (text.empty()) return 0;
  int width = 0;
  int height = 0;
  const std::string value(text);
  TTF_SizeUTF8(ui_font_for_scale(scale), value.c_str(), &width, &height);
  return width;
}

int system_text_height(int scale) {
  return TTF_FontHeight(ui_font_for_scale(scale));
}

void draw_system_text(SDL_Renderer *renderer, int x, int y,
                      std::string_view text, SDL_Color color, int scale) {
  if (text.empty()) return;
  const std::string value(text);
  SDL_Surface *surface = TTF_RenderUTF8_Blended(ui_font_for_scale(scale),
                                                value.c_str(), color);
  if (!surface) return;
  SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
  const SDL_Rect destination{x, y, surface->w, surface->h};
  SDL_FreeSurface(surface);
  if (!texture) return;
  SDL_RenderCopy(renderer, texture, nullptr, &destination);
  SDL_DestroyTexture(texture);
}

void load_system_fonts() {
#ifdef _WIN32
  constexpr std::array<const char *, 2> paths{
      "C:\\Windows\\Fonts\\segoeui.ttf", "C:\\Windows\\Fonts\\arial.ttf"};
#elif defined(__APPLE__)
  constexpr std::array<const char *, 2> paths{
      "/System/Library/Fonts/Supplemental/Arial.ttf",
      "/System/Library/Fonts/Helvetica.ttc"};
#else
  constexpr std::array<const char *, 2> paths{
      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
      "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf"};
#endif
  constexpr std::array<int, 3> sizes{12, 18, 26};
  for (const char *path : paths) {
    for (size_t index = 0; index < sizes.size(); ++index)
      g_ui_fonts[index] = TTF_OpenFont(path, sizes[index]);
    if (std::all_of(g_ui_fonts.begin(), g_ui_fonts.end(),
                    [](TTF_Font *font) { return font != nullptr; }))
      return;
    for (TTF_Font *&font : g_ui_fonts) {
      if (font) TTF_CloseFont(font);
      font = nullptr;
    }
  }
  die(std::string("cannot open a system UI font: ") + TTF_GetError());
}

void unload_system_fonts() {
  for (TTF_Font *&font : g_ui_fonts) {
    if (font) TTF_CloseFont(font);
    font = nullptr;
  }
}

bool contains(const SDL_Rect &rectangle, int x, int y) {
  return x >= rectangle.x && y >= rectangle.y &&
         x < rectangle.x + rectangle.w && y < rectangle.y + rectangle.h;
}

std::string display_name(const std::filesystem::path &path) {
  std::string name = path_to_utf8(path.stem());
  if (name.empty())
    name = path_to_utf8(path.filename());
  return name.empty() ? path_to_utf8(path) : name;
}

std::string abbreviated(std::string_view value, size_t maximum) {
  return ui_font::abbreviate(value, maximum);
}

std::string hex_value(uint64_t value, int width) {
  std::ostringstream stream;
  stream << "0x" << std::hex << std::setw(width) << std::setfill('0') << value;
  return stream.str();
}

enum class FilePurpose {
  Cartridge,
  Rom,
  Spi,
  Nand,
};

std::filesystem::path friendly_initial_directory() {
#ifdef _WIN32
  std::wstring profile(32768, L'\0');
  const DWORD length = GetEnvironmentVariableW(
      L"USERPROFILE", profile.data(), static_cast<DWORD>(profile.size()));
  if (length > 0 && length < profile.size()) {
    profile.resize(length);
    const std::filesystem::path home(profile);
    std::error_code error;
    const std::filesystem::path documents = home / L"Documents";
    if (std::filesystem::is_directory(documents, error))
      return documents;
    error.clear();
    if (std::filesystem::is_directory(home, error))
      return home;
  }
#else
  for (const char *variable : {"HOME", "USERPROFILE"}) {
    if (const char *value = std::getenv(variable)) {
      const std::filesystem::path home = path_from_utf8(value);
      std::error_code error;
      const std::filesystem::path documents = home / "Documents";
      if (std::filesystem::is_directory(documents, error))
        return documents;
      error.clear();
      if (std::filesystem::is_directory(home, error))
        return home;
    }
  }
#endif
  std::error_code error;
  const std::filesystem::path current = std::filesystem::current_path(error);
  return error ? std::filesystem::path{"."} : current;
}

std::optional<std::filesystem::path>
open_native_file_dialog(FilePurpose purpose,
                        const std::filesystem::path &initial_directory) {
#ifndef _WIN32
  (void)initial_directory;
#endif
  if (const char *test_path = std::getenv("MOBIGO2_FILE_DIALOG_TEST_PATH"))
    return path_from_utf8(test_path);
#ifdef _WIN32
  std::array<wchar_t, 32768> selected{};
  const std::wstring initial = initial_directory.wstring();
  const wchar_t *filter =
      purpose == FilePurpose::Cartridge
          ? L"MobiGo cartridges\0*.bin;*.rom;*.cart\0All files\0*.*\0\0"
          : L"Image files\0*.bin;*.rom\0All files\0*.*\0\0";
  OPENFILENAMEW dialog{};
  dialog.lStructSize = sizeof(dialog);
  dialog.lpstrFile = selected.data();
  dialog.nMaxFile = DWORD(selected.size());
  dialog.lpstrFilter = filter;
  dialog.lpstrInitialDir = initial.empty() ? nullptr : initial.c_str();
  dialog.lpstrTitle = L"MobiGo 2 Emulator";
  dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                 OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
  if (!GetOpenFileNameW(&dialog))
    return std::nullopt;
  return std::filesystem::path(selected.data());
#elif defined(__APPLE__)
  const char *script =
      purpose == FilePurpose::Cartridge
          ? "osascript -e 'POSIX path of (choose file with prompt \"Choose a MobiGo cartridge\")'"
          : "osascript -e 'POSIX path of (choose file with prompt \"Choose a firmware image\")'";
  FILE *pipe = popen(script, "r");
  if (!pipe)
    return std::nullopt;
  std::array<char, 4096> output{};
  std::string path;
  if (std::fgets(output.data(), int(output.size()), pipe))
    path = output.data();
  pclose(pipe);
  if (!path.empty() && path.back() == '\n')
    path.pop_back();
  return path.empty() ? std::nullopt : std::optional(path_from_utf8(path));
#else
  const char *command =
      purpose == FilePurpose::Cartridge
          ? "zenity --file-selection --title='Choose a MobiGo cartridge'"
          : "zenity --file-selection --title='Choose a firmware image'";
  FILE *pipe = popen(command, "r");
  if (!pipe)
    return std::nullopt;
  std::array<char, 4096> output{};
  std::string path;
  if (std::fgets(output.data(), int(output.size()), pipe))
    path = output.data();
  pclose(pipe);
  if (!path.empty() && path.back() == '\n')
    path.pop_back();
  return path.empty() ? std::nullopt : std::optional(path_from_utf8(path));
#endif
}

struct Painter {
  SDL_Renderer *renderer = nullptr;
  int mouse_x = -1;
  int mouse_y = -1;

  void color(SDL_Color value) const {
    SDL_SetRenderDrawColor(renderer, value.r, value.g, value.b, value.a);
  }

  void fill(const SDL_Rect &rectangle, SDL_Color value) const {
    color(value);
    SDL_RenderFillRect(renderer, &rectangle);
  }

  void outline(const SDL_Rect &rectangle, SDL_Color value) const {
    color(value);
    SDL_RenderDrawRect(renderer, &rectangle);
  }

  void text(int x, int y, std::string_view text_value,
            SDL_Color text_color = kText, int scale = 2) const {
    draw_system_text(renderer, x, y, text_value, text_color, scale);
  }

  bool button(const SDL_Rect &rectangle, std::string_view label,
              bool primary = false, bool selected = false) const {
    const bool hovered = contains(rectangle, mouse_x, mouse_y);
    SDL_Color background =
        selected ? SDL_Color{37, 92, 96, 255} : (primary ? kAccent : kPanel);
    if (hovered)
      background = primary ? kAccentHover : kPanelHover;
    fill(rectangle, background);
    outline(rectangle, selected || primary ? kAccent : kBorder);
    const int label_width = system_text_width(label, 2);
    text(rectangle.x + std::max(10, (rectangle.w - label_width) / 2),
         rectangle.y + (rectangle.h - system_text_height(2)) / 2, label,
         primary ? SDL_Color{7, 35, 38, 255} : kText);
    return hovered;
  }

  void toggle(const SDL_Rect &rectangle, bool enabled) const {
    fill(rectangle, enabled ? kAccent : SDL_Color{58, 69, 84, 255});
    outline(rectangle, enabled ? kAccentHover : kBorder);
    SDL_Rect knob{enabled ? rectangle.x + rectangle.w - 20 : rectangle.x + 4,
                  rectangle.y + 4, 16, rectangle.h - 8};
    fill(knob, enabled ? SDL_Color{8, 43, 44, 255} : kMuted);
  }

  void line(int x1, int y1, int x2, int y2, SDL_Color value,
            int width = 1) const {
    color(value);
    for (int offset = 0; offset < width; ++offset)
      SDL_RenderDrawLine(renderer, x1, y1 + offset, x2, y2 + offset);
  }
};

enum class Page {
  Library,
  Settings,
  Controls,
  Advanced,
  Diagnostics,
  Outputs,
  Board,
};

enum class EditField {
  MaxSteps,
  OpenWindowAt,
  LogPath,
  TraceLimit,
  TransitionLimit,
  DumpFrame,
  DumpMemory,
  DumpMemoryBase,
  DumpMemoryWords,
  StartPc,
  StartLoggingAt,
  TraceStartInsn,
  TraceLo,
  TraceHi,
  DumpCurrentFrame,
  DumpFrameDir,
  DumpFrameInterval,
  DumpCode,
  DumpCodeBase,
  DumpCodeWords,
  RomBase,
  Efuse0,
  Efuse1,
  Efuse2,
  GpioA,
  GpioB,
  GpioC,
  GpioD,
  GpioE,
  BatteryAdc,
};

struct TextPrompt {
  bool open = false;
  EditField field = EditField::MaxSteps;
  std::string title;
  std::string value;
  std::string error;
};

class Launcher {
public:
  Launcher(Options defaults, const char *)
      : defaults_(std::move(defaults)), config_path_(default_config_path()),
        config_(load_config(config_path_, config_from_defaults(defaults_))) {
    // A moved/new release should heal stale absolute paths saved by an
    // older package whenever the newly resolved bundled image is valid.
    const FirmwareStatus saved = firmware_status(config_);
    const FrontendConfig packaged = config_from_defaults(defaults_);
    const FirmwareStatus bundled = firmware_status(packaged);
    if (saved.rom != FirmwareFileState::Ready &&
        bundled.rom == FirmwareFileState::Ready)
      config_.rom = packaged.rom;
    if (saved.spi != FirmwareFileState::Ready &&
        bundled.spi == FirmwareFileState::Ready)
      config_.spi = packaged.spi;
    if (saved.nand != FirmwareFileState::Ready &&
        bundled.nand == FirmwareFileState::Ready)
      config_.nand = packaged.nand;
  }

  ~Launcher() { shutdown(); }

  LaunchRequest run() {
    initialize();
    while (running_) {
      SDL_Event event;
      while (SDL_PollEvent(&event))
        handle_event(event);
      render();
      if (!vsync_renderer_)
        SDL_Delay(12);
    }
    return result_;
  }

private:
  Options defaults_;
  std::filesystem::path config_path_;
  FrontendConfig config_;
  SDL_Window *window_ = nullptr;
  SDL_Renderer *renderer_ = nullptr;
  bool vsync_renderer_ = false;
  bool running_ = true;
  int mouse_x_ = -1;
  int mouse_y_ = -1;
  Page page_ = Page::Library;
  TextPrompt prompt_;
  LaunchRequest result_;
  std::string notice_;
  bool notice_is_error_ = false;
  bool confirm_restore_ = false;
  int library_scroll_ = 0;
  std::optional<size_t> selected_library_index_;
  std::optional<size_t> binding_capture_;
  std::filesystem::path test_screenshot_;

  void initialize() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) !=
        0)
      die(std::string("cannot start the desktop launcher: ") + SDL_GetError());
    if (TTF_Init() != 0)
      die(std::string("cannot start system font rendering: ") + TTF_GetError());
    load_system_fonts();
    window_ = SDL_CreateWindow("MobiGo 2 Emulator", SDL_WINDOWPOS_CENTERED,
                               SDL_WINDOWPOS_CENTERED, kWindowWidth,
                               kWindowHeight, SDL_WINDOW_SHOWN);
    if (!window_)
      die(SDL_GetError());
    renderer_ = SDL_CreateRenderer(
        window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    vsync_renderer_ = renderer_ != nullptr;
    if (!renderer_)
      renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer_)
      die(SDL_GetError());
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
    if (const char *path = std::getenv("MOBIGO2_FRONTEND_TEST_SCREENSHOT"))
      test_screenshot_ = path_from_utf8(path);
    if (const char *page = std::getenv("MOBIGO2_FRONTEND_TEST_PAGE")) {
      const std::string name = ascii_lower(page);
      if (name == "settings")
        page_ = Page::Settings;
      else if (name == "controls")
        page_ = Page::Controls;
      else if (name == "advanced")
        page_ = Page::Advanced;
      else if (name == "diagnostics")
        page_ = Page::Diagnostics;
      else if (name == "outputs")
        page_ = Page::Outputs;
      else if (name == "board")
        page_ = Page::Board;
    }
  }

  void shutdown() {
    unload_system_fonts();
    TTF_Quit();
    if (renderer_)
      SDL_DestroyRenderer(renderer_);
    if (window_)
      SDL_DestroyWindow(window_);
    renderer_ = nullptr;
    window_ = nullptr;
    if (SDL_WasInit(0))
      SDL_Quit();
  }

  void persist() {
    try {
      save_config(config_path_, config_);
    } catch (const std::exception &error) {
      show_notice(std::string("Settings could not be saved: ") + error.what(),
                  true);
    }
  }

  void show_notice(std::string message, bool error = false) {
    notice_ = std::move(message);
    notice_is_error_ = error;
  }

  std::filesystem::path native_dialog_start(FilePurpose purpose) const {
    std::filesystem::path configured;
    if (purpose == FilePurpose::Rom)
      configured = config_.rom;
    else if (purpose == FilePurpose::Spi)
      configured = config_.spi;
    else if (purpose == FilePurpose::Nand)
      configured = config_.nand;
    if (!configured.empty() && configured.has_parent_path()) {
      std::error_code error;
      if (std::filesystem::is_directory(configured.parent_path(), error))
        return configured.parent_path();
    }
    if (!config_.last_directory.empty())
      return config_.last_directory;
    return friendly_initial_directory();
  }

  void open_native_dialog(FilePurpose purpose) {
    if (const auto path =
            open_native_file_dialog(purpose, native_dialog_start(purpose)))
      choose_file(purpose, *path);
  }

  void begin_prompt(EditField field, std::string title, std::string value) {
    prompt_ = {true, field, std::move(title), std::move(value), {}};
    SDL_StartTextInput();
  }

  void apply_prompt() {
    try {
      if (prompt_.field == EditField::LogPath)
        config_.log_path = path_from_utf8(prompt_.value);
      else if (prompt_.field == EditField::DumpFrame)
        config_.dump_frame = path_from_utf8(prompt_.value);
      else if (prompt_.field == EditField::DumpCurrentFrame)
        config_.dump_current_frame = path_from_utf8(prompt_.value);
      else if (prompt_.field == EditField::DumpFrameDir)
        config_.dump_frame_dir = path_from_utf8(prompt_.value);
      else if (prompt_.field == EditField::DumpMemory)
        config_.dump_memory = path_from_utf8(prompt_.value);
      else if (prompt_.field == EditField::DumpCode)
        config_.dump_code = path_from_utf8(prompt_.value);
      else if (prompt_.field == EditField::StartPc && prompt_.value.empty())
        config_.start_pc_set = false;
      else if ((prompt_.field == EditField::TraceLo ||
                prompt_.field == EditField::TraceHi) &&
               prompt_.value.empty())
        config_.trace_range = false;
      else {
        if (prompt_.value.empty())
          die("Enter a decimal or 0x-prefixed value");
        const uint64_t number = parse_ui_unsigned(prompt_.value);
        switch (prompt_.field) {
        case EditField::MaxSteps:
          config_.max_steps = number;
          break;
        case EditField::OpenWindowAt:
          config_.open_window_at = number;
          break;
        case EditField::TraceLimit:
          if (number == 0)
            die("Trace limit must be at least 1");
          config_.trace_limit = number;
          break;
        case EditField::TransitionLimit:
          if (number == 0)
            die("Transition limit must be at least 1");
          config_.trace_transition_limit = number;
          break;
        case EditField::DumpMemoryBase:
          if (number > kAddrMask)
            die("Address exceeds the 22-bit bus");
          config_.dump_memory_base = uint32_t(number);
          break;
        case EditField::DumpMemoryWords:
          if (number > UINT32_MAX)
            die("Word count is too large");
          if (number > kWordCount - config_.dump_memory_base)
            die("Dump exceeds the 22-bit address space");
          config_.dump_memory_words = uint32_t(number);
          break;
        case EditField::StartPc:
          if (number > kAddrMask)
            die("Address exceeds the 22-bit bus");
          config_.start_pc = uint32_t(number);
          config_.start_pc_set = true;
          break;
        case EditField::StartLoggingAt:
          config_.start_logging_at = number;
          break;
        case EditField::TraceStartInsn:
          config_.trace_start_insn = number;
          break;
        case EditField::TraceLo:
        case EditField::TraceHi:
          if ((prompt_.field == EditField::TraceLo && number > kAddrMask) ||
              (prompt_.field == EditField::TraceHi && number > kWordCount))
            die("Address exceeds the 22-bit bus");
          if (prompt_.field == EditField::TraceLo)
            config_.trace_lo = uint32_t(number);
          else
            config_.trace_hi = uint32_t(number);
          config_.trace_range = true;
          config_.trace = true;
          config_.log = true;
          break;
        case EditField::DumpFrameInterval:
          config_.dump_frame_interval = number;
          break;
        case EditField::DumpCodeBase:
          if (number > kAddrMask)
            die("Address exceeds the 22-bit bus");
          config_.dump_code_base = uint32_t(number);
          break;
        case EditField::DumpCodeWords:
          if (number > kWordCount - config_.dump_code_base)
            die("Dump exceeds the 22-bit address space");
          config_.dump_code_words = uint32_t(number);
          break;
        case EditField::RomBase:
          if (number > kAddrMask)
            die("Address exceeds the 22-bit bus");
          config_.rom_base = uint32_t(number);
          break;
        case EditField::Efuse0:
        case EditField::Efuse1:
        case EditField::Efuse2:
        case EditField::GpioA:
        case EditField::GpioB:
        case EditField::GpioC:
        case EditField::GpioD:
        case EditField::GpioE:
          if (number > UINT16_MAX)
            die("Value must fit in 16 bits");
          if (prompt_.field == EditField::Efuse0)
            config_.efuse0 = uint16_t(number);
          else if (prompt_.field == EditField::Efuse1)
            config_.efuse1 = uint16_t(number);
          else if (prompt_.field == EditField::Efuse2)
            config_.efuse2 = uint16_t(number);
          else if (prompt_.field == EditField::GpioA)
            config_.gpio_a = uint16_t(number);
          else if (prompt_.field == EditField::GpioB)
            config_.gpio_b = uint16_t(number);
          else if (prompt_.field == EditField::GpioC)
            config_.gpio_c = uint16_t(number);
          else if (prompt_.field == EditField::GpioD)
            config_.gpio_d = uint16_t(number);
          else
            config_.gpio_e = uint16_t(number);
          break;
        case EditField::BatteryAdc:
          if (number > 0x0fff)
            die("Battery ADC must be at most 0x0fff");
          config_.battery_adc = uint16_t(number);
          break;
        case EditField::LogPath:
        case EditField::DumpFrame:
        case EditField::DumpCurrentFrame:
        case EditField::DumpFrameDir:
        case EditField::DumpMemory:
        case EditField::DumpCode:
          break;
        }
      }
      prompt_.open = false;
      SDL_StopTextInput();
      persist();
    } catch (const std::exception &error) {
      prompt_.error = error.what();
    }
  }

  void handle_prompt_event(const SDL_Event &event) {
    if (event.type == SDL_TEXTINPUT) {
      append_prompt_text(event.text.text);
      return;
    }
    if (event.type != SDL_KEYDOWN || event.key.repeat)
      return;
    if (event.key.keysym.sym == SDLK_ESCAPE) {
      prompt_.open = false;
      SDL_StopTextInput();
    } else if (event.key.keysym.sym == SDLK_RETURN ||
               event.key.keysym.sym == SDLK_KP_ENTER) {
      apply_prompt();
    } else if (event.key.keysym.sym == SDLK_BACKSPACE &&
               !prompt_.value.empty()) {
      size_t codepoint = prompt_.value.size() - 1;
      while (codepoint > 0 &&
             (static_cast<unsigned char>(prompt_.value[codepoint]) & 0xc0) ==
                 0x80)
        --codepoint;
      prompt_.value.erase(codepoint);
    } else if (event.key.keysym.sym == SDLK_v &&
               (event.key.keysym.mod & (KMOD_CTRL | KMOD_GUI))) {
      char *clipboard = SDL_GetClipboardText();
      if (clipboard) {
        append_prompt_text(clipboard);
        SDL_free(clipboard);
      }
    }
  }

  void append_prompt_text(std::string_view text) {
    constexpr size_t maximum = 240;
    if (prompt_.value.size() >= maximum)
      return;
    size_t bytes = std::min(maximum - prompt_.value.size(), text.size());
    while (bytes > 0 && bytes < text.size() &&
           (static_cast<unsigned char>(text[bytes]) & 0xc0) == 0x80)
      --bytes;
    prompt_.value.append(text.substr(0, bytes));
  }

  void choose_file(FilePurpose purpose, const std::filesystem::path &path) {
    config_.last_directory = path.parent_path();
    if (purpose == FilePurpose::Rom) {
      config_.rom = path;
      show_notice("Internal ROM selected");
    } else if (purpose == FilePurpose::Spi) {
      config_.spi = path;
      show_notice("SPI image selected");
    } else if (purpose == FilePurpose::Nand) {
      config_.nand = path;
      show_notice("NAND image selected");
    } else {
      launch_game(LibraryEntry{path});
      return;
    }
    persist();
  }

  void launch_game(const std::optional<LibraryEntry> &game) {
    const FirmwareStatus status = firmware_status(config_);
    if (!status.ready()) {
      show_notice(status.message() + ". Choose valid images below.", true);
      page_ = Page::Settings;
      return;
    }
    if (const auto issue = launch_validation_issue(config_)) {
      show_notice(issue->message, true);
      if (issue->section == ValidationSection::Advanced)
        page_ = Page::Advanced;
      else if (issue->section == ValidationSection::Outputs)
        page_ = Page::Outputs;
      else
        page_ = Page::Diagnostics;
      return;
    }
    if (game) {
      std::error_code error;
      if (!std::filesystem::is_regular_file(game->path, error)) {
        show_notice("Game file is missing: " + path_to_utf8(game->path), true);
        return;
      }
      remember_game(config_, *game);
    }
    persist();
    result_.action = LaunchAction::Boot;
    result_.options = make_launch_options(defaults_, config_, game);
    running_ = false;
  }

  void remove_library_entry(size_t index) {
    if (index >= config_.library.size())
      return;
    config_.library.erase(config_.library.begin() + std::ptrdiff_t(index));
    library_scroll_ =
        std::min(library_scroll_, std::max(0, int(config_.library.size()) - 1));
    persist();
  }

  void refresh_library() {
    config_ = load_config(config_path_, config_);
    selected_library_index_.reset();
    library_scroll_ = 0;
    show_notice("Recent games reloaded.");
  }

  void handle_drop(const char *raw_path) {
    if (!raw_path)
      return;
    const std::filesystem::path path = path_from_utf8(raw_path);
    SDL_free(const_cast<char *>(raw_path));
    const std::string extension = ascii_lower(path_to_utf8(path.extension()));
    if (extension == ".mba") {
      show_notice("MBA applications are not supported by this desktop release.",
                  true);
      return;
    }
    launch_game(LibraryEntry{path});
  }

  void handle_event(const SDL_Event &event) {
    if (event.type == SDL_QUIT) {
      result_.action = LaunchAction::Quit;
      running_ = false;
      return;
    }
    if (event.type == SDL_MOUSEMOTION) {
      mouse_x_ = event.motion.x;
      mouse_y_ = event.motion.y;
    }
    if (confirm_restore_) {
      if (event.type == SDL_KEYDOWN && !event.key.repeat) {
        if (event.key.keysym.sym == SDLK_ESCAPE)
          confirm_restore_ = false;
        else if (event.key.keysym.sym == SDLK_RETURN)
          restore_defaults();
      } else if (event.type == SDL_MOUSEBUTTONDOWN &&
                 event.button.button == SDL_BUTTON_LEFT) {
        if (contains({504, 398, 138, 42}, event.button.x, event.button.y))
          confirm_restore_ = false;
        else if (contains({654, 398, 166, 42}, event.button.x, event.button.y))
          restore_defaults();
      }
      return;
    }
    if (event.type == SDL_DROPFILE) {
      handle_drop(event.drop.file);
      return;
    }
    if (prompt_.open) {
      handle_prompt_event(event);
      return;
    }
    if (binding_capture_ && event.type == SDL_KEYDOWN && !event.key.repeat) {
      config_.input_bindings.keys[*binding_capture_] = event.key.keysym.sym;
      show_notice(std::string(kBindableControlNames[*binding_capture_]) +
                  " mapped to " + SDL_GetKeyName(event.key.keysym.sym));
      binding_capture_.reset();
      persist();
      return;
    }
    if (event.type == SDL_MOUSEWHEEL && page_ == Page::Library) {
      const int maximum = std::max(0, int(config_.library.size()) - 6);
      library_scroll_ = std::clamp(library_scroll_ - event.wheel.y, 0, maximum);
    }
    if (event.type == SDL_KEYDOWN && !event.key.repeat) {
      const SDL_Keycode key = event.key.keysym.sym;
      const bool control = (event.key.keysym.mod & KMOD_CTRL) != 0;
      if (key == SDLK_ESCAPE) {
        result_.action = LaunchAction::Quit;
        running_ = false;
      } else if (control && key == SDLK_o) {
        open_native_dialog(FilePurpose::Cartridge);
      } else if (key == SDLK_RETURN && page_ == Page::Library) {
        launch_game(std::nullopt);
      }
    }
    if (event.type == SDL_MOUSEBUTTONDOWN &&
        event.button.button == SDL_BUTTON_LEFT) {
      handle_click(event.button.x, event.button.y);
    }
  }

  void handle_click(int x, int y) {
    if (y < 92) {
      if (contains({12, 0, 88, 92}, x, y))
        open_native_dialog(FilePurpose::Cartridge);
      else if (contains({105, 0, 96, 92}, x, y))
        refresh_library();
      else if (contains({232, 0, 88, 92}, x, y) && selected_library_index_ &&
               *selected_library_index_ < config_.library.size())
        launch_game(config_.library[*selected_library_index_]);
      else if (contains({325, 0, 104, 92}, x, y))
        launch_game(std::nullopt);
      else if (contains({448, 0, 104, 92}, x, y)) {
        config_.fullscreen = !config_.fullscreen;
        persist();
      } else if (contains({692, 0, 96, 92}, x, y)) {
        const bool config_open = page_ == Page::Settings || page_ == Page::Advanced ||
                                 page_ == Page::Diagnostics || page_ == Page::Outputs ||
                                 page_ == Page::Board;
        page_ = config_open ? Page::Library : Page::Settings;
        notice_.clear();
      } else if (contains({795, 0, 112, 92}, x, y)) {
        page_ = page_ == Page::Controls ? Page::Library : Page::Controls;
        notice_.clear();
      }
      return;
    }
    if (page_ == Page::Library)
      handle_library_click(x, y);
    else if (page_ == Page::Settings)
      handle_settings_click(x, y);
    else if (page_ == Page::Controls)
      handle_controls_click(x, y);
    else if (page_ == Page::Advanced)
      handle_advanced_click(x, y);
    else if (page_ == Page::Diagnostics)
      handle_diagnostics_click(x, y);
    else if (page_ == Page::Outputs)
      handle_outputs_click(x, y);
    else if (page_ == Page::Board)
      handle_board_click(x, y);
  }

  void handle_library_click(int x, int y) {
    constexpr int row_y = 150;
    constexpr int row_height = 66;
    for (int visible = 0; visible < 6; ++visible) {
      const size_t index = size_t(library_scroll_ + visible);
      if (index >= config_.library.size())
        break;
      const SDL_Rect row{32, row_y + visible * row_height, 960, 56};
      const SDL_Rect remove{944, row.y + 12, 32, 32};
      if (contains(remove, x, y))
        remove_library_entry(index);
      else if (contains(row, x, y)) {
        if (selected_library_index_ && *selected_library_index_ == index)
          launch_game(config_.library[index]);
        else
          selected_library_index_ = index;
      }
    }
  }

  void handle_settings_click(int x, int y) {
    if (contains({180, 175, 52, 28}, x, y))
      config_.audio = !config_.audio;
    else if (contains({240, 226, 52, 28}, x, y))
      config_.show_speed = !config_.show_speed;
    else if (contains({595, 175, 52, 28}, x, y))
      config_.fullscreen = !config_.fullscreen;
    else if (contains({595, 226, 52, 28}, x, y))
      config_.integer_scaling = !config_.integer_scaling;
    else if (contains({552, 277, 38, 34}, x, y))
      config_.window_scale = std::max(1, config_.window_scale - 1);
    else if (contains({650, 277, 38, 34}, x, y))
      config_.window_scale = std::min(6, config_.window_scale + 1);
    else if (contains({844, 380, 132, 34}, x, y))
      open_native_dialog(FilePurpose::Rom);
    else if (contains({844, 425, 132, 34}, x, y))
      open_native_dialog(FilePurpose::Spi);
    else if (contains({844, 470, 132, 34}, x, y))
      open_native_dialog(FilePurpose::Nand);
    else if (contains({810, 630, 166, 34}, x, y))
      page_ = Page::Advanced;
    else
      return;
    persist();
  }

  void handle_advanced_click(int x, int y) {
    if (contains({810, 630, 166, 34}, x, y))
      page_ = Page::Settings;
    else if (contains({430, 120, 52, 28}, x, y)) {
      config_.log = !config_.log;
      if (!config_.log) {
        config_.trace = false;
        config_.trace_transitions = false;
      }
      persist();
    } else if (contains({430, 169, 52, 28}, x, y)) {
      config_.trace = !config_.trace;
      if (config_.trace)
        config_.log = true;
      persist();
    } else if (contains({430, 218, 52, 28}, x, y)) {
      config_.trace_transitions = !config_.trace_transitions;
      if (config_.trace_transitions)
        config_.log = true;
      persist();
    } else if (contains({780, 120, 180, 36}, x, y)) {
      config_.rom_endian = config_.rom_endian == "le" ? "be" : "le";
      persist();
    } else if (contains({780, 169, 52, 28}, x, y)) {
      config_.rom_shadow_low = !config_.rom_shadow_low;
      persist();
    } else if (contains({780, 218, 52, 28}, x, y)) {
      config_.rom_fetch_mirror64 = !config_.rom_fetch_mirror64;
      persist();
    } else if (contains({780, 267, 52, 28}, x, y)) {
      config_.allow_invalid_alu_nop = !config_.allow_invalid_alu_nop;
      persist();
    } else if (contains({780, 316, 52, 28}, x, y)) {
      config_.auto_power_wake = !config_.auto_power_wake;
      persist();
    } else if (contains({780, 365, 52, 28}, x, y)) {
      config_.dump_memory_dma = !config_.dump_memory_dma;
      persist();
    } else if (contains({242, 500, 344, 66}, x, y)) {
      page_ = Page::Diagnostics;
    } else if (contains({600, 500, 360, 66}, x, y)) {
      page_ = Page::Board;
    }
  }

  void handle_diagnostics_click(int x, int y) {
    if (contains({620, 630, 176, 34}, x, y)) {
      page_ = Page::Outputs;
      return;
    }
    if (contains({810, 630, 166, 34}, x, y)) {
      page_ = Page::Advanced;
      return;
    }
    constexpr int first_y = 102;
    constexpr int spacing = 49;
    if (x < 500 || x >= 976 || y < first_y || y >= first_y + spacing * 9)
      return;
    const int row = (y - first_y) / spacing;
    switch (row) {
    case 0:
      begin_prompt(EditField::MaxSteps, "INSTRUCTION LIMIT",
                   std::to_string(config_.max_steps));
      break;
    case 1:
      begin_prompt(EditField::OpenWindowAt, "DEFER WINDOW UNTIL INSTRUCTION",
                   std::to_string(config_.open_window_at));
      break;
    case 2:
      begin_prompt(EditField::LogPath, "LOG OUTPUT PATH",
                   path_to_utf8(config_.log_path));
      break;
    case 3:
      begin_prompt(EditField::TraceLimit, "TRACE LINE LIMIT",
                   std::to_string(config_.trace_limit));
      break;
    case 4:
      begin_prompt(EditField::TransitionLimit, "TRANSITION TRACE LIMIT",
                   std::to_string(config_.trace_transition_limit));
      break;
    case 5:
      begin_prompt(EditField::DumpFrame, "FINAL FRAME BMP PATH",
                   path_to_utf8(config_.dump_frame));
      break;
    case 6:
      begin_prompt(EditField::DumpMemory, "MEMORY DUMP PATH",
                   path_to_utf8(config_.dump_memory));
      break;
    case 7:
      begin_prompt(EditField::DumpMemoryBase, "MEMORY DUMP BASE",
                   hex_value(config_.dump_memory_base, 6));
      break;
    case 8:
      begin_prompt(EditField::DumpMemoryWords, "MEMORY DUMP WORD COUNT",
                   std::to_string(config_.dump_memory_words));
      break;
    }
  }

  void handle_outputs_click(int x, int y) {
    if (contains({810, 630, 166, 34}, x, y)) {
      page_ = Page::Diagnostics;
      return;
    }
    constexpr int first_y = 90;
    constexpr int spacing = 43;
    if (x < 500 || x >= 976 || y < first_y || y >= first_y + spacing * 11)
      return;
    const int row = (y - first_y) / spacing;
    switch (row) {
    case 0:
      begin_prompt(EditField::StartPc, "START PC - EMPTY USES RESET VECTOR",
                   config_.start_pc_set ? hex_value(config_.start_pc, 6) : "");
      break;
    case 1:
      begin_prompt(EditField::StartLoggingAt, "START LOGGING AT INSTRUCTION",
                   std::to_string(config_.start_logging_at));
      break;
    case 2:
      begin_prompt(EditField::TraceStartInsn, "START TRACE AT INSTRUCTION",
                   std::to_string(config_.trace_start_insn));
      break;
    case 3:
      begin_prompt(EditField::TraceLo, "TRACE PC LOW - EMPTY DISABLES RANGE",
                   config_.trace_range ? hex_value(config_.trace_lo, 6) : "");
      break;
    case 4:
      begin_prompt(EditField::TraceHi, "TRACE PC HIGH - EMPTY DISABLES RANGE",
                   config_.trace_range ? hex_value(config_.trace_hi, 6) : "");
      break;
    case 5:
      begin_prompt(EditField::DumpCurrentFrame, "CURRENT FRAME BMP PATH",
                   path_to_utf8(config_.dump_current_frame));
      break;
    case 6:
      begin_prompt(EditField::DumpFrameDir, "FRAME SEQUENCE DIRECTORY",
                   path_to_utf8(config_.dump_frame_dir));
      break;
    case 7:
      begin_prompt(EditField::DumpFrameInterval, "FRAME SEQUENCE INTERVAL",
                   std::to_string(config_.dump_frame_interval));
      break;
    case 8:
      begin_prompt(EditField::DumpCode, "CODE DUMP PATH",
                   path_to_utf8(config_.dump_code));
      break;
    case 9:
      begin_prompt(EditField::DumpCodeBase, "CODE DUMP BASE",
                   hex_value(config_.dump_code_base, 6));
      break;
    case 10:
      begin_prompt(EditField::DumpCodeWords, "CODE DUMP WORD COUNT",
                   std::to_string(config_.dump_code_words));
      break;
    }
  }

  void handle_board_click(int x, int y) {
    if (contains({596, 630, 200, 34}, x, y)) {
      confirm_restore_ = true;
      return;
    }
    if (contains({810, 630, 166, 34}, x, y)) {
      page_ = Page::Advanced;
      return;
    }
    constexpr int first_y = 102;
    constexpr int spacing = 49;
    if (x < 500 || x >= 976 || y < first_y || y >= first_y + spacing * 10)
      return;
    const int row = (y - first_y) / spacing;
    const std::array<std::pair<EditField, uint16_t>, 9> fields{{
        {EditField::Efuse0, config_.efuse0},
        {EditField::Efuse1, config_.efuse1},
        {EditField::Efuse2, config_.efuse2},
        {EditField::GpioA, config_.gpio_a},
        {EditField::GpioB, config_.gpio_b},
        {EditField::GpioC, config_.gpio_c},
        {EditField::GpioD, config_.gpio_d},
        {EditField::GpioE, config_.gpio_e},
        {EditField::BatteryAdc, config_.battery_adc},
    }};
    if (row == 0) {
      begin_prompt(EditField::RomBase, "INTERNAL ROM BASE",
                   hex_value(config_.rom_base, 6));
    } else if (row <= int(fields.size())) {
      static constexpr std::array<const char *, 9> titles{{
          "E-FUSE 0",
          "E-FUSE 1",
          "E-FUSE 2",
          "GPIO A INPUT",
          "GPIO B INPUT",
          "GPIO C INPUT",
          "GPIO D INPUT",
          "GPIO E INPUT",
          "BATTERY ADC",
      }};
      begin_prompt(fields[size_t(row - 1)].first, titles[size_t(row - 1)],
                   hex_value(fields[size_t(row - 1)].second, 4));
    }
  }

  void restore_defaults() {
    const std::filesystem::path rom = config_.rom;
    const std::filesystem::path spi = config_.spi;
    const std::filesystem::path nand = config_.nand;
    const std::filesystem::path last_directory = config_.last_directory;
    std::vector<LibraryEntry> library = std::move(config_.library);
    config_ = config_from_defaults(defaults_);
    config_.rom = rom;
    config_.spi = spi;
    config_.nand = nand;
    config_.last_directory = last_directory;
    config_.library = std::move(library);
    confirm_restore_ = false;
    show_notice("Emulator settings restored; games and firmware were kept.");
    persist();
  }

  void render() {
    Painter painter{renderer_, mouse_x_, mouse_y_};
    painter.fill({0, 0, kWindowWidth, kWindowHeight}, kBackground);
    render_toolbar(painter);
    if (page_ == Page::Library)
      render_library(painter);
    else if (page_ == Page::Settings)
      render_settings(painter);
    else if (page_ == Page::Controls)
      render_controls(painter);
    else if (page_ == Page::Advanced)
      render_advanced(painter);
    else if (page_ == Page::Diagnostics)
      render_diagnostics(painter);
    else if (page_ == Page::Outputs)
      render_outputs(painter);
    else
      render_board(painter);
    render_notice(painter);
    if (prompt_.open)
      render_prompt(painter);
    if (confirm_restore_)
      render_restore_confirmation(painter);
    if (!test_screenshot_.empty()) {
      SDL_RenderFlush(renderer_);
      SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(
          0, kWindowWidth, kWindowHeight, 32, SDL_PIXELFORMAT_ARGB8888);
      if (!surface ||
          SDL_RenderReadPixels(renderer_, nullptr, SDL_PIXELFORMAT_ARGB8888,
                               surface->pixels, surface->pitch) != 0 ||
          SDL_SaveBMP(surface, path_to_utf8(test_screenshot_).c_str()) != 0) {
        if (surface)
          SDL_FreeSurface(surface);
        die(std::string("failed to capture frontend test image: ") +
            SDL_GetError());
      }
      SDL_FreeSurface(surface);
      test_screenshot_.clear();
      result_.action = LaunchAction::Quit;
      running_ = false;
    }
    SDL_RenderPresent(renderer_);
  }

  void render_toolbar_item(const Painter &painter, const SDL_Rect &area,
                           std::string_view label,
                           bool enabled = true, bool active = false) const {
    const bool hovered = enabled && contains(area, painter.mouse_x, painter.mouse_y);
    if (hovered || active)
      painter.fill(area, active ? SDL_Color{61, 79, 83, 255} : kPanel);
    const int width = system_text_width(label, 2);
    painter.text(area.x + (area.w - width) / 2,
                 area.y + (area.h - system_text_height(2)) / 2, label,
                 enabled ? kText : SDL_Color{96, 96, 96, 255}, 2);
  }

  void render_toolbar(const Painter &painter) const {
    constexpr int height = 92;
    painter.fill({0, 0, kWindowWidth, height}, kToolbar);
    painter.line(0, height - 1, kWindowWidth, height - 1, kBorder);
    const bool game_selected = selected_library_index_ &&
        *selected_library_index_ < config_.library.size();
    render_toolbar_item(painter, {12, 0, 88, height}, "Open");
    render_toolbar_item(painter, {105, 0, 96, height}, "Refresh");
    render_toolbar_item(painter, {232, 0, 88, height}, "Play",
                        game_selected);
    render_toolbar_item(painter, {325, 0, 104, height}, "System");
    render_toolbar_item(painter, {448, 0, 104, height}, "FullScr", true,
                        config_.fullscreen);
    render_toolbar_item(painter, {557, 0, 100, height}, "ScrShot", false);
    render_toolbar_item(painter, {692, 0, 96, height}, "Config", true,
                        page_ == Page::Settings || page_ == Page::Advanced ||
                            page_ == Page::Diagnostics || page_ == Page::Outputs ||
                            page_ == Page::Board);
    render_toolbar_item(painter, {795, 0, 112, height}, "Keybinds", true,
                        page_ == Page::Controls);
  }

  void render_header(const Painter &painter, std::string_view title,
                     std::string_view subtitle) const {
    (void)subtitle;
    painter.text(32, 116, title, kText, 3);
  }

  void render_notice(const Painter &painter) const {
    if (notice_.empty())
      return;
    const SDL_Color color = notice_is_error_ ? kDanger : kAccent;
    constexpr SDL_Rect area{32, 630, 500, 34};
    painter.fill(area, SDL_Color{color.r, color.g, color.b, 35});
    painter.outline(area, color);
    painter.text(area.x + 12, area.y + 11, abbreviated(notice_, 51), color, 1);
  }

  void render_library(const Painter &painter) const {
    const FirmwareStatus status = firmware_status(config_);
    if (config_.library.empty()) {
      painter.text(300, 315, "RECENT GAMES WILL SHOW HERE.", kText, 2);
      if (!status.ready())
        painter.text(380, 350, "CONFIGURE FIRMWARE IN CONFIG.",
                     kWarning, 1);
    }
    constexpr int row_y = 150;
    constexpr int row_height = 66;
    for (int visible = 0; visible < 6; ++visible) {
      const size_t index = size_t(library_scroll_ + visible);
      if (index >= config_.library.size())
        break;
      const LibraryEntry &entry = config_.library[index];
      const SDL_Rect row{32, row_y + visible * row_height, 960, 56};
      const bool hovered = contains(row, painter.mouse_x, painter.mouse_y);
      const bool selected = selected_library_index_ &&
                            *selected_library_index_ == index;
      painter.fill(row, selected ? SDL_Color{51, 78, 82, 255}
                                 : hovered ? kPanelHover : kPanel);
      painter.outline(row, selected ? kAccent : kBorder);
      painter.fill({row.x, row.y, 5, row.h}, selected ? kAccent : kMuted);
      std::error_code error;
      const bool exists = std::filesystem::is_regular_file(entry.path, error);
      painter.text(row.x + 24, row.y + 10,
                   abbreviated(display_name(entry.path), 64),
                   exists ? kText : kDanger, 2);
      painter.text(row.x + 24, row.y + 34,
                   abbreviated(path_to_utf8(entry.path.parent_path()), 105),
                   kMuted, 1);
      painter.text(row.x + 808, row.y + 23, "CARTRIDGE", kAccent, 1);
      painter.button({944, row.y + 12, 32, 32}, "X");
    }
  }

  void render_settings(const Painter &painter) const {
    render_header(painter, "SETTINGS",
                  "ACCURATE EMULATION WITH PLAYER-FRIENDLY OPTIONS");

    painter.text(32, 183, "AUDIO", kText, 2);
    painter.toggle({180, 175, 52, 28}, config_.audio);
    painter.text(32, 234, "SHOW SPEED", kText, 2);
    painter.toggle({240, 226, 52, 28}, config_.show_speed);

    painter.text(390, 183, "FULLSCREEN", kText, 2);
    painter.toggle({595, 175, 52, 28}, config_.fullscreen);
    painter.text(390, 234, "INTEGER SCALE", kText, 2);
    painter.toggle({595, 226, 52, 28}, config_.integer_scaling);
    painter.text(390, 285, "WINDOW SCALE", kText, 2);
    painter.button({552, 277, 38, 34}, "-");
    painter.text(612, 286, std::to_string(config_.window_scale) + "X", kAccent,
                 2);
    painter.button({650, 277, 38, 34}, "+");

    painter.text(32, 355, "FIRMWARE", kMuted, 1);
    const FirmwareStatus status = firmware_status(config_);
    render_firmware_row(painter, 380, "INTERNAL ROM", config_.rom, status.rom);
    render_firmware_row(painter, 425, "SPI IMAGE", config_.spi, status.spi);
    render_firmware_row(painter, 470, "NAND IMAGE", config_.nand, status.nand);
    painter.text(32, 515, "128 KIB ROM · 2 MIB SPI · 132 MIB NAND",
                 kMuted, 1);
    painter.button({810, 630, 166, 34}, "ADVANCED >");
  }

  void render_firmware_row(const Painter &painter, int y,
                           std::string_view label,
                           const std::filesystem::path &path,
                           FirmwareFileState state) const {
    painter.fill({32, y, 944, 36}, kPanel);
    painter.text(44, y + 11, label, kText, 1);
    const SDL_Color state_color = state == FirmwareFileState::Ready ? kAccent
                                  : state == FirmwareFileState::WrongSize
                                      ? kDanger
                                      : kWarning;
    painter.text(155, y + 11,
                 state == FirmwareFileState::Ready       ? "OK"
                 : state == FirmwareFileState::WrongSize ? "WRONG SIZE"
                                                         : "MISSING",
                 state_color, 1);
    painter.text(248, y + 11, abbreviated(path_to_utf8(path), 66), kMuted, 1);
    painter.button({844, y, 132, 34}, "BROWSE");
  }

  void render_controls(const Painter &painter) const {
    render_header(painter, "CONTROLS",
                  "CLICK A BINDING, THEN PRESS THE KEY YOU WANT TO USE");
    painter.fill({32, 172, 944, 26}, SDL_Color{52, 52, 52, 255});
    painter.text(44, 181, "MOBIGO CONTROL", kAccent, 1);
    painter.text(464, 181, "KEYBOARD BINDING", kAccent, 1);
    constexpr int first_y = 206;
    constexpr int row_height = 27;
    for (size_t index = 0; index < kBindableControlCount; ++index) {
      const int y = first_y + int(index) * row_height;
      painter.fill({32, y, 944, 25}, index % 2 ? kPanel : kBackground);
      painter.text(44, y + 8, kBindableControlNames[index], kText, 1);
      const bool capturing = binding_capture_ && *binding_capture_ == index;
      painter.button({430, y - 1, 546, 27},
                     capturing ? "PRESS A KEY..."
                               : SDL_GetKeyName(config_.input_bindings.keys[index]),
                     false, capturing);
    }
    painter.button({790, 622, 186, 34}, "RESTORE DEFAULTS");
  }

  void handle_controls_click(int x, int y) {
    if (contains({790, 622, 186, 34}, x, y)) {
      config_.input_bindings = InputBindings{};
      binding_capture_.reset();
      show_notice("Keyboard controls restored to defaults.");
      persist();
      return;
    }
    constexpr int first_y = 206;
    constexpr int row_height = 27;
    if (x < 430 || x >= 976 || y < first_y - 1 ||
        y >= first_y + int(kBindableControlCount) * row_height)
      return;
    const size_t index = size_t((y - (first_y - 1)) / row_height);
    if (index >= kBindableControlCount)
      return;
    binding_capture_ = index;
    show_notice(std::string("Press a key for ") +
                std::string(kBindableControlNames[index]) + ".");
  }

  void render_advanced(const Painter &painter) const {
    render_header(painter, "ADVANCED",
                  "DEVELOPER AND HARDWARE OVERRIDES - CHANGE WITH CARE");
    painter.text(242, 96, "RUNTIME / DIAGNOSTICS", kMuted, 1);
    advanced_toggle(painter, 120, "WRITE LOG", config_.log, 430);
    advanced_toggle(painter, 169, "INSTRUCTION TRACE", config_.trace, 430);
    advanced_toggle(painter, 218, "TRANSITION TRACE", config_.trace_transitions,
                    430);
    painter.text(242, 419, "LOG PATH", kMuted, 1);
    painter.text(242, 441, abbreviated(path_to_utf8(config_.log_path), 55),
                 kText, 1);
    painter.text(242, 463,
                 "TRACE LIMITS: " + std::to_string(config_.trace_limit) +
                     " / " + std::to_string(config_.trace_transition_limit),
                 kMuted, 1);

    painter.text(610, 96, "ROM / BOARD", kMuted, 1);
    advanced_choice(painter, 120, "ROM ENDIAN", config_.rom_endian, 780);
    advanced_toggle(painter, 169, "ROM SHADOW LOW", config_.rom_shadow_low,
                    780);
    advanced_toggle(painter, 218, "64K FETCH MIRROR",
                    config_.rom_fetch_mirror64, 780);
    advanced_toggle(painter, 267, "INVALID ALU AS NOP",
                    config_.allow_invalid_alu_nop, 780);
    advanced_toggle(painter, 316, "AUTO POWER WAKE", config_.auto_power_wake,
                    780);
    advanced_toggle(painter, 365, "MEMORY DUMP DMA", config_.dump_memory_dma,
                    780);

    painter.button({242, 500, 344, 66}, "DIAGNOSTIC VALUES >");
    painter.button({600, 500, 360, 66}, "BOARD OVERRIDES >");
    painter.text(
        242, 585,
        "REPEATABLE SCRIPTED TOUCH/KEY INPUT REMAINS CLI TEST AUTOMATION.",
        kMuted, 1);
    painter.button({810, 630, 166, 34}, "< SETTINGS");
  }

  void render_diagnostics(const Painter &painter) const {
    render_header(painter, "DIAGNOSTIC VALUES",
                  "CLICK A VALUE TO EDIT; DECIMAL AND 0X HEX ARE ACCEPTED");
    const std::array<std::pair<std::string, std::string>, 9> rows{{
        {"INSTRUCTION LIMIT (0=NONE)", std::to_string(config_.max_steps)},
        {"OPEN WINDOW AT (0=NOW)", std::to_string(config_.open_window_at)},
        {"LOG OUTPUT PATH", path_to_utf8(config_.log_path)},
        {"TRACE LINE LIMIT", std::to_string(config_.trace_limit)},
        {"TRANSITION TRACE LIMIT",
         std::to_string(config_.trace_transition_limit)},
        {"FINAL FRAME BMP PATH", path_to_utf8(config_.dump_frame)},
        {"MEMORY DUMP PATH", path_to_utf8(config_.dump_memory)},
        {"MEMORY DUMP BASE", hex_value(config_.dump_memory_base, 6)},
        {"MEMORY DUMP WORDS", std::to_string(config_.dump_memory_words)},
    }};
    render_edit_rows(painter, rows);
    painter.text(242, 606, "EMPTY OUTPUT PATHS DISABLE THAT DUMP.", kMuted, 1);
    painter.button({620, 630, 176, 34}, "MORE OUTPUTS >");
    painter.button({810, 630, 166, 34}, "< ADVANCED");
  }

  void render_outputs(const Painter &painter) const {
    render_header(
        painter, "OUTPUTS / AUTOMATION",
        "PERSISTENT NON-INTERACTIVE CONTROLS; EMPTY PATHS DISABLE OUTPUT");
    const std::array<std::pair<std::string, std::string>, 11> rows{{
        {"START PC", config_.start_pc_set ? hex_value(config_.start_pc, 6)
                                          : "<RESET VECTOR>"},
        {"START LOGGING AT", std::to_string(config_.start_logging_at)},
        {"START TRACE AT", std::to_string(config_.trace_start_insn)},
        {"TRACE PC LOW",
         config_.trace_range ? hex_value(config_.trace_lo, 6) : "<DISABLED>"},
        {"TRACE PC HIGH",
         config_.trace_range ? hex_value(config_.trace_hi, 6) : "<DISABLED>"},
        {"CURRENT FRAME BMP", path_to_utf8(config_.dump_current_frame)},
        {"FRAME SEQUENCE DIR", path_to_utf8(config_.dump_frame_dir)},
        {"FRAME INTERVAL", std::to_string(config_.dump_frame_interval)},
        {"CODE DUMP PATH", path_to_utf8(config_.dump_code)},
        {"CODE DUMP BASE", hex_value(config_.dump_code_base, 6)},
        {"CODE DUMP WORDS", std::to_string(config_.dump_code_words)},
    }};
    constexpr int first_y = 90;
    constexpr int spacing = 43;
    for (size_t index = 0; index < rows.size(); ++index) {
      const int y = first_y + int(index) * spacing;
      painter.text(242, y + 12, rows[index].first, kText, 1);
      painter.button({500, y, 476, 34}, abbreviated(rows[index].second.empty()
                                                        ? "<DISABLED>"
                                                        : rows[index].second,
                                                    36));
    }
    painter.text(242, 580,
                 "REPEATABLE TOUCH/KEY SCRIPTS REMAIN CLI TEST AUTOMATION.",
                 kMuted, 1);
    painter.button({810, 630, 166, 34}, "< DIAGNOSTICS");
  }

  void render_board(const Painter &painter) const {
    render_header(
        painter, "BOARD OVERRIDES",
        "EXPERT HARDWARE INPUTS - DEFAULTS MATCH THE RETAIL MOBIGO 2");
    const std::array<std::pair<std::string, std::string>, 10> rows{{
        {"INTERNAL ROM BASE", hex_value(config_.rom_base, 6)},
        {"E-FUSE 0", hex_value(config_.efuse0, 4)},
        {"E-FUSE 1", hex_value(config_.efuse1, 4)},
        {"E-FUSE 2", hex_value(config_.efuse2, 4)},
        {"GPIO A INPUT", hex_value(config_.gpio_a, 4)},
        {"GPIO B INPUT", hex_value(config_.gpio_b, 4)},
        {"GPIO C INPUT", hex_value(config_.gpio_c, 4)},
        {"GPIO D INPUT", hex_value(config_.gpio_d, 4)},
        {"GPIO E INPUT", hex_value(config_.gpio_e, 4)},
        {"BATTERY ADC", hex_value(config_.battery_adc, 4)},
    }};
    render_edit_rows(painter, rows);
    painter.text(242, 606,
                 "RESTORE DEFAULTS KEEPS FIRMWARE PATHS AND YOUR LIBRARY.",
                 kWarning, 1);
    painter.button({596, 630, 200, 34}, "RESTORE DEFAULTS");
    painter.button({810, 630, 166, 34}, "< ADVANCED");
  }

  template <size_t Count>
  void render_edit_rows(const Painter &painter,
                        const std::array<std::pair<std::string, std::string>,
                                         Count> &rows) const {
    constexpr int first_y = 102;
    constexpr int spacing = 49;
    for (size_t index = 0; index < rows.size(); ++index) {
      const int y = first_y + int(index) * spacing;
      painter.text(242, y + 13, rows[index].first, kText, 1);
      painter.button({500, y, 476, 36}, abbreviated(rows[index].second.empty()
                                                        ? "<DISABLED>"
                                                        : rows[index].second,
                                                    36));
    }
  }

  void advanced_toggle(const Painter &painter, int y, std::string_view label,
                       bool enabled, int x) const {
    painter.text(x == 430 ? 242 : 610, y + 8, label, kText, 1);
    painter.toggle({x, y, 52, 28}, enabled);
  }

  void advanced_choice(const Painter &painter, int y, std::string_view label,
                       std::string_view value, int x) const {
    painter.text(x == 430 ? 242 : 610, y + 12, label, kText, 1);
    painter.button({x, y, 180, 36}, value);
  }

  void render_prompt(const Painter &painter) const {
    painter.fill({0, 0, kWindowWidth, kWindowHeight}, SDL_Color{2, 5, 10, 205});
    painter.fill({190, 210, 644, 230}, SDL_Color{22, 32, 47, 255});
    painter.outline({190, 210, 644, 230}, kBorder);
    painter.text(220, 240, prompt_.title, kText, 2);
    painter.text(220, 276, "TYPE A VALUE, THEN PRESS ENTER", kMuted, 1);
    painter.fill({220, 308, 584, 48}, kBackground);
    painter.outline({220, 308, 584, 48}, kAccent);
    painter.text(234, 325, abbreviated(prompt_.value + "_", 67), kText, 1);
    if (!prompt_.error.empty())
      painter.text(220, 374, abbreviated(prompt_.error, 75), kDanger, 1);
    painter.text(220, 411, "ENTER SAVE    ESC CANCEL    CTRL/CMD+V PASTE",
                 kMuted, 1);
  }

  void render_restore_confirmation(const Painter &painter) const {
    painter.fill({0, 0, kWindowWidth, kWindowHeight}, SDL_Color{2, 5, 10, 205});
    painter.fill({204, 238, 616, 220}, SDL_Color{22, 32, 47, 255});
    painter.outline({204, 238, 616, 220}, kBorder);
    painter.text(238, 270, "RESTORE EMULATOR DEFAULTS?", kText, 2);
    painter.text(238, 315, "FIRMWARE PATHS AND YOUR GAME LIBRARY WILL BE KEPT.",
                 kMuted, 1);
    painter.text(238, 340,
                 "DISPLAY, DIAGNOSTIC, AND BOARD SETTINGS WILL RESET.",
                 kWarning, 1);
    painter.button({504, 398, 138, 42}, "CANCEL");
    painter.button({654, 398, 166, 42}, "RESTORE", true);
  }
};

} // namespace

LaunchRequest run_launcher(const Options &packaged_defaults,
                           const char *argv0) {
  Launcher launcher(packaged_defaults, argv0);
  return launcher.run();
}

#endif

} // namespace mobigo::desktop
