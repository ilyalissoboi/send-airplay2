# C# binding (D52)

Status: **implemented, experimental, offline-tested only.** Snapshot 2026-10-08
(Asia/Tokyo). Step 2a of D49 ([credential-interface.md](credential-interface.md)):
a C# binding over the experimental C interface. The packaged UWP test app (step
2b) is a separate PR. No receiver has been cast to or paired through this binding
yet; its native calls are the ones D48 and D51 tested from C.

## Layout

| Path | Purpose |
| --- | --- |
| `bindings/csharp/SendAirPlay2/` | `netstandard2.0` library, no package dependencies, so UWP (.NET Native) and modern .NET hosts can share it |
| `bindings/csharp/SendAirPlay2.Tests/` | `net8.0` console test runner (`RollForward=Major`) against the real native library, whose path is its argument |

The library loads `send_airplay2` (`send_airplay2.dll`, `libsend_airplay2.so` or
`.dylib`) by the platform's normal rules; a host can redirect that with
`NativeLibrary.SetDllImportResolver`, as the tests do.

## API

| C | C# |
| --- | --- |
| `sap2_cast_*` | `Cast` (`IDisposable`): `Create`, `Start`, `GetStatus`, `WaitForChange`, `Play`, `Pause`, `Seek`, `StopPlayback` (MRP stop), `Stop` (local teardown), `Dispose` |
| `sap2_cast_options` | `CastOptions`, including `CredentialStore` |
| `sap2_media_source` | abstract `MediaSource` (`Size`, `Read`, `OnReleased`); `FileMediaSource` for desktop files |
| `sap2_credential_store` | `ICredentialStore` (`Load`, `SaveNew`, `Erase`) |
| `sap2_pair`, `sap2_forget_profile` | `Pairing.Pair(PairOptions, PinReader)`, `Pairing.ForgetProfile` |
| `sap2_cast_status` | `CastStatus` with nullable scalars |
| `SAP2_*` results | `ResultCode`; failures throw `SendAirPlay2Exception` with fixed text only |

## Ownership, lifetime and threading

These follow the C contracts (public-api.md, credentials.h, pairing.h):

- **Callback delegates live as long as the library may call them.** A cast's
  delegates (media source and credential store) are held by its `SafeHandle`
  and dropped only after `sap2_cast_destroy` returns, including when an
  undisposed cast is destroyed by its finalizer. Pairing and removal keep theirs
  alive until the native call returns.
- **Destroy waits for in-flight calls.** Every cast call passes the `SafeHandle`,
  so P/Invoke reference counting delays destroy until other calls return.
- **A media source is released exactly once:** when its cast is disposed, or at
  once if `Cast.Create` fails (the C library never took ownership then).
- **Exceptions never reach native code.** A throwing `Read` fails that request;
  a throwing store reports Unavailable; a throwing PIN reader cancels.
- **Secrets in managed memory cannot be wiped reliably.** The binding clears its
  temporary record and PIN arrays after each call; host copies are the host's
  responsibility. The PIN reader receives a `char[]`, not a `string`, so it can be
  cleared.
- **Size-aware initialization.** Options are initialized with
  `sap2_cast_options_init_sized` and `sap2_pair_options_init` using the binding's
  struct sizes; a library older than API version 2 is refused with
  `NotSupportedException`.
- Callbacks run on library threads (media reads) or on the calling thread (store
  and PIN) and must not call back into the library for the operation that
  invoked them.

## Validation

`csharp_binding_tests` runs in CTest on shared builds when a .NET SDK is found
(option `SAP2_DOTNET_TESTS`, default on), building into the CMake build tree.
Windows 11 x64, MSVC Release: static 29/29 (the C# test needs the shared library),
shared 30/30. The runner covers:

- runtime API version and version 2 result names;
- struct sizes and field offsets, checked through the native initializers
  (size read back, defaults on the expected fields);
- source ownership: release once on failed create, on dispose, and for
  `FileMediaSource` (stream closed);
- callback lifetime: a store and source referenced only by the cast still work
  after forced collections, and an undisposed cast is destroyed by its finalizer.
  Removing the `SafeHandle` keep-alive makes the runtime fail fast with a
  callback on a collected delegate;
- a cast with a host store: absent, unavailable, throwing and malformed stores;
- profile removal, including an absent profile in the built-in store;
- pairing refusals: invalid options, a malformed existing record and an
  unreachable receiver, with no PIN request and no save.

Not covered: receiver casts or pairing through C#, packaged UWP loading,
`PasswordVault`, `StorageFile` sources, and .NET Native compilation. Those are
step 2b.
