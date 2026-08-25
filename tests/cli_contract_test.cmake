if(NOT DEFINED EMULATOR OR EMULATOR STREQUAL "")
  message(FATAL_ERROR "Pass -DEMULATOR=/path/to/mobigo2_emu")
endif()
if(NOT DEFINED FIRMWARE_DIR OR FIRMWARE_DIR STREQUAL "")
  message(FATAL_ERROR "Pass -DFIRMWARE_DIR=/path/to/firmware")
endif()

# Command-line automation relies on these literal option names. The GUI is an
# opt-in no-argument experience; explicit invocations remain deterministic.
execute_process(
  COMMAND "${EMULATOR}" --help
  RESULT_VARIABLE help_status
  OUTPUT_VARIABLE help_stdout
  ERROR_VARIABLE help_stderr
  TIMEOUT 10
)
if(NOT help_status EQUAL 0)
  message(FATAL_ERROR
    "--help exited ${help_status}\nstdout:\n${help_stdout}\nstderr:\n${help_stderr}")
endif()
set(help_output "${help_stdout}${help_stderr}")
foreach(option IN ITEMS --cart --speed-percent --no-window)
  string(FIND "${help_output}" "${option}" option_at)
  if(option_at EQUAL -1)
    message(FATAL_ERROR "--help no longer advertises required option ${option}")
  endif()
endforeach()

# Invalid invocations must fail promptly instead of opening the desktop UI.
# The exact prefix is useful to terminal wrappers and keeps errors distinct
# from ordinary emulator stdout consumed by starter verification scripts.
set(invalid_option --starter-project-cli-contract-invalid)
execute_process(
  COMMAND "${EMULATOR}" "${invalid_option}"
  RESULT_VARIABLE invalid_status
  OUTPUT_VARIABLE invalid_stdout
  ERROR_VARIABLE invalid_stderr
  TIMEOUT 10
)
string(STRIP "${invalid_stdout}" invalid_stdout)
string(STRIP "${invalid_stderr}" invalid_stderr)
if(NOT invalid_status EQUAL 1)
  message(FATAL_ERROR "invalid option exited ${invalid_status}, expected 1")
endif()
if(NOT invalid_stdout STREQUAL "")
  message(FATAL_ERROR "invalid option unexpectedly wrote stdout: ${invalid_stdout}")
endif()
if(NOT invalid_stderr STREQUAL
    "fatal: unknown argument: ${invalid_option}")
  message(FATAL_ERROR "unexpected invalid-option error: ${invalid_stderr}")
endif()

# A bounded explicit-argument launch must remain headless and deterministic.
# This is the same public surface used by starter verification, benchmarks,
# and automation; no-argument launcher changes must never intercept it.
execute_process(
  COMMAND "${EMULATOR}"
    --rom "${FIRMWARE_DIR}/internalrom.bin"
    --spi "${FIRMWARE_DIR}/spi.bin"
    --nand "${FIRMWARE_DIR}/nand.bin"
    --no-cap --speed-percent 100 --no-window --steps 1
  RESULT_VARIABLE bounded_status
  OUTPUT_VARIABLE bounded_stdout
  ERROR_VARIABLE bounded_stderr
  TIMEOUT 15
)
if(NOT bounded_status EQUAL 0)
  message(FATAL_ERROR
    "bounded launch exited ${bounded_status}\nstdout:\n${bounded_stdout}\nstderr:\n${bounded_stderr}")
endif()
string(FIND "${bounded_stdout}" "Stopped after 1 instructions at PC=0x" stopped_at)
if(stopped_at EQUAL -1)
  message(FATAL_ERROR "bounded launch lost its machine-readable summary: ${bounded_stdout}")
endif()
if(NOT bounded_stderr STREQUAL "")
  message(FATAL_ERROR "bounded launch unexpectedly wrote stderr: ${bounded_stderr}")
endif()
