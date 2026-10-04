![Dil TR](https://github.com/word-sys/puls-diskinfo/blob/main/README_TR.md)

<p align="left">
  <img src="https://raw.githubusercontent.com/word-sys/puls/main/pulsd_icon.svg" width="220" height="220" alt="PULS-DiskInfo Icon"/>
</p>

# PULS DiskInfo

A highly detailed storage health and S.M.A.R.T. monitoring application for Linux, built with **GTK4** and **C**. Inspired by CrystalDiskInfo, it displays critical health status, temperatures, and detailed drive attributes.

<p align="center">
  <img src="https://raw.githubusercontent.com/word-sys/puls-diskinfo/main/screenshots/ss1.png" width="49%">
  <img src="https://raw.githubusercontent.com/word-sys/puls-diskinfo/main/screenshots/ss2.png" width="49%">
</p>
<p align="center">
  <img src="https://raw.githubusercontent.com/word-sys/puls-diskinfo/main/screenshots/ss3.png" width="49%">
  <img src="https://raw.githubusercontent.com/word-sys/puls-diskinfo/main/screenshots/ss4.png" width="49%">
</p>

---

## Features

- **Multi-Drive Support**: Enumerate and monitor multiple drives (NVMe SSD, SATA SSD, SSHD, HDD, USB, etc.).
- **Health Indicators**: Clear, color-coded health badges (Good, Caution, Bad) based on S.M.A.R.T. attributes.
- **Real-Time Temperature**: Dynamic temperature thermometer gauge widget with safe ranges.
- **S.M.A.R.T. Attributes Table**: Color-coded, sortable list of all standard attributes.
- **NVMe Specific Logs**: Comprehensive health metrics for NVMe SSDs (available spare, percentage used, media errors, etc.).
- **Polkit Integration**: Clean privilege separation. The main GUI runs unprivileged, calling a small, auditable helper executable via `pkexec` when querying S.M.A.R.T. data.

---

## Build Dependencies (Ubuntu 22.04+)

Ensure you have the following packages installed:

```bash
sudo apt update
sudo apt install -y \
  build-essential \
  meson \
  ninja-build \
  pkg-config \
  libgtk-4-dev \
  libadwaita-1-dev \
  libjson-glib-dev \
  smartmontools
```

---

## How to Compile & Run

### 1. Configure Build
Set up the build directory using Meson:
```bash
meson setup build
```

### 2. Compile
Compile the application:
```bash
meson compile -C build
```

### 3. Run Locally
Run the compiled binary directly:
```bash
./build/src/puls-diskinfo
```

---

## Installation & Packaging

### Option A: Portable Release Tarball
You can generate a self-contained release package with helper binaries, desktop integration, and install/uninstall scripts:

```bash
# Build the portable tarball
./packaging/build-binary.sh
```
The tarball will be saved in `dist/puls-diskinfo-1.1.2-linux-x86_64.tar.gz`.

To install from the tarball:
```bash
tar xzf dist/puls-diskinfo-1.1.2-linux-x86_64.tar.gz
cd puls-diskinfo-1.1.2-linux-x86_64
sudo ./install.sh
```

---

## License

This project is licensed under the GPL-3.0-or-later License - see the `LICENSE` file for details.
