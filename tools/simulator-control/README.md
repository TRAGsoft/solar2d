# Simulator control

The Solar2D Simulator executable can inspect and change the Lua state of another
running Simulator process. The client is built into the native executable, so
it has no Python, Lua, or other runtime dependency. Projects do not need to
require a library or add control-specific Lua code.

## Start a controllable Simulator

Give the Simulator an explicit private mailbox directory:

```sh
SIMULATOR="/Applications/Corona Simulator.app/Contents/MacOS/Corona Simulator"
CONTROL_DIR="$PWD/.solar2d-control"
mkdir -p "$CONTROL_DIR"
chmod 700 "$CONTROL_DIR"

"$SIMULATOR" \
  -agent-mode YES \
  -project "$PWD" \
  -simulator-control-dir "$CONTROL_DIR"
```

The bridge is disabled unless `-simulator-control-dir` is supplied. The
directory must exist before launch.

## Identify display objects

Every display object can have an optional Simulator automation identifier. It
does not require `widget` or any other library:

```lua
local playButton = display.newRoundedRect( 160, 240, 220, 72, 16 )
playButton.automationId = "playButton"
```

Discovery accepts a non-empty string of at most 128 bytes; other values are
ignored. Keep identifiers unique among the objects in the active display tree,
and assign `nil` to clear one. The identifier is app-authored and remains
stable when the display hierarchy or screen size changes.

The Simulator also assigns each discovered object a numeric `handle`. A handle
such as `@17` is convenient when following an unlabelled object returned by
`display-object-tree` or `hit-test-display-objects`. Handles do not resolve
while an object is outside the active tree, expire when its Lua proxy is
collected, and are not stable across a project relaunch; use `automationId` for
durable selectors.

## Commands

Run the same native executable in control-client mode from another terminal:

```sh
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control runtime-status
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control runtime-metrics
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control runtime-diagnostics
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control runtime-logs
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control runtime-logs --filter 'scene loaded' --follow
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control pause-runtime
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control step-runtime-frame
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control resume-runtime
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control add-lua-breakpoint "$PWD/main.lua" 42
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control add-lua-breakpoint "$PWD/main.lua" 42 --condition 'score > bestScore' --hit-count 5
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control list-lua-breakpoints
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control wait-for-debugger-pause
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control debugger-stack
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control inspect-debugger-frame 0
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control evaluate-debugger-frame 0 'score + bonus'
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control step-over
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control continue-debugger
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control capture-screenshot
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control debug-snapshot
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control start-screen-recording capture.mp4 --fps 60 --output-width 1920 --output-height 1080 --overwrite
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control screen-recording-status
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control stop-screen-recording
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control display-object-tree
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control find-display-object playButton
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control hit-test-display-objects 640 360
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control wait-for-display-object playButton
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control tap-display-object playButton
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control swipe-display-object inventoryList 0 -300 12
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control tap-screen 640 360
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control send-touch-event moved 640 300 640 500
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control swipe-screen 640 500 640 200 12
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control send-mouse-event move 640 360
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control press-back-button
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control send-key-event s pressed --command
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control type-text 'Player name'
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control send-scroll-event 640 360 0 -120
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control controller send-button-event buttonA
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control controller set-axis leftX -1
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control controller connect --id pad2 --profile playstation --player 2
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control controller send-button-event buttonA --id pad2
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control evaluate-lua 'player.score'
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control inspect-lua-value player
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control execute-lua 'player.score = player.score + 100'
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control execute-lua-file /absolute/path/to/experiment.lua
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control wait-for-condition 'player.ready'
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control assert-condition 'player.score >= 100'
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control wait-for-log 'level loaded'
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control run-scenario /absolute/path/to/smoke.scenario
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control relaunch-project
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control quit-simulator
```

`evaluate-lua` evaluates one Lua expression and returns all of its values.
`execute-lua` executes statements. Either command reads standard input when its
code argument is omitted. `execute-lua-file` loads a file into the current Lua
runtime; it does not add a module or require project changes.

`runtime-diagnostics` returns the most recent Lua or control-command error,
including its type, message, stack trace, frame, and sequence number. Its
`latestRuntimeError` value is `null` if no error has occurred. Both it and
`runtime-status` include `runtimeErrorHalted`; status also reports an
`executionState` of `running`, `suspended`, `control-paused`, `debug-paused`,
or `error-halted`. Its `controlPaused` and `stepFramesRemaining` fields expose
automation-controlled execution independently from the platform suspension
state. `debuggerPaused` and `debuggerPauseSequence` identify a live Lua source
pause.

`runtime-metrics` returns a timestamped performance and memory snapshot without
executing project Lua. Frame timing contains up to the latest 120 intervals and
reports the configured rate and budget, latest, average, minimum, maximum, p95,
and observed FPS. Intervals use runtime elapsed time, so time spent suspended,
control-paused, or debug-paused does not appear as an artificial slow frame.

Memory reports the current Lua heap and application texture allocation in
bytes. Rendering reports the previous rendered frame's CPU/GPU timing, resource
work, draw calls, primitives, and binding counts. Renderer statistics are
enabled automatically when a controllable runtime starts; if project code later
disables them with `display.enableStatistics(false)`, `statisticsEnabled` is
false and `previousFrame` is `null`.

Application runtime errors also include a `context` snapshot containing
structured stack frames with source, line, function, locals, and upvalues.
Capture is bounded to 12 frames, 12 locals and 8 upvalues per frame, three
shallow entries per table, 256 bytes per captured string, and 96 KiB overall.
Each affected value, frame, and context reports truncation. Capture uses only
Lua's debug and raw table APIs; it does not call `tostring`, metamethods, or
application code. Control-command errors have a `null` context.

## Runtime execution control

`pause-runtime` stops the scheduler, `enterFrame`, physics, display updates,
and ordinary application-event dispatch without suspending the Simulator's
control mailbox. Inspection, screenshots, Lua evaluation, and other control
commands therefore remain responsive. Simulator-control input is still
delivered while paused, allowing an input handler to run before the next
explicit frame step; ordinary user and platform events are suppressed. Runtime
elapsed time is frozen while frames are not advancing, so timers, transitions,
and animations do not jump by the wall-clock duration of the pause.

`step-runtime-frame [COUNT]` advances from one through 1000 complete runtime
frame attempts and then remains control-paused. It requires a control-paused,
non-suspended runtime and defaults to one frame. The native client waits for
the requested steps before returning the final runtime status, and the mailbox
does not process a later request between a multi-frame step. If application
code suspends or error-halts the runtime, the remaining steps are cancelled.
Use `resume-runtime` to clear the control pause and return to continuous
execution.

Runtime frame stepping advances the scheduler and display as a game frame. Lua
debugger stepping, described below, resumes only until a matching source line.

## Lua source debugging

The source debugger is enabled automatically when the Simulator has a control
directory. It uses the same local mailbox and does not open a network port or
require project code. Add a breakpoint using an existing Lua source file and a
one-based line number:

```sh
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" \
  -simulator-control add-lua-breakpoint "$PWD/scenes/game.lua" 84
```

`add-lua-breakpoint` returns a numeric ID. The same path and line are
deduplicated; adding that location again updates its condition and hit count,
resets its accumulated hits, and preserves its ID. Use `list-lua-breakpoints`
to discover IDs and `remove-lua-breakpoint ID` to delete one. Breakpoint
configuration survives a project relaunch in the same Simulator process, so a
breakpoint can be added after initial startup and activated with
`relaunch-project`.

Add `--condition EXPRESSION` to pause only when an expression evaluated in the
current Lua frame is truthy. Add `--hit-count COUNT` to pause on every COUNTth
condition-matching hit. The condition is evaluated first, so false conditions
do not advance `hits`; a breakpoint without a condition counts every visit.
Hit counters reset when the breakpoint is changed or the project relaunches.
`list-lua-breakpoints` reports `condition`, `hitCount`, and the current `hits`.
If a condition raises an error, execution pauses with a
`breakpoint-condition-error` reason and `debugger-status.conditionError`
contains the traceback. Conditions are ordinary Lua expressions and can call
application functions, so keep them side-effect-free.

When Lua reaches a breakpoint, `runtime-status` reports `debug-paused` and
`debugger-status` reports the reason, breakpoint ID, source, project-relative
source, line, function, and monotonically increasing pause sequence.
`wait-for-debugger-pause` waits for the next or current pause.
`debugger-stack` returns up to 64 live Lua frames. Pass a returned zero-based
frame level to `inspect-debugger-frame` to read its locals and upvalues; at most
100 of each are returned, and tables are represented by normal
`inspect-lua-value` handles.

`evaluate-debugger-frame FRAME EXPRESSION` evaluates an expression using the
selected live frame. Name lookup prefers locals, then upvalues, then globals,
including correct shadowing when a local or upvalue is `nil`. It returns all
expression values; returned tables use the same handles as
`inspect-lua-value`. The expression can be read from standard input when it is
omitted from the command line. Evaluation errors are returned to the client
without resuming or error-halting the application. Like breakpoint conditions,
frame expressions are normal Lua and can invoke functions or metamethods.

`step-into`, `step-over`, and `step-out` resume and wait for the next matching
file-backed Lua source line. `continue-debugger` resumes without waiting for
another pause. Step-over stops when execution reaches the next line at the same
or a shallower Lua stack depth; step-out stops at the next line in the calling
Lua frame. A step can finish without another pause when the outermost chunk
returns, in which case the stepping client reaches its timeout even though the
runtime has resumed; `debugger-status` distinguishes this from a paused
runtime.

While debug-paused, the mailbox accepts runtime status, diagnostics and logs,
breakpoint management, debugger inspection and execution-control commands,
`evaluate-debugger-frame`, and `inspect-lua-value`. Commands that would execute
Lua outside the selected frame or otherwise mutate the application are rejected
until execution continues. Runtime elapsed time is frozen during the pause. An
application call to `debug.sethook()` replaces the Simulator's source hook for
that Lua thread, so projects using their own debug hook cannot simultaneously
use these breakpoint and stepping commands.

## Unhandled runtime errors

When the Lua `unhandledError` listener does not return `true`, a controllable
runtime enters the `error-halted` state after recording the error. The current
frame stops advancing, remaining scheduler tasks do not run, and subsequent
application events and automation input are not dispatched. An error that an
`unhandledError` listener explicitly handles by returning `true` is still
recorded but does not halt the runtime. Errors from `evaluate-lua`,
`execute-lua`, and other control commands are diagnostics only and do not halt
the application.

The control mailbox remains responsive while error-halted. The following safe
inspection, artifact, and lifecycle commands remain available:

- `runtime-status`, `runtime-diagnostics`, and `runtime-logs`
- `capture-screenshot` and `debug-snapshot`
- `screen-recording-status` and `stop-screen-recording`
- `display-object-tree`, `find-display-object`, and
  `hit-test-display-objects`
- `inspect-lua-value`
- breakpoint management and `debugger-status`
- `relaunch-project` and `quit-simulator`

Other commands, including waits, Lua evaluation/execution, input, and
assertions, fail immediately with error code `runtime-error-halted` and include
the latest diagnostics in their result. `relaunch-project` replaces the Lua
runtime and clears the halt as part of the new control session.

`runtime-logs` returns recent Simulator log messages. Pass `--since SEQUENCE`
to return only newer messages, or `--filter TEXT` to return only messages that
contain the given case-sensitive text. `--follow` prints each non-empty result
batch as one line of JSON, flushes it immediately, and continues polling until
the client is interrupted. These options can be combined. Each entry includes
a Unix-epoch `timestampMs`.
`capture-screenshot` writes the current frame to a PNG in the control directory,
or to an optional output path.

`debug-snapshot` writes a PNG and a JSON manifest beside it using the `.png.json`
suffix. The manifest contains runtime status, runtime metrics, the latest
diagnostic, captured logs, and the first page of the display tree. It is
intended as a compact debugging bundle rather than a complete trace.

On macOS 15 or later, `start-screen-recording` records the Simulator device view
to an H.264 `.mp4`. Its path may be relative to the control client's working
directory or absolute. Recording defaults to 60 FPS with audio enabled, the
cursor hidden, a resolution scale of 1, and the `best` capture resolution type.
Pass `--fps FPS`, `--resolution-scale SCALE`, or
`--capture-resolution-type automatic|best|nominal` to change those settings.
Use `--output-width WIDTH` and `--output-height HEIGHT` together for exact even
output dimensions from 2 through 16384. `--no-audio`, `--show-cursor`,
`--reuse-capture-stream`, and `--overwrite` control their corresponding Boolean
options. The destination directory must already exist.

`--reuse-capture-stream` keeps the native capture stream alive between
consecutive recordings with identical capture settings. It avoids recreating
the stream for every short clip and is intended for bounded automation sessions.

Starting and stopping are asynchronous. `start-screen-recording` usually
returns state `"starting"`, and `stop-screen-recording` usually returns
`"stopping"`. Poll `screen-recording-status` until it returns `"recording"`
before performing actions that must appear in the video, then poll until
`"idle"` after stopping before reading the finalized file. It returns
`"unavailable"` when the platform does not support recording.

## Display discovery and semantic input

`display-object-tree` returns the active display hierarchy as a bounded, flat
list. Each node includes its `automationId`, handle, parent handle, child index,
depth, native object type, screen bounds and center, cumulative visibility and
alpha, hit-test state, touch-listener state, and child count. At most 100 nodes
are returned at once; pass the response's `nextCursor` back with `--cursor` to
read the next page.

`find-display-object playButton` resolves one object by `automationId`;
`find-display-object @17` resolves a previously returned handle. Duplicate
identifiers are reported as ambiguous. `tap-display-object` reads the object's
current screen bounds immediately before queuing a touch at its center, so an
agent does not have to remember coordinates across layout changes.
`swipe-display-object` begins at the current center and applies the given
screen-space delta.

`hit-test-display-objects X Y` reports front-to-back candidates at a screen
coordinate. It is explicitly marked `approximate` because it uses transformed
bounds and normal visibility/hit-test flags, not pixel masks or exact vector
geometry.

## Waiting, assertions, and scenarios

`wait-for-condition` evaluates a side-effect-free Lua expression once per
runtime frame until its first return value is truthy. `wait-for-display-object`
waits until a selected object exists, is visible, has non-empty bounds, and can
receive input. `wait-for-log` waits until captured output contains the given
text. `assert-condition` evaluates once and fails unless its first value is
truthy. Each uses the client timeout, which defaults to ten seconds:

```sh
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" \
  -simulator-control --timeout 30 wait-for-condition 'gameState == "playing"'
```

`run-scenario` runs a line-oriented command file. Blank lines and lines
beginning with `--` are ignored; every other line is one normal command followed
by its payload. A scenario is limited to 100 commands, stops at the first
failure, and returns the response from every completed step:

```text
-- smoke.scenario
wait-for-display-object playButton
tap-display-object playButton
wait-for-condition gameState == "playing"
wait-for-log PLAY_BUTTON_TAPPED
assert-condition player.score >= 0
debug-snapshot
```

Failed `wait-for-condition`, `wait-for-display-object`, `wait-for-log`, and
`assert-condition` commands automatically capture `automation-failure.png` and
`automation-failure.png.json` in the control directory. A failed scenario step
does the same and includes the artifact paths in its JSON result.

An executable sample project and scenario are in
[`example`](./example). They exercise delayed discovery, semantic tapping,
waiting, log matching, assertions, and snapshot capture.

## Raw input

`tap-screen` queues touch `began` and `ended` events at the given screen
coordinates; it does not dispatch a separate tap event. The coordinates use the
same simulated screen space returned by `capture-screenshot`.
`send-touch-event` sends an individual `began`, `moved`, `ended`, or `cancelled`
phase and can preserve an explicit gesture start point. `swipe-screen` emits a
began phase, interpolated moved phases, and an ended phase. The optional step
count is from 1 through 120 and defaults to 8.

`send-mouse-event` sends `down`, `up`, `drag`, `move`, `exit`, or `scroll`
phases. `press-back-button` dispatches the platform back action.
`send-key-event` queues a press by default, or one explicit `down` or `up`
phase, and supports Shift, Alt, Control, and Command modifiers. `type-text`
inserts UTF-8 text at the selection of the focused native text field, or
dispatches a `character` event when the simulated screen has focus. It reads
standard input when `TEXT` is omitted. `send-scroll-event` is the compact form
for a mouse scroll event at the given screen coordinate.

Input is dispatched after the control response is written, so handlers may
safely relaunch or close the runtime. Use a `wait-for-*` command or
`assert-condition` after input when the next action depends on its effect.

`controller` provides virtual gamepads through Solar2D's normal input-device
APIs. Commands without `--id` use a default Xbox controller. Use `--id` to
address independent devices, and explicitly `connect` a new ID with an `xbox`,
`playstation`, `nintendo`, or `generic` profile and optional player number from
1 through 4. Profiles set representative product names and controller types;
all provide the standard six-axis extended-gamepad layout.

`controller send-button-event` and `controller set-axis` automatically connect
their target as an Xbox controller if it does not exist yet. `disconnect`
emits a normal `inputDeviceStatus` event, and reconnecting an existing ID
preserves its profile. Connecting it with a different profile or player number
emits a reconfiguration event. Buttons use Solar2D key names such as `buttonA`,
`up`, and `leftShoulderButton1`. Supported axes are `leftX`, `leftY`, `rightX`,
and `rightY` from -1 through 1, plus `leftTrigger` and `rightTrigger` from 0
through 1. Axis values remain set until changed; send 0 to center a stick axis
or release a trigger.

Simulator Lua code can inject the same virtual controller events through
`simulator.sendInput()`:

```lua
simulator.sendInput({
	type = "controller",
	action = "connect",
	controllerId = "player-one",
	controllerProfile = "xbox",
	controllerPlayerNumber = 1,
})
simulator.sendInput({
	type = "controller",
	action = "button",
	controllerId = "player-one",
	keyName = "buttonA",
	phase = "pressed",
})
simulator.sendInput({
	type = "controller",
	action = "axis",
	controllerId = "player-one",
	axisName = "leftX",
	axisValue = -1,
})
```

The controller ID defaults to `default`. Button and axis actions automatically
connect a new Xbox controller, while an explicit `connect` action can select a
profile and player number. Use `action = "disconnect"` to emit the normal input
device disconnection event.

`inspect-lua-value` is read-only. It accepts paths such as
`player.inventory[1]` and `settings["audio"]`, uses raw table access, and never
invokes metamethods. A table response includes its own entries and a handle such
as `12`; inspect that table later with `inspect-lua-value @12`. Large tables
return `nextCursor`, which can be passed as
`inspect-lua-value player --cursor 100`. There is deliberately no depth
argument: nested tables are represented as handles, keeping responses bounded
without making callers choose an arbitrary depth. It remains available while
the runtime is error-halted, allowing globals and surviving table handles to be
examined without resuming application execution.

`relaunch-project` calls the Simulator's normal relaunch mechanism. It replaces
the Lua runtime exactly as a menu or file-triggered relaunch would; it is not a
separate module-reloading system. The client waits until the replacement Lua
runtime is ready before it exits successfully.

All output is JSON. Successful responses have `ok: true` and unsuccessful Lua
loads, runtime errors, invalid paths, and unsupported commands have `ok: false`.
The client exits with status 0, 1, or 2 for success, Simulator/Lua error, or
client/transport error respectively. Add `--timeout SECONDS` immediately after
`-simulator-control` to change the default ten-second deadline.

## Security and lifecycle

The mailbox is a local filesystem interface with no network listener. Treat
access to its directory as permission to execute arbitrary code in the running
project. Launch scripts should create it with user-only permissions and should
not place it in a shared directory. Runtime diagnostics and debug snapshots can
contain captured locals and upvalues, including application secrets; remove
persisted artifacts when they are no longer needed.

The Simulator writes `session.json` once the Lua runtime can accept requests.
Requests are processed on the runtime thread, at most one per frame during
normal execution, so Lua access does not race the engine. A debug pause services
its restricted command set directly from the Lua line hook. Relaunching replaces
the session and invalidates table and display-object handles. A suspended or
otherwise blocked Lua runtime cannot answer until it resumes; control-paused,
debug-paused, and error-halted runtimes keep the mailbox responsive, subject to
their command restrictions above.

## Complete Lua results for tools

The three Lua execution commands accept `--json` immediately after the command:

```sh
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control execute-lua --json 'return {text="Hello", entries={1,2,3}}'
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control evaluate-lua --json 'display.contentWidth'
"$SIMULATOR" -simulator-control-dir "$CONTROL_DIR" -simulator-control execute-lua-file --json /tmp/report.lua
```

The first command returns `{"ok":true,"result":[{"text":"Hello","entries":[1,2,3]}],"encoding":"json"}`.
The `encoding` marker lets tools reject older builds that ignore the flag.
`result` contains every return value, in order; nil becomes null and no returns
produce `[]`. Without `--json`, commands retain their inspection summaries and
table handles. Code can also arrive on stdin, avoiding shell quoting.

Encoding uses Solar2D's `json.encode` with an exception handler that rejects cycles
and unsupported values instead of substituting error strings. Normal JSON rules
apply, including `__jsontype` for empty objects and null for nonfinite numbers.
Return data tables rather than live display objects. Strings and nested tables
are complete: results above 16 MiB fail with `result-too-large`, never truncation.
Requests are limited to 1 MiB; `execute-lua-file` accepts scripts up to 16 MiB.

Runtime failures return `{"ok":false,"error":{"code":"...","message":"..."}}`
and exit 1. `lua-error` includes the execution traceback; `serialization-error`
and `result-too-large` mean the code ran but its result could not be returned.
Direct requests also return JSON for transport failures (exit 2):
`session-unavailable`, `session-changed`, `permission-denied`, `control-io-error`,
`control-busy`, `control-timeout`, and `request-too-large`. File errors identify
the affected path and OS error. Other compound-command transport failures use
`control-error`; CLI argument errors may still be plain stderr. Do not restart
or retry automatically: a timeout can happen after code has executed.
