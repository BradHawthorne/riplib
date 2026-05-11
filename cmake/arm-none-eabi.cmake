# CMake toolchain file for bare-metal ARM Cortex-M33 (RP2350 target).
# Use:
#   cmake -B build-arm -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake
#
# Builds the static library only — does not produce a runnable binary,
# since bare-metal targets supply their own startup, linker script, and
# syscall stubs.  The point of this build is to verify the source tree
# compiles cleanly under the embedded toolchain that RIPlib was
# extracted from.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)

# CMake otherwise tries to link a test program with the host runtime.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# RP2350: Cortex-M33 with the M-profile FPU.  These flags match the
# A2GSPU firmware that RIPlib was extracted from.
set(ARM_CPU_FLAGS
    -mcpu=cortex-m33
    -mthumb
    -mfpu=fpv5-sp-d16
    -mfloat-abi=hard
)
string(REPLACE ";" " " ARM_CPU_FLAGS_STR "${ARM_CPU_FLAGS}")

set(CMAKE_C_FLAGS_INIT   "${ARM_CPU_FLAGS_STR} -ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS_INIT "${ARM_CPU_FLAGS_STR} -ffunction-sections -fdata-sections")
