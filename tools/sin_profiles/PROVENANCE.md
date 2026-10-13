# Offline SIN compiler provenance

Jarvis-HOOK, 2026-10-03. Source: [FFX Editor](https://github.com/WanxTitanx/ffx-editor-main),
`FFXProjectEditor/FfxLib/Ai/Sin/SinPostActionPresetWriter.cs`, checkout HEAD
`399164236638bd34d44c4833b02ea3b15a49d771`; exact source SHA-256
`a1b91fc65b82f20e471af9175417de787399eb07a9149a0b5703fc6c9f362343`. The current source may include
local changes, so the hash identifies the adapted input more precisely than HEAD.

Both repositories carry GNU GPL version 3. The offline compiler is adapted under
that compatible license. Its local extension adds alive checks before extra
commands and avoids a second hit against a dead previous target. Native action
calls, tail state updates and non-turn events are retained. The existing Editor
catalog, parser and validator are referenced; this work does not alter that checkout.

Generated game bytecode and extracted game assets stay in private ignored paths.
Committed generated headers contain authored metadata and hashes only. The new
per-monster matrix is a Hook curation decision, not a claim that historical Editor
drafts passed runtime testing. Native function details are from the user's exact
old/new FFX executables and the recorded local IDA evidence.
