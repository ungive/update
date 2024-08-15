# libupdate

> WIP

Automatic self-update utility for C++ programs on Windows and Mac,
with built-in optional integrity and authenticity checks.

Features:
- Integrity checks of downloads via accompanying checksum files
  (e.g. a "SHA256SUMS" file).
- Authenticity checks of downloads via accompanying signature files
  (e.g. a "SHA256SUMS.sig" file which contains an Ed25519 signature).
- Automatic management of installed versions and pruning of old versions.
- Upon running an update the tray icon of the application remains visible
  if the user decided to pull it into the visible area of the tray menu.
  This is because the update is always moved to a known location.
- Built-in support for fetching the latest release from GitHub
  using the GitHub API.
- Generic API for fetching updates from any HTTPS server.
- Supports ZIP archives on Windows
- Supports DMG archives on Mac (TODO)

Requirements:
- A main executable which uses the `manager` and `updater` class.
- An accompanying launcher executable which only uses the `manager` class,
  more details in the documentation of the manager class.

Dependencies:
- OpenSSL (using find_package)
- yhirose/cpp-httplib (to fetch update files via HTTP/S, submodule)
- nlohmann/json (to parse GitHub API responses, submodule)
- minizip-ng on Windows (to extract zip archives, submodule)
- gtest (for testing only, submodule)
