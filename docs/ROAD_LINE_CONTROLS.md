# Road-line controls and marker feedback

Install through the existing updater after this PR is merged. No file edits are
needed. Pause → QoL → Road Line is the master On/Off switch. The five additional
On/Off switches are Police Escape Line, Mission Line, Friend Line, Enemy Line and
Custom Line. They all start On, apply live, and survive restart/update. Master Off
hides every ribbon without overwriting the individual choices. RoadNav turn cues
and Compass HUD keep their independent controls.

Mission, friendly and hostile destinations use their marker classification;
Custom means a player/compass waypoint. An otherwise unclassified blue route
uses Friend Line. Choosing a custom waypoint to an enemy/mission marker makes it
a custom route until cleared. Police Escape only controls the separate red/blue
ribbon, leaving a simultaneous primary route visible.

All compass hold feedback and destination-arrival text now share the objective
caption beneath the relevant compass icon. A result follows its source icon if
still present; otherwise it keeps that icon's last position. Arrival with no
visible source uses the same caption row beneath the compass center. A fresh
hold temporarily takes priority. The 900 ms hold and existing input gates are
unchanged. These captions follow the existing Compass HUD switch.

Destination symbols use actual marker coordinates, independently of native GPS
road nodes and road-surface fitting. The ribbon still follows roads. The compass
waypoint itself already used exact marker coordinates; that placement is retained.

Moving-target routes refresh their owned GPS helper at 200 ms intervals after at
least one metre of movement, reusing it when SET_BLIP_COORDINATES is available.
Older hosts fall back to replacing that owned helper. The last validated route
is retained during refresh while the player stays near it. The displayed enemy
end joins the live vehicle/ped coordinates each frame when its complete validated
road tail is nearby (eight-metre lateral corridor, up to 30 metres along the last
segment, less than four metres in height). This avoids a stale road-node endpoint.

Native GPS remains asynchronous and road-based. A sharp departure from the road,
a different elevation, or unstreamed collision can prevent an immediate exact
join; a fresh route/surface is then required. Moving-target preview extends up to
1,200 metres with at most 500 points, rather than the normal speed-based preview.
Unvalidated/truncated tails are never stretched through buildings to the enemy.
This is not a guarantee of an uninterrupted ribbon to a far-away/off-road vehicle.
Police escape scheduling and sustained wrong-way trigger timings are unchanged.

## Updater / pause-menu acceptance checklist

1. With GTA IV closed, run the installed Update GTA IV QoL shortcut after merge,
   then launch the game. Pause → QoL: confirm five plain On/Off line switches,
   initially On, beside the existing master Road Line control.
2. During suitable missions/outing/combat, hide and restore Mission, Friend,
   Enemy and Custom independently. During a chase with a simultaneous manual
   route, hide Police Escape and confirm the manual route stays; hide Custom
   and confirm the escape ribbon stays. Master Off hides both. Turn master On
   and confirm the individual choices are retained. Restart/update and recheck.
3. On foot and in a car, hover an icon and hold D-pad Left for 0.9 seconds.
   Check ring plus Setting waypoint, then Waypoint set/cleared, all beneath that
   icon; nothing appears to the compass's right. Check cancellation during
   phone/combat/focus changes. Arrive by car/on foot: Destination reached uses
   the same caption row, without overlapping objective text.
4. Choose a road-adjacent mission marker. Check the destination symbol and
   compass-created waypoint coincide with the marker, rather than the GPS road
   node. Follow a moving enemy car through straight roads and turns: the near
   validated tail should touch its live position, without a one-second lag.
   Check an overpass/off-road departure does not create a long shortcut through
   scenery; allow native GPS/collision streaming to catch up.

Automated coverage includes independent/master/default/persisted switches,
actual x86 menu helpers before/after rebasing, Windows PE mapping, D3D9 visibility,
shared caption geometry/lifetimes, exact destination coordinates and bounded live
endpoint tracking. Native GTA routing and Deck visual acceptance require the
in-game checks above; no FPS improvement is claimed.
