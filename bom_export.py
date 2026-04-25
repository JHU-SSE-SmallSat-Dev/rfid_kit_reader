"""
bom_export.py
Called by rfid_server.exe to read BOM.xlsx and output JSON to stdout.
Returns all parts in the BOM with their RFID part number, description, and order link.

Usage: python bom_export.py <path_to_BOM.xlsx>
Requires: pip install pandas openpyxl
"""
import sys, json
try:
    import pandas as pd
except ImportError:
    print(json.dumps({"error": "pandas not installed. Run: pip install pandas openpyxl"}))
    sys.exit(1)

path = sys.argv[1] if len(sys.argv) > 1 else "files\\BOM.xlsx"

try:
    df = pd.read_excel(path)
except FileNotFoundError:
    print(json.dumps({"error": f"BOM file not found: {path}"}))
    sys.exit(1)
except Exception as e:
    print(json.dumps({"error": str(e)}))
    sys.exit(1)

if 'RFID Part Number' not in df.columns:
    print(json.dumps({"error": "'RFID Part Number' column not found in BOM"}))
    sys.exit(1)

def clean(val):
    s = str(val).strip()
    return '' if s in ('nan', 'None', '') else s

def is_url(val):
    s = clean(val)
    return s.startswith('http://') or s.startswith('https://')

rows = []
for _, r in df.iterrows():
    pn = r.get('RFID Part Number', '')
    if clean(str(pn)) == '':
        continue
    try:
        rfid = str(int(float(pn))).zfill(4)
    except (ValueError, TypeError):
        continue

    link_raw = clean(r.get('Link', ''))
    rows.append({
        'rfid':        rfid,
        'partNumber':  clean(r.get('Part Number', '')),
        'description': clean(r.get('Description', '')),
        'qty':         clean(r.get('Qty', '1')),
        'link':        link_raw if is_url(link_raw) else '',
    })

print(json.dumps(rows))
