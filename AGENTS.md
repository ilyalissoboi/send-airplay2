# Repository instructions

Read [CONTRIBUTING.md](CONTRIBUTING.md) and [docs/README.md](docs/README.md)
before changing the project. Use [docs/design.md](docs/design.md) for architecture,
the public headers for callable contracts, and
[docs/receiver-validation.md](docs/receiver-validation.md) for compatibility limits.

- Inspect the current repository and actual PR head before editing. Continue an
  existing PR only when it covers the requested change; otherwise start a fresh
  `codex/` branch from `origin/main`. Open a focused PR and ask before merging.
- Keep implemented behavior, proposals and receiver observations distinct. Update
  the maintained component guide when changing a contract; do not add session
  journals, personal environment instructions or duplicated checkpoint histories.
- Build and test using CMake/CTest as described in [docs/building.md](docs/building.md).
  Unit and CI success do not establish receiver interoperability. Repeat manual
  checks for changed behavior or concrete defects, reusing unaffected evidence.
- Preserve Apache-2.0 and record third-party source/license provenance before
  adding implementations or dependencies. Retain required notices and source
  material for the optional FFmpeg runtime.
- Inspect the separate Screenbox repository's instructions before editing it.
  Its packaged UWP build and testing requirements differ from this library.
- Keep credentials, PINs, private media, bearer URLs, receiver identifiers and
  personal filesystem paths out of committed documentation and reports.

## C++ readability

Apply these requirements to every C++ change, including tests:

- Follow `.clang-format`; format touched files and verify with
  `clang-format --dry-run --Werror <files>` and `git diff --check`. Preserve
  platform-sensitive include order.
- Use descriptive names, named protocol sizes and clear offset calculations.
  Explain units, byte order and layouts when they matter.
- Keep functions focused. Extract helpers that clarify state transitions,
  authentication and cleanup; avoid abstractions that conceal those operations.
- Use C++17, direct includes and RAII. Explicitly disable copy/move operations
  where duplication would violate resource or secret ownership. Preserve cleanup
  after allocation, authentication and backend failures.
- Document ownership, lifetime, bounds, threading, exceptions, cancellation and
  partial input/output with Doxygen-style comments where appropriate. Explain
  protocol/security invariants without logging secrets or narrating obvious code.
- Make test failures identify their scenario and useful non-secret context.
  Preserve independent fixture bytes and literal boundary expectations.
- Keep readability refactors behavior-preserving and run the applicable static,
  shared and enabled-codec checks. Inspect CI for the actual final PR head and
  report pending or missing checks accurately.
