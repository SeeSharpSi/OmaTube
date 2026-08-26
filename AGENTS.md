# OmaTube Repository Notes

## Build and Verification

- Toolchain is C++20, qmake, Make, and Qt 6.8+ with Core, GUI, Network, Quick, Quick Controls 2, SQL, Test, and SQLite support. Optional native playback adds Qt OpenGL, libmpv development files, and a current `yt-dlp` executable.
- Use `./bin/build`; it selects `qmake6` before `qmake`, configures and builds into `build/`, and does not clean existing output.
- `yt-client.pro` detects libmpv through `pkg-config`. When unavailable, the app builds with only the official player; when available, it defines `OMA_HAS_MPV`, links libmpv, and packages the native backend.
- Use `./bin/test` for full verification. It rebuilds the app, builds and runs all four QtTest suites with `QT_QPA_PLATFORM=offscreen`, then starts the app with an in-memory database via `--quit-after-startup`.
- CI runs `./bin/build` then `./bin/test` with Qt 6.8.3 on Ubuntu 24.04 and macOS 14. No separate lint, formatter, typecheck, or codegen task exists.
- To rebuild and run one QtTest function, use this pattern; replace project, binary, and function together:

```sh
QMAKE="$(command -v qmake6 || command -v qmake)"
mkdir -p build-tests/repository_tests
(cd build-tests/repository_tests &&
  "$QMAKE" ../../tests/repository_tests.pro &&
  make -j"$(nproc 2>/dev/null || echo 1)" &&
  QT_QPA_PLATFORM=offscreen ./repository_tests categoryLifecycle)
```

- Test ownership: `appcontroller_tests` covers playback state, watch tracking, and QSettings-backed playback preferences; `repository_tests` covers SQLite, migrations, and models; `youtube_tests` covers response parsing and refresh orchestration through a fake client with no network; `theme_tests` covers palette parsing, built-in resources, and selection.

## Architecture

- `yt-client.pro` defines the application. `ytclient_core.pri` owns repository, models, YouTube client, and refresh service sources shared by the app and two core test projects.
- `src/main.cpp` creates the sole `AppController`, registers it as QML singleton `YtClient.App`, and loads `qrc:/qml/Main.qml`.
- `AppController` owns persistent storage, network client, refresh service, theme manager, and QML-facing list models. Keep network and persistence logic out of QML.
- Playback defaults to the official YouTube iframe. `MpvPlayerNative` is an optional app-only `QQuickFramebufferObject` backend using libmpv's OpenGL render API; it preserves the same `videoId`, `startSeconds`, and `playbackUpdated` QML contract.
- Native mpv quality values are preferred maximum heights selected through yt-dlp. `Auto` leaves format selection to mpv/yt-dlp; a requested height is not a guarantee that rendition exists.
- `RefreshService` intentionally separates metadata, upload, detail, and per-channel live stages, with at most four concurrent requests. Feed success must remain usable when live checks fail; report live status as incomplete instead of clearing it silently.
- Refresh occurs only at startup or explicit `R`; there is no timer or background polling after a refresh.

## Persistent State and External Calls

- A normal app launch opens `yt-client.sqlite3` under Qt's `AppDataLocation`, reads user `QSettings`, and can consume YouTube quota during startup refresh. Use `./bin/test` or `--quit-after-startup` for smoke verification instead of launching against user data.
- `YT_CLIENT_API_KEY` overrides the API key stored in plain local `QSettings`. Tests must not require a real key.
- Playback backend and preferred maximum height use `QSettings` keys under `playback/`; they do not require SQLite migrations. Tests must isolate QSettings from user configuration.
- mpv playback invokes the external `yt-dlp` executable and makes direct media requests. Automated tests and startup smoke verification must not open videos or require network, libmpv, or yt-dlp at runtime.
- SQLite schema versioning lives in `Repository::migrate()` through `PRAGMA user_version`. Schema changes must update new-database creation, add a forward migration for existing databases, and extend the migration fixture test.

## Explicit Manifests

- qmake does not discover files: add core C++ files to `ytclient_core.pri`, app-only files to `yt-client.pro`, and test files to their matching `tests/*_tests.pro`.
- Keep libmpv headers and sources inside the conditional `packagesExist(mpv)` block in `yt-client.pro`; shared core tests must remain link-independent from libmpv and OpenGL.
- QML and built-in themes are compiled resources. Add or rename them in `qml/resources.qrc`; filesystem presence alone does not package them.
- Built-in theme IDs and resource paths are also hard-coded in `src/thememanager.cpp` (`isThemeId()` and `builtInThemePath()`), so adding a theme requires updating those functions, `qml/resources.qrc`, and theme tests.
