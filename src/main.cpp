#include "audio.hpp"
#include "boot.hpp"
#include "desktop_frontend.hpp"
#include "game_controller.hpp"
#include "mba_overlay.hpp"
#include "realtime_throttle.hpp"
#include "video.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

using namespace mobigo;

namespace {

struct SessionResources {
  Bus &bus;
  Video &video;
  Audio &audio;
  GameControllerInput &game_controller;
  bool cleaned = false;

  void shutdown() noexcept {
    if (cleaned)
      return;
    cleaned = true;
    audio.shutdown();
    game_controller.close(bus);
    if (video.win)
      video.shutdown();
    else if (SDL_WasInit(0))
      SDL_Quit();
  }

  ~SessionResources() { shutdown(); }
};

} // namespace

int main(int argc, char **argv) {
  const bool desktop_session = desktop::should_open_launcher(
      argc, desktop::launcher_disabled_by_environment());
  try {
    for (;;) {
      g_log.close();
      g_log.clear();
      Options opt = parse_args(argc, argv);
      resolve_packaged_firmware(opt, argv[0]);
      if (desktop_session) {
        const desktop::LaunchRequest request =
            desktop::run_launcher(opt, argv[0]);
        if (request.action == desktop::LaunchAction::Quit)
          return 0;
        opt = request.options;
      }
      try {
        if (opt.log && opt.start_logging_at == 0) {
          g_log.open(opt.log_path, std::ios::out | std::ios::trunc);
          if (!g_log)
            die("failed to open log file " + path_to_utf8(opt.log_path));
        }
        Bus bus;
        auto apply_board_settings = [&]() {
          bus.gpio_a_input = opt.gpio_a;
          bus.gpio_b_input = opt.gpio_b;
          bus.gpio_c_input = opt.gpio_c;
          bus.gpio_d_input = opt.gpio_d;
          bus.gpio_e_input = opt.gpio_e;
          bus.battery_adc = opt.battery_adc;
          bus.mmio[0x7ae0 - kMmioBase] = opt.efuse0;
          bus.mmio[0x7ae1 - kMmioBase] = opt.efuse1;
          bus.mmio[0x7ae2 - kMmioBase] = opt.efuse2;
          bus.internal_rom_base = opt.rom_base;
          bus.internal_rom_shadow_low = opt.rom_shadow_low;
          bus.internal_rom_fetch_mirror64 = opt.rom_fetch_mirror64;
        };
        apply_board_settings();
        bool rom_big_endian = false;
        if (opt.rom_endian == "be")
          rom_big_endian = true;
        else if (opt.rom_endian == "le")
          rom_big_endian = false;
        else
          die("unknown ROM endian: " + opt.rom_endian);
        bus.internal_rom =
            bytes_to_words(read_file_bytes(opt.rom), rom_big_endian);
        if (!opt.cart.empty()) {
          const auto cart_words =
              bytes_to_words(read_file_bytes(opt.cart), false);
          bus.cart_mem = cart_words;
        }
        bus.spi.bytes = read_file_bytes(opt.spi);
        bus.nand.bytes = read_file_bytes(opt.nand);
        std::optional<MbaOverlayReport> mba_overlay;
        if (!opt.mba.empty()) {
          mba_overlay = apply_mba_overlay(
              bus.nand.bytes, read_file_bytes(opt.mba), opt.mba_target);
          const MbaOverlayReport &overlay = *mba_overlay;
          bus.configure_mba_application_target(overlay.entry_address, true);
          std::cout << "Applied transient MBA overlay: "
                    << path_to_utf8(opt.mba) << " (" << overlay.mba_bytes
                    << " bytes, target=" << mba_target_name(overlay.target)
                    << ", role="
                    << (overlay.role.empty() ? "<untitled>" : overlay.role)
                    << ", entry=0x" << std::hex << overlay.entry_address
                    << std::dec << ")\nReplaced ";
          for (size_t i = 0; i < overlay.paths.size(); ++i) {
            if (i != 0)
              std::cout << ", ";
            std::cout << overlay.paths[i];
          }
          std::cout << " in " << overlay.filesystem_snapshots
                    << " filesystem snapshot(s); source NAND unchanged\n";
        }
        if (g_log) {
          g_log << "Hardware config: E-Fuse0=0x" << std::hex << opt.efuse0
                << " E-Fuse1=0x" << opt.efuse1 << " E-Fuse2=0x" << opt.efuse2
                << " GPIO-A=0x" << opt.gpio_a << " GPIO-B=0x" << opt.gpio_b
                << " GPIO-C=0x" << opt.gpio_c << " GPIO-D=0x" << opt.gpio_d
                << " GPIO-E=0x" << opt.gpio_e << std::dec << "\n";
        }
        Cpu cpu(bus);
        cpu.trace = opt.trace;
        cpu.trace_range = opt.trace_range;
        cpu.trace_lo = opt.trace_lo;
        cpu.trace_hi = opt.trace_hi;
        cpu.trace_limit = opt.trace_limit;
        cpu.trace_start_insn = opt.trace_start_insn;
        cpu.trace_transitions = opt.trace_transitions;
        cpu.trace_transition_limit = opt.trace_transition_limit;
        cpu.allow_invalid_alu_nop = opt.allow_invalid_alu_nop;
        // The history rings are diagnostic-only. Fast, unlogged execution can
        // skip their per-instruction modulo/branch bookkeeping without
        // changing any guest-visible CPU or MMIO state.
        cpu.track_recent_history = true;
        rom_boot(bus, cpu, opt.start_pc, opt.start_pc_set);

        Video video;
        Audio audio;
        GameControllerInput game_controller;
        SessionResources session_resources{bus, video, audio, game_controller};
        bool fullscreen_active = opt.fullscreen;
        std::string session_title = "MobiGo 2 Emulator";
        auto initialize_video = [&]() {
          if (desktop_session) {
            SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,
                        opt.integer_scaling ? "0" : "1");
          }
          video.init(opt.vsync);
          if (!desktop_session)
            return;
          SDL_SetWindowMinimumSize(video.win, Video::W, Video::H);
          SDL_SetWindowSize(video.win, Video::W * opt.window_scale,
                            Video::H * opt.window_scale);
          SDL_RenderSetLogicalSize(video.ren, Video::W, Video::H);
          SDL_RenderSetIntegerScale(video.ren,
                                    opt.integer_scaling ? SDL_TRUE : SDL_FALSE);
          if (fullscreen_active) {
            if (SDL_SetWindowFullscreen(video.win,
                                        SDL_WINDOW_FULLSCREEN_DESKTOP) != 0)
              std::cerr << "warning: fullscreen unavailable: " << SDL_GetError()
                        << '\n';
          }
          if (!opt.cart.empty())
            session_title += " - " + path_to_utf8(opt.cart.stem());
          else if (!opt.mba.empty())
            session_title += " - " + path_to_utf8(opt.mba.stem());
          SDL_SetWindowTitle(video.win, session_title.c_str());
        };
        const bool deferred_for_mba = opt.window && opt.open_window_on_mba;
        bool window_active = opt.window && !deferred_for_mba &&
                             opt.open_window_at == 0;
        if (window_active)
          initialize_video();
#ifdef __EMSCRIPTEN__
        RealtimeThrottle realtime(false);
#else
        // Headless runs are intentionally uncapped. Windowed runs follow the
        // emulated GPL16250 clock unless --no-cap was requested. A deferred
        // window also defers and rebases this clock at the application entry.
        RealtimeThrottle realtime(window_active && opt.realtime_cap);
        realtime.set_speed_percent(opt.speed_percent);
#endif
        bool audio_active = false;
        // Host playback is opt-in. Headless and silent runs still advance the
        // emulated DAC/SPU without opening a host audio device.
        if (opt.audio && window_active) {
          audio.init();
          audio_active = true;
        }
        if (opt.window)
          game_controller.initialize();
        if (!opt.dump_frame_dir.empty()) {
          if (opt.dump_frame_interval == 0) {
            die("--dump-frame-dir requires a nonzero --dump-frame-interval");
          }
          std::filesystem::create_directories(opt.dump_frame_dir);
        }

        bool quit = false;
        bool exit_desktop = false;
        bool powered_off = false;
        bool paused = false;
        bool muted = false;
        bool logging_started = bool(g_log);
        uint64_t last_render_insn = 0;
        auto next_present = std::chrono::steady_clock::now();
        uint64_t last_dump_frame_insn = 0;
        uint32_t dump_frame_index = 0;
        size_t scripted_touch_index = 0;
        bool scripted_touch_down = false;
        size_t scripted_key_transition_index = 0;
        auto set_touch_from_window = [&](bool pressed, int window_x,
                                         int window_y) {
          int window_w = Video::W;
          int window_h = Video::H;
          if (video.win)
            SDL_GetWindowSize(video.win, &window_w, &window_h);
          // SDL rewrites mouse coordinates into the logical 320x240 space
          // after SDL_RenderSetLogicalSize. Legacy CLI windows do not use a
          // logical renderer and retain the original proportional mapping.
          const int screen_x =
              desktop_session
                  ? std::clamp(window_x, 0, Video::W - 1)
                  : std::clamp(window_x * Video::W / std::max(1, window_w), 0,
                               Video::W - 1);
          const int screen_y =
              desktop_session
                  ? std::clamp(window_y, 0, Video::H - 1)
                  : std::clamp(window_y * Video::H / std::max(1, window_h), 0,
                               Video::H - 1);
          // Supplying the full 0..4095 converter range puts edge clicks
          // outside the physical panel's accepted calibration bounds.
          const TouchAdcPoint adc =
              screen_to_touch_adc(screen_x, screen_y, Video::W, Video::H);
          bus.set_touch(pressed, adc.x, adc.y);
        };
        auto open_window_if_due = [&]() {
          if (window_active || !opt.window)
            return;
          const bool instruction_due =
              opt.open_window_at != 0 && cpu.insns >= opt.open_window_at;
          const bool selected_mba_due =
              opt.open_window_on_mba && mba_overlay &&
              bus.mba_launch_count != 0 &&
              bus.mba_application_entry == mba_overlay->entry_address;
          if (!instruction_due && !selected_mba_due)
            return;
          initialize_video();
          window_active = true;
          if (opt.audio && !audio_active) {
            audio.init();
            audio_active = true;
          }
#ifndef __EMSCRIPTEN__
          realtime.set_enabled(opt.realtime_cap);
          realtime.set_speed_percent(opt.speed_percent);
#endif
          last_render_insn = cpu.insns >= opt.render_interval
                                 ? cpu.insns - opt.render_interval
                                 : 0;
          next_present = std::chrono::steady_clock::now();
          if (g_log) {
            g_log << "WINDOW OPENED insns=" << cpu.insns
                  << " reason="
                  << (selected_mba_due ? "mba-entry" : "instruction")
                  << "\n";
          }
        };
        auto update_host_audio_state = [&]() {
          if (!audio.device)
            return;
          SDL_PauseAudioDevice(audio.device, paused || muted ? 1 : 0);
          if (paused || muted)
            SDL_ClearQueuedAudio(audio.device);
        };
        auto toggle_pause = [&]() {
          paused = !paused;
          update_host_audio_state();
#ifndef __EMSCRIPTEN__
          realtime.rebase();
#endif
          if (video.win)
            SDL_SetWindowTitle(video.win, paused ? "MobiGo 2 Emulator - Paused"
                                                 : "MobiGo 2 Emulator");
        };
        auto reset_guest = [&]() {
          game_controller.release(bus);
          bus.matrix_pressed.fill(0);
          bus.accelerometer.clear_directions();
          bus.system_reset(false);
          apply_board_settings();
          rom_boot(bus, cpu, opt.start_pc, opt.start_pc_set);
          audio.reset_timeline(bus);
          powered_off = false;
          paused = false;
          update_host_audio_state();
#ifndef __EMSCRIPTEN__
          realtime.rebase();
#endif
          if (g_log)
            g_log << "USER RESET start=0x" << std::hex << cpu.lpc() << std::dec
                  << '\n';
        };
        auto save_screenshot = [&]() {
          if (!window_active)
            return;
          const std::filesystem::path directory =
              desktop::user_data_directory() / "screenshots";
          std::error_code error;
          std::filesystem::create_directories(directory, error);
          if (error) {
            std::cerr << "warning: cannot create screenshot folder: "
                      << error.message() << '\n';
            return;
          }
          const auto stamp =
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count();
          const std::filesystem::path path =
              directory / ("mobigo2_" + std::to_string(stamp) + ".bmp");
          video.compose(bus, cpu);
          video.save_bmp(path);
          SDL_SetWindowTitle(video.win, "MobiGo 2 Emulator - Screenshot saved");
          std::cout << "Screenshot saved to " << path_to_utf8(path) << '\n';
        };
        const auto run_started = std::chrono::steady_clock::now();
        auto speed_sample_started = run_started;
        uint64_t speed_sample_cycles = bus.cycles;
        auto update_speed_title = [&]() {
          if (!video.win || !opt.show_speed)
            return;
          const auto now = std::chrono::steady_clock::now();
          const double elapsed =
              std::chrono::duration<double>(now - speed_sample_started).count();
          if (elapsed < 0.25)
            return;
          const uint64_t elapsed_cycles = bus.cycles - speed_sample_cycles;
          const double guest_seconds =
              double(elapsed_cycles) / std::max<uint64_t>(1, bus.system_clock_hz());
          const int percent = std::clamp(
              int(std::lround(guest_seconds / elapsed * 100.0)), 0, 9999);
          SDL_SetWindowTitle(video.win,
                             (session_title + " - " + std::to_string(percent) +
                              "% speed")
                                 .c_str());
          speed_sample_started = now;
          speed_sample_cycles = bus.cycles;
        };
        auto run_iteration = [&]() -> bool {
          if (quit || cpu.halted)
            return false;
          if (opt.log && !logging_started &&
              cpu.insns >= opt.start_logging_at) {
            g_log.open(opt.log_path, std::ios::out | std::ios::trunc);
            if (!g_log)
              die("failed to open log file " + path_to_utf8(opt.log_path));
            logging_started = true;
            g_log << "LOGGING STARTED insns=" << cpu.insns << "\n";
          }
          open_window_if_due();
          update_speed_title();
          if (window_active) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
              if (ev.type == SDL_QUIT)
                quit = true;
              const ControllerCommand controller_command =
                  game_controller.event(bus, ev);
              if (controller_command == ControllerCommand::TogglePause)
                toggle_pause();
              else if (controller_command == ControllerCommand::Reset)
                reset_guest();
              else if (controller_command == ControllerCommand::Screenshot)
                save_screenshot();
              const bool main_window_key =
                  ev.type == SDL_KEYDOWN && video.win &&
                  ev.key.windowID == SDL_GetWindowID(video.win);
              if (main_window_key && !ev.key.repeat) {
                switch (ev.key.keysym.sym) {
                case SDLK_F3:
                  toggle_pause();
                  continue;
                case SDLK_F4:
                  muted = !muted;
                  audio.set_muted(muted);
                  update_host_audio_state();
                  SDL_SetWindowTitle(video.win,
                                     muted ? "MobiGo 2 Emulator - Muted"
                                           : "MobiGo 2 Emulator");
                  continue;
                case SDLK_F5:
                  reset_guest();
                  continue;
                case SDLK_F9:
                  save_screenshot();
                  continue;
                case SDLK_F10:
                  if (desktop_session) {
                    quit = true;
                    continue;
                  }
                  break;
                case SDLK_F11:
                  fullscreen_active = !fullscreen_active;
                  if (SDL_SetWindowFullscreen(
                          video.win, fullscreen_active
                                         ? SDL_WINDOW_FULLSCREEN_DESKTOP
                                         : 0) != 0) {
                    fullscreen_active = !fullscreen_active;
                    std::cerr << "warning: fullscreen toggle failed: "
                              << SDL_GetError() << '\n';
                  }
                  continue;
                case SDLK_F12:
                  exit_desktop = true;
                  quit = true;
                  continue;
                default:
                  break;
                }
              }
              if (ev.type == SDL_WINDOWEVENT &&
                  ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                // SDL may not deliver key-up events after focus moves away.
                bus.matrix_pressed.fill(0);
                bus.accelerometer.clear_directions();
                game_controller.release(bus);
                bus.set_touch(false, bus.touch_adc_x, bus.touch_adc_y);
              }
              if (ev.type == SDL_MOUSEBUTTONDOWN &&
                  ev.button.button == SDL_BUTTON_LEFT) {
                set_touch_from_window(true, ev.button.x, ev.button.y);
              }
              if (ev.type == SDL_MOUSEBUTTONUP &&
                  ev.button.button == SDL_BUTTON_LEFT) {
                set_touch_from_window(false, ev.button.x, ev.button.y);
              }
              if (ev.type == SDL_MOUSEMOTION &&
                  (ev.motion.state & SDL_BUTTON_LMASK)) {
                set_touch_from_window(true, ev.motion.x, ev.motion.y);
              }
              if ((ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) &&
                  !ev.key.repeat) {
                const bool pressed = ev.type == SDL_KEYDOWN;
                if (const std::optional<unsigned> motion =
                        motion_direction_from_sdl(opt.input_bindings,
                                                  ev.key.keysym.sym))
                  bus.set_motion_direction(*motion, pressed);
                if (const std::optional<MatrixKey> key =
                        matrix_key_from_sdl(opt.input_bindings,
                                            ev.key.keysym.sym)) {
                  bus.set_matrix_key(key->row, key->column, pressed);
                } else if (!input_binding_contains(opt.input_bindings,
                                                   ev.key.keysym.sym)) {
                  if (const std::optional<MatrixKey> key =
                               matrix_key_from_sdl(ev.key.keysym.sym)) {
                    // Alphabetic and punctuation keys remain direct MobiGo
                    // keyboard input; the launcher customizes the handheld
                    // controls above rather than turning normal text entry
                    // into a binding maze.
                    bus.set_matrix_key(key->row, key->column, pressed);
                  }
                }
              }
            }
          }

          if (paused) {
#ifndef __EMSCRIPTEN__
            realtime.rebase();
#endif
            SDL_Delay(10);
            return !quit;
          }

          uint64_t pace_segment_cycles = bus.cycles;
          uint64_t pace_segment_clock = bus.system_clock_hz();
          uint64_t pace_clock_generation = bus.clock_change_generation;
          auto account_pace_segment = [&]() {
            if (!realtime.enabled)
              return;
            if (bus.cycles >= pace_segment_cycles) {
              realtime.advance_cycles(bus.cycles - pace_segment_cycles,
                                      pace_segment_clock);
            }
            pace_segment_cycles = bus.cycles;
            pace_segment_clock = bus.system_clock_hz();
            pace_clock_generation = bus.clock_change_generation;
          };

          for (int i = 0; i < kDefaultInstructionBatch && !quit; ++i) {
            if (opt.max_steps && cpu.insns >= opt.max_steps) {
              quit = true;
              break;
            }
            if (scripted_touch_index < opt.scripted_touches.size()) {
              const ScriptedTouch &touch =
                  opt.scripted_touches[scripted_touch_index];
              const bool should_be_down = cpu.insns >= touch.at &&
                                          cpu.insns < touch.at + touch.duration;
              if (should_be_down != scripted_touch_down) {
                set_touch_from_window(should_be_down, touch.x, touch.y);
                scripted_touch_down = should_be_down;
                if (g_log) {
                  g_log << "SCRIPTED TOUCH " << (should_be_down ? "DOWN" : "UP")
                        << " insns=" << cpu.insns << " x=" << touch.x
                        << " y=" << touch.y << "\n";
                }
              }
              if (cpu.insns >= touch.at + touch.duration)
                ++scripted_touch_index;
            }
            while (
                scripted_key_transition_index <
                    opt.scripted_key_transitions.size() &&
                cpu.insns >=
                    opt.scripted_key_transitions[scripted_key_transition_index]
                        .at) {
              const ScriptedKeyTransition &key =
                  opt.scripted_key_transitions[scripted_key_transition_index++];
              bus.set_matrix_key(key.row, key.column, key.pressed);
              if (g_log) {
                g_log << "SCRIPTED KEY " << (key.pressed ? "DOWN" : "UP")
                      << " insns=" << cpu.insns << " key=" << key.name
                      << " row=" << key.row << " column=" << key.column << "\n";
              }
            }
            cpu.step();
            if (realtime.enabled &&
                bus.clock_change_generation != pace_clock_generation) {
              // The clock-writing instruction completes under the old
              // clock; subsequent instructions use the new selection.
              account_pace_segment();
            }
            if (bus.system_reset_requested) {
              account_pace_segment();
              const uint32_t reset_from = cpu.lpc();
              const bool cpu_only_reset = bus.system_reset_preserve_memory;
              if (cpu_only_reset) {
                bus.system_reset_requested = false;
                bus.system_reset_preserve_memory = false;
              } else {
                bus.system_reset(false);
                apply_board_settings();
              }
              const uint32_t start = bus.read(0x00fff7);
              cpu.reset_core(start);
              pace_segment_cycles = bus.cycles;
              pace_segment_clock = bus.system_clock_hz();
              pace_clock_generation = bus.clock_change_generation;
              if (g_log) {
                g_log << "SYSTEM RESET APPLIED insns=" << cpu.insns
                      << " from=0x" << std::hex << reset_from << " start=0x"
                      << start << " cpu_only=" << (cpu_only_reset ? 1 : 0)
                      << " reset_count=" << std::dec << bus.power_reset_count
                      << "\n";
              }
              continue;
            }
            if (bus.ppu_go_pending && bus.cycles >= bus.ppu_go_due_cycles) {
              video.render_ppu_to_framebuffer(bus);
            }
            if (bus.sleep_requested || bus.poweroff_requested) {
              if (opt.auto_power_wake) {
                account_pace_segment();
                bus.system_reset();
                apply_board_settings();
                const uint32_t start = bus.read(0x00fff7);
                cpu.reset_core(start);
                if (g_log) {
                  g_log << "POWER automatic power-key wake reset_vector=0x"
                        << std::hex << start << " reset_count=" << std::dec
                        << bus.power_reset_count << "\n";
                }
                pace_segment_cycles = bus.cycles;
                pace_segment_clock = bus.system_clock_hz();
                pace_clock_generation = bus.clock_change_generation;
              } else {
                powered_off = true;
                if (g_log) {
                  g_log << "POWER OFF source="
                        << (bus.poweroff_requested ? "gpio-d4-latch" : "sleep")
                        << "; automatic wake disabled\n";
                }
                cpu.halted = true;
                break;
              }
            }
          }

          account_pace_segment();
          audio.pump(bus);

          open_window_if_due();
          const auto now = std::chrono::steady_clock::now();
          // A zero presentation value means automatic, not a render on every
          // emulation batch.  Rendering is deliberately kept independent of
          // CPU pacing: composing and uploading a 320x240 frame thousands of
          // times per second can otherwise make an uncapped session slower.
          constexpr uint32_t kAutomaticPresentHz = 120;
          const uint32_t presentation_hz =
              opt.max_present_hz == 0 ? kAutomaticPresentHz : opt.max_present_hz;
          if (window_active &&
              cpu.insns - last_render_insn >= opt.render_interval &&
              now >= next_present) {
            video.render(bus, cpu);
            last_render_insn = cpu.insns;
            next_present =
                now + std::chrono::microseconds(1000000 / presentation_hz);
          }
          if (!opt.dump_frame_dir.empty() &&
              cpu.insns - last_dump_frame_insn >= opt.dump_frame_interval) {
            video.compose(bus, cpu, false);
            std::ostringstream name;
            name << "frame_" << std::setw(5) << std::setfill('0')
                 << dump_frame_index++ << "_insn_" << std::setw(12)
                 << std::setfill('0') << cpu.insns << ".bmp";
            const std::filesystem::path path = opt.dump_frame_dir / name.str();
            video.save_bmp(path);
            last_dump_frame_insn = cpu.insns;
            if (g_log) {
              g_log << "Frame sequence dumped insns=" << cpu.insns << " pc=0x"
                    << std::hex << cpu.lpc() << " ppu=0x"
                    << bus.mmio[0x707f - kMmioBase] << " fbi=0x"
                    << ppu_frame_addr(bus.mmio[0x7078 - kMmioBase],
                                      bus.mmio[0x7079 - kMmioBase])
                    << " fbo=0x"
                    << ppu_frame_addr(bus.mmio[0x707a - kMmioBase],
                                      bus.mmio[0x707b - kMmioBase])
                    << " latch=0x" << bus.last_framebuffer_base
                    << " latch_valid=" << (bus.last_framebuffer_valid ? 1 : 0)
                    << std::dec << " path=" << std::quoted(path_to_utf8(path))
                    << "\n";
            }
          }
          realtime.wait_until_current();
          return !quit && !cpu.halted;
        };

#ifdef __EMSCRIPTEN__
        // The browser must regain control after each emulation slice so SDL
        // can present the canvas and dispatch keyboard/touch events.
        emscripten_set_main_loop_arg(
            [](void *arg) {
              auto *runner = static_cast<decltype(run_iteration) *>(arg);
              if (!(*runner)())
                emscripten_cancel_main_loop();
            },
            &run_iteration, 0, true);
        return 0;
#else
        while (run_iteration()) {
        }
#endif

        if (!opt.dump_frame.empty()) {
          video.compose(bus, cpu);
          video.save_bmp(opt.dump_frame);
          if (g_log) {
            g_log << "Frame dumped to "
                  << std::quoted(path_to_utf8(opt.dump_frame)) << " pc=0x"
                  << std::hex << cpu.lpc() << " ppu=0x"
                  << bus.mmio[0x707f - kMmioBase] << " fbi=0x"
                  << ppu_frame_addr(bus.mmio[0x7078 - kMmioBase],
                                    bus.mmio[0x7079 - kMmioBase])
                  << " fbo=0x"
                  << ppu_frame_addr(bus.mmio[0x707a - kMmioBase],
                                    bus.mmio[0x707b - kMmioBase])
                  << " latch=0x" << bus.last_framebuffer_base
                  << " latch_valid=" << (bus.last_framebuffer_valid ? 1 : 0)
                  << std::dec << "\n";
          }
        }

        if (!opt.dump_current_frame.empty()) {
          video.compose(bus, cpu, false);
          video.save_bmp(opt.dump_current_frame);
          if (g_log) {
            g_log << "Current frame dumped to "
                  << std::quoted(path_to_utf8(opt.dump_current_frame))
                  << " pc=0x" << std::hex << cpu.lpc() << " ppu=0x"
                  << bus.mmio[0x707f - kMmioBase] << " fbi=0x"
                  << ppu_frame_addr(bus.mmio[0x7078 - kMmioBase],
                                    bus.mmio[0x7079 - kMmioBase])
                  << " fbo=0x"
                  << ppu_frame_addr(bus.mmio[0x707a - kMmioBase],
                                    bus.mmio[0x707b - kMmioBase])
                  << " latch=0x" << bus.last_framebuffer_base
                  << " latch_valid=" << (bus.last_framebuffer_valid ? 1 : 0)
                  << std::dec << "\n";
          }
        }

        if (!opt.dump_memory.empty()) {
          if (opt.dump_memory_words == 0) {
            die("--dump-memory requires a nonzero --dump-memory-words");
          }
          std::ofstream out(opt.dump_memory, std::ios::binary);
          if (!out)
            die("failed to open memory dump " + path_to_utf8(opt.dump_memory));
          for (uint32_t i = 0; i < opt.dump_memory_words; ++i) {
            const uint16_t value = opt.dump_memory_dma
                                       ? bus.dma_read(opt.dump_memory_base + i)
                                       : bus.read(opt.dump_memory_base + i);
            const std::array<char, 2> bytes{char(value & 0xff),
                                            char(value >> 8)};
            out.write(bytes.data(), bytes.size());
          }
          if (!out)
            die("failed while writing memory dump " +
                path_to_utf8(opt.dump_memory));
          if (g_log) {
            g_log << "Memory dumped base=0x" << std::hex << opt.dump_memory_base
                  << " words=0x" << opt.dump_memory_words
                  << " path=" << std::quoted(path_to_utf8(opt.dump_memory))
                  << " dma=" << (opt.dump_memory_dma ? 1 : 0) << std::dec
                  << "\n";
          }
        }

        if (!opt.dump_code.empty()) {
          if (opt.dump_code_words == 0) {
            die("--dump-code requires a nonzero --dump-code-words");
          }
          std::ofstream out(opt.dump_code, std::ios::binary);
          if (!out)
            die("failed to open code dump " + path_to_utf8(opt.dump_code));
          for (uint32_t i = 0; i < opt.dump_code_words; ++i) {
            const uint16_t value =
                bus.read_code((opt.dump_code_base + i) & kAddrMask);
            const std::array<char, 2> bytes{char(value & 0xff),
                                            char(value >> 8)};
            out.write(bytes.data(), bytes.size());
          }
          if (!out)
            die("failed while writing code dump " +
                path_to_utf8(opt.dump_code));
          if (g_log) {
            g_log << "Code dumped base=0x" << std::hex << opt.dump_code_base
                  << " words=0x" << opt.dump_code_words
                  << " path=" << std::quoted(path_to_utf8(opt.dump_code))
                  << std::dec << "\n";
          }
        }

        const double run_seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                          run_started)
                .count();
        session_resources.shutdown();
        std::cout << "Stopped after " << cpu.insns << " instructions at PC=0x"
                  << std::hex << cpu.lpc() << std::dec << "\n";
        if (powered_off)
          std::cout << "Power state: off\n";
        std::cout << std::fixed << std::setprecision(3) << "Emulation time "
                  << run_seconds << " s ("
                  << (run_seconds > 0.0
                          ? double(cpu.insns) / run_seconds / 1000000.0
                          : 0.0)
                  << " MIPS)\n";
        if (desktop_session && !exit_desktop)
          continue;
        return 0;
      } catch (const std::exception &error) {
        if (!desktop_session)
          throw;
        g_log.close();
        std::cerr << "fatal: " << error.what() << '\n';
        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR, "MobiGo 2 Emulator",
            (std::string("The game stopped with an error:\n\n") + error.what() +
             "\n\nYou can choose another title from the library.")
                .c_str(),
            nullptr);
        if (SDL_WasInit(0))
          SDL_Quit();
        continue;
      }
    }
  } catch (const std::exception &e) {
    std::cerr << "fatal: " << e.what() << "\n";
    if (argc == 1) {
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "MobiGo 2 Emulator",
                               e.what(), nullptr);
    }
    return 1;
  }
}
