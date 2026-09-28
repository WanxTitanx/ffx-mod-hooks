# MOD-006 future pack live acceptance

Jarvis-HOOK, 2026-09-28. **Procedure only: no deployment or live session is recorded by this document.** Follow `docs/RT2_PROTOCOL.md`. Isolated native tests and a DLL hash are RT1/build evidence, not RT2 or Production.

## Before the separately authorized session

Record permission for the exact DLL deployment and live test, confirm a disposable save, close the Editor and FFX, and verify those processes are absent. Use the exact supported executable; record SHA-256 of the EXE, candidate DLL, manifest, recipe, source snapshot, configuration and relevant existing save files. Record the log's starting byte offset, prefix hash and timestamps. Back up the exact leaves that deployment/configuration will touch. Do not install into original `new_uspc` or change the VBF.

Create a fresh source snapshot with `tools/text_languages/reference.py`; run `TextLanguageValidate` on the **received package**, not the old demonstration. Require exit 0 / `ADMITTED RT0`. Review unsupported families, over-limit strings and unmapped glyphs before proceeding. Choose native English through the existing selector without changing voice choices. Start with `language.text_locale=native` and all unrelated experimental changes held constant.

## Required observations

| Case | Action | Required evidence |
|---|---|---|
| Native baseline | Boot with native text; open original menu, battle and a target event. | Original labels/captions, normal navigation, existing voices; no MOD-006 resource publication. |
| PT-BR selection | Choose PT-BR in F8 System > Text languages, close/reopen the page, then restart. | Choice and Back/cursor persist correctly; startup status distinguishes requested choice from admitted pack. |
| Menu/font | Inspect sentinel menus and every edited menu layout. | `ã/õ/Ã/Õ`, `Ç/ç` and other accents render correctly, with shadows; no clipped variables, numbers or cursor rows. |
| Battle | Trigger each changed battle notice and relevant status/result screen. | Correct substitutions and punctuation; audio and battle behavior unchanged. Demonstrator examples include first strike, AP and status text. |
| Events/captions | Reach each changed event and display both regular/alternate forms actually used. | Same speaker/variable identities, line/page progression and option count/order. The demonstrator uses `ssbt0000` PS3/PSV text; other events need their own observations. |
| Partial coverage | Reach a resource not in the manifest. | Original text loads normally; no fallback loop or blank text. |
| Voices | Exercise the previously selected voice, battle sound and movie audio paths before/after text selection. | Settings and audible language agree with baseline; no PT-BR voice dependency. |
| Save/load | Load the disposable old save, progress minimally, save and reload with PT-BR; restart with native text and reload again. | Both paths load successfully. Native locale byte remains a valid native ID. Do not demand that the entire changed gameplay save has the old hash. |
| Native return/removal | Select original text and restart; test pack absence after closing FFX. | Original language is available and no cached PT-BR/font state leaks across processes. Remove the DLL only through the separately reviewed deployment rollback. |
| Missing/bad pack | With FFX closed, test isolated copies with missing payload/font, invalid hash, wrong version and wrong source identity, one at a time. | Startup rejects the complete pack and retains native behavior; the status/log explains the rejection. Restore the exact valid package afterward. |
| Another native locale | Restart with a native locale other than English and PT-BR preference still saved. | No global locale coercion; original resource route remains intact. |
| Restart boundary | Save native/PT-BR selections repeatedly; do not rename/replace in-use package resources to simulate hot reload. | Current text/font stay paired until restart. File-sharing rejection while the process owns resources is expected. |

Late installation, a pre-existing font, or another owner at either hook address must produce a rejected/unsupported status, not a partial translation. A third-party translation or hook stack requires a new composition test. Do not treat the absence of a crash alone as a passing case.

## Close and restore

Close FFX manually. Capture only the bounded log slice, screenshots/observations, actual loaded DLL/config identities and each case verdict. Reject log rotation, truncation, crash/access violation, unexpected ownership conflict, or unaccounted restoration failure. Restore the exact inventoried leaves from backup and verify their hashes. Distinguish runtime observations from disk-hash evidence.

Acceptance belongs to the exact combination **executable + DLL + pack + configuration + other mods**. Mark unvisited or unsupported resources explicitly. A later pack revision must repeat affected tests. Review the completed RT2 evidence and make any Production promotion as a separate decision.
