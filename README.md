# YT Client

YT Client is a small desktop YouTube feed built with C++20, Qt 6, and QML. It keeps subscriptions and categories locally, shows current live channels separately, and plays videos inside the app.

No YouTube login is used. Each user supplies their own YouTube Data API v3 key.

## Features

- User-defined categories with channels assigned to any number of categories
- Text-only video feed without thumbnails
- Separate live-channel circles
- Cached feed available immediately at startup
- Refresh only at startup or when the user presses `R`
- Optional embedded mpv playback with a preferred maximum video quality
- Built-in Default, Rose Pine, Nord, and system-synced Omarchy themes
- SQLite storage under Qt's platform application-data location
- macOS and Linux builds from one qmake project

## Requirements

- C++20 compiler
- qmake and Make
- Qt 6.8 or newer with Core, GUI, Network, Quick, Quick Controls 2, SQL, and SQLite driver
- Linux only: Qt WebEngine Quick with its QML module
- Optional native playback: libmpv development files and a current `yt-dlp` executable

The official embedded player remains available when libmpv is absent. qmake detects libmpv through `pkg-config`; the Playback settings expose mpv only in builds where it was found.

On Omarchy, Qt, libmpv, and yt-dlp are already installed with the default desktop toolchain. No package installation is required.

On Arch Linux:

```sh
sudo pacman -S --needed base-devel qt6-base qt6-declarative qt6-webengine mpv yt-dlp
```

`mpv` provides the libmpv development files and pkg-config metadata used at build time, while `yt-dlp` is required at runtime by the embedded mpv backend.

On macOS with Homebrew:

```sh
xcode-select --install
brew install qt
# Optional embedded mpv backend:
brew install pkg-config mpv yt-dlp
```

On Ubuntu 24.04 or similar:

```sh
sudo apt install make g++ qt6-base-dev qt6-declarative-dev qt6-webengine-dev libqt6sql6-sqlite
# Optional embedded mpv backend:
sudo apt install pkg-config libmpv-dev yt-dlp
```

Package names vary between Linux distributions.

## Build

```sh
./bin/build
./bin/test
```

Run on macOS:

```sh
open build/yt-client.app
```

Run directly on macOS, including environment variables:

```sh
./build/yt-client.app/Contents/MacOS/yt-client
```

Run on Linux:

```sh
./build/yt-client
```

Build output uses Qt installation selected by `qmake6` or `qmake`. Producing signed, self-contained distribution packages is intentionally separate from source builds because Qt runtime deployment and signing differ between Linux distributions and macOS release channels.

## API Key

Create a Google Cloud project, enable YouTube Data API v3, create an API key, and restrict the key to that API. Enter it through `API key` in the application.

The application offers two storage choices:

- Session only: leave `Remember` unchecked. Key remains in memory until exit.
- Plain local settings: enable `Remember`. This is convenient but not secure secret storage.

For unattended startup without local key storage, set `YT_CLIENT_API_KEY`:

```sh
YT_CLIENT_API_KEY=your-key ./build/yt-client
```

Never commit a key or distribute one shared key with binaries.

## Use

1. Configure API key.
2. Add categories through `Manage`.
3. Add channels by `@handle`, channel URL, or `UC...` channel ID.
4. Assign channels to categories.
5. Press `R` whenever a fresh snapshot is wanted.

Category switching only filters local SQLite data. It does not make network requests. Clicking a video title or live circle replaces the feed with the selected player. The official YouTube embedded player is the default. Builds with libmpv can select `Embedded mpv` and a preferred maximum quality under `Settings` > `Playback`; the actual resolution can be lower when a rendition is unavailable. Use Back or Escape to return to the feed.

## Refresh and Quota

Each refresh retrieves recent upload IDs, fetches video details in batches, and performs one live search per channel. No timer or background polling runs after refresh completes.

YouTube currently gives `search.list` a separate default limit of 100 calls per day. With 25 channels, one startup plus three manual refreshes consumes 100 live-search calls. Feed refresh can still succeed when live-search quota is exhausted; application reports live status as incomplete rather than claiming nobody is live.

Cached video history is retained until the database reaches 10 MB; past that, the oldest published videos are pruned first in, first out while watch statistics are kept. API-derived channel metadata is refreshed before reaching 30 days. User-created categories and channel assignments remain until user removes them.

The feed loads 50 videos at a time. Scrolling toward the bottom serves further cached videos, and once the local cache is exhausted, each additional scroll fetches one deeper uploads page per channel in the active category filter until the channel's uploads playlist is exhausted or the storage cap is reached.

## Architecture

- `AppController`: QML-facing operations and state
- `RefreshService`: staged uploads, details, and live refresh
- `YouTubeClient`: asynchronous HTTPS requests and response parsing
- `Repository`: SQLite schema, migrations, and queries
- `MpvPlayerNative`: optional libmpv OpenGL renderer and playback controls
- `QAbstractListModel` implementations: category, channel, feed, and live data
- QML: presentation and dialogs only

## Policy Notes

The default backend uses documented YouTube APIs and the official YouTube embedded player. Linux uses Qt WebEngine; macOS uses WKWebView. It does not scrape pages, download media, block ads, or modify YouTube playback. Videos disabled by their owner for embedding remain unavailable through that backend.

The optional mpv backend has a different behavior and policy surface: libmpv invokes yt-dlp to resolve YouTube media streams, then plays those streams directly. It can apply a preferred maximum resolution and may play public videos disabled for embedding. Users enabling it are responsible for complying with YouTube terms and applicable law.

- [YouTube API Services Terms](https://developers.google.com/youtube/terms/api-services-terms-of-service)
- [YouTube Developer Policies](https://developers.google.com/youtube/terms/developer-policies)
- [YouTube API quota costs](https://developers.google.com/youtube/v3/determine_quota_cost)
