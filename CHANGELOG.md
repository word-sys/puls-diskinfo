# Changelog

All notable changes to **PULS DiskInfo** will be documented in this file.

---

## [1.1.4] - 2026-11-11

### Added
- **Direct Unprivileged `hwmon` Temperature Monitoring**: Read drive thermal sensors directly via kernel `/sys/block/<dev>/device/hwmon*/temp*_input` without requiring root or external utilities.
- **PCIe Link Speed & Width Inspector**: Identify negotiated and maximum PCIe generations (Gen 1 through Gen 6) and lane widths (x1 to x16) with link downgrade detection.
- **USB Speed & Bridge Controller Identification**: Accurately categorize USB connections (USB 2.0 480M, USB 3.0 5G, USB 3.1 10G, USB 3.2 20G, USB4 40G/80G) and identify bridge controller chipsets (ASMedia, Realtek, JMicron, Genesys Logic, VIA, Innostor, Phison).
- **Enhanced Partition & Mount Point Mapper**: Recursively trace multi-layer device mapper devices (dm-crypt/LUKS, LVM) and parent block devices to build an exhaustive partition and mount point inventory.
- **Dual Storage Capacity Formatters**: Support Decimal SI (1 TB = 10^12 B), Binary IEC (1 TiB = 2^40 B), and Dual format (`512.1 GB (476.9 GiB)`) with exact byte precision and persistent user preference toggle in Preferences.
- **Rich CLI Hardware Telemetry**: Extended both `--headless` human-readable terminal report and `--headless --json` structured output with complete sysfs parameters, sector sizes, TRIM/discard status, write cache, and partition inventories.

---

## [1.1.3] - 2026-11-05

### Added
- **Headless Terminal Mode (`--headless`)**: Full storage health and telemetry reporting directly in CLI environments without requiring a Wayland/X11 display server.
- **Machine-Readable JSON Output (`--json`)**: Structured JSON output format for scripting, automated monitoring, and CI integrations.
- **Mock Storage Simulation Engine (`--mock-disk`, `-m`)**: Full hardware simulator for NVMe SSD (Samsung 980 PRO), SATA SSD (Crucial MX500), mechanical HDD (Seagate IronWolf 8TB), and USB storage (SanDisk Extreme).
- **Core Static Library Target (`libpuls_core.a`)**: Decoupled core business logic from GUI entry points for seamless unit testing and toolchain integration.
- **Comprehensive Unit Testing Suite**: Automated test runner integrated with Meson (`meson test -C build --verbose`) covering data models, formatting helpers, storage simulation, and smartctl JSON parsing.
- **S.M.A.R.T. Multi-Drive Test Fixtures**: Sanitized real-world hardware smartctl dumps covering NVMe, SATA SSD, HDD, wear caution states, and critical drive failures.
- **AddressSanitizer (ASan) & UndefinedBehaviorSanitizer (UBSan)**: Configured `-Db_sanitize=address,undefined` build validation ensuring 100% memory safety with zero leaks and undefined behavior.
- **Structured Diagnostic Logging Framework (`PULS_LOG`)**: Microsecond-precision timestamped and ANSI-colorized logging (`PULS_LOG_DEBUG`, `PULS_LOG_INFO`, `PULS_LOG_WARN`, `PULS_LOG_ERROR`) controlled via `G_MESSAGES_DEBUG=puls`.
- **Modularized GResource Bundle**: Restructured internal assets into `/io/github/word_sys/puls_diskinfo/` (`css/`, `icons/`, `fixtures/`) with automated resource embedding.
- **CI Metadata Validation**: Integrated `desktop-file-validate` and `appstreamcli validate --no-net --pedantic` into automated test execution.

### Changed
- Standardized compiler warning flags to `-Wall -Wextra -Werror=implicit-function-declaration -Werror=return-type -Wformat=2` with 0 compiler warnings.
- Guarded GTK4 display initialization with `gtk_init_check()`, gracefully failing with descriptive errors instead of crashing when no display server is found.
- Updated FreeDesktop categories to `Categories=System;Monitor;`.

---

## [1.1.2] - 2026-07-20

### Added
- Full Turkish language localization with in-app toggle button in the header bar.
- Persistent language and theme settings across sessions.

### Fixed
- Addressed potential crashing issues during device hotplugging.
- Improved disk benchmark tools and UI responsiveness.

---

## [1.1.1] - 2026-07-01

### Added
- Health score percentage and remaining lifetime indicators inspired by Hard Disk Sentinel.
- Circular temperature history telemetry buffer.
- Non-destructive read-only surface scan and seek latency testing.
- S.M.A.R.T. attribute trend tracking and dynamic UI layout adjustments.
- Comprehensive HTML and JSON diagnostic report generation and file export.

---

## [1.1.0] - 2026-06-26

### Added
- LVM and LUKS volume mapping resolution.
- Expanded drive detail fields (device path, wear level, unsafe shutdowns, read ahead, scheduler, write cache, TRIM).
- Dual decimal and hexadecimal raw attribute value display in S.M.A.R.T. table.

### Fixed
- Benchmarking directory security bounds checking.
- Card hover overlay and contrast visual issues.

---

## [1.0.0] - 2026-06-12

### Added
- Initial release of PULS DiskInfo.
- Modern GTK4 and Libadwaita storage health monitoring application.
- S.M.A.R.T. attribute display for ATA/SATA drives.
- NVMe health information and telemetry log decoding.
- Dynamic thermometer gauge widget for real-time drive temperatures.
- Color-coded health indicators (Good / Caution / Bad).
- Polkit privilege separation via `puls-diskinfo-helper`.
