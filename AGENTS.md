# Continuation instructions

Start with [docs/HANDOFF.md](docs/HANDOFF.md), then read
[docs/design.md](docs/design.md) and [docs/receiver-validation.md](docs/receiver-validation.md).
They preserve the user scope, decisions, evidence, current status and next steps.

- Continue on the current PR branch while it is open; inspect its actual head
  before writing. The API is experimental; native-only G1 passed for one recorded receiver/host.
  Use the current PR handoff rather than assuming that casting code is absent.
- Keep user requirements, engineering proposals and tested behavior distinct.
  Update the handoff when making a material decision or reaching a validation gate.
- Build/test using CMake and CTest as described in README.md. Unit/CI success is
  not receiver interoperability. Keep receiver/firmware results explicit.
- Preserve the existing Apache-2.0 license. Record source/license provenance before
  adding third-party implementations or dependencies.
- Inspect current Screenbox repository instructions before editing that project;
  its packaged UWP build and testing requirements differ from this native library.

## C++ readability requirements

Apply these requirements to every C++ change, including tests, before finalizing
the current PR. Make readability part of implementation, not a separate cleanup
left for the reviewer.

- Follow the repository `.clang-format` file. Format touched C++ headers/sources
  and verify them with `clang-format --dry-run --Werror <touched files>`; also run
  `git diff --check`. Preserve platform-sensitive include order.
- Use descriptive names that match actual behavior. A function that constructs a
  nonce from a counter must not suggest that it advances the counter. Name
  protocol sizes/limits and use named calculations for non-obvious offsets and
  bounds; explain units, byte order and layout when they matter.
- Keep functions focused on a coherent responsibility. Split mixed test scenarios
  into named groups and extract helpers when they clarify intent. Avoid arbitrary
  function-length rules or abstractions that hide authentication, state transitions,
  counter advancement or cleanup order.
- Use C++17 idioms, direct includes for used standard facilities, and RAII for
  resource/secret ownership. Declare copy/move behavior explicitly for ownership
  guards when duplication would violate their contract or create unwanted secret
  copies. Preserve cleanup on allocation, backend and authentication failures.
- Document callable contracts and non-obvious invariants with Doxygen-style
  comments where appropriate: ownership/lifetime, bounds, threading, exceptions,
  cancellation, partial input/output and failure state. Explain why protocol and
  security rules exist; do not narrate obvious statements or log secrets.
- Make test failures identify the scenario and useful non-secret context (such as
  a split offset or mutation index). Name fixture layout calculations. Preserve
  independent known-answer bytes and literal protocol boundary expectations rather
  than deriving expected results from the implementation being tested.
- Keep readability refactors behavior-preserving. Retain fixture data and existing
  coverage, run the applicable CMake/CTest static/shared checks described in README,
  and inspect CI for the actual PR head. Update the handoff for material decisions
  or validation gates; do not turn unit/CI success into an interoperability claim.
