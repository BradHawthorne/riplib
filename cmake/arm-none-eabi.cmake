# CMake toolchain file for the Raspberry Pi Pico 2 / RP2350 target.
#
# Use:
#   cmake -B build-arm -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake
#
# Builds the static library only — does not produce a runnable binary,
# since bare-metal targets supply their own startup, linker script, and
# syscall stubs.  Downstream firmware (pico-sdk or A2GSPU firmware)
# links against libriplib.a.
#
# Target: Raspberry Pi RP2350 (Cortex-M33, ARMv8-M Mainline, FPU).
# Reference platform: the A2GSPU card — a dual-RP2350 Apple IIgs
# coprocessor that uses one RP2350 ("Processor V" / RP235XA) for video
# rendering and one RP2350 ("Processor B" / RP235XB) for the host bus,
# storage, and USB.  RIPlib runs on Processor V, where it writes to
# the framebuffer that is then clocked out via HSTX (or PIO bit-banged
# DVI on early prototypes).  Board reference:
#   https://www.facebook.com/groups/5251478676/posts/10166402670968677/
#
# Both RP2350 variants share the same CPU core, so a single toolchain
# file covers both V (RP235XA, 30 GPIO, QFN-60) and B (RP235XB, 48
# GPIO, QFN-80) builds.  Pin count differences are board-level
# concerns, not library concerns.

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
# cores, but RIPlib's reference firmware uses ARM).
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
