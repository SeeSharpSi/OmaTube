# OmaTube Automation Guide

This guide is for coding agents that drive the OmaTube UI. It defines
the only safe launch mode, the fixed fixture, the stable selectors,
and the harness patterns used by `tests/navigation_test.cpp`.

## 1. Safe Launch Mode

Always build first, then launch with automation mode:

```sh
./bin/build
./build/yt-client --automation
```

A normal launch opens `yt-client.sqlite3` under Qt `AppDataLocation`,
reads user `QSettings`, and can start a network refresh that consumes
YouTube quota. Do not use a normal launch for agent testing.
`--automation` uses a temporary SQLite database and temporary settings
path, seeds a fixed fixture, disables startup and manual refresh and
channel resolution, sets image sources to empty so no media or image
requests occur, and loads a fake player that never loads media. It never
writes user state.
`--automation` rejects `--database` and exits nonzero with
`--automation cannot be combined with --database: refusing to risk a user database.`
This protects a user database from fixture seeding.

## 2. Quick Commands

```sh
./bin/build
./build/yt-client --automation
QT_QPA_PLATFORM=offscreen ./build/yt-client --automation --quit-after-startup
./bin/test
QMAKE="$(command -v qmake6 || command -v qmake)"
mkdir -p build-tests/navigation_tests
(cd build-tests/navigation_tests && "$QMAKE" ../../tests/navigation_tests.pro && make -j"$(nproc 2>/dev/null || echo 1)" && QT_QPA_PLATFORM=offscreen ./navigation_tests fullUiNavigation)
QT_QPA_PLATFORM=offscreen ./build-tests/navigation_tests/navigation_tests simpleUiNavigation
```

The first two commands build and launch the interactive automation app.
The offscreen command is a smoke check that opens no video. `./bin/test`
rebuilds the app, runs all five QtTest suites offscreen, then runs a
normal smoke launch and an automation smoke launch. The remaining
commands build and run one navigation test function at a time.

## 3. Automation Fixture

Seeded by `src/automationfixture.cpp` on every automation launch.
Category IDs 1 and 2 are valid only on a fresh database because they
come from sequential `addCategory` calls. Titles are `Automation
Video 1` through `Automation Video 5`, published from fixed
`2026-01-01T12:00:00Z` in descending one-minute intervals, duration
600 seconds, empty avatar URLs.

| Entity | IDs | Mapping and notes |
|---|---|---|
| Categories | 1, 2 | 1 is `Automation Music`, 2 is `Automation Tech` |
| Channels | `UCautomation01`, `UCautomation02` | `UCautomation01` in category 1, `UCautomation02` in category 2 |
| Videos | `AUTO0000001` through `AUTO0000005` | 1 to 3 in `UCautomation01`, 4 to 5 in `UCautomation02` |
| History | `AUTO0000001` | 120 watched seconds, last position 120 of 600, one session, 20% progress |
| Watch Next | `AUTO0000002`, `AUTO0000004` | Committed queue in that order, seeded via `addToWatchNext` |
| Live | none | All `isBroadcast false`, live model stays empty |

## 4. Selectors

Use `objectName` when walking the Qt or QML item tree. Use
`Accessible.name`, `Accessible.role`, and `Accessible.onPressAction`
only for accessibility aware harnesses. Both systems are stable but
serve different harnesses.

Shared selectors, present in full and Simple UI:

| `objectName` | Accessible name | Notes |
|---|---|---|
| `appWindow` | none | Root `ApplicationWindow` |
| `feedPage` | none | Main feed container |
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

Dynamic selectors, created by Repeaters:

| Pattern | Accessible name pattern | Notes |
|---|---|---|
| `categoryButton_<id>` | `Category <id> <name>` | Example: `categoryButton_1` |
| `feedVideo_<videoId>` | `Video <videoId> <title>` | Example: `feedVideo_AUTO0000001`, action calls `App.openVideo`, right-click calls `App.addToWatchNext` |
| `historyVideo_<videoId>` | `History video <videoId> <title>` | Example: `historyVideo_AUTO0000001`, action selects video |
| `watchNextVideo_<videoId>` | `Watch Next video <videoId> <title>` | Example: `watchNextVideo_AUTO0000002`, action selects video |
| `feedVideoOutline_<videoId>` | none | Transparent topmost feed card border overlay, `z:1`, hover width 2 opaque accent |
| `historyVideoOutline_<videoId>` | none | Transparent topmost history card border overlay, `z:1`, hover width 2 opaque accent |
| `watchNextVideoOutline_<videoId>` | none | Transparent topmost Watch Next card border overlay, `z:1`, hover width 2 opaque accent |
| `watchNextUp_<videoId>` | `Move up <videoId>` | Calls `App.moveWatchNext` one slot earlier |
| `watchNextDown_<videoId>` | `Move down <videoId>` | Calls `App.moveWatchNext` one slot later |
| `watchNextRemove_<videoId>` | `Remove from Watch Next <videoId>` | Calls `App.removeFromWatchNext` |
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
| `S` | Opens `settingsWindow` |
| `R` | Shortcut is disabled in automation, so status stays unchanged |
| `Escape` | Closes history, then Watch Next, exits fullscreen, or closes player, in that order |
| `j`, `k` | Scrolls feed, history, or Watch Next by 120 pixels |
| `q` | Quits application |

## 5. Harness Rules

* Repeater delegates are JS owned. `QObject::findChild` cannot see them. Walk `QQuickItem::childItems` recursively from the window content item.
* `historyLoader`, `playerLoader`, and `playerBackendLoader` are lazy. Use `QTRY_VERIFY` or `QTRY_COMPARE` or an equivalent condition wait after every action. Never use a fixed sleep as proof of absence.
* Settings is a separate `QQuickWindow` named `settingsWindow`. Find it through engine root objects or top level windows, not under `appWindow`.
* Map clicks to window coordinates. Take item center, call `mapToScene`, then map from scene through the target window content item before `QTest::mouseClick`.
* `AppController::simpleUiChanged` replaces the root window. `src/main.cpp` loads the other visible QML root, copies window state, then deletes the old window on the next event loop turn. Reacquire window, root item, and all item pointers after the swap.
* Never keep item or window pointers across Loader unload, model reset, or root swap. Reacquire after `historyOpen`, `watchNextOpen`, `playerOpen`, category, or Simple UI changes.

## 6. C++ Snippets

Recursive lookup, adapted from `tests/navigation_test.cpp`:
```cpp
QList<QQuickItem *> findVisualChildrenByName(QQuickItem *parent, const QString &name)
{
    QList<QQuickItem *> matches;
    if (!parent)
        return matches;
    if (parent->objectName() == name)
        matches.append(parent);
    for (QQuickItem *child : parent->childItems())
        matches.append(findVisualChildrenByName(child, name));
    return matches;
}
```
Coordinate mapping for clicks:
```cpp
void clickItem(QQuickItem *item)
{
    QVERIFY(item != nullptr);
    QQuickWindow *window = item->window();
    QVERIFY(window != nullptr);
    const QPointF sceneCenter =
        item->mapToScene(QPointF(item->width() / 2.0, item->height() / 2.0));
    const QPoint target = window->contentItem()->mapFromScene(sceneCenter).toPoint();
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, target);
}
```

## 7. Route Recipes

Feed to player and back: wait for `feedVideo_AUTO0000001`, click it,
expect `playerOpen` true plus `playerPage`, `playerBackendLoader`, and
`automationPlayer`, click `playerBackButton`, expect `playerOpen` false.
Category filter in full UI: click `categoryButton_2`, expect
`selectedCategoryId == 2`, expect `feedVideo_AUTO0000004` and
`feedVideo_AUTO0000005` present, expect `feedVideo_AUTO0000001` through
`feedVideo_AUTO0000003` absent with a condition wait, click
`categoryButton_2` again to clear the filter if needed.
History in full UI: click `historyNavigationButton`, find
`historyLoader`, then wait for its `historyPage` item to load. Expect
`historyVideo_AUTO0000001` under `historyPage`.
History in Simple UI: send `H`, expect window property `historyOpen`
true, find `historyLoader`, then wait for its `historyPage` item and
`historyVideo_AUTO0000001`. Click the history row, expect `playerOpen`
true and `automationPlayer`, click `playerBackButton`, expect
`playerOpen` false, then reacquire the root item before the next step.
Watch Next in full UI: click `watchNextNavigationButton`, find
`watchNextLoader`, then wait for its `watchNextPage` item to load. Expect
`watchNextVideo_AUTO0000002` and `watchNextVideo_AUTO0000004` under
`watchNextPage`, click `feedNavigationButton` to return to the feed.
Watch Next in Simple UI: send `W`, expect window property
`watchNextOpen` true, find `watchNextLoader`, then wait for its
`watchNextPage` item and `watchNextVideo_AUTO0000002`. Click the queue
row, expect `playerOpen` true and `automationPlayer`, click
`playerBackButton`, expect `playerOpen` false, then reacquire the root
item before the next step. Never keep item or window pointers across
`historyOpen`, `watchNextOpen`, `playerOpen`, category, or Simple UI
changes.
Settings tabs in both UIs: open settings with the navigation button or
`S`, find the separate `settingsWindow` and wait for exposure, use its
content item as the new search root, expect `settingsTabs` and all six
tabs above, click each tab and expect `currentIndex` 0 through 5,
close with `settingsCloseButton` or `Escape`, expect hidden.
The executable full UI test clicks all six tabs. The Simple UI test
checks all six selectors and clicks `settingsPlaybackTab`.
Switching Simple UI: open settings, go to `settingsAppearanceTab`,
then use the checkbox labeled `Use simple UI`. It has no stable
`objectName`, so QtTest loads Simple UI by setting
`appearance/simpleUi=true` before controller initialization instead.
After an interactive toggle, wait for `simpleUiChanged`, find the new
`appWindow`, discard all old window and item pointers, and reacquire
from the new root.

## 8. Diagnostics

Inspect `automationMode` (must be true), `playerOpen`,
`selectedCategoryId`, `refreshing`, `historyLoading`,
`statusMessage`, and `simpleUi`, plus QML window property
`historyOpen` for history visibility. Offscreen `raise()` from
settings `open()` can warn; this is expected and not a failure. QML
engine errors such as `objectCreationFailed` are not expected and are
real defects. `R` is disabled. A direct `App.refresh()` call returns
without network work and sets status
`Automation mode: refresh is disabled.` Channel add sets error
`Automation mode: adding channels is disabled.`
`--automation --database <path>` exits nonzero before creating windows.

## 9. Unsupported Scope

No live fixture exists, so live cards are absent by design. Drag
reorder, native file dialogs, and fullscreen behavior stay outside the
navigation suite. Real playback and network access are disabled; the
fake player never loads media. The executable reference is
`tests/navigation_test.cpp`; copy its waits and lookup logic instead
of inventing new selectors.
