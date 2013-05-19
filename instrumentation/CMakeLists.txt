cmake_minimum_required(VERSION 2.8)

project(instrumentation)

set(FILE Assertions.c)
set(DEPS AssertionBase.h)
set(OUTPUT Assertions.bc)

# message(STATUS "DEPFILE FLAGS: " ${CMAKE_DEPFILE_FLAGS_CXX})

add_custom_command(
  OUTPUT ${OUTPUT}
  COMMAND ${CMAKE_C_COMPILER} -cc1 -emit-llvm-bc -Os -o ${OUTPUT}
  ${CMAKE_CURRENT_SOURCE_DIR}/${FILE}
  MAIN_DEPENDENCY ${FILE}
  OUTPUT_REQUIRED_FILES ${FILE}
  DEPENDS ${DEPS}
  COMMENT "Compiling assertions to LLVM Module"
  # DEPENDS ${FILE}
)

set_source_files_properties( ${OUTPUT} PROPERTIES GENERATED TRUE )

add_custom_target(
  assertions_bc ALL
  DEPENDS ${OUTPUT}
  VERBATIM)
