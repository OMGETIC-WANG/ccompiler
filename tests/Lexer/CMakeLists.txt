ccompiler_add_lexer_cases()

foreach(include_case IncludeQuoted IncludeLocalRelative IncludeSearchOrder)
  set_property(
    TEST "Lexer.${include_case}"
    APPEND
    PROPERTY LABELS include)
endforeach()
