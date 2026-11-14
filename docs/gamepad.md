# Gamepads

`engine/platform/Gamepad.h`: up to four pads in the Xbox layout (`PadButton`, `PadAxis`, edges, radial dead
zone). Backends: Linux joystick API, Windows XInput (loaded at run time), web Gamepad API, macOS / iOS
GameController, Android gamepad input events. The game client turns a pad into a mouse-like cursor, so every
game (menus, UI documents, selection, orders) works with it:

```jsonc
"gamepad": { "deadZone": 0.2, "cursorSpeed": 900, "panSpeed": 1, "zoomSpeed": 1,
             "snap": { "targets": "structures", "radius": 70, "flick": true },   // "units", "all", "none"
             "buttons": { "a": "click", "b": "rightClick", "x": "key:Q", "start": "key:Escape", "up": "pan" } }
```

Left stick moves the cursor (quadratic response); with snapping on a full flick jumps to the
nearest building in that direction and a resting cursor settles on a building under it; A press / hold /
release is a mouse click / drag (drag from building to building = a send in garrison games), B right-click, right stick and
d-pad pan the camera, triggers zoom; any binding: `click`, `rightClick`, `middleClick`, `key:<platform::Key>`,
`pan`, or a game action. A real mouse hides the pad cursor.

Chords and game actions (`"chords": {"rb+a": "send:50", "a+a": "upgrade"}`: modifier + button, or a double press
within 0.3 s; `lt` / `rt` name the triggers as buttons): `send:N` (N % of the garrison of the structure under the
cursor when the modifier went down, else of the selected ones, to the structure under the cursor; also sets the
HUD percent), `ability:N` (N-th usable player ability, as its card), `upgrade`, `convert:<tag>`, `cancel`,
`event:<name>` (to view scripts as `ui.on("pad", {action})`). A chord swallows its button's own binding; a
send modifier draws the origin -> cursor arrow. A node-capture game's layout as data:

```jsonc
"gamepad": { "buttons": { "right": "ability:1", "down": "ability:2", "left": "ability:3", "up": "ability:4" },
             "chords": { "rb+b": "send:25", "rb+a": "send:50", "rb+x": "send:75", "rb+y": "send:100",
                         "lb+a": "upgrade", "lb+x": "convert:house", "a+a": "upgrade", "lt+y": "event:emotion4" } }
```

UI Lua: `game.sendPercent(p?)`, `game.useAbility(i)`. Tests: `aiverse_tests gamepad_` (tests/tests_clientkit.inc).
