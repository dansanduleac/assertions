project(instrumentation)

set(FILE Assertions.c)

# foreach, output, string replace

add_custom_command(
  OUTPUT Assertions.bc
  COMMAND ${CMAKE_C_COMPILER} -cc1 -emit-llvm-bc -Os -o Assertions.bc
  ${CMAKE_CURRENT_SOURCE_DIR}/${FILE}
  DEPENDS ${FILE}
)
add_custom_target(assertions_bc ALL DEPENDS Assertions.bc)

# set_source_files_properties( Assertions.bc PROPERTIES GENERATED TRUE )
