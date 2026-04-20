/**
 * rfid_tag_writer.c
 *
 * Two-phase interactive tag writer using EPC bank encoding.
 *
 * EPC layout:  [Part:2 bytes][Kit:2 bytes][Serial:8 bytes]
 *
 * Phase 1 - Write Part Numbers (before kits are assembled):
 *   Type a part number, present all tags of that part type, press Enter.
 *   Scans multiple passes to find all tags, waits 2 seconds, then writes
 *   each one. Tags already written this scan are skipped automatically.
 *
 * Phase 2 - Write Kit Numbers (after grouping parts into kits):
 *   Type a kit number, hold scanner near the assembled kit, press Enter.
 *   Preserves part number and serial, updates kit bytes only.
 *
 * Usage:
 *   rfid_tag_writer.exe COM3
 *   rfid_tag_writer.exe COM3 --power 2700
 *   rfid_tag_writer.exe COM3 --phase1
 *   rfid_tag_writer.exe COM3 --phase2
 */

#include "rfid_common.h"

/* ── Console helpers ─────────────────────────────────────────────────────── */
static int read_line(char *buf, int maxlen)
{
    if (!fgets(buf, maxlen, stdin)) return 0;
    buf[strcspn(buf, "\r\n")] = '\0';
    return 1;
}

static char *trim(char *s)
{
    char *end;
    while (*s == ' ' || *s == '\t') s++;
    if (!*s) return s;
    end = s + strlen(s) - 1;
    while (end > s && (*end == ' ' || *end == '\t')) end--;
    end[1] = '\0';
    return s;
}

static int is_valid_number(const char *s, int *out)
{
    char *end;
    long v;
    if (!s || !*s) return 0;
    v = strtol(s, &end, 10);
    if (*end != '\0') return 0;
    if (v < 0 || v > 9999) return 0;
    *out = (int)v;
    return 1;
}

/* ── Tag batch: scan multiple passes, merge by EPC ──────────────────────── */
#define MAX_BATCH 256

typedef struct {
    char        epc_hex[32];
    TMR_TagData tagdata;
} ScannedTag;

static ScannedTag batch[MAX_BATCH];
static int        batch_count = 0;

/**
 * Run SCAN_PASSES inventory passes of SCAN_PASS_MS each.
 * Merges results so a tag only needs to be seen in one pass.
 * Prints progress so user knows scanning is happening.
 */
static void scan_tags(TMR_Reader *rp)
{
    int pass;
    batch_count = 0;

    for (pass = 1; pass <= SCAN_PASSES; pass++)
    {
        TMR_Status ret;
        printf("  Scanning (pass %d/%d)...\r", pass, SCAN_PASSES);
        fflush(stdout);

        ret = TMR_read(rp, SCAN_PASS_MS, NULL);
        if (TMR_SUCCESS != ret && TMR_ERROR_TAG_ID_BUFFER_FULL != ret)
        {
            fprintf(stderr, "\n  Warning: scan error: %s\n",
                    TMR_strerr(rp, ret));
            continue;
        }

        while (TMR_SUCCESS == TMR_hasMoreTags(rp))
        {
            TMR_TagReadData trd;
            char epc_hex[32];
            int found = 0, i;

            ret = TMR_getNextTag(rp, &trd);
            if (TMR_SUCCESS != ret) continue;

            TMR_bytesToHex(trd.tag.epc, trd.tag.epcByteCount, epc_hex);

            for (i = 0; i < batch_count; i++)
                if (strcmp(batch[i].epc_hex, epc_hex) == 0)
                    { found = 1; break; }

            if (!found && batch_count < MAX_BATCH)
            {
                strncpy(batch[batch_count].epc_hex, epc_hex, 31);
                memcpy(&batch[batch_count].tagdata, &trd.tag,
                       sizeof(TMR_TagData));
                batch_count++;
            }
        }
    }
    printf("  Detected %d tag(s) across %d passes.          \n",
           batch_count, SCAN_PASSES);
}

/* ── Written-this-batch tracking ─────────────────────────────────────────── */
/* Resets per scan batch — tracks only what was written in the current
 * scan round so we don't re-write the same tag twice in one batch. */
#define MAX_WRITTEN 256
static char written_this_batch[MAX_WRITTEN][32];
static int  written_this_batch_count = 0;

static int written_in_batch(const char *epc)
{
    int i;
    for (i = 0; i < written_this_batch_count; i++)
        if (strcmp(written_this_batch[i], epc) == 0) return 1;
    return 0;
}

static void mark_written_in_batch(const char *epc)
{
    if (written_this_batch_count < MAX_WRITTEN)
        strncpy(written_this_batch[written_this_batch_count++], epc, 31);
}

static void reset_batch_tracking(void)
{
    written_this_batch_count = 0;
}

/* ── Blacklist: tags that have failed every attempt across multiple scans ── */
#define MAX_BLACKLIST 64
static char blacklist[MAX_BLACKLIST][32];
static int  blacklist_count = 0;

static int is_blacklisted(const char *epc)
{
    int i;
    for (i = 0; i < blacklist_count; i++)
        if (strcmp(blacklist[i], epc) == 0) return 1;
    return 0;
}

static void add_to_blacklist(const char *epc)
{
    if (!is_blacklisted(epc) && blacklist_count < MAX_BLACKLIST)
        strncpy(blacklist[blacklist_count++], epc, 31);
}

static void reset_blacklist(void) { blacklist_count = 0; }

/* ── Active wait: keep RF on without inventorying tags ───────────────────── */
/**
 * Hold RF power on for wait_ms without running inventory commands.
 * This keeps tags energized and lets them reset to Gen2 state A naturally
 * without re-inventorying them (which would put them back into state B).
 * We do this by running very short reads and immediately discarding results.
 */
static void active_wait(TMR_Reader *rp, int wait_ms)
{
    int i, passes = wait_ms / 500;
    printf("  Settling (%dms)", wait_ms);
    fflush(stdout);
    for (i = 0; i < passes; i++)
    {
        TMR_read(rp, 500, NULL);
        /* drain silently */
        while (TMR_SUCCESS == TMR_hasMoreTags(rp))
        {
            TMR_TagReadData trd;
            TMR_getNextTag(rp, &trd);
        }
        printf(".");
        fflush(stdout);
    }
    printf(" ready.\n");
}


static void run_phase1(TMR_Reader *rp)
{
    char line[64];
    int total_written = 0;

    printf("\n--- PHASE 1: Write Part Numbers ---\n");
    printf("Steps:\n");
    printf("  1. Type a part number (0-9999) and press Enter.\n");
    printf("  2. Present all tags for that part, then press Enter to scan.\n");
    printf("  3. The reader scans 3 passes then waits 2 seconds before writing.\n");
    printf("  4. Type 'next' for a new part number, 'done' to finish.\n\n");

    while (1)
    {
        printf("Part number (0-9999) or 'done': ");
        if (!read_line(line, sizeof(line))) break;
        char *input = trim(line);
        if (strcasecmp(input, "done") == 0) break;

        int part_num;
        if (!is_valid_number(input, &part_num))
        {
            printf("  Must be a number 0-9999. Try again.\n");
            continue;
        }
        printf("\nPart %04d selected.\n", part_num);
        reset_blacklist();

        while (1)
        {
            printf("  Present tags for part %04d then press Enter"
                   " (or 'next'): ", part_num);
            if (!read_line(line, sizeof(line))) goto p1_done;
            if (strcasecmp(trim(line), "next") == 0) break;

            /* Multi-pass scan */
            scan_tags(rp);
            if (batch_count == 0)
            {
                printf("  No tags detected. Check placement and try again.\n");
                continue;
            }

            /* Reset per-batch write tracking */
            reset_batch_tracking();

            /* Keep tags powered during the state-reset window */
            active_wait(rp, PRE_WRITE_WAIT_MS);

            int i, wrote = 0, errors = 0, skipped = 0;

            for (i = 0; i < batch_count; i++)
            {
                uint8_t *old_epc = batch[i].tagdata.epc;
                int      bc      = batch[i].tagdata.epcByteCount;
                uint8_t  new_epc[12];

                if (bc >= 4 && epc_has_part(old_epc) &&
                    epc_part(old_epc) == (uint16_t)part_num)
                {
                    printf("    [SKIP] %s  already Part# %04d\n",
                           batch[i].epc_hex, part_num);
                    skipped++;
                    continue;
                }

                if (is_blacklisted(batch[i].epc_hex))
                {
                    printf("    [BAD ] %s  write-protected or defective"
                           " — discard this tag\n", batch[i].epc_hex);
                    skipped++;
                    continue;
                }

                if (bc >= 4 && epc_has_part(old_epc))
                {
                    printf("    [WARN] %s  has Part# %04u - overwriting\n",
                           batch[i].epc_hex, epc_part(old_epc));
                    epc_encode(new_epc, (uint16_t)part_num,
                               epc_kit(old_epc), old_epc + 4);
                }
                else
                {
                    epc_encode(new_epc, (uint16_t)part_num,
                               FIELD_UNSET, NULL);
                }

                if (write_and_verify(rp, &batch[i].tagdata,
                                     batch[i].epc_hex, new_epc))
                {
                    printf("    [ OK ] %s  =>  Part# %04d\n",
                           batch[i].epc_hex, part_num);
                    mark_written_in_batch(batch[i].epc_hex);
                    wrote++;
                }
                else
                {
                    printf("    [ ERR] %s  failed after %d attempts\n",
                           batch[i].epc_hex, WRITE_RETRIES);
                    add_to_blacklist(batch[i].epc_hex);
                    errors++;
                }

                if (i + 1 < batch_count) sleep_ms(200);
            }

            total_written += wrote;
            printf("  - Result: %d written, %d errors, %d skipped\n\n",
                   wrote, errors, skipped);
        }
    }

p1_done:
    printf("\nPhase 1 complete. %d tag(s) written this session.\n",
           total_written);
    printf("Assemble your kits then run Phase 2.\n");
}

/* ── Phase 2 ─────────────────────────────────────────────────────────────── */
static void run_phase2(TMR_Reader *rp)
{
    char line[64];
    int total_written = 0;

    printf("\n--- PHASE 2: Write Kit Numbers ---\n");
    printf("Steps:\n");
    printf("  1. Place all parts for one kit together.\n");
    printf("  2. Type the kit number (0-9999) and press Enter.\n");
    printf("  3. Hold scanner near the kit and press Enter to scan.\n");
    printf("  4. The reader scans 3 passes then writes all tags.\n");
    printf("  5. Type 'next' for a new kit, 'done' to finish.\n");
    printf("  Tags without a part number are skipped (run Phase 1 first).\n\n");

    while (1)
    {
        printf("Kit number (0-9999) or 'done': ");
        if (!read_line(line, sizeof(line))) break;
        char *input = trim(line);
        if (strcasecmp(input, "done") == 0) break;

        int kit_num;
        if (!is_valid_number(input, &kit_num))
        {
            printf("  Must be a number 0-9999. Try again.\n");
            continue;
        }
        printf("\nKit %04d selected.\n", kit_num);
        reset_blacklist();

        while (1)
        {
            printf("  Hold scanner near kit %04d then press Enter"
                   " (or 'next'): ", kit_num);
            if (!read_line(line, sizeof(line))) goto p2_done;
            if (strcasecmp(trim(line), "next") == 0) break;

            scan_tags(rp);
            if (batch_count == 0)
            {
                printf("  No tags detected. Check placement and try again.\n");
                continue;
            }

            reset_batch_tracking();

            active_wait(rp, PRE_WRITE_WAIT_MS);

            int i, wrote = 0, errors = 0, skipped = 0, no_part = 0;

            for (i = 0; i < batch_count; i++)
            {
                uint8_t *old_epc = batch[i].tagdata.epc;
                int      bc      = batch[i].tagdata.epcByteCount;

                if (bc < 4 || !epc_has_part(old_epc))
                {
                    printf("    [WARN] %s  no part number - run Phase 1 first\n",
                           batch[i].epc_hex);
                    no_part++;
                    continue;
                }

                uint16_t cur_part = epc_part(old_epc);
                uint16_t cur_kit  = epc_kit(old_epc);

                if (epc_has_kit(old_epc) && cur_kit == (uint16_t)kit_num)
                {
                    printf("    [SKIP] %s  Part# %04u already in Kit# %04d\n",
                           batch[i].epc_hex, cur_part, kit_num);
                    skipped++;
                    continue;
                }

                if (is_blacklisted(batch[i].epc_hex))
                {
                    printf("    [BAD ] %s  write-protected or defective"
                           " — discard this tag\n", batch[i].epc_hex);
                    skipped++;
                    continue;
                }

                if (epc_has_kit(old_epc) && cur_kit != (uint16_t)kit_num)
                    printf("    [WARN] %s  Part# %04u was Kit# %04u"
                           " - overwriting\n",
                           batch[i].epc_hex, cur_part, cur_kit);

                uint8_t new_epc[12];
                epc_encode(new_epc, cur_part, (uint16_t)kit_num, old_epc + 4);

                if (write_and_verify(rp, &batch[i].tagdata,
                                     batch[i].epc_hex, new_epc))
                {
                    printf("    [ OK ] %s  Part# %04u  Kit# %04d\n",
                           batch[i].epc_hex, cur_part, kit_num);
                    mark_written_in_batch(batch[i].epc_hex);
                    wrote++;
                }
                else
                {
                    printf("    [ ERR] %s  failed after %d attempts\n",
                           batch[i].epc_hex, WRITE_RETRIES);
                    add_to_blacklist(batch[i].epc_hex);
                    errors++;
                }

                if (i + 1 < batch_count) sleep_ms(200);
            }

            total_written += wrote;
            printf("  - Result: %d written, %d errors, %d skipped,"
                   " %d no-part\n\n",
                   wrote, errors, skipped, no_part);
        }
    }

p2_done:
    printf("\nPhase 2 complete. %d tag(s) assigned to kits.\n", total_written);
    printf("Run rfid_kit_reader to generate your CSV inventory.\n");
}

/* ── Main ────────────────────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    TMR_Reader  r, *rp = &r;
    TMR_Status  ret;
    TMR_ReadPlan plan;
    int power_cdbm = DEFAULT_READ_POWER_CDBM;
    char *start_phase = NULL;
    uint8_t antenna = ANTENNA_PORT;
    int i;
    char line[64];

    if (argc < 2)
    {
        fprintf(stderr,
            "Usage: rfid_tag_writer <PORT> [--power cdBm]"
            " [--phase1|--phase2]\n"
            "  PORT      e.g. COM3\n"
            "  --power   RF power in centidBm (default %d = %.2f dBm)\n"
            "  --phase1  go straight to Phase 1\n"
            "  --phase2  go straight to Phase 2\n",
            DEFAULT_READ_POWER_CDBM, DEFAULT_READ_POWER_CDBM / 100.0);
        return 1;
    }

    for (i = 2; i < argc; i++)
    {
        if (strcmp(argv[i], "--power") == 0 && i+1 < argc)
            power_cdbm = atoi(argv[++i]);
        else if (strcmp(argv[i], "--phase1") == 0)
            start_phase = "1";
        else if (strcmp(argv[i], "--phase2") == 0)
            start_phase = "2";
        else
            die("Unknown argument: %s", argv[i]);
    }

    char uri[64];
    snprintf(uri, sizeof(uri), "tmr:///%s", argv[1]);

    printf("=== SparkFun RFID Tag Writer ===\n");
    printf("Port  : %s\n", uri);
    printf("Power : %.2f dBm\n\n", power_cdbm / 100.0);
    printf("Connecting... ");
    fflush(stdout);

    ret = TMR_create(rp, uri);
    checkerr(rp, ret, "creating reader");

    uint32_t baud = 115200;
    TMR_paramSet(rp, TMR_PARAM_BAUDRATE, &baud);

    ret = TMR_connect(rp);
    checkerr(rp, ret, "connecting reader");
    printf("connected.\n\n");

    setup_reader(rp, power_cdbm);

    ret = TMR_RP_init_simple(&plan, 1, &antenna, TMR_TAG_PROTOCOL_GEN2, 1000);
    checkerr(rp, ret, "initializing read plan");
    ret = TMR_paramSet(rp, TMR_PARAM_READ_PLAN, &plan);
    checkerr(rp, ret, "setting read plan");

    char phase[4] = {0};
    if (start_phase)
    {
        strncpy(phase, start_phase, 3);
    }
    else
    {
        printf("Which phase?\n");
        printf("  1 = Write Part Numbers  (before kits are assembled)\n");
        printf("  2 = Write Kit Numbers   (after grouping parts into kits)\n");
        printf("Enter 1 or 2: ");
        if (!read_line(line, sizeof(line))) goto done;
        strncpy(phase, trim(line), 3);
    }

    if (strcmp(phase, "1") == 0)      run_phase1(rp);
    else if (strcmp(phase, "2") == 0) run_phase2(rp);
    else fprintf(stderr, "Invalid choice '%s'.\n", phase);

done:
    TMR_destroy(rp);
    printf("\nReader disconnected.\n");
    return 0;
}
