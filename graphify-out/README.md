# Graphify Notes

This directory is the local Graphify output for `Black_Pearl_v2.0-main`.

- `graph.json`: machine-readable graph used by `graphify query/explain/path`.
- `graph.html`: browser view of the graph.
- `GRAPH_REPORT.md`: auto-generated report. Do not hand-edit it; it is overwritten by `graphify update .`.
- `README.md`: human-maintained migration notes and v1.1 comparison.

Latest local update:

```powershell
C:\AgentHarness\bin\graphify.ps1 update .
```

Current graph snapshot after the return-origin persistence update:

```text
1621 nodes
3748 edges
157 communities
```

Useful queries:

```powershell
C:\AgentHarness\bin\graphify.ps1 query "NorthCalib state machine D double click parameter_store app_get_raw_heading_deg100" --budget 2500
C:\AgentHarness\bin\graphify.ps1 explain "NorthCalib_Poll"
C:\AgentHarness\bin\graphify.ps1 path "ship_protocol_handle_throttle" "NorthCalib_RequestStart"
C:\AgentHarness\bin\graphify.ps1 explain "app_get_heading_deg100"
```

## v1.1 Comparison

Behavior source:

```text
C:\Users\S\Desktop\STC_PROJECT\Black_Pearl_v1.1\Code_boweny\Device\AutoDrive\NorthCalib.c
C:\Users\S\Desktop\STC_PROJECT\Black_Pearl_v1.1\Code_boweny\Device\AutoDrive\NorthCalib.h
C:\Users\S\Desktop\STC_PROJECT\Black_Pearl_v1.1\Code_boweny\Device\AutoDrive\autodrive.c
C:\Users\S\Desktop\STC_PROJECT\Black_Pearl_v1.1\Code_boweny\Device\AutoDrive\autodrive_cfg.c
C:\Users\S\Desktop\STC_PROJECT\Black_Pearl_v1.1\Code_boweny\Device\WIRELESS\ship_protocol.c
C:\Users\S\Desktop\STC_PROJECT\Black_Pearl_v1.1\User\MainLoop.c
C:\Users\S\Desktop\STC_PROJECT\Black_Pearl_v1.1\doc\project_doc\gps_north_calibration_d_key.md
```

v2.0 landing points:

```text
App/Src/north_calib.c
App/Inc/north_calib.h
App/Src/ship_protocol_cmd.c
App/Src/ship_protocol_points.c
App/Src/ship_protocol_power.c
App/Src/app.c
App/Src/app_ahrs_poll.c
App/Src/autodrive_cmd.c
App/Src/autodrive_config.c
App/Src/autodrive_points.c
Services/Src/parameter_store.c
BoardDevices/Src/board_storage.c
doc/state_machines.md
```

| Area | v1.1 source behavior | v2.0 implementation | Status |
| --- | --- | --- | --- |
| State machine | `IDLE/CHECK_READY/ALIGN_NORTH/RUN_STRAIGHT/CALC/SAVE/DONE/FAILED` in `NorthCalib.c`. | Same state set in `App/Src/north_calib.c`. | Aligned |
| Trigger | `ship_protocol.c` detects D double click within `1000ms`, then calls `NorthCalib_RequestStart()`. | `ship_protocol_service_north_calib_key()` does the same in `ship_protocol_cmd.c`. | Aligned |
| Cancel | E key cancels while NorthCalib is busy. | E edge calls `NorthCalib_Cancel(NORTH_CALIB_FAIL_USER_CANCEL)` before cruise semantics. | Aligned |
| Readiness gates | GPS valid, satellite count >= 7, heading ready, remote online, AutoDrive/ShipControl idle. | Same gates in `NorthCalib_Poll()` `CHECK_READY`. | Aligned |
| Alignment | Request `ShipControl_RequestGpsAlign(0)`, exit at +/-5 deg or 20s fallback. | Same target and timeout behavior. | Aligned |
| Straight run | Request `ShipControl_RequestGpsNav(target, 500)`, run about 10m, min save distance 8m. | Same base speed, target distance, and min save distance. | Aligned |
| Quality gates | Yaw unstable, heading error, timeout, short distance, and offset jump > 45 deg fail without overwriting persisted record. | Same fail reasons and thresholds. | Aligned |
| Persistence | v1.1 writes raw 16-byte NCAL records to EEPROM slot A/B at `0x0200/0x0400`. | v2.0 writes through `parameter_store -> board_storage`; same A/B addresses, with wrapper records and fallback load for old raw 16-byte records. | Layered and compatible |
| Heading API | `MainLoop_GetRawHeadingDeg100()` returns raw fused heading; `MainLoop_GetHeadingDeg100()` returns raw + offset. | `app_get_raw_heading_deg100()` returns raw fused heading; `app_get_heading_deg100()` computes raw + `NorthCalib_GetHeadingOffsetCd()`. | Aligned |
| Motor ownership | NorthCalib and AutoDrive request ShipControl; final motor writes stay in ShipControl/Motor path. | NorthCalib and AutoDrive only call `ShipControl_RequestGpsAlign/Nav/Stop`; final write is `ShipControl_SetMotorTargets()`. | Aligned |
| Protocol `0x12` | Fixed 15-byte GPS/status payload; power and AutoDrive status remain at the tail. | `SHIP_GPS_REPORT_PAYLOAD_LEN` remains `15`; `payload[13]` is power, `payload[14]` is AutoDrive status. | Aligned |
| Busy guard | NorthCalib blocks manual motor update, `0x13/0x14/0x15`, and low-power return while calibration owns GPS heading control. | Same guard split across `ship_protocol_cmd.c`, `ship_protocol_points.c`, and `ship_protocol_power.c`. | Aligned |
| Fishing points | v1.1 documentation says fishing points are RAM-only and lost on power cycle; `0x14` provides the target from the remote. | `AutoDrive_SetFishPositionRaw()` stores only the current runtime target and duplicate debounce state; it no longer maintains a local 5-point table or writes fish points to flash. | Aligned to latest requirement |
| Return origin storage | v1.1 `autodrive_cfg.c` was a RAM config adapter prepared for later EEPROM/Flash. | v2.0 persists only the 10-byte return origin through `parameter_store -> board_storage`; `0x15` switch-only updates do not erase/write flash. | Narrowed by requirement |

## Graphify Evidence

The refreshed graph confirms the main call paths:

```text
ship_protocol_handle_throttle()
  -> ship_protocol_service_north_calib_key()
  -> NorthCalib_RequestStart()
```

`NorthCalib_Poll()` connects to:

```text
board_gps_get_state()
app_get_heading_ready()
app_get_heading_deg100()
ShipControl_RequestGpsAlign()
ShipControl_RequestGpsNav()
ShipControl_Stop()
NorthCalib_SaveRecord()
```

`app_get_heading_deg100()` connects to:

```text
app_wrap_heading_deg100()
NorthCalib_GetHeadingOffsetCd()
```

AutoDrive persistence is now intentionally narrow:

```text
AutoDrive_SetReturnPositionRaw()
  -> AutoDriveCfg_Save()
  -> parameter_store_save_autodrive()
  -> board_storage_write()

AutoDrive_SetFishPositionRaw()
  -> fish_position runtime target only
```

## Open Verification

Host checks that have passed:

```text
gen_board_resources.py
gcc -fsyntax-only for touched App/Services files
git diff --check
graphify update .
```

Full MinGW build is expected to stop before firmware completion because `Platform/Inc/stc32g.h` includes Keil/STC-specific `<intrins.h>`. Use the Keil/C251 project for final target build verification:

```text
RVMDK/STC32G-LIB.uvproj
```
