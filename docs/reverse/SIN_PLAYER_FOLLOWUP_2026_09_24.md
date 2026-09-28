# S.I.N. player follow-up queue — Jarvis-HOOK

The user reported this after the Snowfield test deployment and explicitly asked
to inventory the defects, then resolve them **after the remaining MOD-004/005
work**. Do not interrupt that implementation or deploy another S.I.N. candidate
merely because this queue exists.

Test package: DLL `24B833E8F40C1ACF29D0A08B006CA8CA602536F2DFD2888688049433A07E7CEE`,
private AI pack `84D146B9D29014E154FE44B14E2A15C50AE0220413D0301FE3C503BAC80BC6D0`.
Source commit: `e4eaff80af5aea6adbaa6163aa84a9cfd55f16ba`.

| Case | Player observation | Follow-up |
|---|---|---|
| Monster attributes | Increased correctly | Preserve this result; add regression coverage around the subsequent AI fix. |
| Curse names | Visible correctly | Preserve existing bounded name ownership. |
| Opening Veil | Worked | Preserve its once-only self-support behavior. |
| Counter March | No visible effect; softlock | High priority after equipment work. Identify the exact emitted action, event/return behavior and battle wait condition. |
| Frost-Flood Weave | Targeted its own caster | High priority after equipment work. Verify offensive command target selection and native command target flags. |

The user's hypothesis is that targeting always selects self without distinguishing
offensive and support skills. This is a **hypothesis**, not an established root
cause. Inspect target operands and native queue semantics before changing them;
support commands such as Opening Veil must retain self targeting. Counter March
may additionally have a completion/event issue and must not be assumed identical.

Evidence level: direct player observations. No raw log analysis, reproduction,
agent-run RT2, new build or fix has been performed for this follow-up yet. Reward
AP/Gil acceptance was not mentioned and remains unconfirmed. Complete S.I.N.
gameplay acceptance is not green because of the softlock and targeting defects.


## 2026-09-24 — Repair candidate deployed from main (Jarvis-HOOK)

The UNI-001..008 exporter audit found that typed command targets were discarded
by the self-only emitter. UNI005/006 now address the hostile team, UNI008 the
allied monster team, and UNI007 self. UNI003 and004 avoid queued support actions
being overwritten by the selected native turn. Counter March uses the editor's
explicit guarded native Delay Attack 3006 against the attacker, with a terminal
handler, replacing experimental command 6110. Opening Veil and stat/name behavior
confirmed by the player were preserved. All 47 compatible profiles pass the
semantic target/guard/latch/termination contract; native pack RT1 passes 400 checks.

The user authorized the test installation. Main30a244e DLL 7247bff6 and matching
pack ddad9d6e were deployed with verified backup at 2026-09-24T05:56:45Z. Details:
`docs/reverse/MAIN_WORKSHOP_SIN_2026_09_24.md`; private receipt:
`.superpowers/sdd/2026-09-24-main-workshop-021558Z/deployment-receipt.json`.

**Player confirmation remains pending:** Counter March no longer softlocks;
Frost-Flood attacks the party; support actions select their intended recipients.
The animation/softlock cause itself was not reproduced in a live game session.
