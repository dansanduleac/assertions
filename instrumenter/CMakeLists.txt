project(assertions-instrument)

set(LLVM_LINK_COMPONENTS
     ${LLVM_TARGETS_TO_BUILD}
     asmparser
     bitreader
     bitwriter
     core
     irreader
     linker
 )

# LLVM libraries that we need:
#llvm_map_components_to_libraries(LLVM_LIBS ${LLVM_LINK_COMPONENTS})
#message(STATUS "LLVM_LIBS: " "${LLVM_LIBS}")

add_llvm_executable(${PROJECT_NAME}
	main.cpp
  Callee.cpp
  Caller.cpp
  Common.cpp
)

# Bit of a hack, methinks..
# But I want to reuse the header files deadling with assertion parsing.
include_directories("${LLVM_SRC}/tools/clang/tools/assertions")

add_definitions("-DASSERTIONS_MODULE_PATH=${CMAKE_BINARY_DIR}/instrumentation/Assertions.bc")
# Y U NO WORK...
add_dependencies(${PROJECT_NAME} assertions_bc)


#target_link_libraries(assertions-instrument
#)

# include_directories("${CMAKE_SOURCE_DIR}/../include")

install(TARGETS ${PROJECT_NAME} DESTINATION bin)

