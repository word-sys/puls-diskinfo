/*
 * test-basic.c
 *
 * Basic unit test suite verifying application models and utilities.
 *
 * Copyright (C) 2026 Barın Güzeldemirci
 */

#include <glib.h>
#include "puls-smart-data.h"
#include "puls-disk-manager.h"
#include "puls-mock-data.h"
#include "puls-utils.h"

#ifndef PULS_VERSION
#define PULS_VERSION "1.1.2"
#endif

GResource *puls_diskinfo_get_resource (void);

static void
test_version (void)
{
    g_assert_nonnull (PULS_VERSION);
    g_assert_cmpstr (PULS_VERSION, !=, "");
}

static void
test_format_utils (void)
{
    g_autofree gchar *bytes_str = puls_format_bytes (1000204886016ULL);
    g_assert_nonnull (bytes_str);
    g_assert_true (strstr (bytes_str, "TB") != NULL || strstr (bytes_str, "GB") != NULL);

    g_autofree gchar *temp_str = puls_format_temperature (42);
    g_assert_cmpstr (temp_str, ==, "42 °C");

    g_autofree gchar *temp_na = puls_format_temperature (-1);
    g_assert_cmpstr (temp_na, ==, "N/A");

    g_autofree gchar *hours_str = puls_format_hours (24);
    g_assert_nonnull (hours_str);

    g_autofree gchar *num_str = puls_format_number (1234567);
    g_assert_cmpstr (num_str, ==, "1,234,567");
}

static void
test_smart_data_model (void)
{
    g_autoptr(PulsSmartData) data = puls_smart_data_new ();
    g_assert_nonnull (data);

    puls_smart_data_set_device_path (data, "/dev/test0");
    g_assert_cmpstr (puls_smart_data_get_device_path (data), ==, "/dev/test0");

    puls_smart_data_set_model_name (data, "Test Drive Model");
    g_assert_cmpstr (puls_smart_data_get_model_name (data), ==, "Test Drive Model");

    puls_smart_data_set_health (data, PULS_HEALTH_GOOD);
    g_assert_cmpint (puls_smart_data_get_health (data), ==, PULS_HEALTH_GOOD);

    puls_smart_data_set_health_percent (data, 95);
    g_assert_cmpint (puls_smart_data_get_health_percent (data), ==, 95);

    puls_smart_data_set_temperature (data, 35);
    g_assert_cmpint (puls_smart_data_get_temperature (data), ==, 35);
}

static void
test_mock_manager (void)
{
    puls_set_mock_mode_enabled (TRUE);
    g_assert_true (puls_is_mock_mode_enabled ());

    g_autoptr(PulsDiskManager) manager = puls_disk_manager_new ();
    g_assert_nonnull (manager);

    puls_disk_manager_scan (manager);

    GList *devices = puls_disk_manager_get_devices (manager);
    g_assert_cmpuint (g_list_length (devices), ==, 4);

    PulsSmartData *nvme = puls_disk_manager_get_smart_data (manager, "/dev/nvme0n1");
    g_assert_nonnull (nvme);
    g_assert_cmpstr (puls_smart_data_get_model_name (nvme), ==, "Samsung SSD 980 PRO 1TB");
    g_assert_cmpint (puls_smart_data_get_health (nvme), ==, PULS_HEALTH_GOOD);
    g_assert_cmpint (puls_smart_data_get_health_percent (nvme), ==, 99);

    PulsSmartData *sata = puls_disk_manager_get_smart_data (manager, "/dev/sda");
    g_assert_nonnull (sata);
    g_assert_cmpint (puls_smart_data_get_health (sata), ==, PULS_HEALTH_CAUTION);
    g_assert_cmpint (puls_smart_data_get_health_percent (sata), ==, 78);

    PulsSmartData *hdd = puls_disk_manager_get_smart_data (manager, "/dev/sdb");
    g_assert_nonnull (hdd);
    g_assert_cmpint (puls_smart_data_get_rotation_rpm (hdd), ==, 7200);

    PulsSmartData *usb = puls_disk_manager_get_smart_data (manager, "/dev/sdc");
    g_assert_nonnull (usb);
    g_assert_cmpint (puls_smart_data_get_drive_type (usb), ==, PULS_DRIVE_TYPE_USB);
}

static void
test_gresource_bundle (void)
{
    g_resources_register (puls_diskinfo_get_resource ());

    gsize size = 0;
    guint32 flags = 0;
    GError *error = NULL;

    gboolean has_css = g_resources_get_info ("/io/github/puls/diskinfo/css/style.css",
                                             G_RESOURCE_LOOKUP_FLAGS_NONE,
                                             &size, &flags, &error);
    g_assert_no_error (error);
    g_assert_true (has_css);
    g_assert_cmpuint (size, >, 0);

    gboolean has_legacy_css = g_resources_get_info ("/io/github/puls/diskinfo/style.css",
                                                    G_RESOURCE_LOOKUP_FLAGS_NONE,
                                                    &size, &flags, &error);
    g_assert_no_error (error);
    g_assert_true (has_legacy_css);

    gboolean has_icon = g_resources_get_info ("/io/github/puls/diskinfo/icons/io.github.puls.diskinfo.svg",
                                              G_RESOURCE_LOOKUP_FLAGS_NONE,
                                              &size, &flags, &error);
    g_assert_no_error (error);
    g_assert_true (has_icon);
    g_assert_cmpuint (size, >, 0);

    const gchar *fixtures[] = {
        "/io/github/puls/diskinfo/fixtures/nvme-samsung-980.json",
        "/io/github/puls/diskinfo/fixtures/sata-crucial-mx500.json",
        "/io/github/puls/diskinfo/fixtures/hdd-seagate-ironwolf.json",
        "/io/github/puls/diskinfo/fixtures/caution-drive.json",
        "/io/github/puls/diskinfo/fixtures/failing-drive.json",
    };

    for (guint i = 0; i < G_N_ELEMENTS (fixtures); i++) {
        gboolean exists = g_resources_get_info (fixtures[i],
                                                G_RESOURCE_LOOKUP_FLAGS_NONE,
                                                &size, &flags, &error);
        g_assert_no_error (error);
        g_assert_true (exists);
        g_assert_cmpuint (size, >, 0);

        g_autoptr(GBytes) bytes = g_resources_lookup_data (fixtures[i],
                                                           G_RESOURCE_LOOKUP_FLAGS_NONE,
                                                           &error);
        g_assert_no_error (error);
        g_assert_nonnull (bytes);
    }
}

static void
test_structured_logging (void)
{
    puls_log_init ();

    PULS_LOG_DEBUG ("Test debug log message: %s", "sample");
    PULS_LOG_INFO ("Test info log message: %d", 42);
    PULS_LOG_WARN ("Test warning log message: %f", 3.14);
    PULS_LOG_ERROR ("Test error log message: 0x%x", 0xDEAD);

    PULS_LOG_DOMAIN_DEBUG ("puls/test", "Subdomain debug: %s", "ok");
    PULS_LOG_DOMAIN_INFO ("puls/test", "Subdomain info: %d", 100);
}

int
main (int argc, char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/basic/version", test_version);
    g_test_add_func ("/basic/format-utils", test_format_utils);
    g_test_add_func ("/basic/smart-data-model", test_smart_data_model);
    g_test_add_func ("/basic/mock-manager", test_mock_manager);
    g_test_add_func ("/basic/gresource-bundle", test_gresource_bundle);
    g_test_add_func ("/basic/structured-logging", test_structured_logging);

    return g_test_run ();
}
