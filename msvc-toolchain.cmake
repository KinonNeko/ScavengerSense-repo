set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

set(MSVC_SYSROOT /opt/msvc)
set(MSVC_LIBDIR ${MSVC_SYSROOT}/lib/x86_64-unknown-windows-msvc)

set(CMAKE_C_COMPILER clang-cl)
set(CMAKE_CXX_COMPILER clang-cl)
set(CMAKE_AR llvm-lib)
set(CMAKE_MT llvm-mt)
set(CMAKE_RC_COMPILER llvm-rc)
set(CMAKE_LINKER lld-link)

set(_ss_flags "-fms-compatibility-version=19.42 -Wno-unknown-pragmas -Wno-unused-command-line-argument /EHsc /imsvc${MSVC_SYSROOT}/include/c++/stl /imsvc${MSVC_SYSROOT}/include")
set(CMAKE_C_FLAGS_INIT "${_ss_flags}")
set(CMAKE_CXX_FLAGS_INIT "${_ss_flags}")

set(CMAKE_EXE_LINKER_FLAGS_INIT "/libpath:${MSVC_LIBDIR}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "/libpath:${MSVC_LIBDIR}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "/libpath:${MSVC_LIBDIR}")

set(CMAKE_FIND_ROOT_PATH ${MSVC_SYSROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
