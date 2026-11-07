# Live NPC logic timing

The original APK's live police and other enemy action controllers contain direct
per-call state, pursuit and movement updates. Scaling only shared clip evaluators
does not scale those controllers. For example, `_EKLibChaseTarget` decrements the
police timer at `+0x198` once per call (`0x207e20..0x207e2e`). Running that block
60 times consumes twice the original one-second timer budget.

`jsr_npc30` identifies 54 original action entry points, covering police control,
the shared movement families A/B/C/D/E/G/H, dogs, rivals, vehicles, bosses and
their attack/projectile/auxiliary actions. Every entry is checked against the
APK's instruction signature before installation. Scripted event callbacks stay
with their existing event-time adapters; the player's callback and input are
not part of this registry.

NPC logic runs on the shared original 30 Hz tick with scaling disabled. Held
presentations renew the NPC's collision registrations without executing its AI
again. This is intentionally not a conversion of NPC skeletal poses to 60 Hz;
the live player's physics/input adaptations and 60 Hz presentation remain.

## Collision lifetime

The engine collision pass still executes every presentation. Its transient
return pools cannot simply be retained across a skipped NPC update. The cache
therefore also supports the original static-object, simple sphere movement,
moving-ground and enemy graffiti-position registrations used by these actors.
Stack vectors are copied. Persistent collider geometry and result destinations
are retained exactly as in the engine's original registration API.

NPC identities use only fields within the common action header (`04/08/18/28`),
never the larger player-specific collider offsets. Invalid or changed identities
are rejected before reading cached result slots. The module-local imported
`free` slot is interposed to invalidate NPC references before their collider,
result destination, motion, persistent position, actor, parent or common work
allocation is freed. Cache access and this invalidation use a libnx mutex. Cache exhaustion does not execute
an extra AI step: the next original logic tick remains authoritative.

This introduces API-required mutation of original action/result buffers and the
module's import slot. It does not patch the SDK allocator or other modules.

## Verification and limits

- `tests/test_npc30.py` checks all 54 signatures against the supplied APK, the
  original 300/600 callback cadence, held registration, deletion handling and
  player/event exclusion using a compiled host fixture.
- `tests/test_npc_collision.py` runs the actual ARM cache implementation with
  controlled engine dependencies, tests compact actors, stale-reference rejection
  and free invalidation, executes the isolated real-APK police timer, and compares
  original/trampoline register, flag and stack state for all four new hooks,
  including optional null arguments.

The collision responses are fixtures, not a complete loaded stage. There is no
claim of full game coverage or an 80% whole-project coverage measurement. Console
follow-up must check police pursuit, attacks and spray stun, projectile contact,
dogs, vehicle/boss stages, deaths/despawns and save/load transitions. NPC visuals
may retain their original 30 Hz cadence. No external pose interpolation is added.
