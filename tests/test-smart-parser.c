/*
 * test-smart-parser.c
 *
 * Unit tests verifying smartctl JSON parsing across NVMe, SATA SSD, HDD, and failing drives.
 *
 * Copyright (C) 2026 Barın Güzeldemirci
 */

#include <glib.h>
#include "puls-smart-parser.h"
#include "puls-smart-data.h"

#ifndef TEST_FIXTURES_DIR
#define TEST_FIXTURES_DIR "tests/fixtures"
#endif

static gchar *
load_fixture (const gchar *filename)
{
    g_autofree gchar *path = g_build_filename (TEST_FIXTURES_DIR, filename, NULL);
    gchar *content = NULL;
    GError *error = NULL;

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

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/parser/nvme-samsung-980", test_parser_nvme_samsung);
    g_test_add_func ("/parser/sata-crucial-mx500", test_parser_sata_crucial);
    g_test_add_func ("/parser/hdd-seagate-ironwolf", test_parser_hdd_seagate);
    g_test_add_func ("/parser/caution-drive", test_parser_caution_drive);
    g_test_add_func ("/parser/failing-drive", test_parser_failing_drive);
    g_test_add_func ("/parser/invalid-json", test_parser_invalid_json);

    return g_test_run ();
}
