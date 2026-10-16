# Changelog

All notable changes to **Qddbconv** are documented in this file.
This project adheres to [Semantic Versioning](https://semver.org/) (`vMAJOR.MINOR.PATCH`).

---

## [v1.2.0] - 2026-10-16

### Added
- **Multi-stage runtime encoder & decoder fallback system**:
  - Automatic encoder fallback chain (`CLI binary -> FFmpeg library encoder -> FFmpeg built-in encoder`) when a preset's primary CLI tool (`lame`, `oggenc`, `opusenc`, `flac`, `wavpack`, `faac`, `fdkaac`, `neroaacenc`, `ttaenc`, `twolame`, `aften`) or external FFmpeg library wrapper (`libopus`, `libvorbis`, `libfdk_aac`, `libtwolame`, `libwavpack`) is unavailable on the host system.
  - Automatic tracker module (`.mod`, `.xm`, `.it`, `.s3m`, `.mptm`, etc.) and console chiptune (`.spc`, `.vgm`, `.vgz`, `.nsf`, `.nsfe`, `.gbs`, `.hes`, `.ay`, `.sap`, `.kss`) decoder fallback via `openmpt123`/`xmp` and `gme2wav` when the active FFmpeg binary was built without `--enable-libopenmpt` or `--enable-libgme`.
  - Live per-preset backend inspection (`AudioConverter::describePresetExecution`) cached in memory (`ffmpeg -hide_banner -encoders` queried at most once per session), showing in each preset's tooltip whether it will execute directly as designed (`CLI binary`, `FFmpeg library encoder`, or `FFmpeg built-in encoder`) or via fallback.
  - Post-conversion error summary dialog and `Status` column fallback notes (`Done (fallback: ...)`).
- **Dedicated user presets directory with portable mode & custom path configuration**:
  - Default user presets path moved to `$XDG_DATA_HOME/qddbconv/presets` (`~/.local/share/qddbconv/presets` on Linux; `%LOCALAPPDATA%\qddbconv\presets` or `<app_dir>\presets` in portable mode on Windows) while maintaining 100% compatibility with DeaDBeeF's plain-text `.txt` preset format.
  - Added user presets directory configuration (`Browse...`, `Default`, and `Portable (./presets)` on Windows) in `Settings -> Configure qddbconv...`.
- **Portable 64-bit Windows build (`Qddbconv-1.2.0-win64.zip`) & UI Theme support**:
  - Cross-compiled standalone 64-bit Windows package bundling Qt 6.8.2 runtime, `ffmpeg.exe`, `ffprobe.exe`, CLI audio encoders, `openmpt123.exe`, and `gme2wav.exe`.
  - Embedded Windows application manifest (`qddbconv.manifest` with `Microsoft.Windows.Common-Controls` v6.0.0.0 and `PerMonitorV2` High-DPI awareness) into `qddbconv.rc`.
  - Added `CleanUiProxyStyle` (`QProxyStyle`) and embedded vector KDE Breeze fallback icons (`makeBreezeFallbackIcon`) for uniform toolbar button geometry and crisp icons on Windows/Wine.
  - Added `View -> UI Theme` selector (`Breeze Light`, `Breeze Dark`, `Windows Classic / Native`, `System Default`).

### Changed
- **Main window & menu layout refinements**:
  - Unified all version strings across window title, About dialog, man page, and packages to `v1.2.0` (`vMAJOR.MINOR.PATCH`).
  - Removed duplicate view actions (`UI Theme`, `Toolbar Text Position`, `Show Menubar`) from the `Settings` menu so they reside exclusively under `View`, leaving `Encoder Presets...` and `Configure qddbconv...` in `Settings`.
  - Removed the redundant `Presets...` button next to the `Encoder:` dropdown in the main window (accessible via `Settings -> Encoder Presets...` or `Ctrl+P`).
  - Aligned all combo boxes and spin boxes in `Encoder & DSP` and `More Options` to expand uniformly (`QFormLayout::AllNonFixedFieldsGrow` + `QSizePolicy::Expanding`).
  - Renamed `Options & Cover Art` group in the main window to `More Options` and moved less frequently toggled options (`Resize embedded cover art if larger than ... px` and `Re-tag copied files when bypassing conversion`) into structured groups (`External Tools & Directories`, `Cover Art & File Copy`, `Queue & Interface`) inside `Settings -> Configure qddbconv...`.

---

## [v1.1.0] - 2026-10-15

### Added
- Native C++17 Yamaha SMAF (`.mmf`) parser and synthesizer (`smaf2mid.cpp`):
  - Supports `CNTI`, `OPDA`, `MTR*` (`Mtsq`/`Mtsu`/`Mtsp`/`Mwa*`), and `ATR*` (`Atsq`/`Awa*`) chunks, Okumura Huffman (`FMMU`) decompression, 4-bit Yamaha MA ADPCM & PCM stream decoding, and hybrid MIDI + vocal stream mixing.
  - Built-in preset to extract Standard MIDI (`.mid`) files directly from Yamaha SMAF (`.mmf`) ringtones.
- Standard MIDI (`.mid`, `.midi`, `.kar`, `.rmi`) audio conversion support via `timidity` / `fluidsynth`.
- Complete `qddbconv(1)` manual page documenting formats, Title Formatting placeholders, shortcuts, configuration files, and runtime temporary files.
- Single-folder-aware `Preserve folder structure` calculation (`TrackProbe::commonRootFolder`) that preserves the album directory name when converting a single folder.

### Fixed
- Avoided `E2BIG` (`MAX_ARG_STRLEN`) when embedding large cover art images into Ogg Vorbis (`.ogg`) and Opus (`.opus`) by passing `METADATA_BLOCK_PICTURE` via a temporary `-f ffmetadata` file instead of `argv`.
- Fixed native KDE Plasma file dialogs in the portable AppImage by bundling `libqxdgdesktopportal.so` and `breeze6.so` with `qt.conf` and `$ORIGIN/../lib` RPATH.

---

## [v1.0.0] - 2026-10-14

### Added
- Initial release of **Qddbconv**, a standalone multi-threaded C++17 / Qt6 audio converter inspired by DeaDBeeF, `deadbeef-qt5`, and Qmmp.
- Two-pane layout with Converter Settings on the left and a live Output File Preview & Track Queue table on the right.
- 27 built-in DeaDBeeF/FFmpeg encoder presets plus custom preset management (`Add`, `Edit`, `Copy`, `Remove`).
- Built-in multi-track `.cue` sheet parser and sample-accurate track splitter.
- Track Metadata & Properties editor (`Ctrl+I` / double-click) with CUE-safe source tag writing and automatic album cover art embedding/downscaling.
- KDE HIG menubar with `Ctrl+M` Dolphin-style hamburger menu toggle, configurable toolbar button text positions, and `Ctrl+Q` / top-right `Quit` button.
