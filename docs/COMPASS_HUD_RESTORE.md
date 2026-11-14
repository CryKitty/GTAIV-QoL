# Compass HUD restoration

The b78 user test established that turning Compass HUD Off clears the stuck
wardrobe help prompt. The message observer forwards SET_MESSAGES_WAITING once
and copies its input; it does not print or clear help text. The vanilla wardrobe
script uses PRINT_HELP_FOREVER and explicit CLEAR_HELP, so expiration cannot be
repaired safely by imposing a generic timeout.

The old radar-content resolver recognized a queued callback by its position
between border callbacks, not by a proof that all its work was visual. Skipping
that callback is unsafe for native HUD lifetimes. It now always runs. Only the
dedicated radar border callbacks remain filtered. Shared HUD visibility queries
are no longer hooked; radar geometry is updated from the normal compass tick.
This conservative change needs a Deck wardrobe retest: neither the supplied
logs nor the toggle A/B identify the exact internal cleanup operation. Native
health/ammo fill visuals may remain visible with Compass On. Do not reintroduce
whole-callback suppression to hide them without a visual-only contract.

A separate, deterministic restoration hole exists when the engine copies the
masked layout position into a replacement live radar component. The old code
lost the replacement's restore address because its position was already the
sentinel. Restore now consults the current component before clearing saved
positions, using the prior native baseline for inherited sentinels. No radar
visibility preference, opacity, or native visibility answer is forced on.

Windows x86 tests exercise the production mask against executable fake native
callbacks: content callbacks always execute, all HUD visibility answers are
preserved, replacement components restore before a subsequent tick/query, and
repeated toggles preserve native hidden preferences and opacity. These tests
prove the restoration contract, not the unrecorded Deck component transition or
wardrobe script internals. Deck acceptance remains outstanding.
