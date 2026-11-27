/*
 * test-smart-parser.c
 *
 * Unit tests verifying smartctl JSON parsing across NVMe, SATA SSD, HDD, and failing drives.
 *
 * Copyright (C) 2026 Barın Güzeldemirci
 */

#include <glib.h>
#include <gio/gio.h>
#include "puls-smart-parser.h"
#include "puls-smart-data.h"

#ifndef TEST_FIXTURES_DIR
#define TEST_FIXTURES_DIR "tests/fixtures"
#endif

GResource *puls_diskinfo_get_resource (void);

static gchar *
load_fixture (const gchar *filename)
{
    g_autofree gchar *res_path = g_strdup_printf ("/io/github/puls/diskinfo/fixtures/%s", filename);
    GError *error = NULL;
    GBytes *bytes = g_resources_lookup_data (res_path, G_RESOURCE_LOOKUP_FLAGS_NONE, &error);
    if (bytes) {
        gsize size = 0;
        const gchar *data = g_bytes_get_data (bytes, &size);
        gchar *content = g_strndup (data, size);
        g_bytes_unref (bytes);
        return content;
    }
    g_clear_error (&error);

    g_autofree gchar *path = g_build_filename (TEST_FIXTURES_DIR, filename, NULL);
    gchar *content = NULL;

    if (!g_file_get_contents (path, &content, NULL, &error)) {
        g_error ("Failed to load test fixture '%s': %s", path, error->message);
        g_clear_error (&error);
        return NULL;
    }

    return content;
}

static void
test_parser_nvme_samsung (void)
{
    g_autofree gchar *json = load_fixture ("nvme-samsung-980.json");
    g_assert_nonnull (json);

    GError *error = NULL;
    g_autoptr(PulsSmartData) data = puls_smart_parser_parse_json (json, &error);
    g_assert_no_error (error);
    g_assert_nonnull (data);

    g_assert_cmpstr (puls_smart_data_get_model_name (data), ==, "Samsung SSD 980 PRO 1TB");
    g_assert_cmpstr (puls_smart_data_get_serial_number (data), ==, "S5GXNF0R123456K");
    g_assert_cmpstr (puls_smart_data_get_firmware_version (data), ==, "5B2QGXA7");
    g_assert_cmpuint (puls_smart_data_get_capacity_bytes (data), ==, 1000204886016ULL);
    g_assert_cmpint (puls_smart_data_get_temperature (data), ==, 38);
    g_assert_cmpuint (puls_smart_data_get_power_on_hours (data), ==, 4210);
    g_assert_cmpuint (puls_smart_data_get_power_cycle_count (data), ==, 842);
    g_assert_cmpint (puls_smart_data_get_health (data), ==, PULS_HEALTH_GOOD);
    g_assert_cmpint (puls_smart_data_get_health_percent (data), ==, 99);

    PulsNvmeHealth *nvme = puls_smart_data_get_nvme_health (data);
    g_assert_nonnull (nvme);
    g_assert_cmpuint (nvme->critical_warning, ==, 0);
    g_assert_cmpuint (nvme->percentage_used, ==, 1);
}

static void
test_parser_sata_crucial (void)
{
    g_autofree gchar *json = load_fixture ("sata-crucial-mx500.json");
    g_assert_nonnull (json);

    GError *error = NULL;
    g_autoptr(PulsSmartData) data = puls_smart_parser_parse_json (json, &error);
    g_assert_no_error (error);
    g_assert_nonnull (data);

    g_assert_cmpstr (puls_smart_data_get_model_name (data), ==, "Crucial_CT500MX500SSD1");
    g_assert_cmpstr (puls_smart_data_get_serial_number (data), ==, "2145E5F67890");
    g_assert_cmpuint (puls_smart_data_get_capacity_bytes (data), ==, 500107862016ULL);
    g_assert_cmpint (puls_smart_data_get_rotation_rpm (data), ==, 0);
    g_assert_cmpint (puls_smart_data_get_temperature (data), ==, 41);
    g_assert_cmpuint (puls_smart_data_get_power_on_hours (data), ==, 18450);
    g_assert_true (puls_smart_data_get_supports_trim (data));
    g_assert_true (puls_smart_data_get_supports_ncq (data));
    g_assert_cmpint (puls_smart_data_get_health (data), ==, PULS_HEALTH_GOOD);

    GArray *attrs = puls_smart_data_get_ata_attributes (data);
    g_assert_nonnull (attrs);
    g_assert_cmpuint (attrs->len, >, 0);
}

static void
test_parser_hdd_seagate (void)
{
    g_autofree gchar *json = load_fixture ("hdd-seagate-ironwolf.json");
    g_assert_nonnull (json);

    GError *error = NULL;
    g_autoptr(PulsSmartData) data = puls_smart_parser_parse_json (json, &error);
    g_assert_no_error (error);
    g_assert_nonnull (data);

    g_assert_cmpstr (puls_smart_data_get_model_name (data), ==, "ST8000VN004-2M2101");
    g_assert_cmpstr (puls_smart_data_get_serial_number (data), ==, "WSD3X99A");
    g_assert_cmpuint (puls_smart_data_get_capacity_bytes (data), ==, 8001563222016ULL);
    g_assert_cmpint (puls_smart_data_get_rotation_rpm (data), ==, 7200);
    g_assert_cmpint (puls_smart_data_get_temperature (data), ==, 34);
    g_assert_cmpstr (puls_smart_data_get_form_factor (data), ==, "3.5 inches");
    g_assert_cmpint (puls_smart_data_get_health (data), ==, PULS_HEALTH_GOOD);
}

static void
test_parser_caution_drive (void)
{
    g_autofree gchar *json = load_fixture ("caution-drive.json");
    g_assert_nonnull (json);

    GError *error = NULL;
    g_autoptr(PulsSmartData) data = puls_smart_parser_parse_json (json, &error);
    g_assert_no_error (error);
    g_assert_nonnull (data);

    g_assert_cmpstr (puls_smart_data_get_model_name (data), ==, "Samsung SSD 970 EVO Plus 500GB");
    g_assert_cmpint (puls_smart_data_get_health (data), ==, PULS_HEALTH_CAUTION);
}

static void
test_parser_failing_drive (void)
{
    g_autofree gchar *json = load_fixture ("failing-drive.json");
    g_assert_nonnull (json);

    GError *error = NULL;
    g_autoptr(PulsSmartData) data = puls_smart_parser_parse_json (json, &error);
    g_assert_no_error (error);
    g_assert_nonnull (data);

    g_assert_cmpstr (puls_smart_data_get_model_name (data), ==, "WDC WD10EZEX-00WN4A0");
    g_assert_cmpint (puls_smart_data_get_health (data), ==, PULS_HEALTH_BAD);
}

static void
test_parser_invalid_json (void)
{
    GError *error = NULL;
    g_autoptr(PulsSmartData) data1 = puls_smart_parser_parse_json ("{ bad_syntax: 123", &error);
    g_assert_null (data1);
    g_assert_nonnull (error);
    g_clear_error (&error);

    g_autoptr(PulsSmartData) data2 = puls_smart_parser_parse_json ("[1, 2, 3]", &error);
    g_assert_null (data2);
    g_assert_nonnull (error);
    g_clear_error (&error);
}

typedef struct {
    PulsSmartDeviceType  winning_type;
    const gchar         *success_json;
    const gchar         *failure_json;
    guint                call_count;
    PulsSmartDeviceType  called_types[PULS_SMART_USB_PROBE_MAX_STEPS];
} MockUsbProbeCtx;

static gchar *
mock_usb_probe_fn (const gchar         *device_path G_GNUC_UNUSED,
                   PulsSmartDeviceType  dev_type,
                   gpointer             user_data,
                   GError             **error G_GNUC_UNUSED)
{
    MockUsbProbeCtx *ctx = user_data;
    if (ctx->call_count < PULS_SMART_USB_PROBE_MAX_STEPS)
        ctx->called_types[ctx->call_count] = dev_type;
    ctx->call_count++;

    if (dev_type == ctx->winning_type && ctx->success_json != NULL)
        return g_strdup (ctx->success_json);

    return ctx->failure_json ? g_strdup (ctx->failure_json) : NULL;
}

static void
test_parser_usb_bridge_resolver (void)
{
    /* 1. Flag mapping */
    g_assert_null (puls_smart_device_type_to_flag (PULS_SMART_DEVTYPE_AUTO));
    g_assert_cmpstr (puls_smart_device_type_to_flag (PULS_SMART_DEVTYPE_SAT), ==, "sat");
    g_assert_cmpstr (puls_smart_device_type_to_flag (PULS_SMART_DEVTYPE_SNT_ASMEDIA), ==, "sntasmedia");
    g_assert_cmpstr (puls_smart_device_type_to_flag (PULS_SMART_DEVTYPE_SNT_JMICRON), ==, "sntjmicron");
    g_assert_cmpstr (puls_smart_device_type_to_flag (PULS_SMART_DEVTYPE_SNT_REALTEK), ==, "sntrealtek");

    /* 2. USB VID/PID bridge chipset prediction */
    g_assert_cmpint (puls_smart_device_type_from_usb_ids ("174c", "2362", NULL), ==, PULS_SMART_DEVTYPE_SNT_ASMEDIA);
    g_assert_cmpint (puls_smart_device_type_from_usb_ids ("174C", "55AA", NULL), ==, PULS_SMART_DEVTYPE_SAT);
    g_assert_cmpint (puls_smart_device_type_from_usb_ids ("0bda", "9210", NULL), ==, PULS_SMART_DEVTYPE_SNT_REALTEK);
    g_assert_cmpint (puls_smart_device_type_from_usb_ids ("152d", "0583", NULL), ==, PULS_SMART_DEVTYPE_SNT_JMICRON);
    g_assert_cmpint (puls_smart_device_type_from_usb_ids ("152d", "0578", NULL), ==, PULS_SMART_DEVTYPE_SAT);
    g_assert_cmpint (puls_smart_device_type_from_usb_ids ("05e3", "0749", NULL), ==, PULS_SMART_DEVTYPE_SAT);
    g_assert_cmpint (puls_smart_device_type_from_usb_ids ("ffff", "0001", NULL), ==, PULS_SMART_DEVTYPE_AUTO);

    /* 3. Ordered sequence deduplication and VID/PID prioritization */
    PulsSmartDeviceType order[PULS_SMART_USB_PROBE_MAX_STEPS] = { 0 };
    guint n = puls_smart_build_usb_probe_order (NULL, NULL, order, G_N_ELEMENTS (order));
    g_assert_cmpuint (n, ==, 5);
    g_assert_cmpint (order[0], ==, PULS_SMART_DEVTYPE_AUTO);
    g_assert_cmpint (order[1], ==, PULS_SMART_DEVTYPE_SAT);
    g_assert_cmpint (order[2], ==, PULS_SMART_DEVTYPE_SNT_ASMEDIA);
    g_assert_cmpint (order[3], ==, PULS_SMART_DEVTYPE_SNT_JMICRON);
    g_assert_cmpint (order[4], ==, PULS_SMART_DEVTYPE_SNT_REALTEK);

    PulsUsbBridgeInfo asm_info = {
        .vendor_id = "174c",
        .product_id = "2362",
        .bridge_chip = "ASMedia ASM2362 NVMe-USB Bridge",
    };
    n = puls_smart_build_usb_probe_order (&asm_info, NULL, order, G_N_ELEMENTS (order));
    g_assert_cmpuint (n, ==, 5);
    g_assert_cmpint (order[0], ==, PULS_SMART_DEVTYPE_AUTO);
    g_assert_cmpint (order[1], ==, PULS_SMART_DEVTYPE_SNT_ASMEDIA);
    g_assert_cmpint (order[2], ==, PULS_SMART_DEVTYPE_SAT);

    /* 4. Version/capability gating via PulsSmartctlCaps */
    PulsSmartctlCaps caps_70 = {
        .available = TRUE,
        .major = 7,
        .minor = 0,
        .supports_sat = TRUE,
    };
    n = puls_smart_build_usb_probe_order (&asm_info, &caps_70, order, G_N_ELEMENTS (order));
    g_assert_cmpuint (n, ==, 2);
    g_assert_cmpint (order[0], ==, PULS_SMART_DEVTYPE_AUTO);
    g_assert_cmpint (order[1], ==, PULS_SMART_DEVTYPE_SAT);

    /* 5. Usable JSON detection vs unknown-bridge stub */
    const gchar *unknown_bridge_json =
        "{"
        "  \"smartctl\": {"
        "    \"exit_status\": 1,"
        "    \"messages\": [{"
        "      \"string\": \"/dev/sdb: Unknown USB bridge [0x174c:0x2362]\\nPlease specify device type with the -d option.\","
        "      \"severity\": \"error\""
        "    }]"
        "  }"
        "}";
    const gchar *usb_nvme_json =
        "{"
        "  \"smartctl\": { \"exit_status\": 0 },"
        "  \"device\": { \"name\": \"/dev/sdb\", \"type\": \"sntasmedia\" },"
        "  \"model_name\": \"WD Black SN770 USB\","
        "  \"serial_number\": \"USB-NVME-001\","
        "  \"smart_status\": { \"passed\": true },"
        "  \"temperature\": { \"current\": 41 },"
        "  \"nvme_smart_health_information_log\": {"
        "    \"critical_warning\": 0,"
        "    \"temperature\": 41,"
        "    \"available_spare\": 100,"
        "    \"percentage_used\": 2,"
        "    \"power_on_hours\": 120"
        "  }"
        "}";

    g_assert_false (puls_smart_json_has_device_data (NULL));
    g_assert_false (puls_smart_json_has_device_data ("{ invalid"));
    g_assert_false (puls_smart_json_has_device_data (unknown_bridge_json));
    g_assert_true (puls_smart_json_has_device_data (usb_nvme_json));
    g_assert_true (puls_smart_json_has_device_data (
        "{\"smartctl\":{\"exit_status\":2,\"messages\":[{\"string\":\"Device is in STANDBY mode, exit(2)\"}]}}"));

    /* 6. Full fallback chain starting from AUTO -> sat -> sntasmedia */
    MockUsbProbeCtx ctx1 = {
        .winning_type = PULS_SMART_DEVTYPE_SNT_ASMEDIA,
        .success_json = usb_nvme_json,
        .failure_json = unknown_bridge_json,
    };
    PulsSmartDeviceType resolved = PULS_SMART_DEVTYPE_AUTO;
    GError *err = NULL;
    g_autofree gchar *resolved_json = puls_smart_resolve_usb_bridge_json (
        "/dev/sdb", NULL, NULL, NULL, mock_usb_probe_fn, &ctx1, &resolved, &err);
    g_assert_no_error (err);
    g_assert_nonnull (resolved_json);
    g_assert_cmpint (resolved, ==, PULS_SMART_DEVTYPE_SNT_ASMEDIA);
    g_assert_cmpuint (ctx1.call_count, ==, 3);
    g_assert_cmpint (ctx1.called_types[0], ==, PULS_SMART_DEVTYPE_AUTO);
    g_assert_cmpint (ctx1.called_types[1], ==, PULS_SMART_DEVTYPE_SAT);
    g_assert_cmpint (ctx1.called_types[2], ==, PULS_SMART_DEVTYPE_SNT_ASMEDIA);

    g_autoptr(PulsSmartData) parsed = puls_smart_parser_parse_json (resolved_json, &err);
    g_assert_no_error (err);
    g_assert_nonnull (parsed);
    g_assert_cmpstr (puls_smart_data_get_model_name (parsed), ==, "WD Black SN770 USB");
    g_assert_cmpint (puls_smart_data_get_health (parsed), ==, PULS_HEALTH_GOOD);

    /* 7. VID/PID hint + pre-fetched initial_auto_json avoids redundant probes */
    PulsUsbBridgeInfo rtl_info = {
        .vendor_id = "0bda",
        .product_id = "9210",
        .bridge_chip = "Realtek RTL9210B NVMe/SATA Bridge",
    };
    MockUsbProbeCtx ctx2 = {
        .winning_type = PULS_SMART_DEVTYPE_SNT_REALTEK,
        .success_json = usb_nvme_json,
        .failure_json = unknown_bridge_json,
    };
    resolved = PULS_SMART_DEVTYPE_AUTO;
    g_autofree gchar *rtl_json = puls_smart_resolve_usb_bridge_json (
        "/dev/sdc", &rtl_info, NULL, unknown_bridge_json, mock_usb_probe_fn, &ctx2, &resolved, &err);
    g_assert_no_error (err);
    g_assert_nonnull (rtl_json);
    g_assert_cmpint (resolved, ==, PULS_SMART_DEVTYPE_SNT_REALTEK);
    g_assert_cmpuint (ctx2.call_count, ==, 1);
    g_assert_cmpint (ctx2.called_types[0], ==, PULS_SMART_DEVTYPE_SNT_REALTEK);

    /* 8. All candidates fail -> falls back to initial stub rather than losing error JSON */
    MockUsbProbeCtx ctx3 = {
        .winning_type = PULS_SMART_DEVTYPE_AUTO,
        .success_json = NULL,
        .failure_json = unknown_bridge_json,
    };
    resolved = PULS_SMART_DEVTYPE_SAT;
    g_autofree gchar *fallback_json = puls_smart_resolve_usb_bridge_json (
        "/dev/sdd", NULL, NULL, unknown_bridge_json, mock_usb_probe_fn, &ctx3, &resolved, &err);
    g_assert_no_error (err);
    g_assert_cmpstr (fallback_json, ==, unknown_bridge_json);
    g_assert_cmpint (resolved, ==, PULS_SMART_DEVTYPE_AUTO);
    g_assert_cmpuint (ctx3.call_count, ==, 4);
}

static void
test_parser_exit_status_bitmask (void)
{
    /* 1. Bitmask decoding across bits 0..6 and composite code 68 (64 | 4) */
    PulsSmartctlExitInfo info;

    puls_smartctl_decode_exit_status (-1, &info);
    g_assert_cmpint (info.raw_status, ==, -1);
    g_assert_cmpuint (info.mask, ==, 0);
    g_assert_false (info.is_fatal);

    puls_smartctl_decode_exit_status (0, &info);
    g_assert_cmpint (info.raw_status, ==, 0);
    g_assert_false (info.is_fatal);

    puls_smartctl_decode_exit_status (1, &info);
    g_assert_true (info.has_cmdline_error);
    g_assert_false (info.has_open_failed);
    g_assert_true (info.is_fatal);

    puls_smartctl_decode_exit_status (2, &info);
    g_assert_true (info.has_open_failed);
    g_assert_true (info.is_fatal);

    puls_smartctl_decode_exit_status (68, &info); /* 64 (self-test errors) | 4 (checksum error) */
    g_assert_cmpuint (info.mask, ==, (PULS_SMARTCTL_EXIT_SELFTEST_ERR | PULS_SMARTCTL_EXIT_CHECKSUM));
    g_assert_false (info.has_cmdline_error);
    g_assert_false (info.has_open_failed);
    g_assert_true (info.has_checksum_error);
    g_assert_false (info.has_disk_failing);
    g_assert_false (info.has_prefail_warn);
    g_assert_false (info.has_error_log);
    g_assert_true (info.has_selftest_error);
    g_assert_false (info.is_fatal);

    /* Exit codes 8, 16, 32, 64, 68 are never fatal */
    g_assert_true (puls_smartctl_exit_status_is_fatal (1));
    g_assert_true (puls_smartctl_exit_status_is_fatal (2));
    g_assert_true (puls_smartctl_exit_status_is_fatal (3));
    g_assert_false (puls_smartctl_exit_status_is_fatal (4));
    g_assert_false (puls_smartctl_exit_status_is_fatal (8));
    g_assert_false (puls_smartctl_exit_status_is_fatal (16));
    g_assert_false (puls_smartctl_exit_status_is_fatal (32));
    g_assert_false (puls_smartctl_exit_status_is_fatal (64));
    g_assert_false (puls_smartctl_exit_status_is_fatal (68));

    g_autofree gchar *fmt0 = puls_smartctl_format_exit_status (0);
    g_assert_cmpstr (fmt0, ==, "OK (0)");
    g_autofree gchar *fmt_neg = puls_smartctl_format_exit_status (-1);
    g_assert_cmpstr (fmt_neg, ==, "Unknown");
    g_autofree gchar *fmt68 = puls_smartctl_format_exit_status (68);
    g_assert_cmpstr (fmt68, ==, "Checksum/command error, Self-test errors (68)");

    /* 2. Parsing complete SATA SMART JSON when smartctl returns exit code 68 */
    const gchar *exit68_sata_json =
        "{"
        "  \"json_format_version\": [1, 0],"
        "  \"smartctl\": { \"exit_status\": 68 },"
        "  \"device\": { \"name\": \"/dev/sdb\", \"protocol\": \"ATA\" },"
        "  \"model_name\": \"ST4000VN008-2DR166\","
        "  \"serial_number\": \"ZGY68TEST\","
        "  \"firmware_version\": \"SC60\","
        "  \"user_capacity\": { \"bytes\": 4000787030016 },"
        "  \"rotation_rate\": 5900,"
        "  \"smart_status\": { \"passed\": true },"
        "  \"temperature\": { \"current\": 37 },"
        "  \"power_on_time\": { \"hours\": 14200 },"
        "  \"power_cycle_count\": 210,"
        "  \"ata_smart_attributes\": {"
        "    \"table\": [{"
        "      \"id\": 197, \"name\": \"Current_Pending_Sector\","
        "      \"value\": 100, \"worst\": 100, \"thresh\": 0,"
        "      \"raw\": { \"value\": 0, \"string\": \"0\" }"
        "    }]"
        "  },"
        "  \"ata_smart_error_log\": { \"summary\": { \"count\": 2 } }"
        "}";
    g_assert_true (puls_smart_json_has_device_data (exit68_sata_json));
    g_assert_cmpint (puls_smart_parser_extract_exit_status (exit68_sata_json), ==, 68);

    GError *err = NULL;
    g_autoptr(PulsSmartData) data68 = puls_smart_parser_parse_json (exit68_sata_json, &err);
    g_assert_no_error (err);
    g_assert_nonnull (data68);
    g_assert_cmpstr (puls_smart_data_get_model_name (data68), ==, "ST4000VN008-2DR166");
    g_assert_cmpstr (puls_smart_data_get_serial_number (data68), ==, "ZGY68TEST");
    g_assert_cmpint (puls_smart_data_get_temperature (data68), ==, 37);
    g_assert_cmpuint (puls_smart_data_get_power_on_hours (data68), ==, 14200);
    g_assert_cmpuint (puls_smart_data_get_error_count_total (data68), ==, 2);
    g_assert_cmpint (puls_smart_data_get_smartctl_exit_status (data68), ==, 68);
    g_assert_cmpint (puls_smart_data_get_health (data68), ==, PULS_HEALTH_GOOD);

    /* 3. Exit code 8 (disk failing) marks health BAD even when smart_status is omitted */
    const gchar *exit8_json =
        "{"
        "  \"smartctl\": { \"exit_status\": 8 },"
        "  \"device\": { \"name\": \"/dev/sdc\", \"protocol\": \"ATA\" },"
        "  \"model_name\": \"FAILING_HDD_8\","
        "  \"serial_number\": \"FAIL8\","
        "  \"temperature\": { \"current\": 49 }"
        "}";
    g_assert_true (puls_smart_json_has_device_data (exit8_json));
    g_autoptr(PulsSmartData) data8 = puls_smart_parser_parse_json (exit8_json, &err);
    g_assert_no_error (err);
    g_assert_nonnull (data8);
    g_assert_cmpint (puls_smart_data_get_health (data8), ==, PULS_HEALTH_BAD);
    g_assert_true (puls_smart_data_get_smart_enabled (data8));
    g_assert_cmpint (puls_smart_data_get_smartctl_exit_status (data8), ==, 8);

    /* 4. Exit code 16 (pre-fail warning) upgrades GOOD to CAUTION; 32/64 preserve full parse */
    const gchar *exit16_json =
        "{"
        "  \"smartctl\": { \"exit_status\": 16 },"
        "  \"model_name\": \"WARN_SSD_16\","
        "  \"smart_status\": { \"passed\": true },"
        "  \"temperature\": { \"current\": 43 }"
        "}";
    g_autoptr(PulsSmartData) data16 = puls_smart_parser_parse_json (exit16_json, &err);
    g_assert_no_error (err);
    g_assert_nonnull (data16);
    g_assert_cmpint (puls_smart_data_get_health (data16), ==, PULS_HEALTH_CAUTION);
    g_assert_cmpint (puls_smart_data_get_smartctl_exit_status (data16), ==, 16);

    const gchar *exit32_nvme_json =
        "{"
        "  \"smartctl\": { \"exit_status\": 32 },"
        "  \"model_name\": \"NVME_ERR_LOG_32\","
        "  \"smart_status\": { \"passed\": true },"
        "  \"nvme_smart_health_information_log\": {"
        "    \"critical_warning\": 0, \"temperature\": 40,"
        "    \"available_spare\": 100, \"available_spare_threshold\": 10,"
        "    \"percentage_used\": 3, \"power_on_hours\": 900"
        "  }"
        "}";
    g_autoptr(PulsSmartData) data32 = puls_smart_parser_parse_json (exit32_nvme_json, &err);
    g_assert_no_error (err);
    g_assert_nonnull (data32);
    g_assert_cmpint (puls_smart_data_get_smartctl_exit_status (data32), ==, 32);
    g_assert_cmpuint (puls_smart_data_get_power_on_hours (data32), ==, 900);
    g_assert_cmpint (puls_smart_data_get_temperature (data32), ==, 40);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_resources_register (puls_diskinfo_get_resource ());

    g_test_add_func ("/parser/nvme-samsung-980", test_parser_nvme_samsung);
    g_test_add_func ("/parser/sata-crucial-mx500", test_parser_sata_crucial);
    g_test_add_func ("/parser/hdd-seagate-ironwolf", test_parser_hdd_seagate);
    g_test_add_func ("/parser/caution-drive", test_parser_caution_drive);
    g_test_add_func ("/parser/failing-drive", test_parser_failing_drive);
    g_test_add_func ("/parser/invalid-json", test_parser_invalid_json);
    g_test_add_func ("/parser/usb-bridge-resolver", test_parser_usb_bridge_resolver);
    g_test_add_func ("/parser/exit-status-bitmask", test_parser_exit_status_bitmask);

    return g_test_run ();
}
