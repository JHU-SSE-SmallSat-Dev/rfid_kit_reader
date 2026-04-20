# RFID Kit Manager

Inventory and programming system for the SparkFun Simultaneous RFID Reader
(M7E Hecto / ThingMagic). Encodes part numbers and kit numbers directly into
the EPC bank of standard Gen2 RFID tags, with a browser-based GUI and
command-line tools for all operations.

---

## Project Structure

```
rfid_kit_reader\
├── src\
│   ├── rfid_common.h         shared constants, EPC encode/decode, write logic
│   ├── rfid_kit_reader.c     scan tags, print table, optionally write CSV
│   ├── rfid_tag_writer.c     interactive two-phase tag writer
│   ├── rfid_server.c         HTTP server backend for the GUI
│   ├── rfid_diag.c           diagnostic tool (temperature, power)
│   └── generate_test_data.c  fake CSV generator (no hardware needed)
├── lib\
│   └── mercuryapi-1.37.2.24\ MercuryAPI C source (required version)
├── bin\                       compiled executables (created by build)
├── output\                    CSV exports
├── rfid_gui.html              browser GUI (place in project root)
└── build.bat                  build script
```

---

## Prerequisites

- **MinGW-w64 GCC** from https://winlibs.com/ (GCC 15.x recommended)
  Extract to e.g. `C:\mingw64` and add `C:\mingw64\bin` to PATH.
  Verify: `gcc --version`

- **MercuryAPI 1.37.2.24** C source in `lib\mercuryapi-1.37.2.24\`
  Must be this version — 1.31.x is incompatible with M7E Hecto firmware.

---

## Build

```bat
build.bat
```

Compiles all five executables into `bin\`. On success, prints usage instructions.

---

## GUI Quickstart (Recommended)

1. Run `bin\rfid_server.exe` — the browser opens `rfid_gui.html` automatically.
2. Enter your COM port (e.g. `COM5`) and RF power, click **Connect**.
3. Hold tags near the antenna, click **Scan Tags** — detected tags appear in the table.
4. Enter a part number (0–9999), click **Write Part** (Phase 1).
5. Group tags into a kit, enter a kit number, click **Write Kit** (Phase 2).
6. Click **Export CSV** to save the current table to `output\`.

---

## GUI Reference

### Connection Panel (left sidebar)

| Field | Description |
|---|---|
| COM Port | Windows serial port, e.g. `COM5` |
| RF Power | In centidBm. Default 2700 = 27.00 dBm (max). USB safe max is 2200 = 22.00 dBm. External 5V 1A supply required above 22 dBm. |

Clicking **Connect** runs a quick test scan to verify the reader is responsive,
then stores the port and power for all subsequent operations. It retries once
after a 1-second delay if the port hasn't fully released from a previous command.

### Tag Table

Persists across scans — tags accumulate until you click **Clear**. Each new scan
merges into the existing table; existing tags get their read count incremented
and RSSI updated if higher.

| Column | Description |
|---|---|
| Status | FULL (part+kit written), PART (part only), RAW (unprogrammed), LOCKED (write-protected) |
| Part # | 4-digit part number, or — if unset |
| Kit # | 4-digit kit number, or — if unset |
| EPC | 24-character hex EPC string |
| RSSI | Signal strength in dBm |
| Reads | How many scan operations have seen this tag |

**Sort buttons** — click Part #, Kit #, RSSI, or Reads to sort ascending;
click again to sort descending. Active sort shows an arrow indicator.

**Clear** — wipes the table completely. Tags will reappear on next scan.

### Write Results Panel

Appears after each write operation. Shows:

- **Summary bar** — Detected / Written / Errors / Skipped counts at a glance.
- **Per-tag table** — one row per tag:

| Column | Description |
|---|---|
| Result | OK (green), FAIL (red), SKIP (grey) |
| Old | Previous value before overwrite, or — if unset |
| New | New value written, or — if failed/skipped |
| EPC | Tag EPC (new EPC shown for OK, original for FAIL) |
| Detail | "Wrote successfully", "Failed after 3 attempts", "Already correct — skipped", etc. |

Writing does **not** add tags to the scan table — only an explicit Scan does.
The post-write rescan is silent and only updates the write results panel.

### Operation Log

Full raw output from the reader/writer subprocess, color-coded:
- Cyan — status/progress lines
- Green — successful writes
- Red — errors
- Yellow — warnings
- Grey — skipped / settling / informational

### Drag Handles

Two 5px drag bars sit between the panels. Grab and drag vertically to
redistribute space between the tag table, write results panel, and log.
Minimum panel height is 40px. Heights auto-reset on window resize.

### Export CSV

Exports exactly what is in the tag table (including read counts, accumulated
across all scans since last Clear). Does **not** re-run the reader.

CSV columns: `Kit Number, Part Number, EPC, RSSI (dBm), Read Count`

---

## EPC Layout

Tags use the standard 12-byte (96-bit) Gen2 EPC bank:

```
Byte 0-1   Part Number   big-endian uint16, range 0-9999, 0xFFFF = unset
Byte 2-3   Kit Number    big-endian uint16, range 0-9999, 0xFFFF = unset
Byte 4-11  Serial        8 random bytes, assigned once in Phase 1, never changed
```

---

## Command-Line Tools

### rfid_kit_reader.exe

Scans tags and prints a table. Writes a CSV to `output\` unless `--no-csv` is passed
(the GUI always passes `--no-csv` — CSV is only written via Export CSV).

```
bin\rfid_kit_reader.exe COM5
bin\rfid_kit_reader.exe COM5 --power 2200
bin\rfid_kit_reader.exe COM5 --power 2700 --no-csv
```

Output columns: `EPC  Part#  Kit#  RSSI`

### rfid_tag_writer.exe

Interactive two-phase writer. Prompts for input at each step.

```
bin\rfid_tag_writer.exe COM5           full interactive flow
bin\rfid_tag_writer.exe COM5 --phase1  Phase 1 only (write part numbers)
bin\rfid_tag_writer.exe COM5 --phase2  Phase 2 only (write kit numbers)
```

**Phase 1 — Assign Part Numbers**
Place tags for a single part in the RF field. Enter the part number, press
Enter to scan and write. Repeat for each part. Type `done` to exit.

**Phase 2 — Assign Kit Numbers**
Place all tags for one kit in the field. Enter the kit number, press Enter
to write. Tags must already have a part number from Phase 1. Repeat for each
kit. Type `done` to exit.

Write sequence per tag:
1. Scan all tags (3 passes × 1s)
2. Wait 3s pre-write settling (tags reset to Gen2 state A)
3. Write EPC bank with filter targeting the specific tag
4. Verify with a 1.5s filtered read
5. Retry up to 3 times on failure; blacklist permanently failing tags

### rfid_diag.exe

Reports reader temperature and confirms the connection. Used by the GUI's
status poll to populate the temperature display.

```
bin\rfid_diag.exe COM5
```

### generate_test_data.exe

Generates synthetic CSV files in `output\` with random EPCs and part/kit
numbers. Useful for testing the GUI without hardware.

```
bin\generate_test_data.exe
```

---

## HTTP Server API

`rfid_server.exe` listens on `localhost:8765`. All RFID operations are
performed by spawning subprocesses — the server embeds no MercuryAPI code,
which avoids threading and blocking issues entirely.

| Method | Endpoint | Body | Description |
|---|---|---|---|
| GET | /status | — | Connection state, port, power, temperature |
| POST | /connect | `{"port":"COM5","power":2700}` | Verify port + store settings |
| POST | /disconnect | — | Clear connection state |
| POST | /scan | — | Run kit_reader, return tag JSON + raw log |
| POST | /write/part | `{"part":42}` | Run tag_writer --phase1, return log |
| POST | /write/kit | `{"kit":7}` | Run tag_writer --phase2, return log |
| POST | /csv | `{"tags":[...]}` | Write GUI table to CSV file |
| GET | /csv | — | Download most recently written CSV |

A 500ms delay is inserted before each operation to allow the COM port to
fully release from the previous subprocess.

---

## Write Constants (rfid_common.h)

| Constant | Value | Description |
|---|---|---|
| `DEFAULT_READ_POWER_CDBM` | 2700 | 27.00 dBm — maximum for M7E Hecto |
| `ANTENNA_PORT` | 1 | Onboard PCB antenna |
| `SCAN_PASSES` | 3 | Inventory passes per scan operation |
| `SCAN_PASS_MS` | 1000 | Duration of each pass in milliseconds |
| `PRE_WRITE_WAIT_MS` | 3000 | Settling time before writing begins |
| `WRITE_RETRIES` | 3 | Attempts per tag before blacklisting |
| `VERIFY_MS` | 1500 | Filtered read duration for write verification |

---

## Hardware Notes

| Topic | Detail |
|---|---|
| USB power limit | ~500mA. Reader browns out above ~22 dBm (2200 cdBm) on USB power alone. |
| External supply | 5V 1A via PTH header + USB-UART adapter for serial. Required for full 27 dBm. |
| Onboard antenna | PCB patch — approximately 0.5m range at full power. |
| External antenna | uFL connector on board. Up to ~5m with a suitable patch antenna. |
| Power mode | Set to `TMR_SR_POWER_MODE_FULL` on connect. Prevents sleep/wake preamble delays that would cause timeouts between commands. |
| Temperature | Requires `TMR_READER_STATS_FLAG_ANTENNA_PORTS` alongside `TMR_READER_STATS_FLAG_TEMPERATURE` — antenna flag is mandatory or the reader ignores the request. |
| I/O errors | Usually means the COM port hasn't released from a previous process. The server adds a 500ms delay + one retry to handle this automatically. |

---

## Key Design Decisions

**C instead of Java** — The Java MercuryAPI native serial library crashed on
modern Windows due to incompatible native bindings. The C API uses
`serial_transport_win32.c` with native Windows serial APIs and is stable.

**MercuryAPI version 1.37.2.24** — Required for M7E Hecto firmware
compatibility. Version 1.31.x is incompatible.

**EPC bank instead of User Memory** — User Memory writes had ~50% failure
rates on cheap Gen2 tags. EPC bank is mandatory on all Gen2 tags and far
more reliable.

**Subprocess architecture for server** — Embedding MercuryAPI in a
multithreaded HTTP server caused deadlocks (`TMR_read` blocks all threads
internally). The server instead spawns `rfid_kit_reader.exe` and
`rfid_tag_writer.exe` as subprocesses and parses their text output.
This is completely reliable — no threading or API locking issues possible.

**Silent post-write rescan** — After a write operation the server runs
a rescan to verify tag state, but the GUI suppresses merging these results
into the persistent tag table to prevent write-time EPCs from polluting
the inventory view.
