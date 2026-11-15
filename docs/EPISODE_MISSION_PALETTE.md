# Episode mission palette

Use SDK GET_CURRENT_EPISODE (0x0ECE0B08), the same native used by the game's
scripts. CE episode indexes are 0=GTA IV, 1=TLAD, 2=TBoGT. Detect at the existing
usable-world initialization gate, once per ScriptHook session; a menu episode
switch starts a new session. Unknown values log the raw identity and fall back
to GTA IV yellow. No save paths, executable names or blip sprites identify it.

RGB: GTA IV (245,205,55); TLAD (255,95,80); TBoGT (255,105,200).
Existing ribbon contrast/alpha remains. TLAD coral is lighter than enemy
(235,55,45) and police flash red (245,45,45). Police also alternates blue every
400 ms. Reds necessarily share a hue family; daylight/night acceptance still
requires the user's visual test. Mission turn arrows and destination glyphs
use routeColour too and intentionally follow this change. Custom/friend/enemy/
escape palettes and the disabled Destination Colours fallback are unchanged.
