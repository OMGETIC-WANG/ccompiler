# gtest_discover_tests only preserves the first label in multi-label properties,
# so apply the complete label sets after discovery.
if(ast)
  set_tests_properties(${ast} PROPERTIES LABELS "selftest;ast;frontend")
endif()

if(verifier_registry)
  set_tests_properties(${verifier_registry}
                       PROPERTIES LABELS "selftest;ast;schema;frontend")
endif()

if(lexer)
  set_tests_properties(${lexer} PROPERTIES LABELS "selftest;lexer;frontend")
endif()

if(codegen)
  set_tests_properties(${codegen} PROPERTIES LABELS "selftest;codegen;frontend")
endif()

if(diagnostics)
  set_tests_properties(${diagnostics}
                       PROPERTIES LABELS "selftest;diagnostics;frontend")
endif()

if(cmdline)
  set_tests_properties(${cmdline} PROPERTIES LABELS
                                             "selftest;diagnostics;frontend")
endif()

if(source_manager)
  set_tests_properties(${source_manager} PROPERTIES LABELS "selftest;frontend")
endif()
