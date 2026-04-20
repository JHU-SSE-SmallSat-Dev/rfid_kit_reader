/**
 * rfid_common.h
 * Shared helpers, constants, and reader setup.
 *
 * EPC Layout (12 bytes / 96 bits — standard Gen2 size):
 *
 *   Bytes 0-1  : Part Number  (big-endian uint16, 0-9999)
 *   Bytes 2-3  : Kit Number   (big-endian uint16, 0-9999)
 *   Bytes 4-11 : Unique serial (8 random bytes, assigned once per tag)
 *
 * Write strategy (matches URA behavior):
 *   1. Scan all tags in the batch (multiple passes for reliability)
 *   2. Wait PRE_WRITE_WAIT_MS — tags naturally reset to Gen2 state A
 *   3. Write each tag one at a time using EPC filter
 *   4. Verify each write with a short filtered read
 */
#ifndef RFID_COMMON_H
#define RFID_COMMON_H

#ifndef TMR_ENABLE_SERIAL_READER_ONLY
#define TMR_ENABLE_SERIAL_READER_ONLY
#endif

#include <tm_reader.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <inttypes.h>
#include <time.h>

#ifdef WIN32
#include <windows.h>
#define sleep_ms(ms) Sleep(ms)
#else
#include <unistd.h>
#define sleep_ms(ms) usleep((ms)*1000)
#endif

/* ── EPC layout ────────────────────────────────────────────────────────── */
#define FIELD_UNSET  0xFFFF   /* sentinel: field not yet assigned */

/* ── Tuning constants ──────────────────────────────────────────────────── */
#define DEFAULT_READ_POWER_CDBM  2700
#define ANTENNA_PORT             1

/* Scan: multiple passes merged together to catch all tags reliably */
#define SCAN_PASSES              3
#define SCAN_PASS_MS             1000

/* Wait after scanning before writing — lets all tags reset to Gen2
 * state A naturally (same gap URA uses before issuing a write) */
#define PRE_WRITE_WAIT_MS        3000

/* Per-write retries and verify duration */
#define WRITE_RETRIES            3
#define VERIFY_MS                1500

/* ── Error helpers ─────────────────────────────────────────────────────── */
static void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(1);
}

static void checkerr(TMR_Reader *rp, TMR_Status ret, const char *msg)
{
    if (TMR_SUCCESS != ret)
        die("Error %s: %s", msg, TMR_strerr(rp, ret));
}

/* ── Reader setup ──────────────────────────────────────────────────────── */
static void setup_reader(TMR_Reader *rp, int power_cdbm)
{
    TMR_Status ret;
    TMR_Region region = TMR_REGION_NONE;
    uint8_t antenna = ANTENNA_PORT;

    ret = TMR_paramGet(rp, TMR_PARAM_REGION_ID, &region);
    checkerr(rp, ret, "getting region");

    if (TMR_REGION_NONE == region)
    {
        TMR_RegionList regions;
        TMR_Region store[32];
        regions.list = store;
        regions.max  = 32;
        regions.len  = 0;
        ret = TMR_paramGet(rp, TMR_PARAM_REGION_SUPPORTEDREGIONS, &regions);
        checkerr(rp, ret, "getting supported regions");
        if (regions.len < 1) die("Reader supports no regions");
        region = regions.list[0];
        ret = TMR_paramSet(rp, TMR_PARAM_REGION_ID, &region);
        checkerr(rp, ret, "setting region");
    }

    ret = TMR_paramSet(rp, TMR_PARAM_RADIO_READPOWER, &power_cdbm);
    if (TMR_SUCCESS != ret)
        fprintf(stderr, "Warning: could not set read power: %s\n",
                TMR_strerr(rp, ret));

    ret = TMR_paramSet(rp, TMR_PARAM_RADIO_WRITEPOWER, &power_cdbm);
    if (TMR_SUCCESS != ret)
        fprintf(stderr, "Warning: could not set write power: %s\n",
                TMR_strerr(rp, ret));

    ret = TMR_paramSet(rp, TMR_PARAM_TAGOP_ANTENNA, &antenna);
    if (TMR_SUCCESS != ret)
        fprintf(stderr, "Warning: could not set tagop antenna: %s\n",
                TMR_strerr(rp, ret));

    /* Set power mode to FULL — prevents the reader going into sleep mode
     * between commands which adds a wake-up preamble delay to every
     * operation and can cause the server to time out mid-scan. */
    TMR_SR_PowerMode pm = TMR_SR_POWER_MODE_FULL;
    ret = TMR_paramSet(rp, TMR_PARAM_POWERMODE, &pm);
    if (TMR_SUCCESS != ret)
        fprintf(stderr, "Warning: could not set power mode: %s\n",
                TMR_strerr(rp, ret));

    /* Enable temperature stats — antenna ports flag is mandatory alongside
     * temperature flag or the reader ignores the request */
    TMR_Reader_StatsFlag statsFlag =
        TMR_READER_STATS_FLAG_TEMPERATURE |
        TMR_READER_STATS_FLAG_ANTENNA_PORTS;
    ret = TMR_paramSet(rp, TMR_PARAM_READER_STATS_ENABLE, &statsFlag);
    if (TMR_SUCCESS != ret)
        fprintf(stderr, "Warning: could not enable stats: %s\n",
                TMR_strerr(rp, ret));
}

/* ── EPC encode/decode ─────────────────────────────────────────────────── */

static void epc_encode(uint8_t epc[12], uint16_t part, uint16_t kit,
                        const uint8_t serial[8])
{
    epc[0] = (part >> 8) & 0xFF;
    epc[1] =  part       & 0xFF;
    epc[2] = (kit  >> 8) & 0xFF;
    epc[3] =  kit        & 0xFF;
    if (serial)
    {
        memcpy(epc + 4, serial, 8);
    }
    else
    {
        unsigned int seed = (unsigned int)time(NULL) ^
                            (unsigned int)(uintptr_t)epc;
        int i;
        for (i = 4; i < 12; i++)
        {
            seed = seed * 1664525u + 1013904223u;
            epc[i] = (uint8_t)(seed >> 16);
        }
    }
}

static uint16_t epc_part(const uint8_t epc[12])
{
    return ((uint16_t)epc[0] << 8) | epc[1];
}

static uint16_t epc_kit(const uint8_t epc[12])
{
    return ((uint16_t)epc[2] << 8) | epc[3];
}

static int epc_has_part(const uint8_t epc[12])
{
    uint16_t v = epc_part(epc);
    return (v != 0x0000 && v != 0xFFFF);
}

static int epc_has_kit(const uint8_t epc[12])
{
    uint16_t v = epc_kit(epc);
    return (v != 0x0000 && v != 0xFFFF);
}

/* ── Write and verify ──────────────────────────────────────────────────── */

/**
 * Write new_epc to current_tag using an EPC filter.
 * Must be called after PRE_WRITE_WAIT_MS has elapsed since the last
 * scan so all tags have naturally reset to Gen2 state A.
 */
static int write_epc_once(TMR_Reader *rp, TMR_TagData *current_tag,
                           const uint8_t new_epc[12])
{
    TMR_Status ret;
    TMR_TagOp op;
    TMR_TagData new_tag;
    TMR_TagFilter filter;

    new_tag.epcByteCount = 12;
    memcpy(new_tag.epc, new_epc, 12);

    ret = TMR_TagOp_init_GEN2_WriteTag(&op, &new_tag);
    if (TMR_SUCCESS != ret)
    {
        fprintf(stderr, "      WriteTag init error: %s\n",
                TMR_strerr(rp, ret));
        return 0;
    }

    TMR_TF_init_tag(&filter, current_tag);
    ret = TMR_executeTagOp(rp, &op, &filter, NULL);
    if (TMR_SUCCESS != ret)
        fprintf(stderr, "      WriteTag error: %s\n", TMR_strerr(rp, ret));

    return (TMR_SUCCESS == ret) ? 1 : 0;
}

/**
 * Verify the new EPC is readable after a write.
 */
static int verify_epc_once(TMR_Reader *rp, const uint8_t expected_epc[12])
{
    TMR_Status ret;
    TMR_ReadPlan plan;
    uint8_t antenna = ANTENNA_PORT;
    TMR_TagData target;
    TMR_TagFilter filter;

    target.epcByteCount = 12;
    memcpy(target.epc, expected_epc, 12);
    TMR_TF_init_tag(&filter, &target);

    ret = TMR_RP_init_simple(&plan, 1, &antenna, TMR_TAG_PROTOCOL_GEN2, 1000);
    if (TMR_SUCCESS != ret) return 0;
    TMR_RP_set_filter(&plan, &filter);
    TMR_paramSet(rp, TMR_PARAM_READ_PLAN, &plan);

    ret = TMR_read(rp, VERIFY_MS, NULL);

    int found = 0;
    while (TMR_SUCCESS == TMR_hasMoreTags(rp))
    {
        TMR_TagReadData trd;
        if (TMR_SUCCESS == TMR_getNextTag(rp, &trd))
            if (trd.tag.epcByteCount == 12 &&
                memcmp(trd.tag.epc, expected_epc, 12) == 0)
                found = 1;
    }

    /* Restore unfiltered plan */
    TMR_RP_init_simple(&plan, 1, &antenna, TMR_TAG_PROTOCOL_GEN2, 1000);
    TMR_paramSet(rp, TMR_PARAM_READ_PLAN, &plan);

    return found;
}

/**
 * Write with retries. Always call after PRE_WRITE_WAIT_MS delay.
 */
static int write_and_verify(TMR_Reader *rp, TMR_TagData *current_tag,
                              const char *epc_hex_for_log,
                              const uint8_t new_epc[12])
{
    int attempt;
    for (attempt = 1; attempt <= WRITE_RETRIES; attempt++)
    {
        if (attempt > 1)
            printf("      (retry %d/%d)\n", attempt, WRITE_RETRIES);

        if (!write_epc_once(rp, current_tag, new_epc))
            continue;

        if (verify_epc_once(rp, new_epc))
            return 1;

        fprintf(stderr, "      verify failed (attempt %d)\n", attempt);
    }
    (void)epc_hex_for_log;
    return 0;
}

#endif /* RFID_COMMON_H */
