# Cooperative save protocol and lifecycle

Jarvis-HOOK, 2026-09-29. The required lifecycle below is implemented by the
optional V2 provider and native transport. Stock Fahrenheit/V1 does not expose
these callbacks. See [provider instructions](provider/README.md) and the
[implementation ledger](../../docs/research/FAHRENHEIT_SERVICES_F7_F8_2026_09_29.md).

## Observed paths

At Fahrenheit `cdb145d93295c1c6e2bf4766fda5a12877369f54`,
`src/runtime/save_impl.cs` writes both manual saves and autosaves through managed
`FileStream`. Native FFX Hooks observes the game's `msvcr110` import slots and
specific caller RVAs in `RonsoPoolRuntime.cpp`. A managed file operation does
not pass through those game imports.

Fahrenheit calls `save_local_state` after writing the primary save. Its
`PreSaveGame` event is declared with an implementation TODO in
`src/core/events/game_loop/save_load.cs` and is not invoked by the current
save implementation. The actual selected save path is internal to the save
manager. On load, local module state is dispatched after the native RAM copy,
and modules without an existing state file receive no local-state callback.

Consequently, simply adding a local-state serializer would miss pre-write
projection, first-load identity, failed-load invalidation, paid-checkpoint
selection, and successful-close verification. A fifth equipment slot is only
one consumer of this lifecycle. Ronso ownership, projected temporary fields,
Seymour/Grid8 state, and paid Workshop checkpoints require explicit ordering.

## Required provider contract

The save owner must publish an immutable canonical identity containing the
actual path, save-set identity, final remapped slot, operation kind, and a
unique operation serial. The native bridge must validate the FFX executable,
buffer length, pointer range, owner thread and session generation before
admitting a transaction. Paths must come from the provider's actual operation;
deriving them from a UI selection or scanning directories is insufficient.

For a write, the owner must call the bridge before opening or truncating the
primary file. The bridge receives immutable source bytes and a separate output
buffer, applies native serialization and projections, seals the final checksum,
and stages extension metadata for those exact bytes. The owner writes that
output exactly once, reports aborts, and reports completion only after successful
close. A verified disk readback must match the staged path, length and bytes
before any `WriteVerified` consumer is notified. A metadata failure must not
cause a second primary write or a false successful extension commit.

For a read, the owner must invalidate the destination's old provenance before
the read attempt, including short/failed reads. After a complete disk read and
before game CRC validation/RAM publication, the bridge selects any validated
checkpoint and transforms the separate load image. Consumers receive the
original disk identity and the exact selected bytes. CRC failure, cancellation,
or interrupted copy must reject the pending generation. A later successful
native RAM-copy notification admits that generation exactly once.

Every transaction must finish or abort. A failed or absent bridge must not
silently persist RAM that still needs a projection. Changing save sets, saving
as another slot, two identical saves in different paths, first load without
metadata, and return-to-title must not reuse old identity.

## Reuse and acceptance

Implement the adapter through the existing `NativeSaveEvents` observer registry
and the existing Ronso/Workshop serialization owners. Do not introduce a second
competing `fread` hook or duplicate the persistent stores. Public Fahrenheit
callbacks at the above boundaries are preferable to reflection, private-state
offsets, or interception of unrelated .NET file operations.

Both projects use negotiated services protocol 2. Rendering capability alone
never enables it. V1 frame capabilities remain 3; managedSaveCompatible is 1
only after active V2 save-service initialization, and stays 0 in stock V1.
Concrete native entrypoints are in FahrenheitServices.h and the managed client
is CooperativeClient.cs. These are a local integration, not an upstream-approved
API; no external message or upstream modification is implied.

Acceptance must exercise manual saves, autosaves, slot remapping, save-set
switching, same-byte/different-path saves, short reads, CRC failures, canceled
loads, write/close/readback failures, missing/corrupt metadata, checkpoint
selection, and restore after disabling each feature. RT0/RT1 precede a
separately authorized RT2 run on disposable saves.
