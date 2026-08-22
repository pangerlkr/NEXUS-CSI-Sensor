# `components/` - reserved for future extraction

This directory is intentionally part of the project layout even though the
1.0.0 firmware keeps every module inside [`../main`](../main). ESP-IDF
automatically discovers any component placed here (a subdirectory containing a
`CMakeLists.txt` with `idf_component_register(...)`), so it is the natural home
for functionality that will later be split out of `main/` into independently
reusable, unit-testable components.

The [architecture roadmap](../../docs/ARCHITECTURE.md#11-future-ready-architecture)
lists the planned extensions that will live here:

| Planned component | Purpose |
|-------------------|---------|
| `mqtt_pub`        | Publish presence/motion state to an MQTT broker. |
| `home_assistant`  | MQTT discovery + native HA device integration. |
| `ble_provision`   | BLE-based Wi-Fi provisioning and status beacon. |
| `tinyml`          | On-device activity classification from CSI features. |
| `espnow_mesh`     | Multi-node ESP-NOW fusion between several sensors. |
| `cloud_bridge`    | Optional cloud telemetry / remote dashboard. |

## Extracting a module into a component

1. Create `components/<name>/` with `CMakeLists.txt` and the sources/headers.
2. In `CMakeLists.txt` call `idf_component_register(SRCS ... INCLUDE_DIRS ... REQUIRES ...)`.
3. Add the component name to the `REQUIRES`/`PRIV_REQUIRES` list of the module
   that consumes it (usually `main`).

No changes to the top-level `CMakeLists.txt` are needed - discovery is
automatic. See the [Developer Guide](../../docs/DEVELOPER.md) for conventions.
