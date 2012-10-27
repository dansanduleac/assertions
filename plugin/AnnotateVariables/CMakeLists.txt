cmake_minimum_required (VERSION 2.6)
project (AnnotateVariables)

set(SYMBOL_FILE $PROJECT.exports)

set (CLANG_LIBS
  clang
  clangFrontend
  clangAST
  clangAnalysis
  clangBasic
  clangCodeGen
  clangDriver
  clangFrontendTool
  clangIndex
  clangLex
  clangParse
  clangRewrite
  clangSema
  clangSerialization
  clangStaticAnalyzerCheckers
  clangStaticAnalyzerCore
  clangStaticAnalyzerFrontend
)

set (USER_LIBS
  pthread
)

add_clang_plugin($PROJECT AnnotateVariables.cpp)

set_target_properties($PROJECT PROPERTIES
  LINKER_LANGUAGE CXX
  PREFIX "")
