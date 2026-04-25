================================================================================
  RFID KIT MANAGER
  SparkFun Simultaneous RFID Reader (M7E Hecto / ThingMagic)
================================================================================

Inventory and programming system for Gen2 RFID tags. Encodes part numbers and
kit numbers directly into the EPC bank of standard Gen2 tags, with a
browser-based GUI and command-line tools for all operations.

The intended use case is kit management: each physical hardware component is
tagged with an RFID sticker. Kits can be scanned in seconds, compared against
a Bill of Materials (BOM), and missing parts identified for reorder or tracking.


================================================================================
  DEPENDENCIES
================================================================================

  Required to BUILD (compile the C source code):
  - MinGW-w64 GCC (winlibs.com, GCC 15.x recommended)
  - MercuryAPI 1.37.2.24 C source (must be this exact version)

  Required to RUN the GUI and command-line tools:
  - Any modern web browser (Chrome, Firefox, Edge)
  - SparkFun Simultaneous RFID Reader (M7E Hecto) + serial adapter

  Required to RUN the BOM checker (optional feature):
  - Python 3.10 or newer  (python.org)
  - pandas  1.3+          (pip install pandas)
  - openpyxl  3.0+        (pip install openpyxl)

  Quick install for all Python dependencies:
    pip install pandas openpyxl


================================================================================
  GETTING STARTED
================================================================================

--------------------------------------------------------------------------------
  STEP 1 - Install MinGW-w64 (C compiler)
--------------------------------------------------------------------------------

  Required to compile the C source code. Use WinLibs MinGW-w64.

  1. Go to: https://winlibs.com/
  2. Download the latest GCC release. Choose:
       - Architecture: x86_64
       - Threads:      Win32
       - Exception:    SEH
       - Runtime:      UCRT
     Example filename: winlibs-x86_64-ucrt-...-gcc-15.x.x-...7z

  3. Extract the archive to a simple path, e.g. C:\mingw64

  4. Add GCC to your PATH:
       - Open Start, search "Environment Variables"
       - Under "System variables", select Path, click Edit
       - Click New and add:  C:\mingw64\bin
       - Click OK on all dialogs

  5. Open a new Command Prompt and verify:
       gcc --version
     Should print something like: gcc (MinGW-w64 ...) 15.x.x


--------------------------------------------------------------------------------
  STEP 2 - Install Python 3 (for BOM checker)
--------------------------------------------------------------------------------

  Required to read BOM.xlsx and power the BOM verification feature.
  Skip this step if you will not use the BOM checker.

  1. Go to: https://www.python.org/downloads/
  2. Download and run the installer for Python 3.10 or newer.
  3. IMPORTANT: On the first installer screen, check the box that says
     "Add Python to PATH" before clicking Install.
  4. Verify in a new Command Prompt:
       python --version
     Should print: Python 3.x.x

  Install required Python packages:

    pip install pandas openpyxl

  Package details:

    pandas (>=1.3)
      Reads and parses BOM.xlsx into a structured table.
      Used by bom_export.py to extract part numbers, descriptions,
      quantities, and order links.
        pip install pandas

    openpyxl (>=3.0)
      Required by pandas to open .xlsx format Excel files.
      Without this, pandas cannot read BOM.xlsx at all.
        pip install openpyxl

  Both can be installed in one command:
    pip install pandas openpyxl

  Verify installation:
    python -c "import pandas; import openpyxl; print('OK')"

  Note: the BOM checker will show "BOM not found" or fail silently
  if either package is missing. If Python itself is not on PATH, the
  server will log an error when Check BOM is clicked.


--------------------------------------------------------------------------------
  STEP 3 - Install MercuryAPI 1.37.2.24
--------------------------------------------------------------------------------

  The MercuryAPI C source must be present to compile the RFID tools.
  Version 1.37.2.24 is required. Version 1.31.x is NOT compatible with
  the M7E Hecto firmware.

  1. Obtain mercuryapi-1.37.2.24 (from ThingMagic / Jadak support or your
     existing installation).
  2. Place the extracted folder at:
       rfid_kit_reader\lib\mercuryapi-1.37.2.24\
  3. Verify the path is correct by checking that this file exists:
       rfid_kit_reader\lib\mercuryapi-1.37.2.24\c\src\api\tm_reader.h


--------------------------------------------------------------------------------
  STEP 4 - Place project files
--------------------------------------------------------------------------------

  Your project folder should look like this before building:

  rfid_kit_reader\
  |-- src\
  |   |-- rfid_common.h
  |   |-- rfid_kit_reader.c
  |   |-- rfid_tag_writer.c
  |   |-- rfid_server.c
  |   |-- rfid_diag.c
  |   +-- generate_test_data.c
  |-- lib\
  |   +-- mercuryapi-1.37.2.24\    <-- from Step 3
  |-- files\
  |   +-- BOM.xlsx                  <-- your Bill of Materials
  |-- rfid_gui.html
  |-- bom_export.py
  |-- build.bat
  +-- README.txt

  Create the files\ folder if it does not exist and place your BOM.xlsx
  inside it. The BOM must have a column named "RFID Part Number".


--------------------------------------------------------------------------------
  STEP 5 - Build
--------------------------------------------------------------------------------

  Open a Command Prompt in the rfid_kit_reader\ folder and run:

    build.bat

  This compiles all five executables into the bin\ folder:
    bin\rfid_kit_reader.exe
    bin\rfid_tag_writer.exe
    bin\rfid_server.exe
    bin\rfid_diag.exe
    bin\generate_test_data.exe

  A successful build prints usage instructions at the end.
  If you see "FAILED:", check that GCC is on your PATH (Step 1) and that
  the MercuryAPI source is in the correct location (Step 3).


--------------------------------------------------------------------------------
  STEP 6 - Run the GUI
--------------------------------------------------------------------------------

  From the rfid_kit_reader\ folder, run:

    bin\rfid_server.exe

  The server starts on localhost:8765 and automatically opens rfid_gui.html
  in your default browser.

  If the browser does not open, open rfid_gui.html manually.
  If the server reports "Port 8765 in use", another instance is already
  running - close it first or kill the process in Task Manager.


================================================================================
  QUICK WORKFLOW
================================================================================

  FIRST-TIME TAG PROGRAMMING
  --------------------------
  1. Connect reader to USB and serial adapter, note the COM port.
  2. Run bin\rfid_server.exe, browser opens.
  3. Enter COM port and power, click Connect.
  4. Place a batch of tags for one part number near the antenna.
  5. Enter the part number (e.g. 0001), click Write Part.
     Tags are scanned, settled, written, and verified.
  6. Repeat step 4-5 for each part number.
  7. Place all tags for one kit near the antenna.
  8. Enter the kit number (e.g. 0001), click Write Kit.
  9. Repeat step 7-8 for each kit.

  KIT INVENTORY / BOM CHECK
  -------------------------
  1. Place one kit near the antenna, click Scan Tags.
     All tags in the kit appear in the table.
  2. Repeat for each kit being checked (tags accumulate across scans).
  3. Click Check BOM.
     A new tab opens showing which kits pass/fail the BOM,
     which parts are missing from which kits, and a master parts list.
  4. Click Export XLSX in the new tab to save the full report.

  OR - import a previously exported CSV instead of scanning:
  1. Click Import CSV, select your CSV file.
     Tags load into the table instantly.
  2. Click Check BOM.


================================================================================
  PROJECT STRUCTURE
================================================================================

  rfid_kit_reader\
  |-- src\
  |   |-- rfid_common.h         shared constants, EPC encode/decode, write logic
  |   |-- rfid_kit_reader.c     scan tags, print table, optionally write CSV
  |   |-- rfid_tag_writer.c     interactive two-phase tag writer
  |   |-- rfid_server.c         HTTP server backend for the GUI
  |   |-- rfid_diag.c           diagnostic tool (temperature, power)
  |   +-- generate_test_data.c  fake CSV generator (no hardware needed)
  |-- lib\
  |   +-- mercuryapi-1.37.2.24\ MercuryAPI C source (required version)
  |-- bin\                       compiled executables (created by build)
  |-- files\
  |   +-- BOM.xlsx               your Bill of Materials
  |-- output\                    CSV exports
  |-- rfid_gui.html              browser GUI
  |-- bom_export.py              Python helper: reads BOM.xlsx for GUI
  |-- README.txt                 this file
  +-- build.bat                  build script


================================================================================
  GUI REFERENCE
================================================================================

  CONNECTION PANEL (left sidebar)
  --------------------------------
  COM Port    Windows serial port, e.g. COM5.
              Find yours in Device Manager > Ports (COM & LPT).
  RF Power    In centidBm. Default 2700 = 27.00 dBm (max).
              USB safe maximum is 2200 = 22.00 dBm.
              External 5V 1A supply required above 22 dBm.

  Connect runs a quick test scan to verify the reader is responsive, then
  stores the port and power for all subsequent operations. It retries once
  after a 1-second delay if the port has not fully released from a previous
  command.


  TAG TABLE
  ---------
  Persists across scans. Tags accumulate until you click Clear. Each new
  scan merges into the existing table: read count increments and RSSI
  updates if the new reading is stronger.

  Column    Description
  --------  ---------------------------------------------------------------
  Status    FULL    Has both part # and kit #. Ready for inventory.
            PART    Has part # only. Needs Write Kit (Phase 2).
            RAW     Blank tag. Needs Write Part (Phase 1).
            LOCKED  Failed all 3 write attempts. Tag is defective or
                    write-protected. Will not be written to this session.
                    (Hover over any badge for a full description.)
  Part #    4-digit part number, or --- if unset
  Kit #     4-digit kit number, or --- if unset
  EPC       24-character hex EPC string (unique per physical tag)
  RSSI      Signal strength in dBm (higher = closer/stronger)
  Reads     How many scan operations have detected this tag

  Sort buttons: click Part #, Kit #, RSSI, or Reads to sort.
  Click again to reverse the sort direction. Arrow shows active sort.
  Clear: wipes the table. Tags reappear on next scan.


  WRITE RESULTS PANEL
  -------------------
  Appears after each write operation.

  Summary bar: Detected / Written / Errors / Skipped

  Per-tag table (one row per tag):
  Result    OK (green), FAIL (red), SKIP (grey)
  Old       Previous value before overwrite, or --- if unset
  New       New value written, or --- if failed/skipped
  EPC       Tag EPC (new EPC for OK, original EPC for FAIL)
  Detail    "Wrote successfully", "Failed after 3 attempts",
            "Already correct - skipped", etc.

  Writing does NOT add tags to the scan table. Only an explicit Scan does.
  The post-write rescan is silent.


  BOM VERIFICATION
  ----------------
  Compares every kit in the tag table against the full BOM (files\BOM.xlsx).
  The BOM must have a column named "RFID Part Number".

  Click Check BOM to open a report in a new browser tab. The report has
  three views:

  Master Parts List
    One row per BOM part. Shows how many kits have it, how many are
    missing it, and which kit numbers it is missing from.
    Red-tinted rows indicate parts with any missing instances.

  Kit by Kit
    All kits with full pass/fail detail. Missing parts listed first
    in each kit block, then present parts, then extras.

  Failing Kits
    Only kits that failed, showing only the missing/extra parts.
    Red badge on the tab shows the failing kit count.

  The Export XLSX button in the report tab downloads a workbook with
  the Master Parts List on Sheet 1 and one sheet per kit.

  The Print button prints the full report (both tab views shown).


  IMPORT / EXPORT CSV
  -------------------
  Export CSV  Saves the current tag table to output\ as a CSV file.
              Columns: Kit Number, Part Number, EPC, RSSI (dBm), Read Count
              This CSV exactly matches what is visible in the table,
              including accumulated read counts.

  Import CSV  Loads a previously exported CSV back into the tag table.
              Useful for running a BOM check without the reader connected.
              Merges with any existing table data.


  DRAG HANDLES
  ------------
  Thin bars between the right-side panels can be dragged vertically to
  resize the tag table, write results panel, BOM results panel, and log.
  Minimum panel height is 40px. Heights reset on window resize.


  OPERATION LOG
  -------------
  Full raw output from the reader/writer subprocess, color-coded:
    Cyan   - status and progress lines
    Green  - successful writes
    Red    - errors
    Yellow - warnings
    Grey   - skipped / settling / informational


================================================================================
  EPC LAYOUT
================================================================================

  Tags use the standard 12-byte (96-bit) Gen2 EPC bank:

  Byte 0-1   Part Number   big-endian uint16, range 0-9999, 0xFFFF = unset
  Byte 2-3   Kit Number    big-endian uint16, range 0-9999, 0xFFFF = unset
  Byte 4-11  Serial        8 random bytes, assigned once in Phase 1,
                           never changed on subsequent writes

  Each EPC is globally unique. Assigning a kit number to a tag preserves
  its serial so it can always be identified even if reprogrammed.


================================================================================
  COMMAND-LINE TOOLS
================================================================================

  rfid_kit_reader.exe
  -------------------
  Scans tags and prints a table. Writes a CSV to output\ unless --no-csv
  is passed. The GUI always passes --no-csv.

    bin\rfid_kit_reader.exe COM5
    bin\rfid_kit_reader.exe COM5 --power 2200
    bin\rfid_kit_reader.exe COM5 --power 2700 --no-csv

  Output columns: EPC  Part#  Kit#  RSSI


  rfid_tag_writer.exe
  -------------------
  Interactive two-phase writer. Prompts for input at each step.

    bin\rfid_tag_writer.exe COM5             full interactive flow
    bin\rfid_tag_writer.exe COM5 --phase1    Phase 1 only (part numbers)
    bin\rfid_tag_writer.exe COM5 --phase2    Phase 2 only (kit numbers)

  Phase 1 - Assign Part Numbers:
    Place tags for one part near the antenna. Enter the part number,
    press Enter to scan and write. Repeat for each part. Type "done" exit.

  Phase 2 - Assign Kit Numbers:
    Place all tags for one kit near the antenna. Enter the kit number,
    press Enter to write. Tags must already have a part number.
    Repeat for each kit. Type "done" to exit.

  Write sequence per tag:
    1. Scan all tags in field (3 passes x 1s each)
    2. Wait 3s for tags to reset to Gen2 state A
    3. Write EPC bank with a filter targeting the specific tag
    4. Verify with a 1.5s filtered read
    5. Retry up to 3 times on failure
    6. Blacklist tags that fail all attempts


  rfid_diag.exe
  -------------
  Reports reader temperature and confirms the connection.

    bin\rfid_diag.exe COM5


  generate_test_data.exe
  ----------------------
  Generates synthetic CSVs in output\ for testing without hardware.
  Produces unique EPCs with proper EPC layout (part/kit in bytes 0-3).

    bin\generate_test_data.exe

  Output:
    test_short_YYYYMMDD_HHMMSS.csv   3 kits x  4 parts =   12 rows
    test_long_YYYYMMDD_HHMMSS.csv   50 kits x 40 parts = 2000 rows


  bom_export.py
  -------------
  Python helper called by rfid_server.exe to read files\BOM.xlsx.
  Not intended to be run directly, but can be tested with:

    python bom_export.py files\BOM.xlsx


================================================================================
  HTTP SERVER API
================================================================================

  rfid_server.exe listens on localhost:8765. All RFID operations are
  performed by spawning subprocesses. The server embeds no MercuryAPI code,
  which avoids threading and blocking issues entirely.

  A 500ms delay is applied before each operation to allow the COM port to
  fully release from the previous subprocess.

  GET  /status       Connection state, port, power, temperature
                     Runs rfid_diag.exe to read temperature.

  POST /connect      {"port":"COM5","power":2700}
                     Test-scans the port, stores settings on success.
                     Retries once after 1s delay.

  POST /disconnect   Clears connection state.

  POST /scan         Runs kit_reader --no-csv, returns tag JSON + raw log.

  POST /write/part   {"part":42}
                     Runs tag_writer --phase1.

  POST /write/kit    {"kit":7}
                     Runs tag_writer --phase2.

  POST /csv          {"tags":[...]}
                     Receives GUI tag table, writes CSV to output\.

  GET  /csv          Downloads the most recently written CSV.

  GET  /bom          Runs bom_export.py, returns BOM parts as JSON array.


================================================================================
  WRITE CONSTANTS  (rfid_common.h)
================================================================================

  DEFAULT_READ_POWER_CDBM    2700    27.00 dBm, maximum for M7E Hecto
  ANTENNA_PORT               1       onboard PCB antenna port
  SCAN_PASSES                3       inventory passes per scan operation
  SCAN_PASS_MS               1000    duration of each pass (ms)
  PRE_WRITE_WAIT_MS          3000    settling time before writing begins
  WRITE_RETRIES              3       attempts per tag before blacklisting
  VERIFY_MS                  1500    filtered read duration for verification


================================================================================
  HARDWARE NOTES
================================================================================

  USB power limit
    ~500mA. Reader browns out above ~22 dBm on USB power alone.
    Symptom: reader connects but fails mid-scan or mid-write.

  External supply (recommended for full power)
    5V 1A via PTH header + USB-UART adapter for serial.
    Required for reliable operation above 22 dBm.

  Onboard antenna
    PCB patch antenna. ~0.5m range at full power.

  External antenna
    uFL connector on board. Up to ~5m with a suitable patch antenna.

  Power mode
    Set to TMR_SR_POWER_MODE_FULL on connect. Prevents sleep/wake
    preamble delays that would cause timeouts between commands.

  Temperature reading
    Requires TMR_READER_STATS_FLAG_ANTENNA_PORTS alongside
    TMR_READER_STATS_FLAG_TEMPERATURE or the reader ignores it.

  I/O errors on connect
    COM port not yet released from a previous process. The server adds
    a 500ms delay and one automatic retry. If it persists, wait a few
    seconds and try again.

  Finding your COM port
    Device Manager > Ports (COM & LPT) > USB Serial Device (COMx)
    The port number changes if you plug into a different USB port.


================================================================================
  TROUBLESHOOTING
================================================================================

  "gcc not found" when building
    GCC is not on your PATH. Re-do Step 1, make sure to add
    C:\mingw64\bin (or wherever you extracted MinGW) to the PATH
    system variable, then open a NEW Command Prompt.

  "Cannot find tm_reader.h" when building
    MercuryAPI is missing or in the wrong folder. Re-do Step 3.
    Check: rfid_kit_reader\lib\mercuryapi-1.37.2.24\c\src\api\tm_reader.h

  Server starts but browser does not open
    Open rfid_gui.html manually in your browser.

  "Port 8765 already in use"
    Another instance of rfid_server.exe is running. Open Task Manager,
    find rfid_server.exe under Background Processes, and end it.

  Connect fails with "Input/output error"
    The COM port is held by another process (or a previous failed run).
    Wait 2-3 seconds and try again. The server retries automatically once.

  BOM checker shows "BOM not found" or fails
    First verify both Python packages are installed:
      python -c "import pandas; import openpyxl; print('OK')"
    If the import fails, run: pip install pandas openpyxl
    Then verify the BOM file path is correct:
      python bom_export.py files\BOM.xlsx
    If Python itself is not found, re-do Step 2 and open a new
    Command Prompt after installing.

  Tags not detected during scan
    - Check reader power switch is on
    - Try a higher power level (2700 cdBm)
    - Use an external 5V 1A supply instead of USB
    - Hold tags within 0.5m of the onboard antenna
    - Ensure tags are Gen2 (EPC Class 1 Generation 2)

  Write fails / tags show LOCKED
    - The tag may be physically defective or a read-only tag
    - Try at closer range with higher power
    - Some cheap Gen2 tags have unreliable EPC bank writes;
      try a different batch of tags


================================================================================
  KEY DESIGN DECISIONS
================================================================================

  C instead of Java
    The Java MercuryAPI native serial library crashed on modern Windows.
    The C API uses serial_transport_win32.c with native Windows serial
    APIs and is stable.

  MercuryAPI version 1.37.2.24
    Required for M7E Hecto firmware compatibility.
    Version 1.31.x fails to connect.

  EPC bank instead of User Memory
    User Memory writes had ~50% failure rates on cheap Gen2 tags.
    EPC bank is mandatory on all Gen2 tags and far more reliable.

  Subprocess architecture for the server
    Embedding MercuryAPI in a multithreaded HTTP server caused deadlocks
    (TMR_read blocks all threads internally). The server spawns
    rfid_kit_reader.exe and rfid_tag_writer.exe as subprocesses instead.

  Silent post-write rescan
    After a write the server rescans to verify tag state, but the GUI
    suppresses merging these results into the tag table to prevent
    write-time EPCs from polluting the inventory view.

================================================================================
