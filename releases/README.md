# Releases

Prebuilt firmware binaries and release notes for the NEXUS CSI Sensor.

Binaries are **not** committed to the repository (see [`.gitignore`](../.gitignore));
they are produced by CI and attached to tagged GitHub Releases. This directory
holds the release documentation and is where you place downloaded `.bin` files
when flashing a prebuilt image.

---

## What's in a release

Each tagged release publishes three artifacts, built by
[`.github/workflows/build.yml`](../.github/workflows/build.yml):

| File | Flash offset | Purpose |
|------|--------------|---------|
| `bootloader.bin` | `0x1000` | Second-stage bootloader. |
| `partition-table.bin` | `0x8000` | Partition table (matches [`partitions.csv`](../firmware/partitions.csv)). |
| `nexus_csi_sensor.bin` | `0x20000` | The application (OTA slot 0). |

- **First flash (over USB):** write all three at the offsets above - see
  [Installation §B](../docs/INSTALLATION.md#b-flash-a-prebuilt-binary).
- **Updating an already-running device:** upload **only** `nexus_csi_sensor.bin`
  through the dashboard's OTA page - see
  [Installation → OTA](../docs/INSTALLATION.md#updating-firmware-later-ota).

---

## Versioning

The project follows **semantic versioning** (`MAJOR.MINOR.PATCH`):

- **MAJOR** - breaking changes to the API, config layout, or partition table.
- **MINOR** - backwards-compatible features.
- **PATCH** - backwards-compatible fixes.

The firmware version is set in
[`firmware/CMakeLists.txt`](../firmware/CMakeLists.txt) (`PROJECT_VER`) and
mirrored by `NEXUS_FW_VERSION` in
[`app_config.h`](../firmware/main/app_config.h); it is reported at
`GET /api/status` (`fw`) and shown on the dashboard and TFT. Keep all three in
sync when cutting a release.

> **Config compatibility:** when a release changes `nexus_config_t`, bump
> `NEXUS_CONFIG_VERSION`. On upgrade the device detects the version mismatch and
> falls back to defaults rather than misreading an old blob - call this out in
> the changelog so users know to re-check their settings.

---

## Cutting a release (maintainers)

1. Update `PROJECT_VER`, `NEXUS_FW_VERSION`, and - if the config struct changed - 
   `NEXUS_CONFIG_VERSION`.
2. Add an entry to [CHANGELOG.md](CHANGELOG.md).
3. Tag the commit (`git tag vX.Y.Z && git push --tags`).
4. CI builds the artifacts; attach them to the GitHub Release.

See the full history in [CHANGELOG.md](CHANGELOG.md).
