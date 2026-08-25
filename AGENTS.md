# OmaTube Repository Notes

## Build and Verification

- Toolchain is C++20, qmake, Make, and Qt 6.8+ with Core, GUI, Network, Quick, Quick Controls 2, SQL, Test, and SQLite support.
- Use `./bin/build`; it selects `qmake6` before `qmake`, configures and builds into `build/`, and does not clean existing output.
- Use `./bin/test` for full verification. It rebuilds the app, builds and runs all three QtTest suites with `QT_QPA_PLATFORM=offscreen`, then starts the app with an in-memory database via `--quit-after-startup`.
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

- Test ownership: `repository_tests` covers SQLite, migrations, and models; `youtube_tests` covers response parsing and refresh orchestration through a fake client with no network; `theme_tests` covers palette parsing, built-in resources, and selection.

## Architecture

- `yt-client.pro` defines the application. `ytclient_core.pri` owns repository, models, YouTube client, and refresh service sources shared by the app and two core test projects.
- `src/main.cpp` creates the sole `AppController`, registers it as QML singleton `YtClient.App`, and loads `qrc:/qml/Main.qml`.
- `AppController` owns persistent storage, network client, refresh service, theme manager, and QML-facing list models. Keep network and persistence logic out of QML.
- `RefreshService` intentionally separates metadata, upload, detail, and per-channel live stages, with at most four concurrent requests. Feed success must remain usable when live checks fail; report live status as incomplete instead of clearing it silently.
- Refresh occurs only at startup or explicit `R`; there is no timer or background polling after a refresh.

## Persistent State and External Calls

- A normal app launch opens `yt-client.sqlite3` under Qt's `AppDataLocation`, reads user `QSettings`, and can consume YouTube quota during startup refresh. Use `./bin/test` or `--quit-after-startup` for smoke verification instead of launching against user data.
- `YT_CLIENT_API_KEY` overrides the API key stored in plain local `QSettings`. Tests must not require a real key.
- SQLite schema versioning lives in `Repository::migrate()` through `PRAGMA user_version`. Schema changes must update new-database creation, add a forward migration for existing databases, and extend the migration fixture test.

## Explicit Manifests

- qmake does not discover files: add core C++ files to `ytclient_core.pri`, app-only files to `yt-client.pro`, and test files to their matching `tests/*_tests.pro`.
- QML and built-in themes are compiled resources. Add or rename them in `qml/resources.qrc`; filesystem presence alone does not package them.
- Built-in theme IDs and resource paths are also hard-coded in `src/thememanager.cpp` (`isThemeId()` and `builtInThemePath()`), so adding a theme requires updating those functions, `qml/resources.qrc`, and theme tests.
