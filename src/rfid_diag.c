/**
 * rfid_diag.c
 *
 * Diagnostic tool for the SparkFun M7E Hecto RFID reader.
 * Prints firmware version, configured power levels, temperature,
 * and actual read/write power confirmed back from the reader.
 * Use this to verify whether USB brown-out is limiting your RF power.
 *
 * Usage:
 *   rfid_diag.exe COM3
 *
 * Interpreting results:
 *   - If "Read power (confirmed)" is lower than "Read power (requested)",
 *     the reader is being current-limited by USB — use external 5V power.
 *   - Temperature above 55C means the reader is throttling RF output.
 *     Attach a heatsink or reduce duty cycle.
 *   - Normal idle temperature on USB power: 30-45C.
 */

#include "rfid_common.h"

int main(int argc, char *argv[])
{
    TMR_Reader  r, *rp = &r;
    TMR_Status  ret;
    TMR_ReadPlan plan;
    uint8_t antenna = ANTENNA_PORT;

    if (argc < 2)
    {
        fprintf(stderr, "Usage: rfid_diag <PORT>\n  e.g. rfid_diag COM3\n");
        return 1;
    }

    char uri[64];
    snprintf(uri, sizeof(uri), "tmr:///%s", argv[1]);

    printf("=== SparkFun M7E Hecto Diagnostics ===\n\n");
    printf("Connecting to %s... ", uri);
    fflush(stdout);

    ret = TMR_create(rp, uri);
    checkerr(rp, ret, "creating reader");

    uint32_t baud = 115200;
    TMR_paramSet(rp, TMR_PARAM_BAUDRATE, &baud);

    ret = TMR_connect(rp);
    checkerr(rp, ret, "connecting");
    printf("OK\n\n");

    /* ── Firmware version ──────────────────────────────────────────────── */
    {
        char version[64] = "(unavailable)";
        TMR_paramGet(rp, TMR_PARAM_VERSION_SOFTWARE, &version);
        printf("Firmware version     : %s\n", version);
    }

    /* ── Region ────────────────────────────────────────────────────────── */
    {
        TMR_Region region = TMR_REGION_NONE;
        ret = TMR_paramGet(rp, TMR_PARAM_REGION_ID, &region);
        if (TMR_SUCCESS == ret && region != TMR_REGION_NONE)
            printf("Region               : %d\n", (int)region);
        else
        {
            /* Auto-set region so power reads work */
            TMR_RegionList regions;
            TMR_Region store[32];
            regions.list = store; regions.max = 32; regions.len = 0;
            TMR_paramGet(rp, TMR_PARAM_REGION_SUPPORTEDREGIONS, &regions);
            if (regions.len > 0)
            {
                region = regions.list[0];
                TMR_paramSet(rp, TMR_PARAM_REGION_ID, &region);
                printf("Region               : %d (auto-selected)\n",
                       (int)region);
            }
        }
    }

    /* ── Requested power levels ────────────────────────────────────────── */
    {
        int requested = DEFAULT_READ_POWER_CDBM;
        printf("\nRequested read power : %.2f dBm (%d cdBm)\n",
               requested / 100.0, requested);
        printf("(write power matches read power in this firmware)\n");
    }

    /* ── Confirmed power levels read back from reader ──────────────────── */
    {
        int read_power = 0, write_power = 0;

        /* Set power first so we can read it back */
        int set_power = DEFAULT_READ_POWER_CDBM;
        TMR_paramSet(rp, TMR_PARAM_RADIO_READPOWER,  &set_power);
        TMR_paramSet(rp, TMR_PARAM_RADIO_WRITEPOWER, &set_power);

        ret = TMR_paramGet(rp, TMR_PARAM_RADIO_READPOWER, &read_power);
        if (TMR_SUCCESS == ret)
            printf("Confirmed read power : %.2f dBm (%d cdBm)%s\n",
                   read_power / 100.0, read_power,
                   (read_power < set_power) ?
                   "  *** LOWER THAN REQUESTED — USB power limited! ***" : "");
        else
            printf("Confirmed read power : (could not read back)\n");

        ret = TMR_paramGet(rp, TMR_PARAM_RADIO_WRITEPOWER, &write_power);
        if (TMR_SUCCESS == ret)
            printf("Confirmed write power: %.2f dBm (%d cdBm)%s\n",
                   write_power / 100.0, write_power,
                   (write_power < set_power) ?
                   "  *** LOWER THAN REQUESTED — USB power limited! ***" : "");
        else
            printf("Confirmed write power: (could not read back)\n");
    }

    /* ── Temperature ───────────────────────────────────────────────────── */
    printf("\nReading temperature (requires a brief scan)...\n");
    {
        /* Temperature is only available via reader stats after a read */
        uint8_t ant = ANTENNA_PORT;
        ret = TMR_RP_init_simple(&plan, 1, &ant, TMR_TAG_PROTOCOL_GEN2, 1000);
        if (TMR_SUCCESS == ret)
        {
            TMR_paramSet(rp, TMR_PARAM_READ_PLAN, &plan);
            TMR_read(rp, 500, NULL);
            /* drain */
            while (TMR_SUCCESS == TMR_hasMoreTags(rp))
            {
                TMR_TagReadData trd;
                TMR_getNextTag(rp, &trd);
            }
        }

        TMR_Reader_StatsValues stats;
        memset(&stats, 0, sizeof(stats));
        stats.valid = TMR_READER_STATS_FLAG_TEMPERATURE |
                      TMR_READER_STATS_FLAG_ANTENNA_PORTS;
        ret = TMR_paramGet(rp, TMR_PARAM_READER_STATS, &stats);
        if (TMR_SUCCESS == ret)
        {
            printf("Temperature          : %d C", stats.temperature);
            if (stats.temperature > 55)
                printf("  *** HIGH — reader may be throttling RF output! ***");
            else if (stats.temperature > 45)
                printf("  (warm — consider heatsink for extended use)");
            else
                printf("  (normal)");
            printf("\n");

            /* Interpretation */
            printf("\n--- Power Assessment ---\n");
            if (stats.temperature < 35)
                printf("Temperature is low — reader is likely NOT getting\n"
                       "sufficient current from USB and is under-driving RF.\n"
                       "A cold reader at claimed 27dBm with poor range = brownout.\n"
                       "Try: power via PTH header from a 5V 1A+ supply.\n");
            else if (stats.temperature < 50)
                printf("Temperature looks normal for active RF operation.\n"
                       "If range is still poor, the issue may be antenna\n"
                       "placement or tag orientation rather than power.\n");
            else
                printf("Temperature is elevated — thermal throttling may be\n"
                       "reducing RF output. Attach heatsink to bottom copper pad.\n");
        }
        else
        {
            printf("Temperature          : (not available — %s)\n",
                   TMR_strerr(rp, ret));
            printf("\nNote: Temperature stat requires firmware support.\n");
        }
    }

    /* ── USB power advice ──────────────────────────────────────────────── */
    printf("\n--- USB Power Advice ---\n");
    printf("At 27dBm the M7E draws ~720mA. USB ports supply max ~500mA.\n");
    printf("This means the board browns out above ~22dBm on USB power.\n");
    printf("Solutions:\n");
    printf("  1. Lower power to 2200 cdBm (22dBm) — stable on USB\n");
    printf("  2. Power via PTH header VIN/GND from a 5V 1A phone charger\n");
    printf("     and use a USB-UART adapter on TX/RX for serial comms\n");
    printf("  3. Add external antenna via uFL connector for better range\n");
    printf("     at any power level (up to 16ft with good antenna)\n");

    TMR_destroy(rp);
    return 0;
}
