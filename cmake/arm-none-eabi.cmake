# CMake toolchain file for the Raspberry Pi Pico 2 / RP2350 target.
#
# Use:
#   cmake -B build-arm -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake
#
# Builds the static library only — does not produce a runnable binary,
# since bare-metal targets supply their own startup, linker script, and
# syscall stubs.  Downstream firmware (pico-sdk-based or otherwise)
# links against libriplib.a.
#
# Target: Raspberry Pi RP2350 (Cortex-M33, ARMv8-M Mainline, FPU).
# Both RP2350 variants share the same CPU core, so a single toolchain
# file covers both RP235XA (30 GPIO, QFN-60) and RP235XB (48 GPIO,
# QFN-80) builds.  Pin-count differences are board-level concerns,
# not library concerns.
#
# Other ARM Cortex-M targets (M33-based or compatible) can usually
# build RIPlib by editing the ARM_CPU_FLAGS list below to match their
# core; only the CPU flags and the PICO_RP2350 hint define are
# RP2350-specific.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)

# CMake otherwise tries to link a test program with the host runtime.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# RP2350 = Cortex-M33 + ARMv8-M Mainline + single-precision FPU
# (fpv5-sp-d16).  These flags match the pico-sdk default for the
# ARM core selection (the chip can also boot the Hazard3 RISC-V
# cores; this toolchain file is for the ARM build only).
set(ARM_CPU_FLAGS
    -mcpu=cortex-m33
    -mthumb
    -mfpu=fpv5-sp-d16
    -mfloat-abi=hard
)
string(REPLACE ";" " " ARM_CPU_FLAGS_STR "${ARM_CPU_FLAGS}")

# -ffunction-sections / -fdata-sections lets the firmware linker
# garbage-collect unused symbols.  Combined with -Wl,--gc-sections at
# link time, this keeps the firmware footprint small even when the
# library exposes APIs the firmware doesn't call.
#
# -DPICO_RP2350 is a hint flag matching the pico-sdk convention so
# any downstream code that conditionally compiles for the chip
# family can detect this build.
set(CMAKE_C_FLAGS_INIT   "${ARM_CPU_FLAGS_STR} -ffunction-sections -fdata-sections -DPICO_RP2350=1")
set(CMAKE_CXX_FLAGS_INIT "${ARM_CPU_FLAGS_STR} -ffunction-sections -fdata-sections -DPICO_RP2350=1")
