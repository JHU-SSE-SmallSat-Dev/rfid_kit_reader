# RFID Kit Manager

Inventory and programming system for the SparkFun Simultaneous RFID Reader (M7E Hecto / ThingMagic). Encodes part numbers and kit numbers directly into the EPC bank of standard Gen2 RFID tags, with a browser-based GUI and command-line tools for all operations.

The intended use case is kit management: each physical hardware component is tagged with an RFID sticker. Kits can be scanned in seconds, compared against a Bill of Materials (BOM), and missing parts identified for reorder or tracking.

---

## Dependencies

**Required to build** (compile the C source code):
- **MinGW-w64 GCC** — [winlibs.com](https://winlibs.com/), GCC 15.x recommended
- **MercuryAPI 1.37.2.24** C source — must be this exact version

**Required to run** the GUI and command-line tools:
- Any modern web browser (Chrome, Firefox, Edge)
- SparkFun Simultaneous RFID Reader (M7E Hecto) + USB-UART serial adapter

**Required to run the BOM checker** (optional feature):
- **Python 3.10+** — [python.org](https://www.python.org/downloads/)
- **pandas 1.3+** — `pip install pandas`
- **openpyxl 3.0+** — `pip install openpyxl`

```
pip install pandas openpyxl
```

---

## Getting Started

### Step 1 — Install MinGW-w64 (C Compiler)

Required to compile the C source code.

1. Go to **https://winlibs.com/**
2. Download the latest GCC release. Select:
   - Architecture: **x86_64**
   - Threads: **Win32**
   - Exception: **SEH**
   - Runtime: **UCRT**
3. Extract to a simple path — e.g. `C:\mingw64`
4. Add GCC to your PATH:
   - Open Start → search **Environment Variables**
   - Under **System variables**, select **Path** → **Edit**
   - Click **New** and add `C:\mingw64\bin`
   - Click OK on all dialogs
5. Open a **new** Command Prompt and verify:
   ```
   gcc --version
   ```
   Should print something like: `gcc (MinGW-w64 ...) 15.x.x`

---

### Step 2 — Install Python 3 (for BOM Checker)

Required to read `BOM.xlsx` and power the BOM verification feature. Skip if you will not use the BOM checker.

1. Go to **https://www.python.org/downloads/**
2. Download and run the installer for **Python 3.10 or newer**
3. **Important:** On the first screen, check **"Add Python to PATH"** before clicking Install
4. Verify in a new Command Prompt:
   ```
   python --version
   ```
   Should print: `Python 3.x.x`

5. Install required Python packages:

   ```
   pip install pandas openpyxl
   ```

   | Package | Min Version | Purpose |
   |---|---|---|
   | `pandas` | 1.3+ | Reads and parses `BOM.xlsx` into a structured table. Used by `bom_export.py` to extract part numbers, descriptions, quantities, and order links. |
   | `openpyxl` | 3.0+ | Required by pandas to open `.xlsx` format Excel files. Without this, pandas cannot read `BOM.xlsx` at all. |

   Verify both installed correctly:
   ```
   python -c "import pandas; import openpyxl; print('OK')"
   ```

   > **Note:** The BOM checker will show "BOM not found" or fail silently if either package is missing. If Python itself is not on PATH, the server will log an error when Check BOM is clicked.

---

### Step 3 — Install MercuryAPI 1.37.2.24

The MercuryAPI C source must be present to compile the RFID tools. **Version 1.37.2.24 is required** — version 1.31.x is not compatible with the M7E Hecto firmware.

1. Obtain `mercuryapi-1.37.2.24` from ThingMagic / Jadak support or your existing installation
2. Place the extracted folder at:
   ```
   rfid_kit_reader\lib\mercuryapi-1.37.2.24\
   ```
3. Verify by checking that this file exists:
   ```
   rfid_kit_reader\lib\mercuryapi-1.37.2.24\c\src\api\tm_reader.h
   ```

---

### Step 4 — Place Project Files

Your project folder should look like this before building:

```
rfid_kit_reader\
├── src\
│   ├── rfid_common.h
│   ├── rfid_kit_reader.c
│   ├── rfid_tag_writer.c
│   ├── rfid_server.c
│   ├── rfid_diag.c
│   └── generate_test_data.c
├── lib\
│   └── mercuryapi-1.37.2.24\       ← from Step 3
├── files\
│   └── BOM.xlsx                     ← your Bill of Materials
├── rfid_gui.html
├── bom_export.py
├── build.bat
└── README.txt
```

Create the `files\` folder if it does not exist and place your `BOM.xlsx` inside it. The BOM must have a column named **"RFID Part Number"**.

---

### Step 5 — Build

Open a Command Prompt in the `rfid_kit_reader\` folder and run:

```
build.bat
```

This compiles all five executables into `bin\`:

```
bin\rfid_kit_reader.exe
bin\rfid_tag_writer.exe
bin\rfid_server.exe
bin\rfid_diag.exe
bin\generate_test_data.exe
```

A successful build prints usage instructions at the end. If you see `FAILED:`, check that GCC is on your PATH (Step 1) and that the MercuryAPI source is in the correct location (Step 3).

---

### Step 6 — Run the GUI

From the `rfid_kit_reader\` folder, run:

```
bin\rfid_server.exe
```

The server starts on `localhost:8765` and automatically opens `rfid_gui.html` in your default browser.

- If the browser does not open, open `rfid_gui.html` manually
- If the server reports "Port 8765 in use", another instance is already running — close it in Task Manager

---

## Quick Workflow

### First-Time Tag Programming

1. Connect the reader to USB and serial adapter, note the COM port
2. Run `bin\rfid_server.exe`, browser opens
3. Enter COM port and power, click **Connect**
4. Place a batch of tags for one part number near the antenna
5. Enter the part number (e.g. `0001`), click **Write Part** — tags are scanned, settled, written, and verified
6. Repeat steps 4–5 for each part number
7. Place all tags for one kit near the antenna
8. Enter the kit number (e.g. `0001`), click **Write Kit**
9. Repeat steps 7–8 for each kit

### Kit Inventory / BOM Check

1. Place one kit near the antenna, click **Scan Tags** — all tags appear in the table
2. Repeat for each kit being checked (tags accumulate across scans)
3. Click **Check BOM** — a report tab opens showing pass/fail per kit, missing parts, and a master parts list
4. Click **Export XLSX** in the report tab to save the full report

Or — import a previously exported CSV instead of scanning:

1. Click **Import CSV**, select your CSV file
2. Click **Check BOM**

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
├── files\
│   └── BOM.xlsx               your Bill of Materials
├── output\                    CSV exports
├── rfid_gui.html              browser GUI
├── bom_export.py              Python helper: reads BOM.xlsx for the GUI
├── build.bat                  build script
└── README.txt                 plain-text version of this file
```

---

## GUI Reference

### Connection Panel

| Field | Description |
|---|---|
| COM Port | Windows serial port, e.g. `COM5`. Find yours in Device Manager → Ports (COM & LPT). |
| RF Power | In centidBm. Default `2700` = 27.00 dBm (max). USB safe max is `2200` = 22.00 dBm. External 5V 1A supply required above 22 dBm. |

Clicking **Connect** runs a quick test scan to verify the reader is responsive, then stores the port and power for all subsequent operations. It retries once after a 1-second delay if the port has not fully released from a previous command.

### Tag Table

Persists across scans — tags accumulate until you click **Clear**. Each new scan merges into the existing table: read count increments and RSSI updates if the new reading is stronger.

| Column | Description |
|---|---|
| Status | **FULL** — has both part # and kit #, ready for inventory. **PART** — has part # only, needs Write Kit (Phase 2). **RAW** — blank tag, needs Write Part (Phase 1). **LOCKED** — failed all 3 write attempts, tag is defective or write-protected. Hover over any badge for a full description. |
| Part # | 4-digit part number, or `—` if unset |
| Kit # | 4-digit kit number, or `—` if unset |
| EPC | 24-character hex EPC string (unique per physical tag) |
| RSSI | Signal strength in dBm (higher = closer/stronger) |
| Reads | How many scan operations have detected this tag |

**Sort buttons** — click Part #, Kit #, RSSI, or Reads to sort; click again to reverse. **Clear** wipes the table.

### Write Results Panel

Appears after each write operation. Shows a summary bar (Detected / Written / Errors / Skipped) and a per-tag table with one row per tag:

| Column | Description |
|---|---|
| Result | OK (green), FAIL (red), SKIP (grey) |
| Old | Previous value before overwrite |
| New | New value written |
| EPC | New EPC for successful writes, original EPC for failures |
| Detail | "Wrote successfully", "Failed after 3 attempts", "Already correct — skipped", etc. |

Writing does **not** add tags to the scan table — only an explicit Scan does.

### BOM Verification

Compares every kit in the tag table against the full `files\BOM.xlsx`. Clicking **Check BOM** opens a report in a new browser tab with three views:

- **Master Parts List** — one row per BOM part, showing how many kits have it and which kit numbers are missing it
- **Kit by Kit** — all kits with full pass/fail detail, missing parts listed first
- **Failing Kits** — only failing kits, showing only missing/extra parts (red badge shows count)

The **Export XLSX** button downloads a workbook with the Master Parts List on Sheet 1 and one sheet per kit. The **Print** button prints the full report.

### Import / Export CSV

| Button | Description |
|---|---|
| Export CSV | Saves the current tag table to `output\`. Columns: Kit Number, Part Number, EPC, RSSI (dBm), Read Count. Exactly matches what is visible in the table. |
| Import CSV | Loads a previously exported CSV into the tag table. Useful for BOM checks without the reader connected. Merges with existing table data. |

### Drag Handles

Thin bars between the right-side panels can be dragged vertically to resize the tag table, write results panel, BOM results panel, and log. Minimum panel height is 40px.

---

## EPC Layout

Tags use the standard 12-byte (96-bit) Gen2 EPC bank:

```
Byte 0-1   Part Number   big-endian uint16, range 0–9999, 0xFFFF = unset
Byte 2-3   Kit Number    big-endian uint16, range 0–9999, 0xFFFF = unset
Byte 4-11  Serial        8 random bytes, assigned once in Phase 1, never changed
```

Each EPC is globally unique. Assigning a kit number preserves the serial so a tag can always be identified even if reprogrammed.

---

## Command-Line Tools

### rfid_kit_reader.exe

Scans tags and prints a table. Writes a CSV to `output\` unless `--no-csv` is passed (the GUI always passes `--no-csv`).

```
bin\rfid_kit_reader.exe COM5
bin\rfid_kit_reader.exe COM5 --power 2200
bin\rfid_kit_reader.exe COM5 --power 2700 --no-csv
```

### rfid_tag_writer.exe

Interactive two-phase writer.

```
bin\rfid_tag_writer.exe COM5            full interactive flow
bin\rfid_tag_writer.exe COM5 --phase1   Phase 1 only (part numbers)
bin\rfid_tag_writer.exe COM5 --phase2   Phase 2 only (kit numbers)
```

Write sequence per tag: scan (3×1s passes) → 3s settle → write → 1.5s verify → retry up to 3× → blacklist on failure.

### rfid_diag.exe

Reports reader temperature and confirms the connection.

```
bin\rfid_diag.exe COM5
```

### generate_test_data.exe

Generates synthetic CSVs in `output\` for testing without hardware. All EPCs are unique with proper EPC layout.

```
bin\generate_test_data.exe
```

Outputs: `test_short_*.csv` (3 kits × 4 parts) and `test_long_*.csv` (50 kits × 40 parts).

---

## Troubleshooting

**"gcc not found" when building**
GCC is not on your PATH. Redo Step 1, add `C:\mingw64\bin` to PATH, then open a new Command Prompt.

**"Cannot find tm_reader.h" when building**
MercuryAPI is missing or in the wrong folder. Check: `lib\mercuryapi-1.37.2.24\c\src\api\tm_reader.h`

**Server starts but browser does not open**
Open `rfid_gui.html` manually in your browser.

**"Port 8765 already in use"**
Open Task Manager, find `rfid_server.exe`, and end it.

**Connect fails with "Input/output error"**
COM port is held by a previous process. Wait 2–3 seconds and try again.

**BOM checker shows "BOM not found" or fails**
Ensure `files\BOM.xlsx` exists and Python + both packages are installed:
```
python -c "import pandas; import openpyxl; print('OK')"
python bom_export.py files\BOM.xlsx
```
If `import` fails, run `pip install pandas openpyxl`. If Python is not found at all, redo Step 2 and open a new Command Prompt after installing.

**Tags not detected during scan**
Check the reader power switch. Try higher power (2700 cdBm). Use an external 5V 1A supply. Hold tags within 0.5m of the onboard antenna.

**Write fails / tags show LOCKED**
Tag may be defective or read-only. Try closer range with higher power. Some cheap Gen2 tags have unreliable EPC bank writes — try a different batch.

---

## Hardware Notes

| Topic | Detail |
|---|---|
| USB power limit | ~500mA. Reader browns out above ~22 dBm on USB power alone. |
| External supply | 5V 1A via PTH header + USB-UART adapter. Required for reliable operation above 22 dBm. |
| Onboard antenna | PCB patch. ~0.5m range at full power. |
| External antenna | uFL connector on board. Up to ~5m with a suitable patch antenna. |
| Power mode | Set to `TMR_SR_POWER_MODE_FULL` on connect to prevent sleep/wake preamble delays. |
| Temperature | Requires `TMR_READER_STATS_FLAG_ANTENNA_PORTS` alongside `TMR_READER_STATS_FLAG_TEMPERATURE` or the reader ignores the request. |

---

## Write Constants (`rfid_common.h`)

| Constant | Value | Description |
|---|---|---|
| `DEFAULT_READ_POWER_CDBM` | 2700 | 27.00 dBm — maximum for M7E Hecto |
| `ANTENNA_PORT` | 1 | Onboard PCB antenna port |
| `SCAN_PASSES` | 3 | Inventory passes per scan operation |
| `SCAN_PASS_MS` | 1000 | Duration of each pass in milliseconds |
| `PRE_WRITE_WAIT_MS` | 3000 | Settling time before writing begins |
| `WRITE_RETRIES` | 3 | Attempts per tag before blacklisting |
| `VERIFY_MS` | 1500 | Filtered read duration for verification |

---

## Key Design Decisions

**C instead of Java** — The Java MercuryAPI native serial library crashed on modern Windows. The C API uses `serial_transport_win32.c` with native Windows serial APIs and is stable.

**MercuryAPI version 1.37.2.24** — Required for M7E Hecto firmware compatibility. Version 1.31.x fails to connect.

**EPC bank instead of User Memory** — User Memory writes had ~50% failure rates on cheap Gen2 tags. EPC bank is mandatory on all Gen2 tags and far more reliable.

**Subprocess architecture for the server** — Embedding MercuryAPI in a multithreaded HTTP server caused deadlocks (`TMR_read` blocks all threads internally). The server spawns `rfid_kit_reader.exe` and `rfid_tag_writer.exe` as subprocesses instead.

**Silent post-write rescan** — After a write the server rescans to verify tag state, but the GUI suppresses merging these results into the tag table to prevent write-time EPCs from polluting the inventory view.
