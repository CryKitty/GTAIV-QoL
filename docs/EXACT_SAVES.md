# Exact phone save states: feasibility findings

Requested behavior: the phone Save option remains usable during missions and loading restores that precise instant, including mission execution, actors, vehicles, health, inventory and world state. An ordinary save followed by teleporting is insufficient.

No save writes or phone unlock are included in this change. There is no verified exact-state capture/restore implementation for the supported Complete Edition executable in this repository.

Evidence inspected:

- IV-SDK C_PcSave exposes ordinary SaveSlot/LoadSlot. CTheScripts exposes Save/Load entry points, but this API does not establish that all active mission execution and AI task state can be restored. Its documented hardcoded addresses target older executable versions and cannot simply be called on CE 1.2.0.59.
- Decompiled IV main and phone scripts opt into persistence through THIS_SCRIPT_SHOULD_BE_SAVED. The Snow Storm mission script has no such declaration. This demonstrates selective script persistence; toggling GET_MISSION_FLAG or forcing the save menu is not evidence that the active mission will resume.
- LibertyTweaks Features/Misc/QuickSave.cs saves PlayerPosition and PlayerHeading to a sidecar, then teleports the player after startup. It calls the normal autosave/menu path. This implements position restoration, not a whole mission snapshot.

Before any phone unlock can truthfully provide exact saving, implementation must establish a CE-compatible serializer for mission code/instruction pointers, locals/stacks/timers and globals; mission-owned entities with stable handle remapping; active AI/animation/physics tasks; and load ordering/streaming. It must also define compatibility with other active ASIs and keep snapshots paired with the matching base save and mod versions. A process-memory dump alone is not a verified persistence design for external file/audio/graphics resources.

Acceptance tests must include The Snow Storm before the hospital, after taking the cocaine with police active, death/retry, loading another slot and a complete game restart. Actor deaths, objective stage, player/vehicle position, health, ammunition, wanted state, timers and mission completion must match the captured moment. Until that exists and passes, enabling ordinary saves mid-mission would not meet the requested feature.

Research references:
- https://github.com/ClonkAndre/IV-SDK/blob/main/include/C_PcSave.h
- https://github.com/ClonkAndre/IV-SDK/blob/main/include/CTheScripts.h
- https://github.com/0x52617665/GTAIVMissionScripts/blob/master/IV/elizabeta3.txt
- https://github.com/catsmackaroo/LibertyTweaks/blob/main/LibertyTweaks/Features/Misc/QuickSave.cs
