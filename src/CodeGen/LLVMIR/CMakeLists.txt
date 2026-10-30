if(NOT LLVM_DIR
   AND DEFINED ENV{LLVM_DIR}
   AND NOT "$ENV{LLVM_DIR}" STREQUAL "")
  set(LLVM_DIR
      "$ENV{LLVM_DIR}"
      CACHE PATH "Path to LLVM's CMake package" FORCE)
endif()

if(NOT LLVM_DIR)
  find_program(LLVM_CONFIG_EXECUTABLE NAMES llvm-config)
  if(LLVM_CONFIG_EXECUTABLE)
    execute_process(
      COMMAND "${LLVM_CONFIG_EXECUTABLE}" --cmakedir
      RESULT_VARIABLE LLVM_CMAKEDIR_RESULT
      OUTPUT_VARIABLE LLVM_CMAKEDIR
      ERROR_VARIABLE LLVM_CMAKEDIR_ERROR
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(LLVM_CMAKEDIR_RESULT EQUAL 0 AND EXISTS
                                        "${LLVM_CMAKEDIR}/LLVMConfig.cmake")
      set(LLVM_DIR
          "${LLVM_CMAKEDIR}"
          CACHE PATH "Path to LLVM's CMake package" FORCE)
    else()
      message(WARNING "Could not get LLVM's CMake package directory from "
                      "${LLVM_CONFIG_EXECUTABLE}: ${LLVM_CMAKEDIR_ERROR}")
    endif()
  endif()
endif()

find_package(LLVM 23.1.3 EXACT CONFIG REQUIRED)

target_sources(
  ccompiler PRIVATE Generator.cpp GenContext.cpp GenDecl.cpp GenExpr.cpp
                    ${PROJECT_SOURCE_DIR}/src/JITRunner/JITRunner.cpp)

target_include_directories(ccompiler PRIVATE ${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(ccompiler PRIVATE ${LLVM_DEFINITIONS_LIST})
llvm_map_components_to_libnames(LLVM_COMPONENT_LIBS core support native orcjit)
target_link_libraries(ccompiler PRIVATE ${LLVM_COMPONENT_LIBS})
