# Continuation instructions

Start with [docs/HANDOFF.md](docs/HANDOFF.md), then read
[docs/design.md](docs/design.md) and [docs/receiver-validation.md](docs/receiver-validation.md).
They preserve the user scope, decisions, evidence, current status and next steps.

- Continue on the current PR branch while it is open; inspect its actual head
  before writing. The initial API is experimental and the library cannot cast yet.
- Keep user requirements, engineering proposals and tested behavior distinct.
  Update the handoff when making a material decision or reaching a validation gate.
- Build/test using CMake and CTest as described in README.md. Unit/CI success is
  not receiver interoperability. Keep receiver/firmware results explicit.
- Preserve the existing Apache-2.0 license. Record source/license provenance before
  adding third-party implementations or dependencies.
- Inspect current Screenbox repository instructions before editing that project;
  its packaged UWP build and testing requirements differ from this native library.
