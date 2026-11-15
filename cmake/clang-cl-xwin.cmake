# ac-plus: build the Windows version on Linux with clang-cl and lld-link, against the Windows SDK and MSVC runtime
# that xwin downloads (xwin --accept-license splat --output <dir>). Point XWIN_DIR at that folder:
#   cmake --preset windows-xwin -DXWIN_DIR=$HOME/xwin
# Nookery's own Windows builds use MSVC (see .github/workflows/build.yml); clang-cl makes the same kind of program.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)
set(CMAKE_SYSTEM_VERSION 10.0)
set(CMAKE_TRY_COMPILE_CONFIGURATION Release) # xwin has the release runtime only (no msvcrtd)

if (NOT XWIN_DIR)
    set(XWIN_DIR "$ENV{XWIN_DIR}")
endif ()
if (NOT XWIN_DIR OR NOT EXISTS "${XWIN_DIR}/sdk")
    message(FATAL_ERROR "clang-cl-xwin.cmake: set XWIN_DIR to the folder xwin splat wrote (it has crt/ and sdk/)")
endif ()
set(XWIN_DIR "${XWIN_DIR}" CACHE PATH "xwin's Windows SDK + MSVC runtime" FORCE)
set(ENV{XWIN_DIR} "${XWIN_DIR}") # for try_compile's own runs of this file
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES XWIN_DIR)

set(CMAKE_C_COMPILER clang-cl)
set(CMAKE_CXX_COMPILER clang-cl)
set(CMAKE_LINKER lld-link)
set(CMAKE_AR llvm-lib)
set(CMAKE_RC_COMPILER llvm-rc)
set(CMAKE_MT llvm-mt)
set(CMAKE_C_COMPILER_TARGET x86_64-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET x86_64-pc-windows-msvc)

set(_xwin_inc
    "/imsvc${XWIN_DIR}/crt/include"
    "/imsvc${XWIN_DIR}/sdk/include/ucrt"
    "/imsvc${XWIN_DIR}/sdk/include/um"
    "/imsvc${XWIN_DIR}/sdk/include/shared"
    "/imsvc${XWIN_DIR}/sdk/include/winrt")
string(REPLACE ";" " " _xwin_inc "${_xwin_inc}")
# MSVC only warns about these old-C habits of the decompiled code; newer clang makes them errors
set(_clang_relax "/clang:-Wno-error=implicit-function-declaration /clang:-Wno-error=incompatible-pointer-types /clang:-Wno-error=int-conversion /clang:-Wno-error=return-mismatch /clang:-Wno-main-return-type /clang:-Wno-error=implicit-int /clang:-Wno-error=incompatible-function-pointer-types")
set(CMAKE_C_FLAGS_INIT "${_xwin_inc} ${_clang_relax}")
set(CMAKE_CXX_FLAGS_INIT "${_xwin_inc} /EHsc") # (C++ exceptions, as MSVC builds have them)
set(CMAKE_RC_FLAGS_INIT "-I${XWIN_DIR}/sdk/include/um -I${XWIN_DIR}/sdk/include/shared")

set(_xwin_lib
    "/libpath:${XWIN_DIR}/crt/lib/x86_64"
    "/libpath:${XWIN_DIR}/sdk/lib/um/x86_64"
    "/libpath:${XWIN_DIR}/sdk/lib/ucrt/x86_64")
string(REPLACE ";" " " _xwin_lib "${_xwin_lib}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_xwin_lib}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_xwin_lib}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_xwin_lib}")

# Rust (nod, through corrosion): the Windows target, linked by lld-link
set(Rust_CARGO_TARGET x86_64-pc-windows-msvc)
set(ENV{CARGO_TARGET_X86_64_PC_WINDOWS_MSVC_LINKER} lld-link)

# only Windows things: never the Linux host's libraries (they'd be found under /usr otherwise)
set(CMAKE_FIND_ROOT_PATH "${XWIN_DIR}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
