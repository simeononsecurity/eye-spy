# Firmware-Derived Flock Safety Signatures (eye-spy)

**Provenance:** extracted from a Flock Safety ALPR camera firmware image
(Qualcomm MSM8953 + QCA9377 radio, Android 8.1, codename `hpnotiq`),
analyzed 2026-09-16. Upstream source of this research:
`colonelpanichacks/flock-you`.

The sibling [flock-you-esp32](https://github.com/simeononsecurity/flock-you-esp32)
project carries the full provenance table (file names, internal sources, key
characteristics) in its own
`datasets/firmware_derived_signatures.md`. This file records the **same
signature set as it is represented in eye-spy**, plus the places eye-spy's
architecture differs.

## Where each signature lives here

| Signature | eye-spy constant | Detector / scoring |
|---|---|---|
| OUI `b4:1e:52` (Flock Safety MA-L) | `FLOCK_OUIS[]` | `PTS_FLOCK_OUI` = 5 |
| OUI `00:03:7f` (Qualcomm Atheros QCA9377) | `FLOCK_MFR_OUIS[]` | `PTS_FLOCK_MFR_OUI` = 2 (low tier — see below) |
| Factory-default MACs `00:03:7f:50:00:01`, `00:03:7f:4f:00:16` | `FLOCK_FW_DEFAULT_MACS[]` + `fwDefaultMacMatch()` | `PTS_FW_DEFAULT_MAC` = 6 |
| SSID keyword `fs ext` (covers `FS Ext Battery`) | `FLOCK_SSID_KW[]` | `PTS_FLOCK_SSID` = 5 |
| BLE name `DfuTarg` | `FLOCK_BLE_NAMES[]` | `PTS_FLOCK_BLE` = 5 |
| BLE name shapes (`Penguin-`+10 digits, bare 10-digit serial, `FS Ext Battery`, `DfuTarg`) | `bleNameShapeMatch()` | `PTS_FLOCK_BLE` = 5 |
| BLE mfr company ID `0x09C8` (XUNTONG) | checked inline in `onResult()` | `PTS_FLOCK_BLE_MFR` = 5 |
| Flock accessory service `e8ccbb38-9532-46a8-9fe5-1814df172e6f` | `FLOCK_GATT_UUIDS[0]` | `PTS_FLOCK_GATT` = 5 |
| Nordic legacy DFU service `00001530-1212-efde-1523-785feabcd123` | `FLOCK_GATT_UUIDS[1]` | `PTS_FLOCK_GATT` = 5 |
| Raven services `0x3100`–`0x3500` (range) | `RAVEN_SVC_MIN`/`RAVEN_SVC_MAX` + `ravenUuidInRange()` | `PTS_RAVEN_BLE` = 5 |
| Classic BT names / SDP Device-ID | `api/eyespy.py` only: `CLASSIC_BT_DEVICE_NAMES` + `CLASSIC_BT_SDP_DEVICE_ID` | tagged `classic_bt_name:…` / `classic_bt_sdp_did:qualcomm_001d_1200` — **not implementable on the ESP32**, whose NimBLE stack has no Classic BT |

## Differences from flock-you-esp32 worth knowing

- **Detection surface.** flock-you matches `addr2` on *promiscuously sniffed*
  frames; eye-spy's WiFi engines run against **active-scan results**, so the
  firmware-default MAC check is applied to a scanned **BSSID**
  (`processWifiScan()`) rather than to a frame transmitter. Same signature,
  different collection method.
- **`00:03:7f` sits in the low (mfr) tier here too**, for the same reason: it is
  a chipset vendor's prefix shared with a huge installed base of unrelated
  Atheros gear. Only the two *exact* factory-default addresses are strong, and
  they are matched byte-for-byte.
- **`PTS_FW_DEFAULT_MAC` = 6 is the only WiFi signal that can reach
  `SCORE_ALERT` on its own.** Every other eye-spy engine is a pattern unrelated
  hardware can also produce; a device transmitting from a factory-default radio
  address has never been provisioned. This is a deliberate, documented
  exception to the multi-signal philosophy — see `.clinerules/04-detection-methods.md`.
- **The `onResult()` BLE callback stays flag-only** (no `Serial.print`); the
  `flockGatt`/`ravenBle` log lines come from `CHECK_DET()` in loop context.
- **API-side tagging reads the firmware's text log, not JSON.** `api/eyespy.py`
  derives tags from the parsed `oui` field and the `detection_method` string
  (plus `ssid`), so the exact factory-default MAC is tagged via the
  `Flock-FW-default MAC` method rather than by re-checking a MAC the API never
  sees in full. Consequence worth knowing: eye-spy's BLE log line carries the
  classification but *not* the advertised device name, so the
  `ble_name:penguin_serial` / `bare_serial` / `dfutarg` tags can only be
  produced for **imported** records that include a `device_name` field — not
  from eye-spy's own live serial stream.
- The Raven range sweep and the Flock-GATT table are **separate** detectors on
  purpose: the Flock accessory service is not a Raven service, and reporting it
  as one would mislabel it on the dashboard.

## Status

Build-verified across every eye-spy environment, and unit-tested (46 host-side
tests, including all-six-byte matching, name shapes, and the range parser), but
**not yet observed against a live camera** — field coverage is unmeasured.
