# Deferred test cases

Fixtures in this directory describe intended behavior that the compiler does
not support yet. They are kept out of the active CTest suites so the green
suite reflects supported behavior only.

Each subdirectory names the suite that should eventually own the fixture. When
the compiler support and the corresponding expectations are ready, move the
fixture and its sidecars back into that suite and register it there.

There are currently no deferred cases.
