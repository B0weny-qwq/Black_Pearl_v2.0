# Ship Packet Builder

This helper builds legacy ship protocol packets for:

- `0x13` return-home
- `0x14` goto-point
- `0x15` return-switch + save return point

It matches the current firmware after we aligned point parsing back to the old ship behavior:

- `0x12` GPS report uses legacy raw point bytes
- `0x13/0x14/0x15` now read those point bytes the same way

## Quick Start

### 1. Build a `0x15` save-point packet directly from a `0x12` GPS report

```powershell
python tools\ship_packet_builder\build_ship_packet.py from-gps-report `
  --cmd return-switch `
  --switch 0x31 `
  --report "08 5A 00 45 AD 2F A2 8A 4E 8C CB 58 01 00 00"
```

Notes:

- `--switch 0x31` is a recommended test value for "auto return enabled"
- `--report` can be either:
  - the 15-byte `0x12` payload only
  - or the full `AA ... BB` frame

### 2. Build a packet from manual point fields

```powershell
python tools\ship_packet_builder\build_ship_packet.py from-point `
  --cmd return-switch `
  --switch 0x31 `
  --lon-dir E --lon-whole 12205 --lon-frac 47538 `
  --lat-dir N --lat-whole 3731 --lat-frac 52206
```

You can also generate:

- `--cmd return-home` for `0x13`
- `--cmd goto-point` for `0x14`

## Payload Meaning

Legacy point format is 10 bytes:

```text
lon_dir
lon_whole[low][high]
lon_frac[low][high]
lat_dir
lat_whole[low][high]
lat_frac[low][high]
```

`0x15` payload is:

```text
switch
+ 10-byte point
```

## Output

The script prints:

- payload hex
- full frame hex
- C array payload
- C array frame

## Recommended Field Test Flow

1. Put the boat at the place you want to save as the return point.
2. Read one valid `0x12` GPS report from your tool/log/sniffer.
3. Run `from-gps-report --cmd return-switch --switch 0x31`.
4. Send the generated `0x15` frame to the boat.
5. Power-cycle once if you want to confirm flash persistence.
6. Move the boat more than 10 meters away.
7. Test return by sending `0x13` or by link timeout / low-power trigger.

## Important Notes

- A return point closer than 10 m will not activate auto-drive.
- A return point farther than 800 m will not activate auto-drive.
- `0x30` is treated as the default "not active" switch value in current firmware.
- If your current GPS is invalid or satellites are below 7, the point may save but return will not start.
