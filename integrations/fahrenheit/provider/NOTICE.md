# Cooperative Fahrenheit provider

Jarvis-HOOK, 2026-09-29. This optional integration is not an upstream Fahrenheit
release or an upstream-approved API. It targets reviewed source revision
`cdb145d93295c1c6e2bf4766fda5a12877369f54` only.

Fahrenheit is LGPL-3.0-or-later, copyright 2023-2026 The Fahrenheit contributors.
Keep original SPDX/copyright headers, COPYING and COPYING.LESSER with modified
source. The added cooperative core file is also LGPL-3.0-or-later.

Modified provider surfaces: post-hook-commit initialization, primary save/load
transaction boundaries, canceled load identity, and read-only file resolution.
Native serialization and persistent stores are reused, not replaced.

Supply complete corresponding source and license notices with redistributed
modified Fahrenheit binaries. Building this integration neither installs it nor
promotes its artifacts to an upstream or public release.
