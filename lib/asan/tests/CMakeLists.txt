# Testing rules for AddressSanitizer.
#
# These are broken into two buckets. One set of tests directly interacts with
# the runtime library and checks its functionality. These are the
# no-instrumentation tests.
#
# Another group of tests relies upon the ability to compile the test with
# address sanitizer instrumentation pass. These tests form "integration" tests
# and have some elements of version skew -- they test the *host* compiler's
# instrumentation against the just-built runtime library.

include(CheckCXXCompilerFlag)
include(CompilerRTCompile)

include_directories(..)
include_directories(../..)

set(ASAN_UNITTEST_HEADERS
  asan_mac_test.h
  asan_test_config.h
  asan_test_utils.h)

set(ASAN_UNITTEST_COMMON_CFLAGS
  ${COMPILER_RT_GTEST_CFLAGS}
  -I${COMPILER_RT_SOURCE_DIR}/include
  -I${COMPILER_RT_SOURCE_DIR}/lib
  -I${COMPILER_RT_SOURCE_DIR}/lib/asan
  -I${COMPILER_RT_SOURCE_DIR}/lib/sanitizer_common/tests
  -Wno-format
  -Werror=sign-compare
  -g
  -O2)
append_if(COMPILER_RT_HAS_WNO_VARIADIC_MACROS_FLAG -Wno-variadic-macros ASAN_UNITTEST_COMMON_CFLAGS)

# Use -D instead of definitions to please custom compile command.
list(APPEND ASAN_UNITTEST_COMMON_CFLAGS
  -DASAN_HAS_BLACKLIST=1
  -DASAN_HAS_EXCEPTIONS=1
  -DASAN_UAR=0)

set(ASAN_BLACKLIST_FILE "${CMAKE_CURRENT_SOURCE_DIR}/asan_test.ignore")
set(ASAN_UNITTEST_INSTRUMENTED_CFLAGS
  ${ASAN_UNITTEST_COMMON_CFLAGS}
  -fsanitize=address
  "-fsanitize-blacklist=${ASAN_BLACKLIST_FILE}"
  -mllvm -asan-stack=1
  -mllvm -asan-globals=1
)

# Unit tests require libstdc++.
set(ASAN_UNITTEST_COMMON_LINKFLAGS -lstdc++)
# x86_64 FreeBSD 9.2 additionally requires libc++ to build the tests.
if(CMAKE_SYSTEM MATCHES "FreeBSD-9.2-RELEASE")
  list(APPEND ASAN_UNITTEST_COMMON_LINKFLAGS "-lc++")
endif()

# Unit tests on Mac depend on Foundation.
if(APPLE)
  list(APPEND ASAN_UNITTEST_COMMON_LINKFLAGS -framework Foundation)
endif()
if(ANDROID)
  list(APPEND ASAN_UNITTEST_COMMON_LINKFLAGS -pie)
endif()

set(ASAN_UNITTEST_INSTRUMENTED_LINKFLAGS
  ${ASAN_UNITTEST_COMMON_LINKFLAGS})
# On Android, we link with ASan runtime manually. On other platforms we depend
# on Clang driver behavior, passing -fsanitize=address flag.
if(NOT ANDROID)
  list(APPEND ASAN_UNITTEST_INSTRUMENTED_LINKFLAGS -fsanitize=address)
endif()

set(ASAN_DYNAMIC_UNITTEST_INSTRUMENTED_LINKFLAGS
  ${ASAN_UNITTEST_INSTRUMENTED_LINKFLAGS}
  -shared-libasan)

set(ASAN_UNITTEST_NOINST_LINKFLAGS
  ${ASAN_UNITTEST_COMMON_LINKFLAGS} -lm)
append_if(COMPILER_RT_HAS_LIBDL -ldl ASAN_UNITTEST_NOINST_LINKFLAGS)
append_if(COMPILER_RT_HAS_LIBPTHREAD -lpthread ASAN_UNITTEST_NOINST_LINKFLAGS)
append_if(COMPILER_RT_HAS_LIBPTHREAD -lpthread
          ASAN_DYNAMIC_UNITTEST_INSTRUMENTED_LINKFLAGS)

# Compile source for the given architecture, using compiler
# options in ${ARGN}, and add it to the object list.
macro(asan_compile obj_list source arch kind)
  get_filename_component(basename ${source} NAME)
  set(output_obj "${obj_list}.${basename}.${arch}${kind}.o")
  get_target_flags_for_arch(${arch} TARGET_CFLAGS)
  set(COMPILE_DEPS ${ASAN_UNITTEST_HEADERS} ${ASAN_BLACKLIST_FILE})
  if(NOT COMPILER_RT_STANDALONE_BUILD)
    list(APPEND COMPILE_DEPS gtest asan)
  endif()
  clang_compile(${output_obj} ${source}
                CFLAGS ${ARGN} ${TARGET_CFLAGS}
                DEPS ${COMPILE_DEPS})
  list(APPEND ${obj_list} ${output_obj})
endmacro()

# Link ASan unit test for a given architecture from a set
# of objects in with given linker flags.
macro(add_asan_test test_suite test_name arch kind)
  parse_arguments(TEST "OBJECTS;LINKFLAGS" "WITH_TEST_RUNTIME" ${ARGN})
  get_target_flags_for_arch(${arch} TARGET_LINK_FLAGS)
  set(TEST_DEPS ${TEST_OBJECTS})
  if(NOT COMPILER_RT_STANDALONE_BUILD)
    list(APPEND TEST_DEPS asan)
  endif()
  if(TEST_WITH_TEST_RUNTIME)
    list(APPEND TEST_DEPS ${ASAN_TEST_RUNTIME})
    list(APPEND TEST_OBJECTS lib${ASAN_TEST_RUNTIME}.a)
  endif()
  add_compiler_rt_test(${test_suite} ${test_name}
                       OBJECTS ${TEST_OBJECTS}
                       DEPS ${TEST_DEPS}
                       LINK_FLAGS ${TEST_LINKFLAGS}
                                  ${TARGET_LINK_FLAGS})
endmacro()

# Main AddressSanitizer unit tests.
add_custom_target(AsanUnitTests)
set_target_properties(AsanUnitTests PROPERTIES FOLDER "ASan unit tests")
# ASan benchmarks (not actively used now).
add_custom_target(AsanBenchmarks)
set_target_properties(AsanBenchmarks PROPERTIES FOLDER "Asan benchmarks")

set(ASAN_NOINST_TEST_SOURCES
  ${COMPILER_RT_GTEST_SOURCE}
  asan_fake_stack_test.cc
  asan_noinst_test.cc
  asan_test_main.cc)

set(ASAN_INST_TEST_SOURCES
  ${COMPILER_RT_GTEST_SOURCE}
  asan_asm_test.cc
  asan_globals_test.cc
  asan_interface_test.cc
  asan_test.cc
  asan_oob_test.cc
  asan_mem_test.cc
  asan_str_test.cc
  asan_test_main.cc)
if(APPLE)
  list(APPEND ASAN_INST_TEST_SOURCES asan_mac_test.cc)
endif()

set(ASAN_BENCHMARKS_SOURCES
  ${COMPILER_RT_GTEST_SOURCE}
  asan_benchmarks_test.cc)

# Adds ASan unit tests and benchmarks for architecture.
macro(add_asan_tests_for_arch_and_kind arch kind)
  # Instrumented tests.
  set(ASAN_INST_TEST_OBJECTS)
  foreach(src ${ASAN_INST_TEST_SOURCES})
    asan_compile(ASAN_INST_TEST_OBJECTS ${src} ${arch} ${kind}
      ${ASAN_UNITTEST_INSTRUMENTED_CFLAGS} ${ARGN})
  endforeach()
  if (APPLE)
    # Add Mac-specific helper.
    asan_compile(ASAN_INST_TEST_OBJECTS asan_mac_test_helpers.mm ${arch} ${kind}
                 ${ASAN_UNITTEST_INSTRUMENTED_CFLAGS} -ObjC ${ARGN})
  endif()
  add_asan_test(AsanUnitTests "Asan-${arch}${kind}-Test" ${arch} ${kind}
                OBJECTS ${ASAN_INST_TEST_OBJECTS}
                LINKFLAGS ${ASAN_UNITTEST_INSTRUMENTED_LINKFLAGS})
  if(COMPILER_RT_BUILD_SHARED_ASAN)
    add_asan_test(AsanUnitTests "Asan-${arch}${kind}-Dynamic-Test" ${arch} ${kind}
                  OBJECTS ${ASAN_INST_TEST_OBJECTS}
                  LINKFLAGS ${ASAN_DYNAMIC_UNITTEST_INSTRUMENTED_LINKFLAGS})
  endif()

  # Add static ASan runtime that will be linked with uninstrumented tests.
  set(ASAN_TEST_RUNTIME RTAsanTest.${arch}${kind})
  if(APPLE)
    set(ASAN_TEST_RUNTIME_OBJECTS
      $<TARGET_OBJECTS:RTAsan.osx>
      $<TARGET_OBJECTS:RTInterception.osx>
      $<TARGET_OBJECTS:RTSanitizerCommon.osx>
      $<TARGET_OBJECTS:RTLSanCommon.osx>)
  else()
    set(ASAN_TEST_RUNTIME_OBJECTS
      $<TARGET_OBJECTS:RTAsan.${arch}>
      $<TARGET_OBJECTS:RTInterception.${arch}>
      $<TARGET_OBJECTS:RTLSanCommon.${arch}>
      $<TARGET_OBJECTS:RTSanitizerCommon.${arch}>
      $<TARGET_OBJECTS:RTSanitizerCommonLibc.${arch}>)
  endif()
  add_library(${ASAN_TEST_RUNTIME} STATIC ${ASAN_TEST_RUNTIME_OBJECTS})
  set_target_properties(${ASAN_TEST_RUNTIME} PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  # Uninstrumented tests.
  set(ASAN_NOINST_TEST_OBJECTS)
  foreach(src ${ASAN_NOINST_TEST_SOURCES})
    asan_compile(ASAN_NOINST_TEST_OBJECTS ${src} ${arch} ${kind}
                 ${ASAN_UNITTEST_COMMON_CFLAGS} ${ARGN})
  endforeach()
  add_asan_test(AsanUnitTests "Asan-${arch}${kind}-Noinst-Test" ${arch} ${kind}
                OBJECTS ${ASAN_NOINST_TEST_OBJECTS}
                LINKFLAGS ${ASAN_UNITTEST_NOINST_LINKFLAGS}
                WITH_TEST_RUNTIME)

  # Benchmarks.
  set(ASAN_BENCHMARKS_OBJECTS)
  foreach(src ${ASAN_BENCHMARKS_SOURCES})
    asan_compile(ASAN_BENCHMARKS_OBJECTS ${src} ${arch} ${kind}
                 ${ASAN_UNITTEST_INSTRUMENTED_CFLAGS} ${ARGN})
  endforeach()
  add_asan_test(AsanBenchmarks "Asan-${arch}${kind}-Benchmark" ${arch} ${kind}
                OBJECTS ${ASAN_BENCHMARKS_OBJECTS}
                LINKFLAGS ${ASAN_UNITTEST_INSTRUMENTED_LINKFLAGS})
  if(COMPILER_RT_BUILD_SHARED_ASAN)
    add_asan_test(AsanBenchmarks "Asan-${arch}${kind}-Dynamic-Benchmark" ${arch} ${kind}
                  OBJECTS ${ASAN_BENCHMARKS_OBJECTS}
                  LINKFLAGS ${ASAN_DYNAMIC_UNITTEST_INSTRUMENTED_LINKFLAGS})
  endif()
endmacro()

if(COMPILER_RT_CAN_EXECUTE_TESTS)
  foreach(arch ${ASAN_SUPPORTED_ARCH})
    add_asan_tests_for_arch_and_kind(${arch} "-inline")
    add_asan_tests_for_arch_and_kind(${arch} "-with-calls"
      -mllvm -asan-instrumentation-with-call-threshold=0)
  endforeach()
endif()

if(ANDROID)
  # We assume that unit tests on Android are built in a build
  # tree with fresh Clang as a host compiler.
  
  # Test w/o ASan instrumentation. Link it with ASan statically.
  add_executable(AsanNoinstTest
    $<TARGET_OBJECTS:RTAsan.arm.android>
    $<TARGET_OBJECTS:RTInterception.arm.android>
    $<TARGET_OBJECTS:RTSanitizerCommon.arm.android>
    ${COMPILER_RT_GTEST_SOURCE}
    ${ASAN_NOINST_TEST_SOURCES})
  set_target_compile_flags(AsanNoinstTest ${ASAN_UNITTEST_COMMON_CFLAGS})
  set_target_link_flags(AsanNoinstTest ${ASAN_UNITTEST_NOINST_LINKFLAGS})
  target_link_libraries(AsanNoinstTest log)

  # Test with ASan instrumentation. Link with ASan dynamic runtime.
  add_executable(AsanTest
    ${COMPILER_RT_GTEST_SOURCE}
    ${ASAN_INST_TEST_SOURCES})
  set_target_compile_flags(AsanTest ${ASAN_UNITTEST_INSTRUMENTED_CFLAGS})
  set_target_link_flags(AsanTest ${ASAN_UNITTEST_INSTRUMENTED_LINKFLAGS})
  target_link_libraries(AsanTest clang_rt.asan-arm-android)

  # Setup correct output directory and link flags.
  set_target_properties(AsanNoinstTest AsanTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  # Add unit test to test suite.
  add_dependencies(AsanUnitTests AsanNoinstTest AsanTest)
endif()
