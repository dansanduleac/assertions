cmake_minimum_required(VERSION 2.6)
project(assertions)

set(CMAKE_MODULE_PATH
  ${CMAKE_MODULE_PATH}
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Modules")

# Always build with all warnings
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wstrict-prototypes")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wall")

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

#
# LLVM configuration
#

# This deals with making sure llvm-config is found and sets all the different
# paths for us.
find_package(LLVM REQUIRED)
include_directories(${LLVM_INCLUDE_DIR})   # Not enough, misses binary one
include_directories("${LLVM_OBJ}/include") # There we go.
#set(LIBS ${LIBS} ${LLVM_MODULE_LIBS})
link_directories(${LLVM_LIBRARY_DIR})

# Using install prefix from llvm-config, locate the LLVM cmake files.
# There should be a LLVMConfig.cmake there to set everything up.
set(CMAKE_MODULE_PATH
  "${CMAKE_MODULE_PATH}"
  "${LLVM_PREFIX}/share/llvm/cmake")
include(LLVMConfig)

include(AddLLVM)

# ${LLVM_MODULE_LIBS} is what llvm_map_components_to_libraries does..
add_definitions(${LLVM_DEFINITIONS})
## # This one sets up the LLVM_INSTALL_PREFIX include dir, which is enough.
## include_directories(${LLVM_INCLUDE_DIRS})
## link_directories(${LLVM_LIBRARY_DIRS})
## 
## # TODO:
## # From AddLLVM.cmake:
## # Add external project that may want to be built as part of llvm such as Clang,
## # lld, and Polly. This adds two options. One for the source directory of the
## # project, which defaults to ${CMAKE_CURRENT_SOURCE_DIR}/${name}. Another to
## # enable or disable building it with everthing else.
## # Additional parameter can be specified as the name of directory.
## # macro(add_llvm_external_project name)

# LLVM libraries that we need:
#llvm_map_components_to_libraries(LLVM_LIBS bitreader core irreader)
#set(LLVM_LINK_COMPONENTS ${LLVM_TARGETS_TO_BUILD})

## # Work out of the LLVM source/build trees if LLVM is not installed.
## )
## exec_program(llvm-config ARGS --src-root OUTPUT_VARIABLE LLVM_SRC)
## exec_program(llvm-config ARGS --obj-root OUTPUT_VARIABLE LLVM_OBJ)
## include_directories("${LLVM_SRC}/include" "${LLVM_OBJ}/include")
## link_directories("${LLVM_OBJ}/lib/")
## 
## # Use LLVM's CFLAGS and CXXFLAGS, but filter out optimisations and -DNDEBUG.
## exec_program(llvm-config ARGS --cflags   OUTPUT_VARIABLE LLVM_C)
## exec_program(llvm-config ARGS --cxxflags OUTPUT_VARIABLE LLVM_CXX)
## string(REGEX REPLACE "(.*)-O[0-9] -DNDEBUG(.*)" "\\1\\2" LLVM_C ${LLVM_C})
## string(REGEX REPLACE "(.*)-O[0-9] -DNDEBUG(.*)" "\\1\\2" LLVM_CXX ${LLVM_CXX})
## set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${LLVM_C}")
## set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${LLVM_CXX_FLAGS}")

# Important CXXFLAGS that llvm-config --cxxflags doesn't give us:
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility-inlines-hidden")

# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-nested-anon-types")

include_directories("${CMAKE_SOURCE_DIR}/include")

add_subdirectory(instrumentation)
add_subdirectory(instrumenter)
