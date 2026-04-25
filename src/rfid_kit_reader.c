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
 *   rfid_kit_reader.exe COM3 --power 2700
 *   rfid_kit_reader.exe COM3 --no-csv
 *   rfid_kit_reader.exe COM3 --continuous
 *   rfid_kit_reader.exe COM3 --continuous --interval 2000
 */

#include "rfid_common.h"

#ifdef WIN32
#include <windows.h>
#include <direct.h>
#include <signal.h>
#define mkdir_compat(p) _mkdir(p)
#else
#include <sys/stat.h>
#include <signal.h>
#define mkdir_compat(p) mkdir(p, 0755)
#endif

/* ── Tag record ──────────────────────────────────────────────────────────── */
typedef struct {
    char     epc_hex[32];
    uint16_t part;
    uint16_t kit;
    int      rssi;
    int      programmed;
    int      seen_count;
} TagRecord;

#define MAX_TAGS 2000
static TagRecord records[MAX_TAGS];
static int       record_count = 0;

/* ── Continuous mode stop flag (set by Ctrl+C handler) ───────────────────── */
static volatile int g_stop = 0;

#ifdef WIN32
static BOOL WINAPI ctrl_handler(DWORD type)
{
    (void)type;
    g_stop = 1;
    return TRUE;
}
#else
static void sig_handler(int s) { (void)s; g_stop = 1; }
#endif

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

    fprintf(fp, "Kit Number,Part Number,EPC,RSSI (dBm),Read Count\n");
    for (i = 0; i < record_count; i++)
    {
        if (!records[i].programmed) continue;
        fprintf(fp, "%04u,%04u,%s,%d,%d\n",
                records[i].kit, records[i].part,
                records[i].epc_hex, records[i].rssi,
                records[i].seen_count);
        written++;
    }
    fclose(fp);
    printf("\nCSV written to: %s  (%d rows)\n", filename, written);
}

static void print_table(void)
{
    int i;
    printf("\n%-26s %-8s %-8s %-6s %s\n",
           "EPC", "Part #", "Kit #", "RSSI", "Reads");
    printf("------------------------------------------------------------------\n");
    for (i = 0; i < record_count; i++)
    {
        if (records[i].programmed)
            printf("%-26s %-8u %-8u %-6d %d\n",
                   records[i].epc_hex,
                   records[i].part, records[i].kit,
                   records[i].rssi, records[i].seen_count);
        else
            printf("%-26s %-8s %-8s %-6d %d  [not programmed]\n",
                   records[i].epc_hex, "---", "---",
                   records[i].rssi, records[i].seen_count);
    }
}

/* ── One full scan pass — returns number of new tags found ───────────────── */
static int do_scan_pass(TMR_Reader *rp, int continuous)
{
    TMR_Status ret;
    int new_tags = 0;
    int pass;

    for (pass = 1; pass <= SCAN_PASSES; pass++)
    {
        if (!continuous)
        {
            printf("  Pass %d/%d...\r", pass, SCAN_PASSES);
            fflush(stdout);
        }

        ret = TMR_read(rp, SCAN_PASS_MS, NULL);
        if (TMR_ERROR_TAG_ID_BUFFER_FULL == ret)
        {
            if (!continuous)
                printf("\n  Tag buffer full on pass %d.\n", pass);
        }
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

            TMR_bytesToHex(trd.tag.epc, trd.tag.epcByteCount, epc_hex);
            idx = find_record(epc_hex);

            if (idx < 0)
            {
                /* New tag */
                if (record_count >= MAX_TAGS) continue;
                idx = record_count++;
                strncpy(records[idx].epc_hex, epc_hex,
                        sizeof(records[idx].epc_hex) - 1);
                records[idx].rssi       = trd.rssi;
                records[idx].seen_count = 1;
                new_tags++;

                if (trd.tag.epcByteCount >= 4)
                {
                    records[idx].part = epc_part(trd.tag.epc);
                    records[idx].kit  = epc_kit(trd.tag.epc);
                    records[idx].programmed =
                        epc_has_part(trd.tag.epc) &&
                        epc_has_kit(trd.tag.epc);
                }

                if (continuous)
                {
                    time_t now = time(NULL);
                    struct tm *lt = localtime(&now);
                    char ts[20];
                    strftime(ts, sizeof(ts), "%H:%M:%S", lt);

                    if (records[idx].programmed)
                        printf("\n[%s] [NEW] %-26s  Part# %04u  Kit# %04u  %d dBm\n",
                               ts, epc_hex,
                               records[idx].part, records[idx].kit,
                               records[idx].rssi);
                    else
                        printf("\n[%s] [NEW] %-26s  (not programmed)  %d dBm\n",
                               ts, epc_hex, records[idx].rssi);
                    fflush(stdout);
                }
            }
            else
            {
                int updated = 0;
                if (trd.rssi > records[idx].rssi)
                {
                    records[idx].rssi = trd.rssi;
                    updated = 1;
                }
                records[idx].seen_count++;

                if (continuous && updated)
                {
                    time_t now = time(NULL);
                    struct tm *lt = localtime(&now);
                    char ts[20];
                    strftime(ts, sizeof(ts), "%H:%M:%S", lt);
                    printf("\n[%s] [UPD] %-26s  RSSI now %d dBm  (seen %d x)\n",
                           ts, epc_hex,
                           records[idx].rssi, records[idx].seen_count);
                    fflush(stdout);
                }
            }
        }
    }

    if (!continuous)
        printf("                              \r");

    return new_tags;
}

int main(int argc, char *argv[])
{
    TMR_Reader  r, *rp = &r;
    TMR_Status  ret;
    TMR_ReadPlan plan;
    int power_cdbm     = DEFAULT_READ_POWER_CDBM;
    int write_csv_flag = 1;
    int continuous     = 0;
    int interval_ms    = 1000;
    uint8_t antenna    = ANTENNA_PORT;
    int i;

    if (argc < 2)
    {
        fprintf(stderr,
            "Usage: rfid_kit_reader <PORT> [options]\n"
            "  --power N      RF power in centidBm (default %d = %.2f dBm)\n"
            "  --no-csv       Do not write CSV on exit\n"
            "  --continuous   Scan continuously until Ctrl+C\n"
            "  --interval N   Pause between continuous cycles in ms (default 1000)\n",
            DEFAULT_READ_POWER_CDBM, DEFAULT_READ_POWER_CDBM / 100.0);
        return 1;
    }

    for (i = 2; i < argc; i++)
    {
        if      (strcmp(argv[i], "--power") == 0 && i+1 < argc)
            power_cdbm = atoi(argv[++i]);
        else if (strcmp(argv[i], "--no-csv") == 0)
            write_csv_flag = 0;
        else if (strcmp(argv[i], "--continuous") == 0)
            continuous = 1;
        else if (strcmp(argv[i], "--interval") == 0 && i+1 < argc)
            interval_ms = atoi(argv[++i]);
        else
            die("Unknown argument: %s", argv[i]);
    }

    char uri[64];
    snprintf(uri, sizeof(uri), "tmr:///%s", argv[1]);

    printf("=== SparkFun RFID Kit Reader ===\n");
    printf("Port     : %s\n", uri);
    printf("Power    : %.2f dBm\n", power_cdbm / 100.0);
    if (continuous)
        printf("Mode     : CONTINUOUS (Ctrl+C to stop)\n\n");
    else
        printf("Scan     : %d passes x %dms\n\n", SCAN_PASSES, SCAN_PASS_MS);

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

#ifdef WIN32
    SetConsoleCtrlHandler(ctrl_handler, TRUE);
#else
    signal(SIGINT, sig_handler);
#endif

    if (continuous)
    {
        int cycle = 0;
        int total_new = 0;

        printf("Scanning continuously. New tags printed as detected.\n");
        printf("%-10s %-6s %-26s  %s\n",
               "Time", "Event", "EPC", "Details");
        printf("--------------------------------------------------------------\n");

        while (!g_stop)
        {
            cycle++;
            int new_this = do_scan_pass(rp, 1);
            total_new += new_this;

            printf("\r  Cycle %4d | %4d unique tags | %4d new this session   ",
                   cycle, record_count, total_new);
            fflush(stdout);

            if (!g_stop && interval_ms > 0)
                sleep_ms(interval_ms);
        }

        printf("\n\nStopped after %d cycles.\n", cycle);
        print_table();
    }
    else
    {
        printf("Scanning (%d passes x %dms)...\n", SCAN_PASSES, SCAN_PASS_MS);
        do_scan_pass(rp, 0);
        printf("Unique tags: %d\n\n", record_count);
        print_table();
    }

    qsort(records, record_count, sizeof(TagRecord), cmp_records);
    if (write_csv_flag)
        write_csv();
    else
        printf("\n(CSV not written)\n");

    TMR_destroy(rp);
    return 0;
}
