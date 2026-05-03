# Changelog

## v0.21 - Release Candidate

- Added an optional Ko-fi coffee-money tip link in the README, release docs, and Help/About UI; tips are appreciated but never required, and the app remains free software.
- Made the Ko-fi URL clickable in the Windows About dialog.
- Improved streamed parser performance by reading file data in chunks instead of byte-by-byte, reducing Windows open-file delays on larger logs.
- Added README instructions for sharing the NPS log directory and opening remote logs from Windows or Linux.
- Added a public release export script that packages only user-facing docs, build sources, icons, the sample log, binaries, and checksums.
- Sanitized the bundled sample log and README XML example so public packages use only synthetic accounts, sample device names, documentation IP ranges, and sample MAC addresses.
- Added repeatable sanitizing for larger realistic NPS test logs and included anonymized public-safe sample captures.
- Standardized anonymized company identity values on the fictional `CORPN` company name.
- Included sanitized sample logs in all public binary distro packages, not only the source archive.
- Added a release-package guard so private captured logs such as `logs/IN*.log` cannot be included in public artifacts.
- Clarified README install/run instructions for the portable no-installer release packages.
- Documented where `nps-logview.ini` is stored on Windows and Linux, including portable executable-folder priority, write-test rules, per-user fallback paths, and per-build window sections.
- Added the resolved `nps-logview.ini` path to the Windows and Linux About dialogs.
- Added streamed file loading for GTK, Windows 64-bit, Windows 32-bit, and the CLI parser helper.
- Added bottom status-bar progress during file loading.
- Removed the GUI 50 MB file-size rejection; loaded records remain capped at 4,096.
- Added row-boundary snapping for the list/details splitter.
- Added a Windows system-DPI-aware manifest to both Windows builds.
- Expanded README log format documentation for XML, CSV/text, decoding behavior, request/reply pairing, and parser limits.

## v0.20

- Added timestamp-header ordering tied to the saved Most Recent First setting.
- Added column hide/show and drag reordering.
- Added decoded CSV, text, and HTML export.
- Improved status bar file details and file-change state.
- Cleaned known compiler warnings.

## Earlier Milestones

- Added pair-aware filtering and rejected exchange highlighting.
- Added right-click Copy Record on the top list.
- Added persistent window state, recent files, column widths, and platform-specific INI sections.
- Expanded RADIUS attribute and Vendor-Specific Attribute decoding.
- Added shared decoding for IPv6, selected hex fields, and Microsoft NPS `Class`.
