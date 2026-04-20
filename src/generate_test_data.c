/**
 * generate_test_data.c
 *
 * Generates fake RFID inventory CSVs in the same format as rfid_kit_reader,
 * for testing without hardware. Every EPC is globally unique across all rows
 * and all files — matching real-world Gen2 tag behaviour.
 *
 *   output\test_short_YYYYMMDD_HHMMSS.csv  —  3 kits x  4 parts =  12 rows
 *   output\test_long_YYYYMMDD_HHMMSS.csv   — 50 kits x 40 parts = 2000 rows
 *
 * Usage:
 *   generate_test_data.exe
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef WIN32
#include <direct.h>
#include <windows.h>
#define mkdir_compat(p) _mkdir(p)
#else
#include <sys/stat.h>
#define mkdir_compat(p) mkdir(p, 0755)
#endif

/* ── RNG ─────────────────────────────────────────────────────────────────── */
/* xorshift64 — long period (2^64-1), good distribution, no short cycles */
static unsigned long long rng_state;

static void rng_seed(void)
{
    /* Mix time + clock + address bits for a high-entropy seed */
    unsigned long long s = (unsigned long long)time(NULL);
    s ^= (unsigned long long)clock() << 32;
    s ^= (unsigned long long)(uintptr_t)&s;   /* stack address entropy */
#ifdef WIN32
    s ^= (unsigned long long)GetTickCount64();
#endif
    /* ensure non-zero */
    rng_state = s ? s : 0xDEADBEEFCAFEBABEULL;
}

static unsigned long long rng_next(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

/* ── EPC generation ──────────────────────────────────────────────────────── */
/* EPC layout matches rfid_common.h:
 *   Bytes 0-1  : Part Number  (big-endian uint16)
 *   Bytes 2-3  : Kit Number   (big-endian uint16)
 *   Bytes 4-11 : Unique serial (8 random bytes)
 */
static void make_epc(char *out, int kit, int part)
{
    unsigned char epc[12];
    /* Part number */
    epc[0] = (part >> 8) & 0xFF;
    epc[1] =  part       & 0xFF;
    /* Kit number */
    epc[2] = (kit  >> 8) & 0xFF;
    epc[3] =  kit        & 0xFF;
    /* 8-byte unique serial from RNG */
    unsigned long long r1 = rng_next();
    unsigned long long r2 = rng_next();
    epc[4]  = (r1 >> 56) & 0xFF; epc[5]  = (r1 >> 48) & 0xFF;
    epc[6]  = (r1 >> 40) & 0xFF; epc[7]  = (r1 >> 32) & 0xFF;
    epc[8]  = (r2 >> 56) & 0xFF; epc[9]  = (r2 >> 48) & 0xFF;
    epc[10] = (r2 >> 40) & 0xFF; epc[11] = (r2 >> 32) & 0xFF;

    int i;
    for (i = 0; i < 12; i++)
        sprintf(out + i*2, "%02X", epc[i]);
    out[24] = '\0';
}

/* ── Row / sort ──────────────────────────────────────────────────────────── */
typedef struct { char kit[5]; char part[5]; char epc[25]; int rssi; } Row;

static int cmp_rows(const void *a, const void *b)
{
    const Row *ra = (const Row *)a;
    const Row *rb = (const Row *)b;
    int c = strcmp(ra->kit, rb->kit);
    return c ? c : strcmp(ra->part, rb->part);
}

/* ── CSV writer ──────────────────────────────────────────────────────────── */
static void generate_csv(const char *filename, int num_kits, int parts_per_kit)
{
    int total = num_kits * parts_per_kit;
    Row *rows = (Row *)malloc(total * sizeof(Row));
    FILE *fp;
    int k, p, idx = 0;

    if (!rows) { fprintf(stderr, "Out of memory\n"); exit(1); }

    for (k = 0; k < num_kits; k++)
    {
        for (p = 0; p < parts_per_kit; p++)
        {
            sprintf(rows[idx].kit,  "%04d", k + 1);
            sprintf(rows[idx].part, "%04d", p + 1);
            make_epc(rows[idx].epc, k + 1, p + 1);
            /* RSSI between -40 and -79 dBm */
            rows[idx].rssi = -40 - (int)(rng_next() % 40);
            idx++;
        }
    }

    qsort(rows, total, sizeof(Row), cmp_rows);

    fp = fopen(filename, "w");
    if (!fp)
    {
        fprintf(stderr, "Cannot open %s\n", filename);
        free(rows);
        return;
    }

    fprintf(fp, "Kit Number,Part Number,EPC,RSSI (dBm),Read Count\n");
    for (idx = 0; idx < total; idx++)
        fprintf(fp, "%s,%s,%s,%d,%d\n",
                rows[idx].kit, rows[idx].part,
                rows[idx].epc, rows[idx].rssi, 1);

    fclose(fp);
    free(rows);

    printf("  %-50s  %d kits x %d parts = %d rows\n",
           filename, num_kits, parts_per_kit, total);
}

/* ── Main ────────────────────────────────────────────────────────────────── */
int main(void)
{
    char short_file[256], long_file[256];
    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    mkdir_compat("output");
    rng_seed();

    snprintf(short_file, sizeof(short_file),
             "output\\test_short_%04d%02d%02d_%02d%02d%02d.csv",
             t->tm_year+1900, t->tm_mon+1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec);

    snprintf(long_file, sizeof(long_file),
             "output\\test_long_%04d%02d%02d_%02d%02d%02d.csv",
             t->tm_year+1900, t->tm_mon+1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec);

    printf("Generating test data...\n");
    generate_csv(short_file,  3,  4);
    generate_csv(long_file,  50, 40);
    printf("Done.\n");

    return 0;
}
