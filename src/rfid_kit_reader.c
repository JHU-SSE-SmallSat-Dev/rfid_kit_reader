/**
 * rfid_kit_reader.c
 *
 * Scans for all Gen2 tags, decodes Part Number and Kit Number from the
 * first 4 bytes of each tag's EPC, sorts by Kit then Part, and writes
 * a timestamped CSV to the output\ folder.
 *
 * EPC layout:  [Part:2 bytes][Kit:2 bytes][Serial:8 bytes]
 *
 * Usage:
 *   rfid_kit_reader.exe COM3
 *   rfid_kit_reader.exe COM3 --duration 5000 --power 2700
 */

#include "rfid_common.h"

#ifdef WIN32
#include <windows.h>
#include <direct.h>
#define mkdir_compat(p) _mkdir(p)
#else
#include <sys/stat.h>
#define mkdir_compat(p) mkdir(p, 0755)
#endif

/* ── Tag record ──────────────────────────────────────────────────────────── */
typedef struct {
    char     epc_hex[32];   /* full EPC as hex string        */
    uint16_t part;          /* decoded part number           */
    uint16_t kit;           /* decoded kit number            */
    int      rssi;
    int      programmed;    /* 1 if part+kit look valid      */
} TagRecord;

#define MAX_TAGS 2000
static TagRecord records[MAX_TAGS];
static int       record_count = 0;

static int find_record(const char *epc_hex)
{
    int i;
    for (i = 0; i < record_count; i++)
        if (strcmp(records[i].epc_hex, epc_hex) == 0) return i;
    return -1;
}

static int cmp_records(const void *a, const void *b)
{
    const TagRecord *ra = (const TagRecord *)a;
    const TagRecord *rb = (const TagRecord *)b;
    if (ra->kit != rb->kit)   return (int)ra->kit  - (int)rb->kit;
    if (ra->part != rb->part) return (int)ra->part - (int)rb->part;
    return 0;
}

static void write_csv(void)
{
    char filename[256];
    FILE *fp;
    int i, written = 0;
    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    mkdir_compat("output");
    snprintf(filename, sizeof(filename),
             "output\\rfid_kits_%04d%02d%02d_%02d%02d%02d.csv",
             t->tm_year+1900, t->tm_mon+1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec);

    fp = fopen(filename, "w");
    if (!fp) { fprintf(stderr, "Cannot open %s\n", filename); return; }

    fprintf(fp, "Kit Number,Part Number,EPC,RSSI (dBm)\n");
    for (i = 0; i < record_count; i++)
    {
        if (!records[i].programmed) continue;
        fprintf(fp, "\"%04u\",\"%04u\",\"%s\",%d\n",
                records[i].kit, records[i].part,
                records[i].epc_hex, records[i].rssi);
        written++;
    }
    fclose(fp);
    printf("\nCSV written to: %s  (%d rows)\n", filename, written);
}

int main(int argc, char *argv[])
{
    TMR_Reader  r, *rp = &r;
    TMR_Status  ret;
    TMR_ReadPlan plan;
    int power_cdbm  = DEFAULT_READ_POWER_CDBM;
    int write_csv_flag = 1;  /* default: write CSV; --no-csv disables */
    uint8_t antenna = ANTENNA_PORT;
    int i;
    int raw = 0;

    if (argc < 2)
    {
        fprintf(stderr,
            "Usage: rfid_kit_reader <PORT> [--power cdBm]\n"
            "  PORT      e.g. COM3\n"
            "  --power   RF power in cdBm (default %d = %.2f dBm)\n",
            DEFAULT_READ_POWER_CDBM, DEFAULT_READ_POWER_CDBM / 100.0);
        return 1;
    }

    for (i = 2; i < argc; i++)
    {
        if (strcmp(argv[i], "--power") == 0 && i+1 < argc)
            power_cdbm = atoi(argv[++i]);
        else if (strcmp(argv[i], "--no-csv") == 0)
            write_csv_flag = 0;
        else
            die("Unknown argument: %s", argv[i]);
    }

    char uri[64];
    snprintf(uri, sizeof(uri), "tmr:///%s", argv[1]);

    printf("=== SparkFun RFID Kit Reader ===\n");
    printf("Port     : %s\n", uri);
    printf("Scan     : %d passes x %dms\n", SCAN_PASSES, SCAN_PASS_MS);
    printf("Power    : %.2f dBm\n\n", power_cdbm / 100.0);

    ret = TMR_create(rp, uri);
    checkerr(rp, ret, "creating reader");

    uint32_t baud = 115200;
    TMR_paramSet(rp, TMR_PARAM_BAUDRATE, &baud);

    printf("Connecting... ");
    fflush(stdout);
    ret = TMR_connect(rp);
    checkerr(rp, ret, "connecting reader");
    printf("connected.\n");

    printf("Configuring... ");
    fflush(stdout);
    setup_reader(rp, power_cdbm);

    ret = TMR_RP_init_simple(&plan, 1, &antenna, TMR_TAG_PROTOCOL_GEN2, 1000);
    checkerr(rp, ret, "initializing read plan");
    ret = TMR_paramSet(rp, TMR_PARAM_READ_PLAN, &plan);
    checkerr(rp, ret, "setting read plan");
    printf("done.\n\n");

    /* Multi-pass scan — merge all passes so no tag is missed */
    printf("Scanning (%d passes x %dms)...\n",
           SCAN_PASSES, SCAN_PASS_MS);
    int pass;
    for (pass = 1; pass <= SCAN_PASSES; pass++)
    {
        printf("  Pass %d/%d...\r", pass, SCAN_PASSES);
        fflush(stdout);
        ret = TMR_read(rp, SCAN_PASS_MS, NULL);
        if (TMR_ERROR_TAG_ID_BUFFER_FULL == ret)
            fprintf(stdout, "\n  Tag buffer full on pass %d.\n", pass);
        else if (TMR_SUCCESS != ret)
        {
            fprintf(stderr, "\n  Warning: pass %d error: %s\n",
                    pass, TMR_strerr(rp, ret));
            continue;
        }

        while (TMR_SUCCESS == TMR_hasMoreTags(rp))
        {
            TMR_TagReadData trd;
            char epc_hex[32];
            int idx;

            ret = TMR_getNextTag(rp, &trd);
            if (TMR_SUCCESS != ret) continue;
            raw++;

            TMR_bytesToHex(trd.tag.epc, trd.tag.epcByteCount, epc_hex);
            idx = find_record(epc_hex);

            if (idx < 0)
            {
                if (record_count >= MAX_TAGS) continue;
                idx = record_count++;
                strncpy(records[idx].epc_hex, epc_hex,
                        sizeof(records[idx].epc_hex) - 1);
                records[idx].rssi = trd.rssi;

                if (trd.tag.epcByteCount >= 4)
                {
                    records[idx].part = epc_part(trd.tag.epc);
                    records[idx].kit  = epc_kit(trd.tag.epc);
                    records[idx].programmed =
                        epc_has_part(trd.tag.epc) &&
                        epc_has_kit(trd.tag.epc);
                }
            }
            else if (trd.rssi > records[idx].rssi)
            {
                records[idx].rssi = trd.rssi;
            }
        }
    }
    printf("                              \r"); /* clear pass line */

    printf("Raw reads: %d   Unique tags: %d\n\n", raw, record_count);

    /* Print summary table */
    printf("%-26s %-8s %-8s %s\n", "EPC", "Part #", "Kit #", "RSSI");
    printf("%s\n", "------------------------------------------------------------");
    for (i = 0; i < record_count; i++)
    {
        if (records[i].programmed)
            printf("%-26s %-8u %-8u %d dBm\n",
                   records[i].epc_hex,
                   records[i].part, records[i].kit,
                   records[i].rssi);
        else
            printf("%-26s %-8s %-8s %d dBm  [not programmed]\n",
                   records[i].epc_hex, "---", "---", records[i].rssi);
    }

    /* Sort and optionally write CSV */
    qsort(records, record_count, sizeof(TagRecord), cmp_records);
    if (write_csv_flag)
        write_csv();
    else
        printf("\n(CSV not written — use Export CSV in the GUI or omit --no-csv)\n");

    TMR_destroy(rp);
    return 0;
}
