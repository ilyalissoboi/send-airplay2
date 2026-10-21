# Contributing

## Find the relevant contract

Read the [architecture](docs/design.md) and the guide for the component you plan
to change. Public headers in [include/send_airplay2](include/send_airplay2) define
the callable contracts. The [documentation index](docs/README.md) links protocol,
media, binding, packaging and receiver-test guides.

Check open issues and pull requests before beginning overlapping work. Keep a
change focused and describe the problem, resulting behavior and validation in
its pull request. Use a feature branch based on current `main`; do not merge
unrelated work into an existing change. Conventional commit titles are preferred.

## Build and validate

Follow [building.md](docs/building.md). For native changes, run the applicable
static and shared CMake/CTest suites. Conversion changes also need the enabled
codec tests; UWP changes need AppContainer builds and binary checks. C# changes
need the shared-library binding tests. CI covers these configurations, but its
success does not establish playback on a receiver.

Format touched C++ headers and sources using the repository `.clang-format`,
preserving platform-sensitive include order:

```sh
clang-format -i path/to/changed.cpp path/to/changed.h
clang-format --dry-run --Werror path/to/changed.cpp path/to/changed.h
git diff --check
```

Use C++17, descriptive names, direct includes and RAII. Keep responsibilities
focused and make ownership, cancellation, threading, bounds and failure states
explicit in callable documentation. Secret/resource owners must not be copied
when duplication would violate their contract. Refactors should preserve
behavior and coverage. Tests should identify their scenario without exposing
secrets and retain independent known-answer bytes and literal boundary values.

## Receiver validation

Use [manual-validation.md](docs/manual-validation.md) for changes that affect
receiver behavior. Record the tested commit, binaries, host, receiver model and
firmware. Separate automated preparation/cleanup results from a person's
observations of sound, picture and receiver controls. Keep failures and skipped
checks explicit; do not treat sender `playing` telemetry as audible playback.

Reuse unchanged receiver evidence. Repeat consent/cancellation or a complete
codec matrix when the affected code or a reproducible defect warrants it,
rather than for every documentation or packaging change. Contributor reports
belong in the relevant issue or pull request; avoid committing machine-specific
logs, personal paths, receiver addresses, names, identifiers, credential records,
PINs or bearer media URLs.

Fixtures must be repository-authored or have recorded redistribution rights.
For encoded fixtures retain their generator, configuration and content hashes;
see [test fixture provenance](tests/fixtures/README.md). Do not add private films
or captured authentication transcripts.

## Dependencies and releases

Preserve the project's Apache-2.0 license. Record source revision, license and
redistribution material in [dependencies.md](docs/dependencies.md) before adding
third-party code or changing a dependency profile. Keep conversion optional and
its dynamic runtime separate from the codec-free package.

Follow [nuget-package.md](docs/nuget-package.md) when preparing a distribution.
Never replace published bytes under an existing version. Publication and merging
are maintainer actions; preparing a pull request does not authorize either.

Changes to the separate Screenbox project must follow that repository's own
instructions and packaged-app validation requirements.
