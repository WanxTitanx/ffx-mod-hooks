# Fifth-slot order and individual Scan controls

Jarvis-HOOK, 2026-09-26. Follow-up to main `88fe49f`, developed on
`fix/workshop-scan-20260926`. This records implementation and isolated validation,
not live visual acceptance or authorization to install a DLL.

## Corrected requirements

The user explicitly corrected the earlier interpretation: four OPEN native
slots suffice to unlock the fifth, regardless of their occupancy. Placing or
replacing the fifth ability requires all four native abilities to be FILLED.
The shared core now enforces these separate conditions, and the Workshop picker
explains incomplete native abilities instead of offering invalid placements.

Unlock still charges ten owner spheres with Master substitution and binds the
owner. Fifth customization still charges ceil(1.5 times the native recipe) plus
200000 Gil. Development waivers do not bypass slot order. No new restriction is
applied to loading existing extensions: clearing an ordinary ability does not
destroy an existing fifth or its binding. Persistent layouts and transient ABI3
are unchanged. No fifth-slot Fusion or vanilla Customize writing bypass is added.

## Missing visual features: observed configuration

The installed file was read back as SHA256
`b46b6ce72b7269abb6ac63ef206b57d6a539cdbef14623fa4832bb5de4dbe802`.
The `_isolated/ffx-hooks.ini` read for diagnosis had `labs.element_scan_dark=0`
and no `labs.equipment_workshop_native_ui` key, whose default is OFF. The inspected
startup records show Workshop save admission, but no initialization messages for
either visual hook. No running-process module or GPU state was inspected.

These OFF settings explain why enabling Workshop alone does not show the new
native details or extra Scan columns. They do not establish whether the enabled
renderer is visually correct in the player's session. Both startup gates now
have explicit diagnostic logs. The F10 header reports the actual native detail
hook ON/OFF state, and Scan settings report the actual Scan hook ON/OFF state.

To enable native Equipment/Customize/battle details, select both Equipment
Workshop and Native equipment details in F8 > Reforge, then restart. To enable
Scan extensions, select Extra Scan elements there, then restart. Nothing in this
fix changes the player's installed INI or implicitly enables a visual hook.

## Independent extra elements

Open F8 > Reforge > Scan element colors > Enabled extra elements. Holy, Darkness
and Custom extra have independent toggles, permitting any subset of zero to
three extra elements. Preferences default to ON behind the default-OFF master;
the master remains the prerequisite for any effect. Toggle writes use existing
configuration persistence/readback. Invalid values fail closed, and failed writes
preserve the previous choice. Color settings and the custom 0x20/0x40 bit remain
independent; no new element name or damage/Nul behavior is invented.

Enabled columns are packed without holes, retain the proper colors and actual
weak/absorb/null/resist masks, and resize only the admitted panel call. With zero
extras, the original 385-pixel panel and four-element presentation are retained.
One, two and three extras use 434, 497 and 560 design pixels respectively.

## Verification

Commands were run from the native-ui worktree. Private fixtures use PE SHA256
`78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced`.
The byte profiles/render entry points are unchanged from the earlier native UI
implementation. No game image or extracted asset is included in the commit.

| Surface | Result |
|---|---:|
| Fifth-order matrix | RED 55/116; GREEN 116/116, normal and ASan/UBSan |
| Workshop core/economy/progression | 1283/2660/62, each normal and ASan/UBSan |
| Python host/save tests with fixtures | 37/37 |
| Presentation/element pure cores | 15/3126, each normal and ASan/UBSan |
| Real native presentation harness | 126/126 Windows and identical executable on Proton |
| Menu/settings/transaction | 11478/29/34 Windows and Proton |
| Runtime/store/native save flow | 67/12/38 OFF + 38 ON, Windows and Proton |
| Ronso I/O | 18 ON / 13 OFF, Windows and Proton |
| F7 runtime/UI; F8 runtime | 3621/59; 3884, Windows and Proton |
| Native effects | 6368/6368, current MinGW binary in private Proton prefix |
| Release MSVC + PolyHook2 and DLL loaders | Build and Windows/Proton load/export/wait passed |

The menu regression exercises all 256 affinity bytes for every three-toggle
combination. The native harness exercises every combination for both custom bits,
checking panel width, emitted columns, palette identities and extra mask reads.
It retains the original return values and earlier OFF/thread/hidden-data tests.
Native rendering uses private mapped PE functions with explicit device/world
substitutions; it is not a GPU screenshot or a test in the user's live save.

Red/green logs for missing individual controls and missing ON/OFF explanations
are retained under `.superpowers/slot-scan-fix-20260926/`. The new order matrix is
part of `research/equipment_workshop/run_checks.py`, not an unregistered test.
Three pre-existing C4996 warnings remain. Review was inline, not independent.

## Candidate and evidence

Candidate: `.superpowers/native-ui-20260925/vm-native-slot-scan-1e68567c/candidate/ffx-hooks.dll`
within `/home/wanderson/Documents/ffx-hooks-native-ui-20260925`.

SHA256: `ed920390fd0ba697b3192a7ce14e5f373a2c483fd6ddecea1f079f6190b833d1`.
Size: 1851904 bytes. Windows lane:
`C:/VMTasks/ffx-hooks-native-ui-20260926-slot-scan-1e68567c`.
The build verified 419 source inputs and recovery verified 17 artifacts. The
fourteen-case Proton replay is recorded in
`.superpowers/native-ui-20260925/resume-verification-351b242d60/receipt.json`.
Local checks and the Windows loader are recorded in
`.superpowers/slot-scan-fix-20260926/`.

No deployment, installed-configuration edit, game launch, process termination,
live save test or Production promotion was performed. The new candidate still
requires separate deployment authorization and enabled-feature visual acceptance.
