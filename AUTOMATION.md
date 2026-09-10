# OmaTube Automation Guide

This guide is for coding agents that drive the OmaTube UI. It defines
the safe launch modes, the timed JSON CLI, the fixed fixture, and the
stable selectors. Agents navigate with JSON sequences only. Do not add
C++ harness code or edit QtTest sources to navigate the UI.

## 1. Safe Launch Modes

Build first, then use one of the automation modes:

```sh
./bin/build
./build/yt-client --automation
./build/yt-client --automation-sequence /tmp/sil-48.json
./build/yt-client --automation-sequence /tmp/sil-48.json --automation-ui simple
./build/yt-client --automation --automation-ui simple
QT_QPA_PLATFORM=offscreen ./build/yt-client --automation --quit-after-startup
```

All automation modes use a disposable temporary SQLite database and a
temporary settings path, seed the fixed fixture below, disable startup
and manual refresh and channel resolution, set image sources to empty
so no media or image requests occur, load a fake player that never
loads media, and ignore saved settings and `YT_CLIENT_API_KEY`. They
never write user state. A normal launch without these flags opens the
user database, reads user settings, and can consume YouTube quota, so
do not use a normal launch for agent testing.

Flag rules:

* `--automation` is interactive automation. It opens the app and waits
  for manual input.
* `--automation-sequence <path>` runs a timed JSON sequence and exits
  after the last action or capture. It implies `--automation`.
* `--automation-ui full|simple` selects the initial UI. Default is
  `full`. It may be used with `--automation-sequence` or with plain
  `--automation`. It requires an automation mode and is rejected
  otherwise.
* `--database` is forbidden in any automation mode. The launch exits
  nonzero before creating windows. This protects a user database from
  fixture seeding.
* `--automation-sequence` forbids `--quit-after-startup`.
* `--automation --quit-after-startup` is retained as a startup smoke
  check. It opens no video.

The sequence input file is caller-owned and is never deleted by the
app. It may be any readable path; a temporary `/tmp` JSON file is
recommended.

## 2. Sequence Runner

The sequence file is a JSON array of events. Example:

```json
[
  {"type": "click", "target": "historyNavigationButton", "atMs": 500},
  {"type": "screenshot", "filename": "sil-48-history.png", "atMs": 1200}
]
```

Validation and timing:

* The whole file is structurally validated before any event runs.
  Unknown fields are rejected.
* `atMs` is required on every event. It must be a nonnegative integer
  not larger than 2147483647. Events must be sorted in nondecreasing
  `atMs` order. Ties preserve array order. Diagnostics use zero-based
  event indices.
* Startup waits for the first window to become ready, up to 5 seconds.
  When ready, the timeline zero starts. An empty array `[]` opens the
  fixture, waits for startup ready, then exits success.
* Later times are best-effort absolute milliseconds from timeline zero.
  They are not added relative delays. Expensive rendering can make an
  event late; order is still preserved.
* Timed actions do not auto-wait for missing UI. A click or key event
  against a missing, hidden, disabled, or ambiguous target fails with
  an event index and time diagnostic and exits nonzero. Agents must add
  explicit delays for Loader swaps, root swaps, and rendering. Do not
  assume a fixed render duration; allow generous gaps after navigation,
  history, Watch Next, player, settings, and Simple UI changes.
* The script exits after the last action or capture. Any failure,
  including an early quit before the rest of the sequence could run,
  exits nonzero.

## 3. Events

There are four event types: `key_press`, `key_release`, `click`, and
`screenshot`.

### 3.1 Key press and release

```json
[
  {"type": "key_press", "key": "H", "atMs": 200},
  {"type": "key_release", "key": "H", "atMs": 300}
]
```

* `key` is required and case-insensitive. Single characters accept
  letters, digits, and printable ASCII: `A` and `a` both type lowercase
  `a` without Shift. Named keys are `Escape`, `Space`, `Return`,
  `Enter`, `Tab`, `Backspace`, `Delete`, `Left`, `Right`, `Up`, `Down`,
  `F1` through `F35`, `Ctrl` (also `Control`), `Shift`, `Alt`, and
  `Meta`; `Insert`, `Home`, `End`, `PageUp`, and `PageDown` are also
  accepted.
* Keys are logical Qt key events, not physical positions, layouts,
  scancodes, or compositor shortcuts. Hold Shift with separate
  press/release events for uppercase letters and US shifted punctuation
  (`Shift` plus `1` types `!`). There are no chords and no `modifiers`
  field. Shift-hold example, types `H`:

```json
[
  {"type": "key_press", "key": "Shift", "atMs": 500},
  {"type": "key_press", "key": "h", "atMs": 550},
  {"type": "key_release", "key": "h", "atMs": 600},
  {"type": "key_release", "key": "Shift", "atMs": 650}
]
```
* There is no implicit autorepeat. Each press needs its release.
* A duplicate press without release, or a release without a matching
  press, is invalid. Keys still held at the end of the run are released
  during completion or failure handling.
* Optional `window` selects `appWindow` or `settingsWindow`. Default is
  the active visible OmaTube window: the modal settings window when
  open, otherwise the main window. A release is delivered to the
  original press recipient, even if settings focus changed windows in
  between.

### 3.2 Click

```json
[
  {"type": "click", "target": "settingsAppearanceTab", "window": "settingsWindow", "button": "left", "atMs": 1000}
]
```

* Target is either `target` with an `objectName`, or numeric logical
  window-local `x` and `y` coordinates. The two forms are exclusive.
* `target` clicks the item center. The target must be visible, enabled,
  onscreen, and unique at runtime. There is no automatic scrolling and
  no direct QML method activation.
* Optional `window` selects `appWindow` or `settingsWindow`. Default is
  the active visible window as for keys. A click cannot pass through
  the modal settings window to the main window.
* Optional `button` is `left`, `middle`, or `right`. Default is `left`.
* Targets are re-resolved at runtime after Loader and root swaps, so a
  recorded pointer is never reused. There is no drag or wheel event.

### 3.3 Screenshot

```json
[
  {"type": "screenshot", "filename": "sil-48-settings.png", "window": "settingsWindow", "atMs": 1400}
]
```

* `filename` is required and must be a PNG basename written directly to
  `/tmp`, for example `sil-48-settings.png`. The `.png` extension check
  is case-insensitive but lowercase is recommended. Directories,
  traversal, symlinks, and overwrites are rejected. Files are created
  with `QFile::NewOnly` semantics and filenames must be unique per run.
  The runner refuses to overwrite, so callers must choose UNUSED names
  across reruns, not just within one sequence.
* Optional `window` selects `appWindow` or `settingsWindow`. Default is
  the active visible window.
* Screenshots capture the chosen Qt Quick window contents only. They do
  not capture other windows, the desktop, the cursor, Hyprland
  compositor blur, or window decorations. Device pixel ratio may scale
  the image beyond logical coordinates.
* The runner prints each capture path. In `--automation-sequence` mode
  all log output goes explicitly to stderr, so no `QT_FORCE_STDERR_LOGGING`
  is needed.
* Screenshots are app-local. There are no `grim` or desktop
  dependencies and no compositor shortcuts. The app links the QtTest
  module for this runner. A screenshot may target `appWindow` explicitly
  while the modal settings window is open; input events still cannot
  bypass the modal.

## 4. Automation Fixture

Seeded on every automation launch. Category IDs 1 and 2 are valid only
on a fresh database because they come from sequential `addCategory`
calls. Titles are `Automation Video 1` through `Automation Video 5`,
published from fixed `2026-01-01T12:00:00Z` in descending one-minute
intervals, duration 600 seconds, empty avatar URLs.

| Entity | IDs | Mapping and notes |
|---|---|---|
| Categories | 1, 2 | 1 is `Automation Music`, 2 is `Automation Tech` |
| Channels | `UCautomation01`, `UCautomation02` | `UCautomation01` in category 1, `UCautomation02` in category 2 |
| Videos | `AUTO0000001` through `AUTO0000005` | 1 to 3 in `UCautomation01`, 4 to 5 in `UCautomation02` |
| History | `AUTO0000001` | 120 watched seconds, last position 120 of 600, one session, 20% progress |
| Watch Next | `AUTO0000002`, `AUTO0000004` | Committed queue in that order, seeded via `addToWatchNext` |
| Live | none | All `isBroadcast false`, live model stays empty |

Network access and real playback stay disabled in automation. The fake
player never loads media. There is no live fixture, so live cards are
absent by design.

## 5. Selectors

Use `objectName` values as `target` names in click events or as window
names. `Accessible.name` values are informational for accessibility
checks.

Shared selectors, present in full and Simple UI:

| `objectName` | Accessible name | Notes |
|---|---|---|
| `appWindow` | none | Root `ApplicationWindow` |
| `feedPage` | none | Main feed container |
| `liveNowLabel` | none | `LIVE NOW` header above the live channel list; text switches to the hovered live video title |
| `historyLoader` | none | Lazy history `Loader`, active when history is open |
| `watchNextLoader` | none | Lazy Watch Next `Loader`, active when Watch Next is open |
| `playerLoader` | none | Lazy player `Loader`, active when `playerOpen` is true |
| `feedbackNotice` | none | Transient clipboard and Watch Next feedback popup |
| `feedbackLabel` | none | Text inside `feedbackNotice` |
| `historyPage` | none | Loaded history content |
| `watchNextPage` | none | Loaded Watch Next content |
| `playerPage` | none | Loaded `VideoPlayerPage` |
| `playerBackendLoader` | none | Backend loader inside `playerPage` |
| `automationPlayer` | none | Fake player, text is `Automation player <videoId>` |
| `playerBackButton` | `Back from player` | Calls `App.closePlayer()` |
| `settingsWindow` | none | Separate `ApplicationWindow`, not under `appWindow` |
| `settingsTabs` | none | Tab bar, check `currentIndex` |
| `settingsChannelsTab` | `Settings channels tab` | Tab index 0 |
| `settingsCategoriesTab` | `Settings categories tab` | Tab index 1 |
| `settingsFeedTab` | `Settings feed tab` | Tab index 2 |
| `settingsAppearanceTab` | `Settings appearance tab` | Tab index 3 |
| `settingsApiTab` | `Settings data API tab` | Tab index 4 |
| `settingsPlaybackTab` | `Settings playback tab` | Tab index 5 |
| `settingsCloseButton` | `Close settings` | Hides settings window |
| `simpleUiCheckBox` | none | Checkbox labeled `Use simple UI` in the appearance tab of both UIs |

Dynamic selectors, created by Repeaters:

| Pattern | Accessible name pattern | Notes |
|---|---|---|
| `categoryButton_<id>` | `Category <id> <name>` | Example: `categoryButton_1` |
| `feedVideo_<videoId>` | `Video <videoId> <title>` | Example: `feedVideo_AUTO0000001`, opens the player |
| `historyVideo_<videoId>` | `History video <videoId> <title>` | Example: `historyVideo_AUTO0000001`, selects video |
| `watchNextVideo_<videoId>` | `Watch Next video <videoId> <title>` | Example: `watchNextVideo_AUTO0000002`, selects video |
| `feedVideoOutline_<videoId>` | none | Transparent topmost feed card border overlay, `z:1` |
| `historyVideoOutline_<videoId>` | none | Transparent topmost history card border overlay, `z:1` |
| `watchNextVideoOutline_<videoId>` | none | Transparent topmost Watch Next card border overlay, `z:1` |
| `watchNextUp_<videoId>` | `Move up <videoId>` | Moves queue entry one slot earlier |
| `watchNextDown_<videoId>` | `Move down <videoId>` | Moves queue entry one slot later |
| `watchNextRemove_<videoId>` | `Remove from Watch Next <videoId>` | Removes queue entry |
| `liveVideo_<videoId>` | `Live video <videoId> <channel> <title>` | No fixture entry, expect absent |

Full UI only selectors:

| `objectName` | Accessible name |
|---|---|
| `feedNavigationButton` | `Show feed` |
| `historyNavigationButton` | `Show history` |
| `watchNextNavigationButton` | `Show Watch Next` |
| `settingsNavigationButton` | `Open settings` |
| `refreshButton` | `Refresh feed`, disabled in automation |

Simple UI keyboard routes, no navigation buttons:

| Key | Effect |
|---|---|
| `H` | Toggles `historyOpen`, reloads watch history, closes Watch Next |
| `W` | Toggles `watchNextOpen`, reloads Watch Next, closes history |
| `C` | Opens `settingsWindow` |
| `R` | Shortcut is disabled in automation, so status stays unchanged |
| `F` | Closes history or Watch Next and returns to feed |
| `Escape` | Exits fullscreen or closes player |
| `j`, `k` | Scrolls feed, history, or Watch Next by 120 pixels |
| `q` | Quits application |

`simpleUiCheckBox` causes a root window replacement when toggled. The
old `appWindow` is destroyed and a new one is created, so every cached
target becomes invalid. After clicking the checkbox, allow an explicit
delay and then use fresh lookups against the new root. Do not reuse
earlier targets across the swap.

## 6. Runnable Examples

Save each example as a `/tmp` JSON file and run it with
`./build/yt-client --automation-sequence <file>`, adding
`--automation-ui simple` for the Simple UI variants.

Feed to player and back (full UI):

```json
[
  {"type": "click", "target": "feedVideo_AUTO0000001", "atMs": 500},
  {"type": "screenshot", "filename": "sil-48-player.png", "atMs": 1500},
  {"type": "click", "target": "playerBackButton", "atMs": 2000},
  {"type": "screenshot", "filename": "sil-48-feed.png", "atMs": 3000}
]
```

Category filter (full UI):

```json
[
  {"type": "click", "target": "categoryButton_2", "atMs": 500},
  {"type": "screenshot", "filename": "sil-48-category.png", "atMs": 1500}
]
```

History and Watch Next (full UI):

```json
[
  {"type": "click", "target": "historyNavigationButton", "atMs": 500},
  {"type": "screenshot", "filename": "sil-48-history.png", "atMs": 1500},
  {"type": "click", "target": "feedNavigationButton", "atMs": 2000},
  {"type": "click", "target": "watchNextNavigationButton", "atMs": 3000},
  {"type": "screenshot", "filename": "sil-48-watchnext.png", "atMs": 4000}
]
```

History and Watch Next (Simple UI, same routes are valid in both UIs
when navigation buttons are replaced by keys):

```json
[
  {"type": "key_press", "key": "H", "atMs": 500},
  {"type": "key_release", "key": "H", "atMs": 550},
  {"type": "screenshot", "filename": "sil-48-simple-history.png", "atMs": 1500},
  {"type": "key_press", "key": "F", "atMs": 2000},
  {"type": "key_release", "key": "F", "atMs": 2050},
  {"type": "key_press", "key": "W", "atMs": 2500},
  {"type": "key_release", "key": "W", "atMs": 2550},
  {"type": "screenshot", "filename": "sil-48-simple-watchnext.png", "atMs": 3500}
]
```

Settings tabs and screenshot (both UIs; `C` opens settings in full
and Simple UI):

```json
[
  {"type": "key_press", "key": "C", "atMs": 500},
  {"type": "key_release", "key": "C", "atMs": 550},
  {"type": "click", "target": "settingsAppearanceTab", "window": "settingsWindow", "atMs": 1500},
  {"type": "screenshot", "filename": "sil-48-settings.png", "window": "settingsWindow", "atMs": 2200},
  {"type": "click", "target": "settingsCloseButton", "window": "settingsWindow", "atMs": 2700}
]
```

Switching Simple UI from the appearance tab (both UIs; `C` valid in
both, `settingsNavigationButton` is full UI only):

```json
[
  {"type": "key_press", "key": "C", "atMs": 500},
  {"type": "key_release", "key": "C", "atMs": 550},
  {"type": "click", "target": "settingsAppearanceTab", "window": "settingsWindow", "atMs": 1500},
  {"type": "click", "target": "simpleUiCheckBox", "window": "settingsWindow", "atMs": 2000},
  {"type": "screenshot", "filename": "sil-48-after-swap.png", "atMs": 4000}
]
```

The long gap after `simpleUiCheckBox` is intentional. The root window
is replaced, so allow time for the swap and then treat all later
targets as fresh lookups.

## 7. Diagnostics

Useful state for failure reports: `automationMode` (must be true),
`playerOpen`, `selectedCategoryId`, `refreshing`, `historyLoading`,
`statusMessage`, and `simpleUi`, plus QML window property
`historyOpen` for history visibility. Offscreen `raise()` from settings
`open()` can warn; this is expected and not a failure. QML engine
errors such as `objectCreationFailed` are not expected and are real
defects. `R` is disabled. A direct `App.refresh()` call returns without
network work and sets status
`Automation mode: refresh is disabled.` Channel add sets error
`Automation mode: adding channels is disabled.`
`--automation --database <path>` and any `--automation-sequence` run
with `--database` exit nonzero before creating windows.

## 8. Unsupported Scope

No live fixture exists, so live cards are absent by design. Drag
reorder, native file dialogs, and fullscreen behavior stay outside the
automation suite. Coordinate clicks use logical window-local positions,
do not auto-scroll, and depend on window layout; they can reach targets
without `objectName`. The regression reference is
`tests/automation_test.cpp`; see also `./bin/test` for the full suite.
