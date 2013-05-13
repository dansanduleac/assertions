cmake_minimum_required(VERSION 2.6)
project(assertions)

set(CMAKE_MODULE_PATH
  ${CMAKE_MODULE_PATH}
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Modules")

message(STATUS "CMAKE_MODULE_PATH: " "${CMAKE_MODULE_PATH}")

# Always build with all warnings and with warning=>error
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wstrict-prototypes -Werror")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wall -Werror")

# Default to libc++ if installed.
find_package(LibCXX)

set(USE_LIBCXX ${LIBCXX_FOUND} CACHE BOOL "Use libc++ when building")
if (USE_LIBCXX)
  message(STATUS "Using libc++")
else ()
  message(STATUS "Using the default system STL implementation")
endif ()

if (USE_LIBCXX)
  set(CMAKE_CXX_LINK_FLAGS "${CMAKE_CXX_LINK_FLAGS} -stdlib=libc++")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")

  include_directories(${LIBCXX_INCLUDE_DIRS})
endif ()

# Set -D DEBUG on debug builds, in addition to the CMake default of setting
# -D NDEBUG for release builds and assuming debug mode otherwise.
set_directory_properties(PROPERTIES COMPILE_DEFINITIONS_DEBUG DEBUG)

include_directories("${CMAKE_SOURCE_DIR}/include")

# A top-level 'test' target to drive all tests of TESLA subdirectories.
# add_custom_target(test ALL DEPENDS libtesla-test tesla-test)


## START SECTION: assertions
## =========================

# #
# # TESLA uses some C++11 extensions like 'auto', but doesn't require a
# # C++11-compliant standard library.
# #
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-c++11-extensions")


#
# LLVM configuration
#
find_package(LLVM REQUIRED)
include(AddLLVM)

add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})
message(STATUS "LLVM_DEFINITIONS: " "${LLVM_INCLUDE_DIRS}")
link_directories(${LLVM_LIBRARY_DIRS})

# LLVM libraries that we need:
llvm_map_components_to_libraries(LLVM_LIBS bitreader)
set(LLVM_LINK_COMPONENTS ${LLVM_TARGETS_TO_BUILD})

# Work out of the LLVM source/build trees if LLVM is not installed.
exec_program(llvm-config ARGS --src-root OUTPUT_VARIABLE LLVM_SRC)
exec_program(llvm-config ARGS --obj-root OUTPUT_VARIABLE LLVM_OBJ)
include_directories("${LLVM_SRC}/include" "${LLVM_OBJ}/include")
link_directories("${LLVM_OBJ}/lib/")

# Use LLVM's CFLAGS and CXXFLAGS, but filter out optimisations and -DNDEBUG.
exec_program(llvm-config ARGS --cflags   OUTPUT_VARIABLE LLVM_C)
exec_program(llvm-config ARGS --cxxflags OUTPUT_VARIABLE LLVM_CXX)
string(REGEX REPLACE "(.*)-O[0-9] -DNDEBUG(.*)" "\\1\\2" LLVM_C ${LLVM_C})
string(REGEX REPLACE "(.*)-O[0-9] -DNDEBUG(.*)" "\\1\\2" LLVM_CXX ${LLVM_CXX})
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${LLVM_C}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${LLVM_CXX_FLAGS}")

# Important CXXFLAGS that llvm-config --cxxflags doesn't give us:
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-nested-anon-types")

add_subdirectory(instrumenter)
