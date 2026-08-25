#pragma once

#include "bus.hpp"

namespace mobigo {

enum class ControllerCommand {
  None,
  TogglePause,
  Reset,
  Screenshot,
};

// SDL's controller database normalizes Xbox, PlayStation, Nintendo, and many
// generic pads to one layout. Guest controls stay separate from host commands
// so pause/reset never appear as phantom MobiGo key presses.
struct GameControllerInput {
  SDL_GameController *controller = nullptr;
  SDL_JoystickID instance = -1;
  std::array<bool, 9> matrix_state{};
  std::array<bool, 4> motion_state{};

  ~GameControllerInput() { close_without_bus(); }

  void initialize() {
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
      std::cerr << "warning: game controller support unavailable: "
                << SDL_GetError() << '\n';
      return;
    }
    for (int index = 0; index < SDL_NumJoysticks() && !controller; ++index)
      open(index);
  }

  void open(int device_index) {
    if (controller || !SDL_IsGameController(device_index))
      return;
    controller = SDL_GameControllerOpen(device_index);
    if (!controller) {
      std::cerr << "warning: failed to open game controller: " << SDL_GetError()
                << '\n';
      return;
    }
    instance =
        SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller));
  }

  void release(Bus &bus) {
    static constexpr std::array<MatrixKey, 9> keys{{
        {3, 3},
        {4, 3},
        {3, 4},
        {4, 4}, // left, right, up, down
        {3, 5},
        {4, 2},
        {4, 5},
        {3, 2},
        {4, 1}, // A/B/X/back/start
    }};
    for (size_t i = 0; i < keys.size(); ++i) {
      if (matrix_state[i])
        bus.set_matrix_key(keys[i].row, keys[i].column, false);
      matrix_state[i] = false;
    }
    for (size_t i = 0; i < motion_state.size(); ++i) {
      if (motion_state[i])
        bus.set_motion_direction(unsigned(i), false);
      motion_state[i] = false;
    }
  }

  void close(Bus &bus) {
    release(bus);
    close_without_bus();
  }

  ControllerCommand event(Bus &bus, const SDL_Event &event) {
    if (event.type == SDL_CONTROLLERDEVICEADDED) {
      open(event.cdevice.which);
      return ControllerCommand::None;
    }
    if (event.type == SDL_CONTROLLERDEVICEREMOVED &&
        event.cdevice.which == instance) {
      close(bus);
      for (int index = 0; index < SDL_NumJoysticks() && !controller; ++index)
        open(index);
      return ControllerCommand::None;
    }
    if (!controller)
      return ControllerCommand::None;
    const bool own_button = (event.type == SDL_CONTROLLERBUTTONDOWN ||
                             event.type == SDL_CONTROLLERBUTTONUP) &&
                            event.cbutton.which == instance;
    const bool own_axis =
        event.type == SDL_CONTROLLERAXISMOTION && event.caxis.which == instance;
    if (!own_button && !own_axis)
      return ControllerCommand::None;

    if (event.type == SDL_CONTROLLERBUTTONDOWN) {
      switch (event.cbutton.button) {
      case SDL_CONTROLLER_BUTTON_START:
        return ControllerCommand::TogglePause;
      case SDL_CONTROLLER_BUTTON_Y:
        return ControllerCommand::Screenshot;
      case SDL_CONTROLLER_BUTTON_GUIDE:
        return ControllerCommand::Reset;
      default:
        break;
      }
    }
    update_guest_controls(bus);
    return ControllerCommand::None;
  }

private:
  void close_without_bus() {
    if (controller)
      SDL_GameControllerClose(controller);
    controller = nullptr;
    instance = -1;
  }

  bool button(SDL_GameControllerButton value) const {
    return SDL_GameControllerGetButton(controller, value) != 0;
  }

  int axis(SDL_GameControllerAxis value) const {
    return SDL_GameControllerGetAxis(controller, value);
  }

  void update_guest_controls(Bus &bus) {
    constexpr int threshold = 16000;
    const int left_x = axis(SDL_CONTROLLER_AXIS_LEFTX);
    const int left_y = axis(SDL_CONTROLLER_AXIS_LEFTY);
    const int right_x = axis(SDL_CONTROLLER_AXIS_RIGHTX);
    const int right_y = axis(SDL_CONTROLLER_AXIS_RIGHTY);
    const std::array<bool, 9> next_matrix{{
        button(SDL_CONTROLLER_BUTTON_DPAD_LEFT) || left_x < -threshold,
        button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || left_x > threshold,
        button(SDL_CONTROLLER_BUTTON_DPAD_UP) || left_y < -threshold,
        button(SDL_CONTROLLER_BUTTON_DPAD_DOWN) || left_y > threshold,
        button(SDL_CONTROLLER_BUTTON_A),
        button(SDL_CONTROLLER_BUTTON_B),
        button(SDL_CONTROLLER_BUTTON_X),
        button(SDL_CONTROLLER_BUTTON_BACK),
        false, // Start is reserved for host pause.
    }};
    static constexpr std::array<MatrixKey, 9> keys{{
        {3, 3},
        {4, 3},
        {3, 4},
        {4, 4},
        {3, 5},
        {4, 2},
        {4, 5},
        {3, 2},
        {4, 1},
    }};
    for (size_t index = 0; index < next_matrix.size(); ++index) {
      if (next_matrix[index] == matrix_state[index])
        continue;
      matrix_state[index] = next_matrix[index];
      bus.set_matrix_key(keys[index].row, keys[index].column,
                         next_matrix[index]);
    }

    const std::array<bool, 4> next_motion{{
        (right_x < -threshold),
        (right_x > threshold),
        (right_y < -threshold),
        (right_y > threshold),
    }};
    for (size_t index = 0; index < next_motion.size(); ++index) {
      if (next_motion[index] == motion_state[index])
        continue;
      motion_state[index] = next_motion[index];
      bus.set_motion_direction(unsigned(index), next_motion[index]);
    }
  }
};

} // namespace mobigo
