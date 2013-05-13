project(assertions-instrument)

set(LLVM_LINK_COMPONENTS ${LLVM_TARGETS_TO_BUILD}
	asmparser
	bitreader
	bitwriter
)

add_llvm_executable(${PROJECT_NAME}
	main.cpp
    #Assertion.cpp
    Callee.cpp
    Caller.cpp
    #Instrumentation.cpp
)

#target_link_libraries(assertions-instrument
#)

include_directories("${CMAKE_SOURCE_DIR}/../include")

install(TARGETS ${PROJECT_NAME} DESTINATION bin)

