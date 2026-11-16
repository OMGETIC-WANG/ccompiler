find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(CCOMPILER_PYTHON_EXECUTABLE
    "${Python3_EXECUTABLE}"
    PARENT_SCOPE)
execute_process(
  COMMAND "${Python3_EXECUTABLE}" -c "import yaml"
  RESULT_VARIABLE CCOMPILER_PYYAML_CHECK
  ERROR_VARIABLE CCOMPILER_PYYAML_ERROR)
if(NOT CCOMPILER_PYYAML_CHECK EQUAL 0)
  message(
    FATAL_ERROR
      "PyYAML is required to generate diagnostics: ${CCOMPILER_PYYAML_ERROR}")
endif()

set(CCOMPILER_ERROR_DEFINITIONS "${CMAKE_CURRENT_SOURCE_DIR}/Errors.yaml")
set(CCOMPILER_ERROR_GENERATOR "${PROJECT_SOURCE_DIR}/tools/generate_errors.py")
set(CCOMPILER_GENERATED_DIR "${PROJECT_BINARY_DIR}/generated")
set(CCOMPILER_ERROR_HEADER
    "${CCOMPILER_GENERATED_DIR}/Diagnostics/Errors.generated.h")
set(CCOMPILER_ERROR_SOURCE
    "${CCOMPILER_GENERATED_DIR}/Diagnostics/Errors.generated.cpp")
set(CCOMPILER_ERROR_VERIFY_SOURCE
    "${CCOMPILER_GENERATED_DIR}/Diagnostics/ErrorsVerify.generated.cpp")

add_custom_command(
  OUTPUT "${CCOMPILER_ERROR_HEADER}" "${CCOMPILER_ERROR_SOURCE}"
         "${CCOMPILER_ERROR_VERIFY_SOURCE}"
  COMMAND
    "${Python3_EXECUTABLE}" "${CCOMPILER_ERROR_GENERATOR}" --definitions
    "${CCOMPILER_ERROR_DEFINITIONS}" --header "${CCOMPILER_ERROR_HEADER}"
    --source "${CCOMPILER_ERROR_SOURCE}" --verify-source
    "${CCOMPILER_ERROR_VERIFY_SOURCE}"
  DEPENDS "${CCOMPILER_ERROR_DEFINITIONS}" "${CCOMPILER_ERROR_GENERATOR}"
  COMMENT "Generating structured diagnostics"
  VERBATIM)
add_custom_target(
  ccompiler_generate_errors
  DEPENDS "${CCOMPILER_ERROR_HEADER}" "${CCOMPILER_ERROR_SOURCE}"
          "${CCOMPILER_ERROR_VERIFY_SOURCE}")
set_source_files_properties("${CCOMPILER_ERROR_SOURCE}" PROPERTIES GENERATED
                                                                   TRUE)

target_sources(ccompiler_frontend PRIVATE "${CCOMPILER_ERROR_SOURCE}")
target_include_directories(ccompiler_frontend
                           PUBLIC "${CCOMPILER_GENERATED_DIR}")
add_dependencies(ccompiler_frontend ccompiler_generate_errors)

# The verification source is compiled only by ccompiler_tests.
set(CCOMPILER_ERROR_VERIFY_SOURCE
    "${CCOMPILER_ERROR_VERIFY_SOURCE}"
    PARENT_SCOPE)
