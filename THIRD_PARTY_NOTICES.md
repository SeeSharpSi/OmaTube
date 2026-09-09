# Third-party notices

OmaTube is licensed under GPL-3.0-or-later. Bundled software retains its
own license. A binary package is not a license grant for every component
under the OmaTube license.

The Arch package includes a private Qt/QML/Qt WebEngine runtime, libmpv,
FFmpeg and transitive libraries, audio client modules, standalone yt-dlp,
and Deno. `/opt/omatube/share/manifest.json` records the files, checksums,
original build inputs, and package versions. `/opt/omatube/licenses/`
contains collected Arch license texts and pinned upstream helper notices.
The manifest is an inventory, not proof of license compliance.

- Qt includes LGPL/GPL components and Qt WebEngine includes Chromium
  third-party code. See https://www.qt.io/licensing/ and the exact Qt
  source releases corresponding to the recorded builder packages.
- The default libmpv build is GPL-enabled. FFmpeg's license depends on
  its build configuration. See https://github.com/mpv-player/mpv/blob/master/Copyright
  and https://ffmpeg.org/legal.html.
- yt-dlp source is Unlicense, but its standalone PyInstaller distribution
  includes GPL-3.0-or-later dependencies. It must not be described as an
  Unlicense-only binary. The package includes its upstream third-party
  notice file. See https://github.com/yt-dlp/yt-dlp#licensing.
- Deno is MIT-licensed and includes separately licensed dependencies.
  Retain its upstream notices and review its exact release sources.

## Before public distribution

Publish the exact OmaTube source, packaging scripts, and required
corresponding source for all bundled GPL/LGPL components, including build
recipes, patches, and applicable relinking materials. Retrieve the exact
Arch packaging revisions and upstream source versions from the manifest;
a link to a moving upstream branch is not a substitute. Review Chromium,
codec, helper, and transitive-library notices for completeness. Preserve
the source materials for as long as the relevant license requires.

The packaging script collects installed notices and inventories versions;
it does not assemble or validate a complete corresponding-source release.
Do not publish the binary package until this review is complete.
