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
#define PULS_VERSION "1.1.3"
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

    gboolean has_icon = g_resources_get_info ("/io/github/word_sys/puls_diskinfo/icons/io.github.word_sys.puls_diskinfo.svg",
                                              G_RESOURCE_LOOKUP_FLAGS_NONE,
                                              &size, &flags, &error);
    g_assert_no_error (error);
    g_assert_true (has_icon);
    g_assert_cmpuint (size, >, 0);

    const gchar *fixtures[] = {
        "/io/github/word_sys/puls_diskinfo/fixtures/nvme-samsung-980.json",
        "/io/github/word_sys/puls_diskinfo/fixtures/sata-crucial-mx500.json",
        "/io/github/word_sys/puls_diskinfo/fixtures/hdd-seagate-ironwolf.json",
        "/io/github/word_sys/puls_diskinfo/fixtures/caution-drive.json",
        "/io/github/word_sys/puls_diskinfo/fixtures/failing-drive.json",
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

static void
test_drive_types_and_transports (void)
{
    /* 1. Drive type stringification */
    g_assert_cmpstr (puls_drive_type_to_string (PULS_DRIVE_TYPE_NVME_SSD), ==, "NVMe SSD");
    g_assert_cmpstr (puls_drive_type_to_string (PULS_DRIVE_TYPE_SATA_SSD), ==, "SATA SSD");
    g_assert_cmpstr (puls_drive_type_to_string (PULS_DRIVE_TYPE_SATA_HDD), ==, "SATA HDD");
    g_assert_cmpstr (puls_drive_type_to_string (PULS_DRIVE_TYPE_SAS_SSD),  ==, "SAS SSD");
    g_assert_cmpstr (puls_drive_type_to_string (PULS_DRIVE_TYPE_SAS_HDD),  ==, "SAS HDD");
    g_assert_cmpstr (puls_drive_type_to_string (PULS_DRIVE_TYPE_USB),      ==, "USB");
    g_assert_cmpstr (puls_drive_type_to_string (PULS_DRIVE_TYPE_SSHD),     ==, "SSHD");
    g_assert_cmpstr (puls_drive_type_to_string (PULS_DRIVE_TYPE_EMMC),     ==, "eMMC");
    g_assert_cmpstr (puls_drive_type_to_string (PULS_DRIVE_TYPE_SD_CARD),  ==, "SD Card");
    g_assert_cmpstr (puls_drive_type_to_string (PULS_DRIVE_TYPE_VIRTUAL),  ==, "Virtual");
    g_assert_cmpstr (puls_drive_type_to_string (PULS_DRIVE_TYPE_UNKNOWN),  ==, "Unknown");

    /* 2. Drive type icons */
    g_assert_cmpstr (puls_drive_type_to_icon (PULS_DRIVE_TYPE_NVME_SSD), ==, "drive-harddisk-solidstate-symbolic");
    g_assert_cmpstr (puls_drive_type_to_icon (PULS_DRIVE_TYPE_SATA_SSD), ==, "drive-harddisk-solidstate-symbolic");
    g_assert_cmpstr (puls_drive_type_to_icon (PULS_DRIVE_TYPE_SAS_SSD),  ==, "drive-harddisk-solidstate-symbolic");
    g_assert_cmpstr (puls_drive_type_to_icon (PULS_DRIVE_TYPE_EMMC),     ==, "drive-harddisk-solidstate-symbolic");
    g_assert_cmpstr (puls_drive_type_to_icon (PULS_DRIVE_TYPE_USB),      ==, "drive-removable-media-symbolic");
    g_assert_cmpstr (puls_drive_type_to_icon (PULS_DRIVE_TYPE_SD_CARD),  ==, "drive-removable-media-symbolic");
    g_assert_cmpstr (puls_drive_type_to_icon (PULS_DRIVE_TYPE_SATA_HDD), ==, "drive-harddisk-symbolic");
    g_assert_cmpstr (puls_drive_type_to_icon (PULS_DRIVE_TYPE_SAS_HDD),  ==, "drive-harddisk-symbolic");
    g_assert_cmpstr (puls_drive_type_to_icon (PULS_DRIVE_TYPE_SSHD),     ==, "drive-harddisk-symbolic");
    g_assert_cmpstr (puls_drive_type_to_icon (PULS_DRIVE_TYPE_VIRTUAL),  ==, "drive-multidisk-symbolic");

    /* 3. Classification helpers */
    g_assert_true (puls_drive_type_is_ssd (PULS_DRIVE_TYPE_NVME_SSD));
    g_assert_true (puls_drive_type_is_ssd (PULS_DRIVE_TYPE_SATA_SSD));
    g_assert_true (puls_drive_type_is_ssd (PULS_DRIVE_TYPE_SAS_SSD));
    g_assert_true (puls_drive_type_is_ssd (PULS_DRIVE_TYPE_EMMC));
    g_assert_false (puls_drive_type_is_ssd (PULS_DRIVE_TYPE_SATA_HDD));
    g_assert_false (puls_drive_type_is_ssd (PULS_DRIVE_TYPE_SAS_HDD));

    g_assert_true (puls_drive_type_is_rotational (PULS_DRIVE_TYPE_SATA_HDD));
    g_assert_true (puls_drive_type_is_rotational (PULS_DRIVE_TYPE_SAS_HDD));
    g_assert_true (puls_drive_type_is_rotational (PULS_DRIVE_TYPE_SSHD));
    g_assert_false (puls_drive_type_is_rotational (PULS_DRIVE_TYPE_NVME_SSD));
    g_assert_false (puls_drive_type_is_rotational (PULS_DRIVE_TYPE_SATA_SSD));

    /* 4. Transport protocol stringification & parsing */
    g_assert_cmpstr (puls_transport_protocol_to_string (PULS_TRANSPORT_PCIE),    ==, "PCIe");
    g_assert_cmpstr (puls_transport_protocol_to_string (PULS_TRANSPORT_SATA),    ==, "SATA");
    g_assert_cmpstr (puls_transport_protocol_to_string (PULS_TRANSPORT_SAS),     ==, "SAS");
    g_assert_cmpstr (puls_transport_protocol_to_string (PULS_TRANSPORT_USB),     ==, "USB");
    g_assert_cmpstr (puls_transport_protocol_to_string (PULS_TRANSPORT_MMC),     ==, "MMC");
    g_assert_cmpstr (puls_transport_protocol_to_string (PULS_TRANSPORT_VIRTUAL), ==, "Virtual");
    g_assert_cmpstr (puls_transport_protocol_to_string (PULS_TRANSPORT_UNKNOWN), ==, "Unknown");

    g_assert_cmpint (puls_transport_protocol_from_string ("pcie"),    ==, PULS_TRANSPORT_PCIE);
    g_assert_cmpint (puls_transport_protocol_from_string ("NVMe"),    ==, PULS_TRANSPORT_PCIE);
    g_assert_cmpint (puls_transport_protocol_from_string ("sata"),    ==, PULS_TRANSPORT_SATA);
    g_assert_cmpint (puls_transport_protocol_from_string ("ata"),     ==, PULS_TRANSPORT_SATA);
    g_assert_cmpint (puls_transport_protocol_from_string ("SAS"),     ==, PULS_TRANSPORT_SAS);
    g_assert_cmpint (puls_transport_protocol_from_string ("SCSI"),    ==, PULS_TRANSPORT_SAS);
    g_assert_cmpint (puls_transport_protocol_from_string ("usb"),     ==, PULS_TRANSPORT_USB);
    g_assert_cmpint (puls_transport_protocol_from_string ("mmc"),     ==, PULS_TRANSPORT_MMC);
    g_assert_cmpint (puls_transport_protocol_from_string ("sd"),      ==, PULS_TRANSPORT_MMC);
    g_assert_cmpint (puls_transport_protocol_from_string ("virtual"), ==, PULS_TRANSPORT_VIRTUAL);
    g_assert_cmpint (puls_transport_protocol_from_string ("loop"),    ==, PULS_TRANSPORT_VIRTUAL);
    g_assert_cmpint (puls_transport_protocol_from_string ("other"),   ==, PULS_TRANSPORT_UNKNOWN);
    g_assert_cmpint (puls_transport_protocol_from_string (NULL),      ==, PULS_TRANSPORT_UNKNOWN);

    /* 5. PulsSmartData transport protocol getter/setter */
    g_autoptr(PulsSmartData) data = puls_smart_data_new ();
    g_assert_cmpint (puls_smart_data_get_transport_protocol (data), ==, PULS_TRANSPORT_UNKNOWN);
    puls_smart_data_set_transport_protocol (data, PULS_TRANSPORT_PCIE);
    g_assert_cmpint (puls_smart_data_get_transport_protocol (data), ==, PULS_TRANSPORT_PCIE);

    /* 6. Detection mappings */
    g_assert_cmpint (puls_detect_drive_type ("/dev/nvme0n1"), ==, PULS_DRIVE_TYPE_NVME_SSD);
    g_assert_cmpint (puls_detect_transport_protocol ("/dev/nvme0n1"), ==, PULS_TRANSPORT_PCIE);
    g_assert_cmpint (puls_detect_drive_type ("/dev/loop0"), ==, PULS_DRIVE_TYPE_VIRTUAL);
    g_assert_cmpint (puls_detect_transport_protocol ("/dev/loop0"), ==, PULS_TRANSPORT_VIRTUAL);
    g_assert_cmpint (puls_detect_transport_protocol ("/dev/mmcblk0"), ==, PULS_TRANSPORT_MMC);
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
    g_test_add_func ("/basic/drive-types-and-transports", test_drive_types_and_transports);

    return g_test_run ();
}
