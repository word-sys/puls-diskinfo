/*
 * test-basic.c
 *
 * Basic unit test suite verifying application models and utilities.
 *
 * Copyright (C) 2026 Barın Güzeldemirci
 */

#include <glib.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <gio/gunixinputstream.h>
#include <gio/gunixoutputstream.h>
#include "../helper/puls-helper-protocol.h"
#include "../helper/puls-helper-stream.h"
#include "puls-smart-data.h"
#include "puls-smart-parser.h"
#include "puls-disk-manager.h"
#include "puls-mock-data.h"
#include "puls-utils.h"
#include "puls-settings.h"

#ifndef PULS_VERSION
#define PULS_VERSION "1.1.6"
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

static void
test_extended_storage_metrics (void)
{
    /* 1. PCIe generation stringifier & parser */
    g_assert_cmpstr (puls_pcie_generation_to_string (PULS_PCIE_GEN_1), ==, "PCIe 1.0");
    g_assert_cmpstr (puls_pcie_generation_to_string (PULS_PCIE_GEN_2), ==, "PCIe 2.0");
    g_assert_cmpstr (puls_pcie_generation_to_string (PULS_PCIE_GEN_3), ==, "PCIe 3.0");
    g_assert_cmpstr (puls_pcie_generation_to_string (PULS_PCIE_GEN_4), ==, "PCIe 4.0");
    g_assert_cmpstr (puls_pcie_generation_to_string (PULS_PCIE_GEN_5), ==, "PCIe 5.0");
    g_assert_cmpstr (puls_pcie_generation_to_string (PULS_PCIE_GEN_6), ==, "PCIe 6.0");
    g_assert_cmpstr (puls_pcie_generation_to_string (PULS_PCIE_GEN_UNKNOWN), ==, "Unknown");

    g_assert_cmpint (puls_pcie_generation_from_string ("PCIe 4.0"), ==, PULS_PCIE_GEN_4);
    g_assert_cmpint (puls_pcie_generation_from_string ("Gen3"),     ==, PULS_PCIE_GEN_3);
    g_assert_cmpint (puls_pcie_generation_from_string ("gen5"),     ==, PULS_PCIE_GEN_5);
    g_assert_cmpint (puls_pcie_generation_from_string (NULL),       ==, PULS_PCIE_GEN_UNKNOWN);

    /* 2. Sector format stringifier & detector */
    g_assert_cmpstr (puls_sector_format_to_string (PULS_SECTOR_FORMAT_512N), ==, "512n");
    g_assert_cmpstr (puls_sector_format_to_string (PULS_SECTOR_FORMAT_512E), ==, "512e");
    g_assert_cmpstr (puls_sector_format_to_string (PULS_SECTOR_FORMAT_4KN),  ==, "4Kn");
    g_assert_cmpstr (puls_sector_format_to_string (PULS_SECTOR_FORMAT_OTHER), ==, "Custom");
    g_assert_cmpstr (puls_sector_format_to_string (PULS_SECTOR_FORMAT_UNKNOWN), ==, "Unknown");

    g_assert_cmpint (puls_sector_format_detect (512, 512),   ==, PULS_SECTOR_FORMAT_512N);
    g_assert_cmpint (puls_sector_format_detect (512, 4096),  ==, PULS_SECTOR_FORMAT_512E);
    g_assert_cmpint (puls_sector_format_detect (4096, 4096), ==, PULS_SECTOR_FORMAT_4KN);
    g_assert_cmpint (puls_sector_format_detect (0, 0),       ==, PULS_SECTOR_FORMAT_UNKNOWN);

    /* 3. PulsSmartData extended metrics getters & setters */
    g_autoptr(PulsSmartData) data = puls_smart_data_new ();

    puls_smart_data_set_pcie_current_gen (data, PULS_PCIE_GEN_4);
    puls_smart_data_set_pcie_current_width (data, 4);
    puls_smart_data_set_pcie_max_gen (data, PULS_PCIE_GEN_5);
    puls_smart_data_set_pcie_max_width (data, 16);
    g_assert_cmpint (puls_smart_data_get_pcie_current_gen (data), ==, PULS_PCIE_GEN_4);
    g_assert_cmpuint (puls_smart_data_get_pcie_current_width (data), ==, 4);
    g_assert_cmpint (puls_smart_data_get_pcie_max_gen (data), ==, PULS_PCIE_GEN_5);
    g_assert_cmpuint (puls_smart_data_get_pcie_max_width (data), ==, 16);

    puls_smart_data_set_protocol_version (data, "NVMe 2.0");
    g_assert_cmpstr (puls_smart_data_get_protocol_version (data), ==, "NVMe 2.0");

    puls_smart_data_set_tbw_rating_bytes (data, 1200000000000000ULL);
    g_assert_cmpuint (puls_smart_data_get_tbw_rating_bytes (data), ==, 1200000000000000ULL);

    puls_smart_data_set_wear_level_percent (data, 5);
    g_assert_cmpint (puls_smart_data_get_wear_level_percent (data), ==, 5);

    /* Sector format computed automatically from logical & physical sizes */
    puls_smart_data_set_logical_sector_size (data, 512);
    puls_smart_data_set_physical_sector_size (data, 4096);
    g_assert_cmpint (puls_smart_data_get_sector_format (data), ==, PULS_SECTOR_FORMAT_512E);

    /* Sector format explicit override */
    puls_smart_data_set_sector_format (data, PULS_SECTOR_FORMAT_4KN);
    g_assert_cmpint (puls_smart_data_get_sector_format (data), ==, PULS_SECTOR_FORMAT_4KN);

    /* 4. Thermal sensor lifecycle & array management */
    PulsThermalSensor *sensor = puls_thermal_sensor_new ("Controller", 62, 85, 100);
    g_assert_nonnull (sensor);
    g_assert_cmpstr (sensor->name, ==, "Controller");
    g_assert_cmpint (sensor->temperature, ==, 62);
    g_assert_cmpint (sensor->warning_temp, ==, 85);
    g_assert_cmpint (sensor->critical_temp, ==, 100);

    PulsThermalSensor *sensor_copy = puls_thermal_sensor_copy (sensor);
    g_assert_nonnull (sensor_copy);
    g_assert_cmpstr (sensor_copy->name, ==, "Controller");
    puls_thermal_sensor_free (sensor_copy);

    puls_smart_data_add_thermal_sensor (data, sensor);
    puls_thermal_sensor_free (sensor);

    PulsThermalSensor sensor2 = { (gchar *)"NAND Flash", 40, 70, 80 };
    puls_smart_data_add_thermal_sensor (data, &sensor2);

    GArray *sensors = puls_smart_data_get_thermal_sensors (data);
    g_assert_nonnull (sensors);
    g_assert_cmpuint (sensors->len, ==, 2);

    PulsThermalSensor *item0 = &g_array_index (sensors, PulsThermalSensor, 0);
    g_assert_cmpstr (item0->name, ==, "Controller");
    g_assert_cmpint (item0->temperature, ==, 62);

    PulsThermalSensor *item1 = &g_array_index (sensors, PulsThermalSensor, 1);
    g_assert_cmpstr (item1->name, ==, "NAND Flash");
    g_assert_cmpint (item1->temperature, ==, 40);

    puls_smart_data_clear_thermal_sensors (data);
    g_assert_cmpuint (sensors->len, ==, 0);

    /* 5. Mock drives metrics validation */
    g_autoptr(PulsDiskManager) manager = puls_disk_manager_new ();
    puls_disk_manager_set_mock_mode (manager, TRUE);
    puls_disk_manager_scan (manager);

    PulsSmartData *nvme = puls_disk_manager_get_smart_data (manager, "/dev/nvme0n1");
    g_assert_nonnull (nvme);
    g_assert_cmpint (puls_smart_data_get_pcie_current_gen (nvme), ==, PULS_PCIE_GEN_4);
    g_assert_cmpuint (puls_smart_data_get_pcie_current_width (nvme), ==, 4);
    g_assert_cmpstr (puls_smart_data_get_protocol_version (nvme), ==, "NVMe 1.4");
    g_assert_cmpuint (puls_smart_data_get_tbw_rating_bytes (nvme), ==, 600000000000000ULL);
    g_assert_cmpint (puls_smart_data_get_wear_level_percent (nvme), ==, 1);
    g_assert_cmpuint (puls_smart_data_get_thermal_sensors (nvme)->len, ==, 3);

    PulsSmartData *sata = puls_disk_manager_get_smart_data (manager, "/dev/sda");
    g_assert_nonnull (sata);
    g_assert_cmpint (puls_smart_data_get_sector_format (sata), ==, PULS_SECTOR_FORMAT_512N);
    g_assert_cmpint (puls_smart_data_get_wear_level_percent (sata), ==, 22);

    PulsSmartData *hdd = puls_disk_manager_get_smart_data (manager, "/dev/sdb");
    g_assert_nonnull (hdd);
    g_assert_cmpint (puls_smart_data_get_sector_format (hdd), ==, PULS_SECTOR_FORMAT_512E);
}

static void
test_sysfs_helpers (void)
{
    /* 1. Device name sanitization */
    g_autofree gchar *s1 = puls_sanitize_device_name ("/dev/sda");
    g_assert_cmpstr (s1, ==, "sda");

    g_autofree gchar *s2 = puls_sanitize_device_name ("nvme0n1");
    g_assert_cmpstr (s2, ==, "nvme0n1");

    g_autofree gchar *s3 = puls_sanitize_device_name ("/dev/mapper/mpatha");
    g_assert_cmpstr (s3, ==, "mpatha");

    g_assert_null (puls_sanitize_device_name (NULL));
    g_assert_null (puls_sanitize_device_name (""));
    g_assert_null (puls_sanitize_device_name ("."));
    g_assert_null (puls_sanitize_device_name (".."));
    g_assert_null (puls_sanitize_device_name ("../sda"));
    g_assert_null (puls_sanitize_device_name ("sda;rm"));

    /* 2. Sysfs block path builder */
    g_autofree gchar *p1 = puls_sysfs_get_block_path ("sda", "queue/rotational");
    g_assert_cmpstr (p1, ==, "/sys/block/sda/queue/rotational");

    g_autofree gchar *p2 = puls_sysfs_get_block_path ("/dev/nvme0n1", "size");
    g_assert_cmpstr (p2, ==, "/sys/block/nvme0n1/size");

    /* Prevent directory traversal */
    g_assert_null (puls_sysfs_get_block_path ("sda", "../etc/passwd"));
    g_assert_null (puls_sysfs_get_block_path (NULL, "size"));

    /* 3. File reading against a temporary mock hierarchy */
    GError *error = NULL;
    g_autofree gchar *tmp_dir = g_dir_make_tmp ("puls-sysfs-test-XXXXXX", &error);
    g_assert_no_error (error);
    g_assert_nonnull (tmp_dir);

    g_autofree gchar *file_size = g_build_filename (tmp_dir, "size", NULL);
    g_autofree gchar *file_model = g_build_filename (tmp_dir, "model", NULL);
    g_autofree gchar *file_temp = g_build_filename (tmp_dir, "temp", NULL);
    g_autofree gchar *file_large = g_build_filename (tmp_dir, "large", NULL);

    g_file_set_contents (file_size, "2097152000\n", -1, &error);
    g_assert_no_error (error);

    g_file_set_contents (file_model, "  Samsung SSD 980 PRO \r\n", -1, &error);
    g_assert_no_error (error);

    g_file_set_contents (file_temp, "-42\n", -1, &error);
    g_assert_no_error (error);

    gchar large_buf[512];
    memset (large_buf, 'A', sizeof (large_buf) - 1);
    large_buf[sizeof (large_buf) - 1] = '\0';
    g_file_set_contents (file_large, large_buf, -1, &error);
    g_assert_no_error (error);

    /* Test puls_sysfs_read_string */
    g_autofree gchar *read_model = puls_sysfs_read_string (file_model, 64);
    g_assert_cmpstr (read_model, ==, "Samsung SSD 980 PRO");

    g_autofree gchar *capped = puls_sysfs_read_string (file_large, 10);
    g_assert_nonnull (capped);
    g_assert_cmpuint (strlen (capped), ==, 10);

    /* Test non-existent & invalid paths */
    g_autofree gchar *nonexistent_path = g_build_filename (tmp_dir, "does_not_exist", NULL);
    g_assert_null (puls_sysfs_read_string (nonexistent_path, 64));
    g_assert_null (puls_sysfs_read_string ("relative/path", 64));
    g_assert_null (puls_sysfs_read_string (NULL, 64));

    /* Test puls_sysfs_read_uint64 */
    gboolean ok = FALSE;
    guint64 sz = puls_sysfs_read_uint64 (file_size, 0, &ok);
    g_assert_true (ok);
    g_assert_cmpuint (sz, ==, 2097152000ULL);

    guint64 def_sz = puls_sysfs_read_uint64 (nonexistent_path, 999, &ok);
    g_assert_false (ok);
    g_assert_cmpuint (def_sz, ==, 999);

    /* Test puls_sysfs_read_int64 */
    gint64 temp = puls_sysfs_read_int64 (file_temp, 0, &ok);
    g_assert_true (ok);
    g_assert_cmpint (temp, ==, -42);

    /* Cleanup temporary files */
    unlink (file_size);
    unlink (file_model);
    unlink (file_temp);
    unlink (file_large);
    rmdir (tmp_dir);
}

static void
test_hwmon_temperature (void)
{
    /* 1. Test with mock hwmon directory */
    GError *error = NULL;
    g_autofree gchar *tmp_dir = g_dir_make_tmp ("puls-hwmon-test-XXXXXX", &error);
    g_assert_no_error (error);
    g_assert_nonnull (tmp_dir);

    g_autofree gchar *t1_in = g_build_filename (tmp_dir, "temp1_input", NULL);
    g_autofree gchar *t1_lbl = g_build_filename (tmp_dir, "temp1_label", NULL);
    g_autofree gchar *t1_max = g_build_filename (tmp_dir, "temp1_max", NULL);
    g_autofree gchar *t1_crit = g_build_filename (tmp_dir, "temp1_crit", NULL);
    g_autofree gchar *t2_in = g_build_filename (tmp_dir, "temp2_input", NULL);
    g_autofree gchar *t2_lbl = g_build_filename (tmp_dir, "temp2_label", NULL);
    g_autofree gchar *t2_crit = g_build_filename (tmp_dir, "temp2_crit", NULL);

    g_file_set_contents (t1_in, "41850\n", -1, &error);
    g_assert_no_error (error);
    g_file_set_contents (t1_lbl, "Composite\n", -1, &error);
    g_assert_no_error (error);
    g_file_set_contents (t1_max, "70000\n", -1, &error);
    g_assert_no_error (error);
    g_file_set_contents (t1_crit, "80000\n", -1, &error);
    g_assert_no_error (error);

    g_file_set_contents (t2_in, "48500\n", -1, &error);
    g_assert_no_error (error);
    g_file_set_contents (t2_lbl, "Sensor 1\n", -1, &error);
    g_assert_no_error (error);
    g_file_set_contents (t2_crit, "90000\n", -1, &error);
    g_assert_no_error (error);

    /* Direct reading from directory */
    gint temp = puls_sysfs_read_hwmon_temp_from_dir (tmp_dir);
    g_assert_cmpint (temp, ==, 42);

    GList *sensors = puls_sysfs_read_hwmon_sensors_from_dir (tmp_dir);
    g_assert_nonnull (sensors);
    g_assert_cmpuint (g_list_length (sensors), ==, 2);

    PulsThermalSensor *s1 = sensors->data;
    g_assert_cmpstr (s1->name, ==, "Composite");
    g_assert_cmpint (s1->temperature, ==, 42);
    g_assert_cmpint (s1->warning_temp, ==, 70);
    g_assert_cmpint (s1->critical_temp, ==, 80);

    PulsThermalSensor *s2 = sensors->next->data;
    g_assert_cmpstr (s2->name, ==, "Sensor 1");
    g_assert_cmpint (s2->temperature, ==, 49);
    g_assert_cmpint (s2->warning_temp, ==, -1);
    g_assert_cmpint (s2->critical_temp, ==, 90);

    g_list_free_full (sensors, (GDestroyNotify)puls_thermal_sensor_free);

    unlink (t1_in);
    unlink (t1_lbl);
    unlink (t1_max);
    unlink (t1_crit);
    unlink (t2_in);
    unlink (t2_lbl);
    unlink (t2_crit);
    rmdir (tmp_dir);

    /* 2. Test live hardware if available */
    if (g_file_test ("/sys/block/nvme0n1", G_FILE_TEST_EXISTS)) {
        gint live_temp = puls_sysfs_read_hwmon_temp ("nvme0n1");
        if (live_temp >= 0) {
            g_assert_cmpint (live_temp, >=, 0);
            g_assert_cmpint (live_temp, <=, 120);

            /* Also test with /dev/ path prefix (allow ±2 °C for live thermal sensor jitter) */
            gint path_temp = puls_sysfs_read_hwmon_temp ("/dev/nvme0n1");
            g_assert_cmpint (path_temp, >=, 0);
            g_assert_cmpint (path_temp, <=, 120);
            g_assert_cmpint (abs (path_temp - live_temp), <=, 2);

            GList *live_sensors = puls_sysfs_read_hwmon_sensors ("nvme0n1");
            g_assert_nonnull (live_sensors);
            g_list_free_full (live_sensors, (GDestroyNotify)puls_thermal_sensor_free);
        }
    }
}

static void
test_queue_attributes_trim (void)
{
    /* 1. PulsSmartData discard accessors & auto-trim trigger */
    g_autoptr(PulsSmartData) d = puls_smart_data_new ();
    g_assert_cmpuint (puls_smart_data_get_discard_granularity (d), ==, 0);
    g_assert_cmpuint (puls_smart_data_get_discard_max_bytes (d), ==, 0);
    g_assert_false (puls_smart_data_get_supports_trim (d));

    puls_smart_data_set_discard_granularity (d, 512);
    puls_smart_data_set_discard_max_bytes (d, 2199023255040ULL);
    g_assert_cmpuint (puls_smart_data_get_discard_granularity (d), ==, 512);
    g_assert_cmpuint (puls_smart_data_get_discard_max_bytes (d), ==, 2199023255040ULL);
    g_assert_true (puls_smart_data_get_supports_trim (d));

    /* 2. Mock queue directory test */
    GError *error = NULL;
    g_autofree gchar *tmp_dir = g_dir_make_tmp ("puls-queue-mock-XXXXXX", &error);
    g_assert_no_error (error);
    g_assert_nonnull (tmp_dir);

    g_autofree gchar *p_disc_gran = g_build_filename (tmp_dir, "discard_granularity", NULL);
    g_autofree gchar *p_disc_max  = g_build_filename (tmp_dir, "discard_max_bytes", NULL);
    g_autofree gchar *p_disc_hw   = g_build_filename (tmp_dir, "discard_max_hw_bytes", NULL);
    g_autofree gchar *p_rot       = g_build_filename (tmp_dir, "rotational", NULL);
    g_autofree gchar *p_lbs       = g_build_filename (tmp_dir, "logical_block_size", NULL);
    g_autofree gchar *p_pbs       = g_build_filename (tmp_dir, "physical_block_size", NULL);
    g_autofree gchar *p_opt_io    = g_build_filename (tmp_dir, "optimal_io_size", NULL);
    g_autofree gchar *p_min_io    = g_build_filename (tmp_dir, "minimum_io_size", NULL);
    g_autofree gchar *p_wcache    = g_build_filename (tmp_dir, "write_cache", NULL);
    g_autofree gchar *p_max_seg   = g_build_filename (tmp_dir, "max_segments", NULL);
    g_autofree gchar *p_max_sec   = g_build_filename (tmp_dir, "max_sectors_kb", NULL);

    g_file_set_contents (p_disc_gran, "512\n", -1, &error);
    g_assert_no_error (error);
    g_file_set_contents (p_disc_max, "2199023255040\n", -1, &error);
    g_assert_no_error (error);
    g_file_set_contents (p_disc_hw, "2199023255040\n", -1, &error);
    g_assert_no_error (error);
    g_file_set_contents (p_rot, "0\n", -1, &error);
    g_assert_no_error (error);
    g_file_set_contents (p_lbs, "512\n", -1, &error);
    g_assert_no_error (error);
    g_file_set_contents (p_pbs, "4096\n", -1, &error);
    g_assert_no_error (error);
    g_file_set_contents (p_opt_io, "131072\n", -1, &error);
    g_assert_no_error (error);
    g_file_set_contents (p_min_io, "4096\n", -1, &error);
    g_assert_no_error (error);
    g_file_set_contents (p_wcache, "write back\n", -1, &error);
    g_assert_no_error (error);
    g_file_set_contents (p_max_seg, "128\n", -1, &error);
    g_assert_no_error (error);
    g_file_set_contents (p_max_sec, "1280\n", -1, &error);
    g_assert_no_error (error);

    PulsQueueAttributes attrs;
    gboolean read_ok = puls_sysfs_read_queue_attributes_from_dir (tmp_dir, &attrs);
    g_assert_true (read_ok);
    g_assert_cmpuint (attrs.discard_granularity, ==, 512);
    g_assert_cmpuint (attrs.discard_max_bytes, ==, 2199023255040ULL);
    g_assert_cmpuint (attrs.discard_max_hw_bytes, ==, 2199023255040ULL);
    g_assert_true (attrs.supports_discard);
    g_assert_false (attrs.rotational);
    g_assert_cmpuint (attrs.logical_block_size, ==, 512);
    g_assert_cmpuint (attrs.physical_block_size, ==, 4096);
    g_assert_cmpuint (attrs.optimal_io_size, ==, 131072);
    g_assert_cmpuint (attrs.minimum_io_size, ==, 4096);
    g_assert_cmpstr (attrs.write_cache, ==, "write back");
    g_assert_true (attrs.write_cache_enabled);
    g_assert_cmpuint (attrs.max_segments, ==, 128);
    g_assert_cmpuint (attrs.max_sectors_kb, ==, 1280);

    /* Test 512e sector format detection */
    g_assert_cmpint (puls_sector_format_detect (attrs.logical_block_size, attrs.physical_block_size),
                     ==, PULS_SECTOR_FORMAT_512E);

    /* Test rotational / no-discard modification */
    g_file_set_contents (p_rot, "1\n", -1, &error);
    g_assert_no_error (error);
    g_file_set_contents (p_disc_max, "0\n", -1, &error);
    g_assert_no_error (error);
    g_file_set_contents (p_wcache, "none\n", -1, &error);
    g_assert_no_error (error);

    read_ok = puls_sysfs_read_queue_attributes_from_dir (tmp_dir, &attrs);
    g_assert_true (read_ok);
    g_assert_true (attrs.rotational);
    g_assert_false (attrs.supports_discard);
    g_assert_false (attrs.write_cache_enabled);

    /* Cleanup mock files */
    unlink (p_disc_gran);
    unlink (p_disc_max);
    unlink (p_disc_hw);
    unlink (p_rot);
    unlink (p_lbs);
    unlink (p_pbs);
    unlink (p_opt_io);
    unlink (p_min_io);
    unlink (p_wcache);
    unlink (p_max_seg);
    unlink (p_max_sec);
    rmdir (tmp_dir);

    /* 3. Edge cases and invalid inputs */
    memset (&attrs, 0, sizeof (attrs));
    g_assert_false (puls_sysfs_read_queue_attributes_from_dir (NULL, &attrs));
    g_assert_false (puls_sysfs_read_queue_attributes_from_dir ("/path/does/not/exist", &attrs));
    g_assert_false (puls_sysfs_read_queue_attributes (NULL, &attrs));
    g_assert_false (puls_sysfs_read_queue_attributes ("../invalid", &attrs));
    g_assert_cmpuint (puls_sysfs_read_queue_uint64 ("nonexistent_dev_xyz", "discard_max_bytes", 777), ==, 777);
    g_assert_null (puls_sysfs_read_queue_string ("nonexistent_dev_xyz", "write_cache", 32));
    g_assert_false (puls_sysfs_detect_trim_support ("nonexistent_dev_xyz"));

    /* 4. Live hardware testing */
    if (g_file_test ("/sys/block/nvme0n1/queue", G_FILE_TEST_IS_DIR)) {
        PulsQueueAttributes nvme_attrs;
        gboolean nvme_ok = puls_sysfs_read_queue_attributes ("nvme0n1", &nvme_attrs);
        g_assert_true (nvme_ok);
        g_assert_cmpuint (nvme_attrs.logical_block_size, >=, 512);
        g_assert_cmpuint (nvme_attrs.physical_block_size, >=, 512);
        g_assert_false (nvme_attrs.rotational);
        g_assert_true (nvme_attrs.supports_discard);
        g_assert_true (puls_sysfs_detect_trim_support ("nvme0n1"));
        g_assert_true (puls_sysfs_detect_trim_support ("/dev/nvme0n1"));

        /* With device path */
        PulsQueueAttributes dev_attrs;
        g_assert_true (puls_sysfs_read_queue_attributes ("/dev/nvme0n1", &dev_attrs));
        g_assert_cmpuint (dev_attrs.logical_block_size, ==, nvme_attrs.logical_block_size);
        g_assert_cmpuint (dev_attrs.discard_granularity, ==, nvme_attrs.discard_granularity);

        /* Granular reader */
        guint64 r_rot = puls_sysfs_read_queue_uint64 ("nvme0n1", "rotational", 999);
        g_assert_cmpuint (r_rot, ==, 0);
    }

    if (g_file_test ("/sys/block/loop0/queue", G_FILE_TEST_IS_DIR)) {
        PulsQueueAttributes loop_attrs;
        gboolean loop_ok = puls_sysfs_read_queue_attributes ("loop0", &loop_attrs);
        g_assert_true (loop_ok);
        g_assert_cmpuint (loop_attrs.logical_block_size, >=, 512);
    }
}

static void
test_pcie_link_inspector (void)
{
    /* 1. PCIe GT/s speed to generation translation */
    g_assert_cmpint (puls_pcie_speed_to_generation ("16.0 GT/s PCIe"), ==, PULS_PCIE_GEN_4);
    g_assert_cmpint (puls_pcie_speed_to_generation ("16.0 GT/s"),      ==, PULS_PCIE_GEN_4);
    g_assert_cmpint (puls_pcie_speed_to_generation ("16 GT/s"),        ==, PULS_PCIE_GEN_4);
    g_assert_cmpint (puls_pcie_speed_to_generation ("2.5 GT/s"),       ==, PULS_PCIE_GEN_1);
    g_assert_cmpint (puls_pcie_speed_to_generation ("5.0 GT/s"),       ==, PULS_PCIE_GEN_2);
    g_assert_cmpint (puls_pcie_speed_to_generation ("8.0 GT/s"),       ==, PULS_PCIE_GEN_3);
    g_assert_cmpint (puls_pcie_speed_to_generation ("32.0 GT/s"),      ==, PULS_PCIE_GEN_5);
    g_assert_cmpint (puls_pcie_speed_to_generation ("64.0 GT/s"),      ==, PULS_PCIE_GEN_6);
    g_assert_cmpint (puls_pcie_speed_to_generation (NULL),             ==, PULS_PCIE_GEN_UNKNOWN);
    g_assert_cmpint (puls_pcie_speed_to_generation ("invalid"),        ==, PULS_PCIE_GEN_UNKNOWN);

    /* 2. Generation to max speed string */
    g_assert_cmpstr (puls_pcie_generation_to_max_speed_str (PULS_PCIE_GEN_1), ==, "2.5 GT/s");
    g_assert_cmpstr (puls_pcie_generation_to_max_speed_str (PULS_PCIE_GEN_2), ==, "5.0 GT/s");
    g_assert_cmpstr (puls_pcie_generation_to_max_speed_str (PULS_PCIE_GEN_3), ==, "8.0 GT/s");
    g_assert_cmpstr (puls_pcie_generation_to_max_speed_str (PULS_PCIE_GEN_4), ==, "16.0 GT/s");
    g_assert_cmpstr (puls_pcie_generation_to_max_speed_str (PULS_PCIE_GEN_5), ==, "32.0 GT/s");
    g_assert_cmpstr (puls_pcie_generation_to_max_speed_str (PULS_PCIE_GEN_6), ==, "64.0 GT/s");
    g_assert_cmpstr (puls_pcie_generation_to_max_speed_str (PULS_PCIE_GEN_UNKNOWN), ==, "Unknown");

    /* 3. PCIe link description formatting */
    g_autofree gchar *f1 = puls_format_pcie_link (PULS_PCIE_GEN_4, 4, PULS_PCIE_GEN_4, 4);
    g_assert_cmpstr (f1, ==, "PCIe 4.0 x4 (Current: PCIe 4.0 x4 | Max: PCIe 4.0 x4)");

    g_autofree gchar *f2 = puls_format_pcie_link (PULS_PCIE_GEN_3, 2, PULS_PCIE_GEN_4, 4);
    g_assert_cmpstr (f2, ==, "PCIe 3.0 x2 (Current: PCIe 3.0 x2 | Max: PCIe 4.0 x4)");

    g_autofree gchar *f3 = puls_format_pcie_link (PULS_PCIE_GEN_4, 4, PULS_PCIE_GEN_UNKNOWN, 0);
    g_assert_cmpstr (f3, ==, "PCIe 4.0 x4");

    g_assert_null (puls_format_pcie_link (PULS_PCIE_GEN_UNKNOWN, 0, PULS_PCIE_GEN_UNKNOWN, 0));

    /* 4. Mock sysfs PCIe hierarchy */
    GError *error = NULL;
    g_autofree gchar *tmp_dir = g_dir_make_tmp ("puls-pcie-mock-XXXXXX", &error);
    g_assert_no_error (error);
    g_assert_nonnull (tmp_dir);

    g_autofree gchar *p_curr_spd = g_build_filename (tmp_dir, "current_link_speed", NULL);
    g_autofree gchar *p_curr_w   = g_build_filename (tmp_dir, "current_link_width", NULL);
    g_autofree gchar *p_max_spd  = g_build_filename (tmp_dir, "max_link_speed", NULL);
    g_autofree gchar *p_max_w    = g_build_filename (tmp_dir, "max_link_width", NULL);

    g_file_set_contents (p_curr_spd, "16.0 GT/s PCIe\n", -1, &error);
    g_assert_no_error (error);
    g_file_set_contents (p_curr_w, "4\n", -1, &error);
    g_assert_no_error (error);
    g_file_set_contents (p_max_spd, "16.0 GT/s PCIe\n", -1, &error);
    g_assert_no_error (error);
    g_file_set_contents (p_max_w, "4\n", -1, &error);
    g_assert_no_error (error);

    PulsPcieLinkInfo link_info = { 0 };
    gboolean read_ok = puls_sysfs_read_pcie_link_from_dir (tmp_dir, &link_info);
    g_assert_true (read_ok);
    g_assert_cmpint (link_info.current_gen, ==, PULS_PCIE_GEN_4);
    g_assert_cmpuint (link_info.current_width, ==, 4);
    g_assert_cmpint (link_info.max_gen, ==, PULS_PCIE_GEN_4);
    g_assert_cmpuint (link_info.max_width, ==, 4);
    g_assert_false (link_info.is_downgraded);
    g_assert_cmpstr (link_info.description, ==, "PCIe 4.0 x4 (Current: PCIe 4.0 x4 | Max: PCIe 4.0 x4)");
    puls_pcie_link_info_clear (&link_info);

    /* Test degraded link state */
    g_file_set_contents (p_curr_spd, "8.0 GT/s PCIe\n", -1, &error);
    g_assert_no_error (error);
    g_file_set_contents (p_curr_w, "2\n", -1, &error);
    g_assert_no_error (error);

    read_ok = puls_sysfs_read_pcie_link_from_dir (tmp_dir, &link_info);
    g_assert_true (read_ok);
    g_assert_cmpint (link_info.current_gen, ==, PULS_PCIE_GEN_3);
    g_assert_cmpuint (link_info.current_width, ==, 2);
    g_assert_cmpint (link_info.max_gen, ==, PULS_PCIE_GEN_4);
    g_assert_cmpuint (link_info.max_width, ==, 4);
    g_assert_true (link_info.is_downgraded);
    g_assert_cmpstr (link_info.description, ==, "PCIe 3.0 x2 (Current: PCIe 3.0 x2 | Max: PCIe 4.0 x4)");
    puls_pcie_link_info_clear (&link_info);

    /* Cleanup mock files */
    unlink (p_curr_spd);
    unlink (p_curr_w);
    unlink (p_max_spd);
    unlink (p_max_w);
    rmdir (tmp_dir);

    /* 5. Edge cases & invalid inputs */
    PulsPcieLinkInfo empty_info = { 0 };
    g_assert_false (puls_sysfs_read_pcie_link_from_dir (NULL, &empty_info));
    g_assert_false (puls_sysfs_read_pcie_link_from_dir ("/path/does/not/exist", &empty_info));
    g_assert_false (puls_sysfs_read_pcie_link (NULL, &empty_info));
    g_assert_false (puls_sysfs_read_pcie_link ("../invalid", &empty_info));
    g_assert_null (puls_sysfs_find_pcie_dir (NULL));
    g_assert_null (puls_sysfs_find_pcie_dir ("nonexistent_device_xyz"));

    /* 6. Live hardware detection if nvme0n1 is available */
    if (g_file_test ("/sys/block/nvme0n1/device/device/current_link_speed", G_FILE_TEST_EXISTS)) {
        g_autofree gchar *pci_dir = puls_sysfs_find_pcie_dir ("nvme0n1");
        g_assert_nonnull (pci_dir);

        PulsPcieLinkInfo live_info = { 0 };
        gboolean live_ok = puls_sysfs_read_pcie_link ("nvme0n1", &live_info);
        g_assert_true (live_ok);
        g_assert_cmpint (live_info.current_gen, >=, PULS_PCIE_GEN_1);
        g_assert_cmpuint (live_info.current_width, >=, 1);
        g_assert_nonnull (live_info.description);

        /* Test path alias */
        PulsPcieLinkInfo dev_info = { 0 };
        g_assert_true (puls_sysfs_read_pcie_link ("/dev/nvme0n1", &dev_info));
        g_assert_cmpint (dev_info.current_gen, ==, live_info.current_gen);
        g_assert_cmpuint (dev_info.current_width, ==, live_info.current_width);

        puls_pcie_link_info_clear (&live_info);
        puls_pcie_link_info_clear (&dev_info);
    }
}

static void
test_usb_speed_and_bridge_inspector (void)
{
    /* 1. Enum mappings & Mbps rate conversions */
    g_assert_cmpint (puls_usb_speed_from_mbps (1),     ==, PULS_USB_SPEED_LOW);
    g_assert_cmpint (puls_usb_speed_from_mbps (12),    ==, PULS_USB_SPEED_FULL);
    g_assert_cmpint (puls_usb_speed_from_mbps (480),   ==, PULS_USB_SPEED_HIGH);
    g_assert_cmpint (puls_usb_speed_from_mbps (5000),  ==, PULS_USB_SPEED_SUPER);
    g_assert_cmpint (puls_usb_speed_from_mbps (10000), ==, PULS_USB_SPEED_SUPER_PLUS);
    g_assert_cmpint (puls_usb_speed_from_mbps (20000), ==, PULS_USB_SPEED_SUPER_PLUS_X2);
    g_assert_cmpint (puls_usb_speed_from_mbps (40000), ==, PULS_USB_SPEED_USB4_40G);
    g_assert_cmpint (puls_usb_speed_from_mbps (80000), ==, PULS_USB_SPEED_USB4_80G);
    g_assert_cmpint (puls_usb_speed_from_mbps (0),     ==, PULS_USB_SPEED_UNKNOWN);

    /* 2. String parsing */
    g_assert_cmpint (puls_usb_speed_from_string ("480"),         ==, PULS_USB_SPEED_HIGH);
    g_assert_cmpint (puls_usb_speed_from_string ("480M"),        ==, PULS_USB_SPEED_HIGH);
    g_assert_cmpint (puls_usb_speed_from_string ("USB 2.0"),     ==, PULS_USB_SPEED_HIGH);
    g_assert_cmpint (puls_usb_speed_from_string ("5000"),        ==, PULS_USB_SPEED_SUPER);
    g_assert_cmpint (puls_usb_speed_from_string ("5G"),          ==, PULS_USB_SPEED_SUPER);
    g_assert_cmpint (puls_usb_speed_from_string ("USB 3.0"),     ==, PULS_USB_SPEED_SUPER);
    g_assert_cmpint (puls_usb_speed_from_string ("10000"),       ==, PULS_USB_SPEED_SUPER_PLUS);
    g_assert_cmpint (puls_usb_speed_from_string ("10G"),         ==, PULS_USB_SPEED_SUPER_PLUS);
    g_assert_cmpint (puls_usb_speed_from_string ("USB 3.1"),     ==, PULS_USB_SPEED_SUPER_PLUS);
    g_assert_cmpint (puls_usb_speed_from_string ("20000"),       ==, PULS_USB_SPEED_SUPER_PLUS_X2);
    g_assert_cmpint (puls_usb_speed_from_string ("20G"),         ==, PULS_USB_SPEED_SUPER_PLUS_X2);
    g_assert_cmpint (puls_usb_speed_from_string ("Gen 2x2"),     ==, PULS_USB_SPEED_SUPER_PLUS_X2);
    g_assert_cmpint (puls_usb_speed_from_string ("40000"),       ==, PULS_USB_SPEED_USB4_40G);
    g_assert_cmpint (puls_usb_speed_from_string ("40G"),         ==, PULS_USB_SPEED_USB4_40G);
    g_assert_cmpint (puls_usb_speed_from_string ("USB4"),        ==, PULS_USB_SPEED_USB4_40G);
    g_assert_cmpint (puls_usb_speed_from_string (NULL),          ==, PULS_USB_SPEED_UNKNOWN);
    g_assert_cmpint (puls_usb_speed_from_string ("nonexistent"), ==, PULS_USB_SPEED_UNKNOWN);

    /* 3. Generation badges */
    g_assert_cmpstr (puls_usb_speed_to_generation_badge (PULS_USB_SPEED_LOW),           ==, "USB 1.0 (1.5M)");
    g_assert_cmpstr (puls_usb_speed_to_generation_badge (PULS_USB_SPEED_FULL),          ==, "USB 1.1 (12M)");
    g_assert_cmpstr (puls_usb_speed_to_generation_badge (PULS_USB_SPEED_HIGH),          ==, "USB 2.0 (480M)");
    g_assert_cmpstr (puls_usb_speed_to_generation_badge (PULS_USB_SPEED_SUPER),         ==, "USB 3.0 (5G)");
    g_assert_cmpstr (puls_usb_speed_to_generation_badge (PULS_USB_SPEED_SUPER_PLUS),    ==, "USB 3.1 (10G)");
    g_assert_cmpstr (puls_usb_speed_to_generation_badge (PULS_USB_SPEED_SUPER_PLUS_X2), ==, "USB 3.2 (20G)");
    g_assert_cmpstr (puls_usb_speed_to_generation_badge (PULS_USB_SPEED_USB4_40G),      ==, "USB4 (40G)");
    g_assert_cmpstr (puls_usb_speed_to_generation_badge (PULS_USB_SPEED_USB4_80G),      ==, "USB4 (80G)");
    g_assert_cmpstr (puls_usb_speed_to_generation_badge (PULS_USB_SPEED_UNKNOWN),       ==, "USB");

    /* 4. Bridge chip identification database */
    g_autofree gchar *b1 = puls_identify_usb_bridge_chip ("174c", "2362", "ASMedia", "ASM2362 NVMe");
    g_assert_nonnull (b1);
    g_assert_true (strstr (b1, "ASM2362") != NULL);

    g_autofree gchar *b2 = puls_identify_usb_bridge_chip ("0bda", "9210", "Realtek", "RTL9210B-CG");
    g_assert_nonnull (b2);
    g_assert_true (strstr (b2, "RTL9210") != NULL);

    g_autofree gchar *b3 = puls_identify_usb_bridge_chip ("152d", "0583", "JMicron", "JMS583 NVMe");
    g_assert_nonnull (b3);
    g_assert_true (strstr (b3, "JMS583") != NULL);

    g_autofree gchar *b4 = puls_identify_usb_bridge_chip ("174c", "2364", "ASMedia", "ASM2364");
    g_assert_nonnull (b4);
    g_assert_true (strstr (b4, "ASM2364") != NULL);

    g_assert_null (puls_identify_usb_bridge_chip (NULL, NULL, NULL, NULL));

    /* 5. Mock USB sysfs hierarchy */
    GError *error = NULL;
    g_autofree gchar *tmp_dir = g_dir_make_tmp ("puls-usb-mock-XXXXXX", &error);
    g_assert_no_error (error);
    g_assert_nonnull (tmp_dir);

    g_autofree gchar *f_spd = g_build_filename (tmp_dir, "speed", NULL);
    g_autofree gchar *f_ver = g_build_filename (tmp_dir, "version", NULL);
    g_autofree gchar *f_vid = g_build_filename (tmp_dir, "idVendor", NULL);
    g_autofree gchar *f_pid = g_build_filename (tmp_dir, "idProduct", NULL);
    g_autofree gchar *f_mfg = g_build_filename (tmp_dir, "manufacturer", NULL);
    g_autofree gchar *f_prd = g_build_filename (tmp_dir, "product", NULL);
    g_autofree gchar *f_ser = g_build_filename (tmp_dir, "serial", NULL);

    g_file_set_contents (f_spd, "10000\n", -1, &error);
    g_assert_no_error (error);
    g_file_set_contents (f_ver, " 3.10\n", -1, &error);
    g_assert_no_error (error);
    g_file_set_contents (f_vid, "174c\n", -1, &error);
    g_assert_no_error (error);
    g_file_set_contents (f_pid, "2362\n", -1, &error);
    g_assert_no_error (error);
    g_file_set_contents (f_mfg, "ASMedia\n", -1, &error);
    g_assert_no_error (error);
    g_file_set_contents (f_prd, "ASM2362 Enclosure\n", -1, &error);
    g_assert_no_error (error);
    g_file_set_contents (f_ser, "MS0987654321\n", -1, &error);
    g_assert_no_error (error);

    PulsUsbBridgeInfo usb_info = { 0 };
    gboolean read_ok = puls_sysfs_read_usb_bridge_info_from_dir (tmp_dir, &usb_info);
    g_assert_true (read_ok);
    g_assert_cmpint (usb_info.speed, ==, PULS_USB_SPEED_SUPER_PLUS);
    g_assert_cmpuint (usb_info.speed_mbps, ==, 10000);
    g_assert_cmpstr (usb_info.vendor_id, ==, "174c");
    g_assert_cmpstr (usb_info.product_id, ==, "2362");
    g_assert_nonnull (usb_info.bridge_chip);
    g_assert_true (strstr (usb_info.bridge_chip, "ASM2362") != NULL);
    g_assert_nonnull (usb_info.display_string);
    g_assert_true (strstr (usb_info.display_string, "USB 3.1 (10G)") != NULL);
    g_assert_true (strstr (usb_info.display_string, "ASM2362") != NULL);
    puls_usb_bridge_info_clear (&usb_info);

    /* Test USB 2.0 fallback */
    g_file_set_contents (f_spd, "480\n", -1, &error);
    g_assert_no_error (error);
    g_file_set_contents (f_vid, "058f\n", -1, &error);
    g_assert_no_error (error);
    g_file_set_contents (f_pid, "6387\n", -1, &error);
    g_assert_no_error (error);
    g_file_set_contents (f_prd, "Flash Disk\n", -1, &error);
    g_assert_no_error (error);

    read_ok = puls_sysfs_read_usb_bridge_info_from_dir (tmp_dir, &usb_info);
    g_assert_true (read_ok);
    g_assert_cmpint (usb_info.speed, ==, PULS_USB_SPEED_HIGH);
    g_assert_cmpuint (usb_info.speed_mbps, ==, 480);
    g_assert_nonnull (usb_info.display_string);
    g_assert_true (strstr (usb_info.display_string, "USB 2.0 (480M)") != NULL);
    puls_usb_bridge_info_clear (&usb_info);

    /* Cleanup mock files */
    unlink (f_spd);
    unlink (f_ver);
    unlink (f_vid);
    unlink (f_pid);
    unlink (f_mfg);
    unlink (f_prd);
    unlink (f_ser);
    rmdir (tmp_dir);

    /* 6. PulsSmartData accessors */
    g_autoptr(PulsSmartData) d = puls_smart_data_new ();
    g_assert_cmpint (puls_smart_data_get_usb_speed (d), ==, PULS_USB_SPEED_UNKNOWN);
    g_assert_null (puls_smart_data_get_usb_bridge_chip (d));

    puls_smart_data_set_usb_speed (d, PULS_USB_SPEED_SUPER_PLUS_X2);
    puls_smart_data_set_usb_bridge_chip (d, "ASMedia ASM2364 NVMe-USB Bridge");
    g_assert_cmpint (puls_smart_data_get_usb_speed (d), ==, PULS_USB_SPEED_SUPER_PLUS_X2);
    g_assert_cmpstr (puls_smart_data_get_usb_bridge_chip (d), ==, "ASMedia ASM2364 NVMe-USB Bridge");

    /* 7. Edge cases & error safety */
    PulsUsbBridgeInfo empty = { 0 };
    g_assert_false (puls_sysfs_read_usb_bridge_info_from_dir (NULL, &empty));
    g_assert_false (puls_sysfs_read_usb_bridge_info_from_dir ("/path/not/exist", &empty));
    g_assert_false (puls_sysfs_read_usb_bridge_info (NULL, &empty));
    g_assert_false (puls_sysfs_read_usb_bridge_info ("../invalid", &empty));
    g_assert_null (puls_sysfs_find_usb_device_dir (NULL));
    g_assert_null (puls_sysfs_find_usb_device_dir ("nonexistent_dev_xyz"));
    g_assert_null (puls_detect_usb_speed_version (NULL));
    g_assert_null (puls_detect_usb_speed_version ("nonexistent_dev_xyz"));
}

static void
test_partition_mount_mapper (void)
{
    /* 1. Edge cases and invalid arguments */
    g_assert_null (puls_get_disk_partitions_from_mounts (NULL, "/proc/mounts"));
    g_assert_null (puls_get_disk_partitions_from_mounts ("/dev/sda", NULL));
    g_assert_null (puls_get_disk_partitions_from_mounts ("", "/proc/mounts"));
    g_assert_null (puls_get_disk_partitions_from_mounts ("/dev/sda", ""));
    g_assert_null (puls_get_disk_partitions_from_mounts ("/dev/sda", "/nonexistent/test/path/mounts"));
    g_assert_null (puls_get_disk_partitions (NULL));
    g_assert_null (puls_get_disk_partitions (""));

    /* 2. Create mock /proc/mounts file with multi-disk and multi-partition data */
    gchar *tmp_mounts = g_build_filename (g_get_tmp_dir (), "puls_mock_mounts_XXXXXX", NULL);
    int fd = g_mkstemp (tmp_mounts);
    g_assert_cmpint (fd, !=, -1);

    const gchar *mock_content =
        "/dev/sda1 /boot ext4 rw,relatime 0 0\n"
        "/dev/sda2 / ext4 rw,relatime 0 0\n"
        "/dev/mapper/sda3_crypt /home ext4 rw,relatime 0 0\n"
        "/dev/sda1 /boot ext4 rw,relatime 0 0\n" /* Duplicate entry to verify deduplication */
        "/dev/nvme0n1p1 /boot/efi vfat rw,relatime 0 0\n"
        "/dev/nvme0n1p2 /var ext4 rw,relatime 0 0\n"
        "/dev/mapper/nvme0n1p3_crypt /opt btrfs rw,relatime 0 0\n"
        "/dev/sdb1 /mnt/backup xfs rw,relatime 0 0\n"
        "/dev/sdc1 /mnt/usb vfat rw,relatime 0 0\n"
        "proc /proc proc rw,nosuid,nodev,noexec,relatime 0 0\n"
        "sysfs /sys sysfs rw,nosuid,nodev,noexec,relatime 0 0\n"
        "tmpfs /tmp tmpfs rw,nosuid,nodev 0 0\n";

    ssize_t written = write (fd, mock_content, strlen (mock_content));
    g_assert_cmpint (written, ==, (ssize_t)strlen (mock_content));
    close (fd);

    /* 3. Query SATA disk partitions (/dev/sda) */
    GList *sda_parts = puls_get_disk_partitions_from_mounts ("/dev/sda", tmp_mounts);
    g_assert_nonnull (sda_parts);
    g_assert_cmpuint (g_list_length (sda_parts), ==, 3);

    PulsPartitionInfo *p1 = g_list_nth_data (sda_parts, 0);
    g_assert_nonnull (p1);
    g_assert_cmpstr (p1->device_path, ==, "/dev/sda1");
    g_assert_cmpstr (p1->mount_point, ==, "/boot");
    g_assert_cmpstr (p1->fs_type, ==, "ext4");

    PulsPartitionInfo *p2 = g_list_nth_data (sda_parts, 1);
    g_assert_nonnull (p2);
    g_assert_cmpstr (p2->device_path, ==, "/dev/sda2");
    g_assert_cmpstr (p2->mount_point, ==, "/");
    g_assert_cmpstr (p2->fs_type, ==, "ext4");

    PulsPartitionInfo *p3 = g_list_nth_data (sda_parts, 2);
    g_assert_nonnull (p3);
    g_assert_cmpstr (p3->device_path, ==, "/dev/mapper/sda3_crypt");
    g_assert_cmpstr (p3->mount_point, ==, "/home");
    g_assert_cmpstr (p3->fs_type, ==, "ext4");

    g_list_free_full (sda_parts, (GDestroyNotify)puls_partition_info_free);

    /* 4. Query bare disk name ("sda") */
    GList *sda_bare = puls_get_disk_partitions_from_mounts ("sda", tmp_mounts);
    g_assert_nonnull (sda_bare);
    g_assert_cmpuint (g_list_length (sda_bare), ==, 3);
    g_list_free_full (sda_bare, (GDestroyNotify)puls_partition_info_free);

    /* 5. Query NVMe disk partitions (/dev/nvme0n1) */
    GList *nvme_parts = puls_get_disk_partitions_from_mounts ("/dev/nvme0n1", tmp_mounts);
    g_assert_nonnull (nvme_parts);
    g_assert_cmpuint (g_list_length (nvme_parts), ==, 3);

    PulsPartitionInfo *np1 = g_list_nth_data (nvme_parts, 0);
    g_assert_nonnull (np1);
    g_assert_cmpstr (np1->device_path, ==, "/dev/nvme0n1p1");
    g_assert_cmpstr (np1->mount_point, ==, "/boot/efi");
    g_assert_cmpstr (np1->fs_type, ==, "vfat");

    PulsPartitionInfo *np2 = g_list_nth_data (nvme_parts, 1);
    g_assert_nonnull (np2);
    g_assert_cmpstr (np2->device_path, ==, "/dev/nvme0n1p2");
    g_assert_cmpstr (np2->mount_point, ==, "/var");
    g_assert_cmpstr (np2->fs_type, ==, "ext4");

    PulsPartitionInfo *np3 = g_list_nth_data (nvme_parts, 2);
    g_assert_nonnull (np3);
    g_assert_cmpstr (np3->device_path, ==, "/dev/mapper/nvme0n1p3_crypt");
    g_assert_cmpstr (np3->mount_point, ==, "/opt");
    g_assert_cmpstr (np3->fs_type, ==, "btrfs");

    g_list_free_full (nvme_parts, (GDestroyNotify)puls_partition_info_free);

    /* 6. Query secondary disk (/dev/sdb) */
    GList *sdb_parts = puls_get_disk_partitions_from_mounts ("/dev/sdb", tmp_mounts);
    g_assert_nonnull (sdb_parts);
    g_assert_cmpuint (g_list_length (sdb_parts), ==, 1);

    PulsPartitionInfo *sdbp = sdb_parts->data;
    g_assert_cmpstr (sdbp->device_path, ==, "/dev/sdb1");
    g_assert_cmpstr (sdbp->mount_point, ==, "/mnt/backup");
    g_assert_cmpstr (sdbp->fs_type, ==, "xfs");

    g_list_free_full (sdb_parts, (GDestroyNotify)puls_partition_info_free);

    /* 7. Query disk with no mounted partitions (/dev/sde) */
    GList *sde_parts = puls_get_disk_partitions_from_mounts ("/dev/sde", tmp_mounts);
    g_assert_null (sde_parts);

    /* Clean up mock mounts file */
    unlink (tmp_mounts);
    g_free (tmp_mounts);

    /* 8. Live /proc/mounts smoke test on the host system */
    GList *live_parts = puls_get_disk_partitions ("/dev/nvme0n1");
    if (live_parts != NULL) {
        for (GList *l = live_parts; l != NULL; l = l->next) {
            PulsPartitionInfo *lp = l->data;
            g_assert_nonnull (lp->device_path);
            g_assert_nonnull (lp->mount_point);
            g_assert_nonnull (lp->fs_type);
        }
        g_list_free_full (live_parts, (GDestroyNotify)puls_partition_info_free);
    }
}

static void
test_capacity_formatters (void)
{
    /* 1. Decimal Format (SI, Base 1000) */
    g_autofree gchar *dec_0 = puls_format_bytes_decimal (0);
    g_assert_cmpstr (dec_0, ==, "0 B");

    g_autofree gchar *dec_500 = puls_format_bytes_decimal (500);
    g_assert_cmpstr (dec_500, ==, "500 B");

    g_autofree gchar *dec_1k = puls_format_bytes_decimal (1000);
    g_assert_cmpstr (dec_1k, ==, "1.0 KB");

    g_autofree gchar *dec_1m = puls_format_bytes_decimal (1000000ULL);
    g_assert_cmpstr (dec_1m, ==, "1.0 MB");

    g_autofree gchar *dec_1g = puls_format_bytes_decimal (1000000000ULL);
    g_assert_cmpstr (dec_1g, ==, "1.0 GB");

    g_autofree gchar *dec_512g = puls_format_bytes_decimal (512110190592ULL);
    g_assert_cmpstr (dec_512g, ==, "512.1 GB");

    g_autofree gchar *dec_1t = puls_format_bytes_decimal (1000000000000ULL);
    g_assert_cmpstr (dec_1t, ==, "1.0 TB");

    g_autofree gchar *dec_2t = puls_format_bytes_decimal (2000000000000ULL);
    g_assert_cmpstr (dec_2t, ==, "2.0 TB");

    g_autofree gchar *dec_1p = puls_format_bytes_decimal (1000000000000000ULL);
    g_assert_cmpstr (dec_1p, ==, "1.0 PB");

    /* 2. Binary Format (IEC, Base 1024) */
    g_autofree gchar *bin_0 = puls_format_bytes_binary (0);
    g_assert_cmpstr (bin_0, ==, "0 B");

    g_autofree gchar *bin_512 = puls_format_bytes_binary (512);
    g_assert_cmpstr (bin_512, ==, "512 B");

    g_autofree gchar *bin_1k = puls_format_bytes_binary (1024);
    g_assert_cmpstr (bin_1k, ==, "1.0 KiB");

    g_autofree gchar *bin_1m = puls_format_bytes_binary (1024ULL * 1024ULL);
    g_assert_cmpstr (bin_1m, ==, "1.0 MiB");

    g_autofree gchar *bin_1g = puls_format_bytes_binary (1024ULL * 1024ULL * 1024ULL);
    g_assert_cmpstr (bin_1g, ==, "1.0 GiB");

    g_autofree gchar *bin_476g = puls_format_bytes_binary (512110190592ULL);
    g_assert_cmpstr (bin_476g, ==, "476.9 GiB");

    g_autofree gchar *bin_1t = puls_format_bytes_binary (1024ULL * 1024ULL * 1024ULL * 1024ULL);
    g_assert_cmpstr (bin_1t, ==, "1.0 TiB");

    g_autofree gchar *bin_931g = puls_format_bytes_binary (1000000000000ULL);
    g_assert_cmpstr (bin_931g, ==, "931.3 GiB");

    /* 3. Dual Format: Decimal with Binary in parentheses */
    g_autofree gchar *dual_512 = puls_format_bytes_dual (512110190592ULL);
    g_assert_cmpstr (dual_512, ==, "512.1 GB (476.9 GiB)");

    g_autofree gchar *dual_1t = puls_format_bytes_dual (1000000000000ULL);
    g_assert_cmpstr (dual_1t, ==, "1.0 TB (931.3 GiB)");

    g_autofree gchar *dual_small = puls_format_bytes_dual (512);
    g_assert_cmpstr (dual_small, ==, "512 B");

    /* 4. Format according to PulsCapacityUnit */
    g_autofree gchar *unit_dual = puls_format_capacity_unit (512110190592ULL, PULS_CAPACITY_UNIT_DUAL);
    g_assert_cmpstr (unit_dual, ==, "512.1 GB (476.9 GiB)");

    g_autofree gchar *unit_dec = puls_format_capacity_unit (512110190592ULL, PULS_CAPACITY_UNIT_DECIMAL);
    g_assert_cmpstr (unit_dec, ==, "512.1 GB");

    g_autofree gchar *unit_bin = puls_format_capacity_unit (512110190592ULL, PULS_CAPACITY_UNIT_BINARY);
    g_assert_cmpstr (unit_bin, ==, "476.9 GiB");

    /* 5. Format exact capacity representations */
    g_autofree gchar *exact_dual = puls_format_capacity_unit_exact (512110190592ULL, PULS_CAPACITY_UNIT_DUAL);
    g_assert_true (strstr (exact_dual, "512.1 GB (476.9 GiB)") != NULL);
    g_assert_true (strstr (exact_dual, "512,110,190,592") != NULL);

    g_autofree gchar *exact_dec = puls_format_capacity_unit_exact (512110190592ULL, PULS_CAPACITY_UNIT_DECIMAL);
    g_assert_cmpstr (exact_dec, ==, "512.1 GB (512,110,190,592 bytes)");

    g_autofree gchar *exact_bin = puls_format_capacity_unit_exact (512110190592ULL, PULS_CAPACITY_UNIT_BINARY);
    g_assert_cmpstr (exact_bin, ==, "476.9 GiB (512,110,190,592 bytes)");

    /* 6. PulsSettings user preference toggle */
    PulsSettings *settings = puls_settings_get_default ();
    g_assert_nonnull (settings);

    PulsCapacityUnit orig_unit = puls_settings_get_capacity_unit (settings);
    puls_settings_set_capacity_unit (settings, PULS_CAPACITY_UNIT_DECIMAL);
    g_assert_cmpint (puls_settings_get_capacity_unit (settings), ==, PULS_CAPACITY_UNIT_DECIMAL);

    puls_settings_set_capacity_unit (settings, PULS_CAPACITY_UNIT_BINARY);
    g_assert_cmpint (puls_settings_get_capacity_unit (settings), ==, PULS_CAPACITY_UNIT_BINARY);

    puls_settings_set_capacity_unit (settings, PULS_CAPACITY_UNIT_DUAL);
    g_assert_cmpint (puls_settings_get_capacity_unit (settings), ==, PULS_CAPACITY_UNIT_DUAL);

    puls_settings_set_capacity_unit (settings, orig_unit);
}

static void
test_phase2_final_integration (void)
{
    /* 1. Mock Mode Integration */
    PulsDiskManager *mgr_mock = puls_disk_manager_new ();
    g_assert_nonnull (mgr_mock);
    puls_disk_manager_set_mock_mode (mgr_mock, TRUE);
    g_assert_true (puls_disk_manager_get_mock_mode (mgr_mock));

    puls_disk_manager_scan (mgr_mock);
    guint mock_count = puls_disk_manager_get_device_count (mgr_mock);
    g_assert_cmpuint (mock_count, >, 0);

    GList *mock_devs = puls_disk_manager_get_devices (mgr_mock);
    g_assert_nonnull (mock_devs);

    for (GList *l = mock_devs; l != NULL; l = l->next) {
        const gchar *path = l->data;
        PulsSmartData *data = puls_disk_manager_get_smart_data (mgr_mock, path);
        g_assert_nonnull (data);
        g_assert_cmpstr (puls_smart_data_get_device_path (data), ==, path);
        g_assert_nonnull (puls_smart_data_get_model_name (data));
        g_assert_cmpuint (puls_smart_data_get_capacity_bytes (data), >, 0);
    }
    g_object_unref (mgr_mock);

    /* 2. Live Host Scan & Sysfs Fallback Engine Integration */
    PulsDiskManager *mgr_live = puls_disk_manager_new ();
    g_assert_nonnull (mgr_live);
    puls_disk_manager_set_mock_mode (mgr_live, FALSE);
    g_assert_false (puls_disk_manager_get_mock_mode (mgr_live));

    puls_disk_manager_scan (mgr_live);
    guint live_count = puls_disk_manager_get_device_count (mgr_live);

    if (live_count > 0) {
        GList *live_devs = puls_disk_manager_get_devices (mgr_live);
        for (GList *l = live_devs; l != NULL; l = l->next) {
            const gchar *path = l->data;
            PulsSmartData *data = puls_disk_manager_get_smart_data (mgr_live, path);
            g_assert_nonnull (data);
            g_assert_cmpstr (puls_smart_data_get_device_path (data), ==, path);

            /* Model, serial, firmware populated via sysfs */
            g_assert_nonnull (puls_smart_data_get_model_name (data));
            g_assert_nonnull (puls_smart_data_get_serial_number (data));
            g_assert_nonnull (puls_smart_data_get_firmware_version (data));

            /* Drive type, transport, and form factor */
            PulsDriveType dtype = puls_smart_data_get_drive_type (data);
            g_assert_cmpint (dtype, !=, PULS_DRIVE_TYPE_UNKNOWN);
            g_assert_nonnull (puls_drive_type_to_string (dtype));

            PulsTransportProtocol transport = puls_smart_data_get_transport_protocol (data);
            g_assert_nonnull (puls_transport_protocol_to_string (transport));

            const gchar *ff = puls_smart_data_get_form_factor (data);
            g_assert_nonnull (ff);

            /* Capacity */
            guint64 cap = puls_smart_data_get_capacity_bytes (data);
            g_assert_cmpuint (cap, >, 0);

            /* Queue attributes: sectors, trim, write cache */
            guint32 log_sec = puls_smart_data_get_logical_sector_size (data);
            g_assert_cmpuint (log_sec, >, 0);

            /* Partitions */
            GList *parts = puls_get_disk_partitions (path);
            if (parts != NULL) {
                for (GList *p = parts; p != NULL; p = p->next) {
                    PulsPartitionInfo *pinfo = p->data;
                    g_assert_nonnull (pinfo->device_path);
                    g_assert_nonnull (pinfo->mount_point);
                    g_assert_nonnull (pinfo->fs_type);
                }
                g_list_free_full (parts, (GDestroyNotify)puls_partition_info_free);
            }
        }
    }
    g_object_unref (mgr_live);
}

typedef struct {
    guint added_count;
    guint removed_count;
    guint updated_count;
    gchar *last_added;
    gchar *last_removed;
    gchar *last_updated;
} HotplugSignalStats;

static void
on_test_disk_added (PulsDiskManager *mgr G_GNUC_UNUSED,
                    const gchar     *device_path,
                    gpointer         user_data)
{
    HotplugSignalStats *stats = user_data;
    stats->added_count++;
    g_free (stats->last_added);
    stats->last_added = g_strdup (device_path);
}

static void
on_test_disk_removed (PulsDiskManager *mgr G_GNUC_UNUSED,
                      const gchar     *device_path,
                      gpointer         user_data)
{
    HotplugSignalStats *stats = user_data;
    stats->removed_count++;
    g_free (stats->last_removed);
    stats->last_removed = g_strdup (device_path);
}

static void
on_test_disk_updated (PulsDiskManager *mgr G_GNUC_UNUSED,
                      const gchar     *device_path,
                      gpointer         user_data)
{
    HotplugSignalStats *stats = user_data;
    stats->updated_count++;
    g_free (stats->last_updated);
    stats->last_updated = g_strdup (device_path);
}

static void
test_hotplug_monitoring (void)
{
    /* 1. Validate raw netlink uevent payload parsing */
    const gchar add_payload[] =
        "add@/devices/pci0000:00/0000:00:14.0/usb1/block/sdz\0"
        "ACTION=add\0"
        "DEVPATH=/devices/pci0000:00/0000:00:14.0/usb1/block/sdz\0"
        "SUBSYSTEM=block\0"
        "DEVNAME=sdz\0"
        "DEVTYPE=disk\0"
        "SEQNUM=4201\0";

    PulsUeventInfo info;
    g_assert_true (puls_disk_manager_parse_uevent (add_payload, sizeof (add_payload), &info));
    g_assert_cmpint (info.action, ==, PULS_UEVENT_ADD);
    g_assert_cmpstr (info.subsystem, ==, "block");
    g_assert_cmpstr (info.devtype, ==, "disk");
    g_assert_cmpstr (info.devname, ==, "/dev/sdz");
    g_assert_cmpstr (info.devpath, ==, "/devices/pci0000:00/0000:00:14.0/usb1/block/sdz");
    puls_uevent_info_clear (&info);
    g_assert_cmpint (info.action, ==, PULS_UEVENT_UNKNOWN);
    g_assert_null (info.devname);

    /* 2. Simulated USB drive attachment & detachment signals */
    g_autoptr(PulsDiskManager) mgr = puls_disk_manager_new ();
    puls_disk_manager_set_mock_mode (mgr, TRUE);

    HotplugSignalStats stats = { 0 };
    g_signal_connect (mgr, "disk-added", G_CALLBACK (on_test_disk_added), &stats);
    g_signal_connect (mgr, "disk-removed", G_CALLBACK (on_test_disk_removed), &stats);
    g_signal_connect (mgr, "disk-updated", G_CALLBACK (on_test_disk_updated), &stats);

    /* Partitions, non-block subsystems, and virtual devices must be ignored */
    const gchar part_payload[] =
        "ACTION=add\0SUBSYSTEM=block\0DEVTYPE=partition\0DEVNAME=sdz1\0";
    g_assert_false (puls_disk_manager_handle_uevent (mgr, part_payload, sizeof (part_payload)));

    const gchar usb_if_payload[] =
        "ACTION=add\0SUBSYSTEM=usb\0DEVTYPE=usb_interface\0DEVNAME=usb1\0";
    g_assert_false (puls_disk_manager_handle_uevent (mgr, usb_if_payload, sizeof (usb_if_payload)));

    const gchar loop_payload[] =
        "ACTION=add\0SUBSYSTEM=block\0DEVTYPE=disk\0DEVNAME=loop0\0";
    g_assert_false (puls_disk_manager_handle_uevent (mgr, loop_payload, sizeof (loop_payload)));

    g_assert_cmpuint (stats.added_count, ==, 0);
    g_assert_cmpuint (puls_disk_manager_get_device_count (mgr), ==, 0);

    /* Simulate USB disk attach */
    g_assert_true (puls_disk_manager_handle_uevent (mgr, add_payload, sizeof (add_payload)));
    g_assert_cmpuint (stats.added_count, ==, 1);
    g_assert_cmpstr (stats.last_added, ==, "/dev/sdz");
    g_assert_cmpuint (puls_disk_manager_get_device_count (mgr), ==, 1);
    g_assert_nonnull (puls_disk_manager_get_smart_data (mgr, "/dev/sdz"));

    /* Duplicate add event should be a no-op */
    g_assert_false (puls_disk_manager_handle_uevent (mgr, add_payload, sizeof (add_payload)));
    g_assert_cmpuint (stats.added_count, ==, 1);

    /* Change event updates existing tracked disk */
    const gchar change_payload[] =
        "ACTION=change\0SUBSYSTEM=block\0DEVTYPE=disk\0DEVNAME=sdz\0";
    g_assert_true (puls_disk_manager_handle_uevent (mgr, change_payload, sizeof (change_payload)));
    g_assert_cmpuint (stats.updated_count, ==, 1);
    g_assert_cmpstr (stats.last_updated, ==, "/dev/sdz");

    /* Simulate USB disk detach */
    const gchar remove_payload[] =
        "remove@/devices/pci0000:00/0000:00:14.0/usb1/block/sdz\0"
        "ACTION=remove\0SUBSYSTEM=block\0DEVTYPE=disk\0DEVNAME=sdz\0";
    g_assert_true (puls_disk_manager_handle_uevent (mgr, remove_payload, sizeof (remove_payload)));
    g_assert_cmpuint (stats.removed_count, ==, 1);
    g_assert_cmpstr (stats.last_removed, ==, "/dev/sdz");
    g_assert_cmpuint (puls_disk_manager_get_device_count (mgr), ==, 0);
    g_assert_null (puls_disk_manager_get_smart_data (mgr, "/dev/sdz"));

    /* 3. Explicit add_mock_device + remove_device roundtrip */
    PulsSmartData *nvme = puls_smart_data_new ();
    puls_smart_data_set_device_path (nvme, "/dev/nvme9n1");
    puls_disk_manager_add_mock_device (mgr, "/dev/nvme9n1", nvme);
    g_assert_cmpuint (stats.added_count, ==, 2);
    g_assert_true (puls_disk_manager_remove_device (mgr, "/dev/nvme9n1"));
    g_assert_cmpuint (stats.removed_count, ==, 2);
    g_assert_cmpstr (stats.last_removed, ==, "/dev/nvme9n1");
    g_assert_false (puls_disk_manager_remove_device (mgr, "/dev/nvme9n1"));

    /* 4. Start / stop hotplug monitoring lifecycle */
    g_assert_false (puls_disk_manager_is_monitoring (mgr));
    puls_disk_manager_start_monitoring (mgr);
    g_assert_true (puls_disk_manager_is_monitoring (mgr));
    /* Idempotent second start call */
    puls_disk_manager_start_monitoring (mgr);
    g_assert_true (puls_disk_manager_is_monitoring (mgr));

    while (g_main_context_pending (NULL))
        g_main_context_iteration (NULL, FALSE);

    puls_disk_manager_stop_monitoring (mgr);
    g_assert_false (puls_disk_manager_is_monitoring (mgr));

    g_free (stats.last_added);
    g_free (stats.last_removed);
    g_free (stats.last_updated);
}

static void
test_nvme_topology_separation (void)
{
    /* 1. Controller vs namespace vs partition name classification */
    g_assert_true  (puls_nvme_is_controller_name ("nvme0"));
    g_assert_true  (puls_nvme_is_controller_name ("/dev/nvme12"));
    g_assert_false (puls_nvme_is_controller_name ("nvme0n1"));
    g_assert_false (puls_nvme_is_controller_name ("nvme0n1p1"));
    g_assert_false (puls_nvme_is_controller_name ("sda"));

    g_assert_true  (puls_nvme_is_namespace_name ("nvme0n1"));
    g_assert_true  (puls_nvme_is_namespace_name ("/dev/nvme0n2"));
    g_assert_true  (puls_nvme_is_namespace_name ("/dev/nvme1c0n3"));
    g_assert_false (puls_nvme_is_namespace_name ("nvme0"));
    g_assert_false (puls_nvme_is_namespace_name ("/dev/nvme0n1p1"));
    g_assert_false (puls_nvme_is_namespace_name ("/dev/sda"));

    g_autofree gchar *ctrl = NULL;
    guint32 nsid = 0;
    g_assert_true (puls_nvme_parse_device_name ("/dev/nvme1c0n4", &ctrl, &nsid));
    g_assert_cmpstr (ctrl, ==, "nvme1");
    g_assert_cmpuint (nsid, ==, 4);
    g_clear_pointer (&ctrl, g_free);

    g_assert_true (puls_nvme_parse_device_name ("/dev/nvme0n1p2", &ctrl, &nsid));
    g_assert_cmpstr (ctrl, ==, "nvme0");
    g_assert_cmpuint (nsid, ==, 1);
    g_clear_pointer (&ctrl, g_free);

    g_assert_false (puls_nvme_parse_device_name ("/dev/sda1", &ctrl, &nsid));

    /* 2. SMART target resolution routes NVMe namespaces to their controller */
    g_autofree gchar *c0 = puls_nvme_resolve_controller_path ("/dev/nvme0n1");
    g_assert_cmpstr (c0, ==, "/dev/nvme0");
    g_autofree gchar *t0 = puls_nvme_resolve_smart_target ("/dev/nvme0n1");
    g_assert_cmpstr (t0, ==, "/dev/nvme0");
    g_autofree gchar *t2 = puls_nvme_resolve_smart_target ("/dev/nvme2c0n3");
    g_assert_cmpstr (t2, ==, "/dev/nvme2");
    g_autofree gchar *ts = puls_nvme_resolve_smart_target ("/dev/sda");
    g_assert_cmpstr (ts, ==, "/dev/sda");
    g_assert_null (puls_nvme_resolve_controller_path ("/dev/sda"));

    /* 3. Multi-namespace mock sysfs topology tree linked to single controller */
    g_autofree gchar *tmp_root = g_dir_make_tmp ("puls_nvme_topo_XXXXXX", NULL);
    g_assert_nonnull (tmp_root);

    g_autofree gchar *ctrl_dir = g_build_filename (tmp_root, "nvme0", NULL);
    g_autofree gchar *ns1_dir  = g_build_filename (tmp_root, "nvme0n1", NULL);
    g_autofree gchar *ns2_dir  = g_build_filename (tmp_root, "nvme0n2", NULL);
    g_assert_cmpint (g_mkdir_with_parents (ctrl_dir, 0755), ==, 0);
    g_assert_cmpint (g_mkdir_with_parents (ns1_dir, 0755), ==, 0);
    g_assert_cmpint (g_mkdir_with_parents (ns2_dir, 0755), ==, 0);

    g_autofree gchar *p_cntlid = g_build_filename (ctrl_dir, "cntlid", NULL);
    g_autofree gchar *p_trans  = g_build_filename (ctrl_dir, "transport", NULL);
    g_autofree gchar *p_nqn    = g_build_filename (ctrl_dir, "subsysnqn", NULL);
    g_autofree gchar *p_state  = g_build_filename (ctrl_dir, "state", NULL);
    g_assert_true (g_file_set_contents (p_cntlid, "1\n", -1, NULL));
    g_assert_true (g_file_set_contents (p_trans,  "pcie\n", -1, NULL));
    g_assert_true (g_file_set_contents (p_nqn,    "nqn.2016-06.io.puls:nvm-test\n", -1, NULL));
    g_assert_true (g_file_set_contents (p_state,  "live\n", -1, NULL));

    g_autofree gchar *ns1_nsid = g_build_filename (ns1_dir, "nsid", NULL);
    g_autofree gchar *ns1_eui  = g_build_filename (ns1_dir, "eui", NULL);
    g_autofree gchar *ns1_wwid = g_build_filename (ns1_dir, "wwid", NULL);
    g_autofree gchar *ns1_dev  = g_build_filename (ns1_dir, "device", NULL);
    g_assert_true (g_file_set_contents (ns1_nsid, "1\n", -1, NULL));
    g_assert_true (g_file_set_contents (ns1_eui,  "0025388b11000001\n", -1, NULL));
    g_assert_true (g_file_set_contents (ns1_wwid, "eui.0025388b11000001\n", -1, NULL));
    g_assert_cmpint (symlink ("../nvme0", ns1_dev), ==, 0);

    g_autofree gchar *ns2_nsid = g_build_filename (ns2_dir, "nsid", NULL);
    g_autofree gchar *ns2_nguid = g_build_filename (ns2_dir, "nguid", NULL);
    g_autofree gchar *ns2_dev  = g_build_filename (ns2_dir, "device", NULL);
    g_assert_true (g_file_set_contents (ns2_nsid, "2\n", -1, NULL));
    g_assert_true (g_file_set_contents (ns2_nguid, "00000000-0000-0002-0025-380000000002\n", -1, NULL));
    g_assert_cmpint (symlink ("../nvme0", ns2_dev), ==, 0);

    PulsNvmeTopology topo1 = { 0 };
    g_assert_true (puls_nvme_read_topology_from_dir (ns1_dir, &topo1));
    g_assert_true (topo1.is_nvme);
    g_assert_cmpstr (topo1.controller_name, ==, "nvme0");
    g_assert_cmpstr (topo1.controller_path, ==, "/dev/nvme0");
    g_assert_cmpstr (topo1.namespace_name,  ==, "nvme0n1");
    g_assert_cmpstr (topo1.namespace_path,  ==, "/dev/nvme0n1");
    g_assert_cmpuint (topo1.nsid, ==, 1);
    g_assert_cmpint (topo1.cntlid, ==, 1);
    g_assert_cmpstr (topo1.transport, ==, "pcie");
    g_assert_cmpstr (topo1.subsysnqn, ==, "nqn.2016-06.io.puls:nvm-test");
    g_assert_cmpstr (topo1.eui64,     ==, "0025388b11000001");
    g_assert_cmpstr (topo1.wwid,      ==, "eui.0025388b11000001");
    g_assert_cmpstr (topo1.state,     ==, "live");
    puls_nvme_topology_clear (&topo1);

    PulsNvmeTopology topo2 = { 0 };
    g_assert_true (puls_nvme_read_topology_from_dir (ns2_dir, &topo2));
    g_assert_cmpstr (topo2.controller_path, ==, "/dev/nvme0");
    g_assert_cmpstr (topo2.namespace_path,  ==, "/dev/nvme0n2");
    g_assert_cmpuint (topo2.nsid, ==, 2);
    g_assert_cmpstr (topo2.nguid, ==, "00000000-0000-0002-0025-380000000002");
    puls_nvme_topology_clear (&topo2);

    /* Clean up mock topology directory */
    unlink (p_cntlid); unlink (p_trans); unlink (p_nqn); unlink (p_state);
    unlink (ns1_nsid); unlink (ns1_eui); unlink (ns1_wwid); unlink (ns1_dev);
    unlink (ns2_nsid); unlink (ns2_nguid); unlink (ns2_dev);
    rmdir (ctrl_dir); rmdir (ns1_dir); rmdir (ns2_dir); rmdir (tmp_root);

    /* 4. Reject bare controller uevent and populate namespace topology on valid uevent */
    g_autoptr(PulsDiskManager) mgr = puls_disk_manager_new ();
    puls_disk_manager_set_mock_mode (mgr, TRUE);

    const gchar ctrl_uevent[] = "ACTION=add\0SUBSYSTEM=block\0DEVTYPE=disk\0DEVNAME=nvme0\0";
    g_assert_false (puls_disk_manager_handle_uevent (mgr, ctrl_uevent, sizeof (ctrl_uevent)));
    g_assert_cmpuint (puls_disk_manager_get_device_count (mgr), ==, 0);

    const gchar ns_uevent[] = "ACTION=add\0SUBSYSTEM=block\0DEVTYPE=disk\0DEVNAME=nvme9n2\0";
    g_assert_true (puls_disk_manager_handle_uevent (mgr, ns_uevent, sizeof (ns_uevent)));
    PulsSmartData *ns_data = puls_disk_manager_get_smart_data (mgr, "/dev/nvme9n2");
    g_assert_nonnull (ns_data);
    g_assert_cmpstr (puls_smart_data_get_device_path (ns_data), ==, "/dev/nvme9n2");
    g_assert_cmpstr (puls_smart_data_get_controller_path (ns_data), ==, "/dev/nvme9");
    g_assert_cmpuint (puls_smart_data_get_nvme_nsid (ns_data), ==, 2);

    /* 5. Live host check if /sys/block/nvme0n1 exists */
    if (g_file_test ("/sys/block/nvme0n1", G_FILE_TEST_IS_DIR)) {
        PulsNvmeTopology live_topo = { 0 };
        g_assert_true (puls_nvme_read_topology ("/dev/nvme0n1", &live_topo));
        g_assert_cmpstr (live_topo.controller_path, ==, "/dev/nvme0");
        g_assert_cmpstr (live_topo.namespace_path, ==, "/dev/nvme0n1");
        g_assert_cmpuint (live_topo.nsid, >=, 1);
        puls_nvme_topology_clear (&live_topo);
    }
}

static void
remove_temp_tree (const gchar *path)
{
    GDir *dir = g_dir_open (path, 0, NULL);
    if (dir != NULL) {
        const gchar *entry;
        while ((entry = g_dir_read_name (dir)) != NULL) {
            g_autofree gchar *child = g_build_filename (path, entry, NULL);
            if (g_file_test (child, G_FILE_TEST_IS_DIR) &&
                !g_file_test (child, G_FILE_TEST_IS_SYMLINK)) {
                remove_temp_tree (child);
            } else {
                unlink (child);
            }
        }
        g_dir_close (dir);
    }
    rmdir (path);
}

static void
test_pseudo_device_filter (void)
{
    /* 1. Pseudo, virtual clutter, partitions, and optical drives excluded */
    static const gchar * const rejected[] = {
        "loop0", "loop24", "/dev/loop5",
        "zram0", "zram3",
        "ram0", "ram15",
        "dm-0", "dm-7",
        "sr0", "sr1", "scd0", "cdrom", "pktcdvd0",
        "fd0", "md0", "md127", "nbd0", "zd0", "drbd0", "rbd0",
        "nvme0", "nvme0n1p1",
        "sda1", "sda12", "hda1", "vda1", "xvda2",
        "mmcblk0p1", "mmcblk0boot0", "mmcblk0boot1", "mmcblk0rpmb",
        "sd", "vd", "mmcblk", "", NULL
    };

    for (guint i = 0; i < G_N_ELEMENTS (rejected); i++) {
        g_assert_false (puls_disk_manager_is_real_disk_name (rejected[i]));
    }

    /* 2. Physical disks and VM disks (/dev/vd*, /dev/xvd*) preserved */
    static const gchar * const accepted[] = {
        "sda", "/dev/sda", "sdz", "sdab",
        "hda", "vda", "vdb", "vdaa", "xvda",
        "nvme0n1", "/dev/nvme1n2", "nvme0c0n1",
        "mmcblk0", "mmcblk1"
    };

    for (guint i = 0; i < G_N_ELEMENTS (accepted); i++) {
        g_assert_true (puls_disk_manager_is_real_disk_name (accepted[i]));
    }

    g_assert_cmpint (puls_detect_drive_type ("/dev/vda"), ==, PULS_DRIVE_TYPE_VIRTUAL);
    g_assert_cmpint (puls_detect_transport_protocol ("/dev/vda"), ==, PULS_TRANSPORT_VIRTUAL);

    /* 3. Synthetic /sys/block scan with 25 loop devices (snap/docker), dm-verity, optical, hidden */
    g_autofree gchar *tmp_sys = g_dir_make_tmp ("puls_sys_block_XXXXXX", NULL);
    g_assert_nonnull (tmp_sys);

    for (gint i = 0; i < 25; i++) {
        g_autofree gchar *loop_name = g_strdup_printf ("loop%d", i);
        g_autofree gchar *loop_dir = g_build_filename (tmp_sys, loop_name, "loop", NULL);
        g_assert_cmpint (g_mkdir_with_parents (loop_dir, 0755), ==, 0);
        if (i == 0 || i == 1) {
            g_autofree gchar *bf = g_build_filename (loop_dir, "backing_file", NULL);
            const gchar *target = (i == 0)
                ? "/var/lib/snapd/snaps/core22_1234.snap\n"
                : "/var/lib/docker/overlay2/layer.img\n";
            g_assert_true (g_file_set_contents (bf, target, -1, NULL));
        }
    }

    /* Pseudo & dm-verity entries */
    static const gchar * const extra_pseudo[] = {
        "zram0", "ram0", "fd0", "md0", "nbd0", "mmcblk0boot0", "nvme0"
    };
    for (guint i = 0; i < G_N_ELEMENTS (extra_pseudo); i++) {
        g_autofree gchar *d = g_build_filename (tmp_sys, extra_pseudo[i], NULL);
        g_assert_cmpint (g_mkdir_with_parents (d, 0755), ==, 0);
    }

    g_autofree gchar *dm_sub = g_build_filename (tmp_sys, "dm-0", "dm", NULL);
    g_assert_cmpint (g_mkdir_with_parents (dm_sub, 0755), ==, 0);
    g_autofree gchar *dm_uuid = g_build_filename (dm_sub, "uuid", NULL);
    g_assert_true (g_file_set_contents (dm_uuid, "VERITY-root-1234\n", -1, NULL));

    g_autofree gchar *sr_dev = g_build_filename (tmp_sys, "sr0", "device", NULL);
    g_assert_cmpint (g_mkdir_with_parents (sr_dev, 0755), ==, 0);
    g_autofree gchar *sr_type = g_build_filename (sr_dev, "type", NULL);
    g_assert_true (g_file_set_contents (sr_type, "5\n", -1, NULL));

    /* Optical SCSI ROM & hidden multipath paths disguised under sd* names */
    g_autofree gchar *sdb_dev = g_build_filename (tmp_sys, "sdb", "device", NULL);
    g_assert_cmpint (g_mkdir_with_parents (sdb_dev, 0755), ==, 0);
    g_autofree gchar *sdb_type = g_build_filename (sdb_dev, "type", NULL);
    g_assert_true (g_file_set_contents (sdb_type, "5\n", -1, NULL));

    g_autofree gchar *sdc_dir = g_build_filename (tmp_sys, "sdc", NULL);
    g_assert_cmpint (g_mkdir_with_parents (sdc_dir, 0755), ==, 0);
    g_autofree gchar *sdc_hidden = g_build_filename (sdc_dir, "hidden", NULL);
    g_assert_true (g_file_set_contents (sdc_hidden, "1\n", -1, NULL));

    /* Valid physical and VM disks */
    static const gchar * const real_dirs[] = { "nvme0n1", "sda", "vda", "vdb" };
    for (guint i = 0; i < G_N_ELEMENTS (real_dirs); i++) {
        g_autofree gchar *d = g_build_filename (tmp_sys, real_dirs[i], NULL);
        g_assert_cmpint (g_mkdir_with_parents (d, 0755), ==, 0);
    }

    GList *scanned = puls_disk_manager_scan_sys_block_dir (tmp_sys);
    g_assert_cmpuint (g_list_length (scanned), ==, 4);
    g_assert_cmpstr (g_list_nth_data (scanned, 0), ==, "/dev/nvme0n1");
    g_assert_cmpstr (g_list_nth_data (scanned, 1), ==, "/dev/sda");
    g_assert_cmpstr (g_list_nth_data (scanned, 2), ==, "/dev/vda");
    g_assert_cmpstr (g_list_nth_data (scanned, 3), ==, "/dev/vdb");
    g_list_free_full (scanned, g_free);

    remove_temp_tree (tmp_sys);

    /* 4. Live host /sys/block scan must contain zero loop/zram/dm/sr entries */
    GList *host_devs = puls_disk_manager_scan_sys_block_dir ("/sys/block");
    for (GList *l = host_devs; l != NULL; l = l->next) {
        const gchar *p = l->data;
        g_assert_false (g_str_has_prefix (p, "/dev/loop"));
        g_assert_false (g_str_has_prefix (p, "/dev/zram"));
        g_assert_false (g_str_has_prefix (p, "/dev/dm-"));
        g_assert_false (g_str_has_prefix (p, "/dev/ram"));
        g_assert_false (g_str_has_prefix (p, "/dev/sr"));
    }
    g_list_free_full (host_devs, g_free);
}

static void
test_scsi_generic_mapping (void)
{
    /* 1. Name & device path validation */
    g_assert_true  (puls_scsi_is_generic_name ("sg0"));
    g_assert_true  (puls_scsi_is_generic_name ("/dev/sg0"));
    g_assert_true  (puls_scsi_is_generic_name ("sg12"));
    g_assert_false (puls_scsi_is_generic_name ("sg"));
    g_assert_false (puls_scsi_is_generic_name ("sg0a"));
    g_assert_false (puls_scsi_is_generic_name ("/dev/sda"));
    g_assert_false (puls_scsi_is_generic_name ("/dev/sg0/extra"));
    g_assert_false (puls_scsi_is_generic_name (""));
    g_assert_false (puls_scsi_is_generic_name (NULL));

    g_assert_true  (puls_device_path_is_valid ("/dev/sg0"));
    g_assert_false (puls_device_path_is_valid ("/dev/sg"));
    g_assert_false (puls_device_path_is_valid ("/dev/sg0p1"));

    /* 2. SCSI H:B:T:L address parsing */
    g_autofree gchar *hbtl = NULL;
    g_assert_true (puls_scsi_parse_hbtl_address ("/sys/devices/pci0000:00/host2/target2:0:1/2:0:1:0", &hbtl));
    g_assert_cmpstr (hbtl, ==, "2:0:1:0");
    g_clear_pointer (&hbtl, g_free);
    g_assert_true (puls_scsi_parse_hbtl_address ("0:0:0:0", &hbtl));
    g_assert_cmpstr (hbtl, ==, "0:0:0:0");
    g_clear_pointer (&hbtl, g_free);
    g_assert_false (puls_scsi_parse_hbtl_address ("/sys/devices/nvme0", &hbtl));
    g_assert_null (hbtl);

    /* 3. Full mock sysfs hierarchy testing standard, legacy, H:B:T:L fallback, & SES enclosure */
    g_autofree gchar *tmp_sys = g_dir_make_tmp ("puls_scsi_sysfs_XXXXXX", NULL);
    g_assert_nonnull (tmp_sys);

    /* sg0 <-> sda (standard layout: block/sda + scsi_generic/sg0) */
    g_autofree gchar *dev0 = g_build_filename (tmp_sys, "devices", "pci0", "host0", "target0:0:0", "0:0:0:0", NULL);
    g_autofree gchar *dev0_blk = g_build_filename (dev0, "block", "sda", NULL);
    g_autofree gchar *dev0_sg  = g_build_filename (dev0, "scsi_generic", "sg0", NULL);
    g_assert_cmpint (g_mkdir_with_parents (dev0_blk, 0755), ==, 0);
    g_assert_cmpint (g_mkdir_with_parents (dev0_sg, 0755), ==, 0);

    g_autofree gchar *p_v0 = g_build_filename (dev0, "vendor", NULL);
    g_autofree gchar *p_m0 = g_build_filename (dev0, "model", NULL);
    g_autofree gchar *p_r0 = g_build_filename (dev0, "rev", NULL);
    g_autofree gchar *p_t0 = g_build_filename (dev0, "type", NULL);
    g_assert_true (g_file_set_contents (p_v0, "ATA     \n", -1, NULL));
    g_assert_true (g_file_set_contents (p_m0, "WDC WD40EFRX    \n", -1, NULL));
    g_assert_true (g_file_set_contents (p_r0, "82.0\n", -1, NULL));
    g_assert_true (g_file_set_contents (p_t0, "0\n", -1, NULL));

    g_autofree gchar *cls_sg0_dir = g_build_filename (tmp_sys, "class", "scsi_generic", "sg0", NULL);
    g_autofree gchar *cls_sg0_dev = g_build_filename (cls_sg0_dir, "device", NULL);
    g_assert_cmpint (g_mkdir_with_parents (cls_sg0_dir, 0755), ==, 0);
    g_assert_cmpint (symlink ("../../../devices/pci0/host0/target0:0:0/0:0:0:0", cls_sg0_dev), ==, 0);

    g_autofree gchar *blk_sda_dir = g_build_filename (tmp_sys, "block", "sda", NULL);
    g_autofree gchar *blk_sda_dev = g_build_filename (blk_sda_dir, "device", NULL);
    g_assert_cmpint (g_mkdir_with_parents (blk_sda_dir, 0755), ==, 0);
    g_assert_cmpint (symlink ("../../devices/pci0/host0/target0:0:0/0:0:0:0", blk_sda_dev), ==, 0);

    /* sg1 <-> sdb (legacy layout: block:sdb symlink + generic symlink) */
    g_autofree gchar *dev1 = g_build_filename (tmp_sys, "devices", "pci0", "host1", "target1:0:2", "1:0:2:0", NULL);
    g_assert_cmpint (g_mkdir_with_parents (dev1, 0755), ==, 0);
    g_autofree gchar *cls_sg1_dir = g_build_filename (tmp_sys, "class", "scsi_generic", "sg1", NULL);
    g_autofree gchar *cls_sg1_dev = g_build_filename (cls_sg1_dir, "device", NULL);
    g_assert_cmpint (g_mkdir_with_parents (cls_sg1_dir, 0755), ==, 0);
    g_assert_cmpint (symlink ("../../../devices/pci0/host1/target1:0:2/1:0:2:0", cls_sg1_dev), ==, 0);

    g_autofree gchar *blk_sdb_dir = g_build_filename (tmp_sys, "block", "sdb", NULL);
    g_autofree gchar *blk_sdb_dev = g_build_filename (blk_sdb_dir, "device", NULL);
    g_assert_cmpint (g_mkdir_with_parents (blk_sdb_dir, 0755), ==, 0);
    g_assert_cmpint (symlink ("../../devices/pci0/host1/target1:0:2/1:0:2:0", blk_sdb_dev), ==, 0);

    g_autofree gchar *legacy_blk_link = g_build_filename (dev1, "block:sdb", NULL);
    g_autofree gchar *legacy_gen_link = g_build_filename (dev1, "generic", NULL);
    g_assert_cmpint (symlink ("../../../../block/sdb", legacy_blk_link), ==, 0);
    g_assert_cmpint (symlink ("../../../../class/scsi_generic/sg1", legacy_gen_link), ==, 0);

    /* sg2: SAS enclosure SES device (type 13, no block device) */
    g_autofree gchar *dev2 = g_build_filename (tmp_sys, "devices", "pci0", "host2", "target2:0:10", "2:0:10:0", NULL);
    g_assert_cmpint (g_mkdir_with_parents (dev2, 0755), ==, 0);
    g_autofree gchar *p_t2 = g_build_filename (dev2, "type", NULL);
    g_assert_true (g_file_set_contents (p_t2, "13\n", -1, NULL));
    g_autofree gchar *cls_sg2_dir = g_build_filename (tmp_sys, "class", "scsi_generic", "sg2", NULL);
    g_autofree gchar *cls_sg2_dev = g_build_filename (cls_sg2_dir, "device", NULL);
    g_assert_cmpint (g_mkdir_with_parents (cls_sg2_dir, 0755), ==, 0);
    g_assert_cmpint (symlink ("../../../devices/pci0/host2/target2:0:10/2:0:10:0", cls_sg2_dev), ==, 0);

    /* sg3 <-> sdc via H:B:T:L class scan fallback (no scsi_generic subdir or generic link) */
    g_autofree gchar *dev3 = g_build_filename (tmp_sys, "devices", "pci0", "host2", "target2:0:11", "2:0:11:0", NULL);
    g_autofree gchar *dev3_blk = g_build_filename (dev3, "block", "sdc", NULL);
    g_assert_cmpint (g_mkdir_with_parents (dev3_blk, 0755), ==, 0);
    g_autofree gchar *cls_sg3_dir = g_build_filename (tmp_sys, "class", "scsi_generic", "sg3", NULL);
    g_autofree gchar *cls_sg3_dev = g_build_filename (cls_sg3_dir, "device", NULL);
    g_assert_cmpint (g_mkdir_with_parents (cls_sg3_dir, 0755), ==, 0);
    g_assert_cmpint (symlink ("../../../devices/pci0/host2/target2:0:11/2:0:11:0", cls_sg3_dev), ==, 0);
    g_autofree gchar *blk_sdc_dir = g_build_filename (tmp_sys, "block", "sdc", NULL);
    g_autofree gchar *blk_sdc_dev = g_build_filename (blk_sdc_dir, "device", NULL);
    g_assert_cmpint (g_mkdir_with_parents (blk_sdc_dir, 0755), ==, 0);
    g_assert_cmpint (symlink ("../../devices/pci0/host2/target2:0:11/2:0:11:0", blk_sdc_dev), ==, 0);

    /* Verify bidirectional resolution */
    g_autofree gchar *b0 = puls_scsi_generic_to_block_from_sysfs (tmp_sys, "/dev/sg0");
    g_autofree gchar *g0 = puls_scsi_block_to_generic_from_sysfs (tmp_sys, "/dev/sda");
    g_assert_cmpstr (b0, ==, "/dev/sda");
    g_assert_cmpstr (g0, ==, "/dev/sg0");

    g_autofree gchar *b1 = puls_scsi_generic_to_block_from_sysfs (tmp_sys, "sg1");
    g_autofree gchar *g1 = puls_scsi_block_to_generic_from_sysfs (tmp_sys, "sdb");
    g_assert_cmpstr (b1, ==, "/dev/sdb");
    g_assert_cmpstr (g1, ==, "/dev/sg1");

    g_autofree gchar *b2 = puls_scsi_generic_to_block_from_sysfs (tmp_sys, "/dev/sg2");
    g_assert_null (b2);

    g_autofree gchar *b3 = puls_scsi_generic_to_block_from_sysfs (tmp_sys, "/dev/sg3");
    g_autofree gchar *g3 = puls_scsi_block_to_generic_from_sysfs (tmp_sys, "/dev/sdc");
    g_assert_cmpstr (b3, ==, "/dev/sdc");
    g_assert_cmpstr (g3, ==, "/dev/sg3");

    /* Verify full info struct population */
    PulsScsiGenericInfo info = { 0 };
    g_assert_true (puls_scsi_read_generic_info_from_sysfs (tmp_sys, "/dev/sg0", &info));
    g_assert_cmpstr (info.sg_name, ==, "sg0");
    g_assert_cmpstr (info.sg_path, ==, "/dev/sg0");
    g_assert_cmpstr (info.block_name, ==, "sda");
    g_assert_cmpstr (info.block_path, ==, "/dev/sda");
    g_assert_cmpstr (info.scsi_address, ==, "0:0:0:0");
    g_assert_cmpstr (info.vendor, ==, "ATA");
    g_assert_cmpstr (info.model, ==, "WDC WD40EFRX");
    g_assert_cmpstr (info.rev, ==, "82.0");
    g_assert_cmpint (info.scsi_type, ==, 0);
    puls_scsi_generic_info_clear (&info);

    /* Verify sorted mapping enumeration */
    GList *mappings = puls_scsi_list_generic_mappings_from_sysfs (tmp_sys);
    g_assert_cmpuint (g_list_length (mappings), ==, 4);
    PulsScsiGenericInfo *m0 = g_list_nth_data (mappings, 0);
    PulsScsiGenericInfo *m2 = g_list_nth_data (mappings, 2);
    g_assert_cmpstr (m0->sg_name, ==, "sg0");
    g_assert_cmpstr (m0->block_path, ==, "/dev/sda");
    g_assert_cmpstr (m2->sg_name, ==, "sg2");
    g_assert_cmpint (m2->scsi_type, ==, 13);
    g_assert_null (m2->block_path);
    g_list_free_full (mappings, (GDestroyNotify)puls_scsi_generic_info_free);

    /* 4. PulsSmartData accessor check */
    g_autoptr(PulsSmartData) sd = puls_smart_data_new ();
    g_assert_null (puls_smart_data_get_scsi_generic_path (sd));
    puls_smart_data_set_scsi_generic_path (sd, "/dev/sg0");
    g_assert_cmpstr (puls_smart_data_get_scsi_generic_path (sd), ==, "/dev/sg0");

    remove_temp_tree (tmp_sys);
}

static void
create_mock_md_array (const gchar *sys_block_dir,
                      const gchar *md_name,
                      const gchar *level,
                      const gchar *array_state,
                      const gchar *raid_disks,
                      const gchar *degraded,
                      const gchar *sync_action)
{
    g_autofree gchar *md_sub = g_build_filename (sys_block_dir, md_name, "md", NULL);
    g_autofree gchar *slaves = g_build_filename (sys_block_dir, md_name, "slaves", NULL);
    g_assert_cmpint (g_mkdir_with_parents (md_sub, 0755), ==, 0);
    g_assert_cmpint (g_mkdir_with_parents (slaves, 0755), ==, 0);

    if (level) {
        g_autofree gchar *p = g_build_filename (md_sub, "level", NULL);
        g_assert_true (g_file_set_contents (p, level, -1, NULL));
    }
    if (array_state) {
        g_autofree gchar *p = g_build_filename (md_sub, "array_state", NULL);
        g_assert_true (g_file_set_contents (p, array_state, -1, NULL));
    }
    if (raid_disks) {
        g_autofree gchar *p = g_build_filename (md_sub, "raid_disks", NULL);
        g_assert_true (g_file_set_contents (p, raid_disks, -1, NULL));
    }
    if (degraded) {
        g_autofree gchar *p = g_build_filename (md_sub, "degraded", NULL);
        g_assert_true (g_file_set_contents (p, degraded, -1, NULL));
    }
    if (sync_action) {
        g_autofree gchar *p = g_build_filename (md_sub, "sync_action", NULL);
        g_assert_true (g_file_set_contents (p, sync_action, -1, NULL));
    }
}

static void
add_mock_md_slave (const gchar *sys_block_dir,
                   const gchar *md_name,
                   const gchar *slave_name,
                   const gchar *member_state)
{
    g_autofree gchar *slave_entry = g_build_filename (sys_block_dir, md_name, "slaves", slave_name, NULL);
    g_assert_cmpint (g_mkdir_with_parents (slave_entry, 0755), ==, 0);

    if (member_state) {
        g_autofree gchar *dev_sub = g_strdup_printf ("dev-%s", slave_name);
        g_autofree gchar *dev_dir = g_build_filename (sys_block_dir, md_name, "md", dev_sub, NULL);
        g_assert_cmpint (g_mkdir_with_parents (dev_dir, 0755), ==, 0);
        g_autofree gchar *state_file = g_build_filename (dev_dir, "state", NULL);
        g_assert_true (g_file_set_contents (state_file, member_state, -1, NULL));
    }
}

static void
test_mdadm_raid_member_resolution (void)
{
    /* 1. md device name validation */
    g_assert_true  (puls_raid_is_md_device_name ("md0"));
    g_assert_true  (puls_raid_is_md_device_name ("/dev/md127"));
    g_assert_true  (puls_raid_is_md_device_name ("md_d0"));
    g_assert_false (puls_raid_is_md_device_name ("md"));
    g_assert_false (puls_raid_is_md_device_name ("md0p1"));
    g_assert_false (puls_raid_is_md_device_name ("sda"));
    g_assert_false (puls_raid_is_md_device_name ("nvme0n1"));
    g_assert_false (puls_raid_is_md_device_name (""));
    g_assert_false (puls_raid_is_md_device_name (NULL));

    /* 2. Build mock /sys/block with md0 (RAID1), md1 (Degraded RAID5 + spare), md2 (NVMe RAID10 resync) */
    g_autofree gchar *tmp_sys = g_dir_make_tmp ("puls_mdadm_sysfs_XXXXXX", NULL);
    g_assert_nonnull (tmp_sys);

    create_mock_md_array (tmp_sys, "md0", "raid1\n", "clean\n", "2\n", "0\n", "idle\n");
    add_mock_md_slave (tmp_sys, "md0", "sda1", "in_sync\n");
    add_mock_md_slave (tmp_sys, "md0", "sdb1", "in_sync\n");

    create_mock_md_array (tmp_sys, "md1", "raid5\n", "active\n", "3\n", "1\n", "idle\n");
    add_mock_md_slave (tmp_sys, "md1", "sda2", "in_sync\n");
    add_mock_md_slave (tmp_sys, "md1", "sdc", "spare\n");

    create_mock_md_array (tmp_sys, "md2", "raid10\n", "active\n", "4\n", "0\n", "resync\n");
    add_mock_md_slave (tmp_sys, "md2", "nvme0n1p2", "in_sync\n");

    /* 3. Verify primary detection on /dev/sda -> /dev/md0 [RAID1] */
    PulsRaidMemberInfo info = { 0 };
    g_assert_true (puls_raid_detect_member_from_sysfs (tmp_sys, "/dev/sda", &info));
    g_assert_cmpstr (info.array_name, ==, "md0");
    g_assert_cmpstr (info.array_path, ==, "/dev/md0");
    g_assert_cmpstr (info.level, ==, "RAID1");
    g_assert_cmpstr (info.array_state, ==, "clean");
    g_assert_cmpstr (info.member_device, ==, "sda1");
    g_assert_cmpstr (info.member_state, ==, "in_sync");
    g_assert_cmpint (info.raid_disks, ==, 2);
    g_assert_cmpint (info.degraded_count, ==, 0);
    g_assert_cmpstr (info.status_text, ==, "Member of /dev/md0 [RAID1]");
    puls_raid_member_info_clear (&info);

    /* 4. Multi-array membership listing on /dev/sda (md0 + md1) */
    GList *sda_list = puls_raid_list_memberships_from_sysfs (tmp_sys, "/dev/sda");
    g_assert_cmpuint (g_list_length (sda_list), ==, 2);
    PulsRaidMemberInfo *m0 = g_list_nth_data (sda_list, 0);
    PulsRaidMemberInfo *m1 = g_list_nth_data (sda_list, 1);
    g_assert_cmpstr (m0->status_text, ==, "Member of /dev/md0 [RAID1]");
    g_assert_cmpstr (m1->status_text, ==, "Member of /dev/md1 [RAID5, Degraded]");
    g_list_free_full (sda_list, (GDestroyNotify)puls_raid_member_info_free);

    /* 5. Whole-disk spare slave (/dev/sdc -> /dev/md1 [RAID5, Spare]) */
    g_assert_true (puls_raid_detect_member_from_sysfs (tmp_sys, "/dev/sdc", &info));
    g_assert_cmpstr (info.member_device, ==, "sdc");
    g_assert_cmpstr (info.member_state, ==, "spare");
    g_assert_cmpstr (info.status_text, ==, "Member of /dev/md1 [RAID5, Spare]");
    puls_raid_member_info_clear (&info);

    /* 6. NVMe partition slave rebuilding (/dev/nvme0n1 -> /dev/md2 [RAID10, Rebuilding]) */
    g_assert_true (puls_raid_detect_member_from_sysfs (tmp_sys, "/dev/nvme0n1", &info));
    g_assert_cmpstr (info.array_path, ==, "/dev/md2");
    g_assert_cmpstr (info.member_device, ==, "nvme0n1p2");
    g_assert_cmpstr (info.status_text, ==, "Member of /dev/md2 [RAID10, Rebuilding]");
    puls_raid_member_info_clear (&info);

    /* 7. Non-member disk returns FALSE */
    g_assert_false (puls_raid_detect_member_from_sysfs (tmp_sys, "/dev/sdd", &info));
    g_assert_false (puls_raid_detect_member_from_sysfs (tmp_sys, "/dev/md0", &info));

    /* 8. PulsSmartData RAID accessors */
    g_autoptr(PulsSmartData) sd = puls_smart_data_new ();
    g_assert_null (puls_smart_data_get_raid_array_path (sd));
    g_assert_null (puls_smart_data_get_raid_level (sd));
    g_assert_null (puls_smart_data_get_raid_member_status (sd));
    puls_smart_data_set_raid_array_path (sd, "/dev/md0");
    puls_smart_data_set_raid_level (sd, "RAID1");
    puls_smart_data_set_raid_member_status (sd, "Member of /dev/md0 [RAID1]");
    g_assert_cmpstr (puls_smart_data_get_raid_array_path (sd), ==, "/dev/md0");
    g_assert_cmpstr (puls_smart_data_get_raid_level (sd), ==, "RAID1");
    g_assert_cmpstr (puls_smart_data_get_raid_member_status (sd), ==, "Member of /dev/md0 [RAID1]");

    remove_temp_tree (tmp_sys);
}

static void
test_zfs_pool_member_detection (void)
{
    /* 1. ZFS disk / vdev label signature identification */
    guint8 buf[256];
    memset (buf, 0, sizeof (buf));
    g_assert_false (puls_zfs_has_label_signature (buf, sizeof (buf)));
    g_assert_false (puls_zfs_has_label_signature (NULL, 0));

    /* Little-endian ZFS uberblock magic 0x0000000000bab10c */
    buf[64] = 0x0c; buf[65] = 0xb1; buf[66] = 0xba; buf[67] = 0x00;
    g_assert_true (puls_zfs_has_label_signature (buf, sizeof (buf)));

    /* Big-endian ZFS uberblock magic */
    memset (buf, 0, sizeof (buf));
    buf[128 + 5] = 0xba; buf[128 + 6] = 0xb1; buf[128 + 7] = 0x0c;
    g_assert_true (puls_zfs_has_label_signature (buf, sizeof (buf)));

    /* Paired XDR nvlist vdev label keys */
    const gchar *nvlist_partial = "version\0pool_guid\0";
    g_assert_false (puls_zfs_has_label_signature ((const guint8 *)nvlist_partial, 18));
    const gchar *nvlist_valid = "\0\0\0\x09pool_guid\0\0\0\x09vdev_tree\0";
    g_assert_true (puls_zfs_has_label_signature ((const guint8 *)nvlist_valid, 27));

    /* 2. Mock /dev/disk/by-id directory & multi-pool zpool status parsing */
    g_autofree gchar *tmp_root = g_dir_make_tmp ("puls_zfs_dev_XXXXXX", NULL);
    g_assert_nonnull (tmp_root);
    g_autofree gchar *by_id_dir = g_build_filename (tmp_root, "disk", "by-id", NULL);
    g_assert_cmpint (g_mkdir_with_parents (by_id_dir, 0755), ==, 0);

    /* Create real targets for realpath/puls_sysfs_resolve_link to resolve */
    g_autofree gchar *fake_sda1 = g_build_filename (tmp_root, "sda1", NULL);
    g_autofree gchar *fake_sdc  = g_build_filename (tmp_root, "sdc", NULL);
    g_assert_true (g_file_set_contents (fake_sda1, "", 0, NULL));
    g_assert_true (g_file_set_contents (fake_sdc,  "", 0, NULL));

    g_autofree gchar *link_ata = g_build_filename (by_id_dir, "ata-WDC_WD40EFRX_WD1234-part1", NULL);
    g_autofree gchar *link_wwn = g_build_filename (by_id_dir, "wwn-0x5000c500deadbeef", NULL);
    g_assert_cmpint (symlink ("../../sda1", link_ata), ==, 0);
    g_assert_cmpint (symlink ("../../sdc",  link_wwn), ==, 0);

    const gchar *mock_status =
        "  pool: tank\n"
        " state: DEGRADED\n"
        "config:\n"
        "\n"
        "\tNAME                                           STATE     READ WRITE CKSUM\n"
        "\ttank                                           DEGRADED     0     0     0\n"
        "\t  raidz1-0                                     DEGRADED     0     0     0\n"
        "\t    /dev/disk/by-id/ata-WDC_WD40EFRX_WD1234-part1  ONLINE   0     0     0\n"
        "\t    /dev/sdb1                                  DEGRADED     2     1     5\n"
        "\t    /dev/nvme0n1p3                             ONLINE       0     0     0\n"
        "\tlogs\n"
        "\t  /dev/nvme1n1p1                               ONLINE       0     0     0\n"
        "\tcache\n"
        "\t  /dev/nvme1n1p2                               ONLINE       0     0     0\n"
        "\tspares\n"
        "\t  wwn-0x5000c500deadbeef                       AVAIL\n"
        "\n"
        "errors: No known data errors\n"
        "\n"
        "  pool: rpool\n"
        " state: ONLINE\n"
        "config:\n"
        "\tNAME             STATE     READ WRITE CKSUM\n"
        "\trpool            ONLINE       0     0     0\n"
        "\t  mirror-0       ONLINE       0     0     0\n"
        "\t    /dev/nvme0n1p4 ONLINE     0     0     0\n"
        "errors: No known data errors\n";

    /* /dev/sda matched via /dev/disk/by-id symlink */
    PulsZfsMemberInfo info = { 0 };
    g_assert_true (puls_zfs_detect_member_from_status (mock_status, by_id_dir, "/dev/sda", &info));
    g_assert_cmpstr (info.pool_name, ==, "tank");
    g_assert_cmpstr (info.pool_state, ==, "DEGRADED");
    g_assert_cmpstr (info.vdev_type, ==, "RAIDZ1");
    g_assert_cmpstr (info.member_state, ==, "ONLINE");
    g_assert_cmpstr (info.status_text, ==, "ZFS Pool: tank [RAIDZ1, ONLINE]");
    puls_zfs_member_info_clear (&info);

    /* /dev/sdb matched directly with error counters */
    g_assert_true (puls_zfs_detect_member_from_status (mock_status, by_id_dir, "/dev/sdb", &info));
    g_assert_cmpstr (info.vdev_type, ==, "RAIDZ1");
    g_assert_cmpstr (info.member_state, ==, "DEGRADED");
    g_assert_cmpuint (info.read_errors, ==, 2);
    g_assert_cmpuint (info.write_errors, ==, 1);
    g_assert_cmpuint (info.cksum_errors, ==, 5);
    g_assert_cmpstr (info.status_text, ==, "ZFS Pool: tank [RAIDZ1, DEGRADED]");
    puls_zfs_member_info_clear (&info);

    /* /dev/sdc matched via bare wwn-* symlink under by_id_dir as SPARE */
    g_assert_true (puls_zfs_detect_member_from_status (mock_status, by_id_dir, "/dev/sdc", &info));
    g_assert_cmpstr (info.vdev_type, ==, "SPARE");
    g_assert_cmpstr (info.member_state, ==, "AVAIL");
    g_assert_cmpstr (info.status_text, ==, "ZFS Pool: tank [SPARE, AVAIL]");
    puls_zfs_member_info_clear (&info);

    /* /dev/nvme1n1 has both LOG and CACHE vdev partitions */
    GList *nvme1_list = puls_zfs_list_memberships_from_status (mock_status, by_id_dir, "/dev/nvme1n1");
    g_assert_cmpuint (g_list_length (nvme1_list), ==, 2);
    PulsZfsMemberInfo *z0 = g_list_nth_data (nvme1_list, 0);
    PulsZfsMemberInfo *z1 = g_list_nth_data (nvme1_list, 1);
    g_assert_cmpstr (z0->vdev_type, ==, "LOG");
    g_assert_cmpstr (z1->vdev_type, ==, "CACHE");
    g_list_free_full (nvme1_list, (GDestroyNotify)puls_zfs_member_info_free);

    /* /dev/nvme0n1 participates in both tank (RAIDZ1) and rpool (MIRROR) */
    GList *nvme0_list = puls_zfs_list_memberships_from_status (mock_status, by_id_dir, "/dev/nvme0n1");
    g_assert_cmpuint (g_list_length (nvme0_list), ==, 2);
    PulsZfsMemberInfo *p0 = g_list_nth_data (nvme0_list, 0);
    PulsZfsMemberInfo *p1 = g_list_nth_data (nvme0_list, 1);
    g_assert_cmpstr (p0->pool_name, ==, "tank");
    g_assert_cmpstr (p0->vdev_type, ==, "RAIDZ1");
    g_assert_cmpstr (p1->pool_name, ==, "rpool");
    g_assert_cmpstr (p1->vdev_type, ==, "MIRROR");
    g_list_free_full (nvme0_list, (GDestroyNotify)puls_zfs_member_info_free);

    /* Non-member disk returns FALSE */
    g_assert_false (puls_zfs_detect_member_from_status (mock_status, by_id_dir, "/dev/sde", &info));

    /* 3. PulsSmartData ZFS accessors */
    g_autoptr(PulsSmartData) sd = puls_smart_data_new ();
    g_assert_null (puls_smart_data_get_zfs_pool_name (sd));
    g_assert_null (puls_smart_data_get_zfs_vdev_type (sd));
    g_assert_null (puls_smart_data_get_zfs_member_status (sd));
    puls_smart_data_set_zfs_pool_name (sd, "tank");
    puls_smart_data_set_zfs_vdev_type (sd, "RAIDZ1");
    puls_smart_data_set_zfs_member_status (sd, "ZFS Pool: tank [RAIDZ1, ONLINE]");
    g_assert_cmpstr (puls_smart_data_get_zfs_pool_name (sd), ==, "tank");
    g_assert_cmpstr (puls_smart_data_get_zfs_vdev_type (sd), ==, "RAIDZ1");
    g_assert_cmpstr (puls_smart_data_get_zfs_member_status (sd), ==, "ZFS Pool: tank [RAIDZ1, ONLINE]");

    remove_temp_tree (tmp_root);
}

static void
test_form_factor_detection_engine (void)
{
    /* 1. Stringifier & parser coverage */
    g_assert_cmpstr (puls_form_factor_to_string (PULS_FORM_FACTOR_M2_2280), ==, "M.2 2280");
    g_assert_cmpstr (puls_form_factor_to_string (PULS_FORM_FACTOR_M2_2242), ==, "M.2 2242");
    g_assert_cmpstr (puls_form_factor_to_string (PULS_FORM_FACTOR_M2_2230), ==, "M.2 2230");
    g_assert_cmpstr (puls_form_factor_to_string (PULS_FORM_FACTOR_2_5_INCH), ==, "2.5\"");
    g_assert_cmpstr (puls_form_factor_to_string (PULS_FORM_FACTOR_3_5_INCH), ==, "3.5\"");
    g_assert_cmpstr (puls_form_factor_to_string (PULS_FORM_FACTOR_U2_U3), ==, "U.2 / U.3");
    g_assert_cmpstr (puls_form_factor_to_string (PULS_FORM_FACTOR_PCIE_AIC), ==, "PCIe Add-in Card");
    g_assert_cmpstr (puls_form_factor_to_string (PULS_FORM_FACTOR_MSATA), ==, "mSATA");

    g_assert_cmpint (puls_form_factor_from_string ("3.5 inches"), ==, PULS_FORM_FACTOR_3_5_INCH);
    g_assert_cmpint (puls_form_factor_from_string ("2.5 inches"), ==, PULS_FORM_FACTOR_2_5_INCH);
    g_assert_cmpint (puls_form_factor_from_string ("M.2 2280"),   ==, PULS_FORM_FACTOR_M2_2280);
    g_assert_cmpint (puls_form_factor_from_string ("M.2 2242"),   ==, PULS_FORM_FACTOR_M2_2242);
    g_assert_cmpint (puls_form_factor_from_string ("M.2 2230"),   ==, PULS_FORM_FACTOR_M2_2230);
    g_assert_cmpint (puls_form_factor_from_string ("U.2"),        ==, PULS_FORM_FACTOR_U2_U3);
    g_assert_cmpint (puls_form_factor_from_string ("HHHL AIC"),   ==, PULS_FORM_FACTOR_PCIE_AIC);
    g_assert_cmpint (puls_form_factor_from_string (NULL),         ==, PULS_FORM_FACTOR_UNKNOWN);

    /* 2. NVMe M.2 2280 / 2242 / 2230 / U.2 / PCIe Add-in Card heuristics */
    g_assert_cmpint (
        puls_detect_form_factor (NULL, "Samsung SSD 980 PRO 1TB", "/dev/nvme0n1",
                                 PULS_DRIVE_TYPE_NVME_SSD, PULS_TRANSPORT_PCIE, 0, 1000204886016ULL, 4),
        ==, PULS_FORM_FACTOR_M2_2280);

    g_assert_cmpint (
        puls_detect_form_factor ("M.2", "Micron_3400_MTFDKBA512TFH", "/dev/nvme0n1",
                                 PULS_DRIVE_TYPE_NVME_SSD, PULS_TRANSPORT_PCIE, 0, 512110190592ULL, 4),
        ==, PULS_FORM_FACTOR_M2_2280);

    g_assert_cmpint (
        puls_detect_form_factor (NULL, "WD PC SN740 SDDPNQD-1T00", "/dev/nvme0n1",
                                 PULS_DRIVE_TYPE_NVME_SSD, PULS_TRANSPORT_PCIE, 0, 1024209543168ULL, 4),
        ==, PULS_FORM_FACTOR_M2_2230);

    g_assert_cmpint (
        puls_detect_form_factor (NULL, "WD PC SN520 NVMe", "/dev/nvme0n1",
                                 PULS_DRIVE_TYPE_NVME_SSD, PULS_TRANSPORT_PCIE, 0, 512110190592ULL, 2),
        ==, PULS_FORM_FACTOR_M2_2242);

    g_assert_cmpint (
        puls_detect_form_factor (NULL, "Generic NVMe Drive", "/dev/nvme0n1",
                                 PULS_DRIVE_TYPE_NVME_SSD, PULS_TRANSPORT_PCIE, 0, 512000000000ULL, 4),
        ==, PULS_FORM_FACTOR_M2);

    g_assert_cmpint (
        puls_detect_form_factor (NULL, "INTEL SSDPE2KX040T8 P4510", "/dev/nvme1n1",
                                 PULS_DRIVE_TYPE_NVME_SSD, PULS_TRANSPORT_PCIE, 0, 4000787030016ULL, 4),
        ==, PULS_FORM_FACTOR_U2_U3);

    g_assert_cmpint (
        puls_detect_form_factor (NULL, "Intel Optane SSD 905P", "/dev/nvme2n1",
                                 PULS_DRIVE_TYPE_NVME_SSD, PULS_TRANSPORT_PCIE, 0, 960197124096ULL, 4),
        ==, PULS_FORM_FACTOR_PCIE_AIC);

    /* PCIe x8/x16 width implies Add-in Card */
    g_assert_cmpint (
        puls_detect_form_factor (NULL, "Enterprise NVMe Card", "/dev/nvme3n1",
                                 PULS_DRIVE_TYPE_NVME_SSD, PULS_TRANSPORT_PCIE, 0, 3200000000000ULL, 8),
        ==, PULS_FORM_FACTOR_PCIE_AIC);

    /* 3. SATA 3.5" & 2.5" & mSATA heuristics */
    g_assert_cmpint (
        puls_detect_form_factor ("3.5 inches", "ST4000VN008-2DR166", "/dev/sda",
                                 PULS_DRIVE_TYPE_SATA_HDD, PULS_TRANSPORT_SATA, 5980, 4000787030016ULL, 0),
        ==, PULS_FORM_FACTOR_3_5_INCH);

    g_assert_cmpint (
        puls_detect_form_factor (NULL, "WDC WD40EFRX-68WT0N0", "/dev/sda",
                                 PULS_DRIVE_TYPE_SATA_HDD, PULS_TRANSPORT_SATA, 5400, 4000787030016ULL, 0),
        ==, PULS_FORM_FACTOR_3_5_INCH);

    g_assert_cmpint (
        puls_detect_form_factor (NULL, "Generic 7200RPM HDD", "/dev/sdb",
                                 PULS_DRIVE_TYPE_SATA_HDD, PULS_TRANSPORT_SATA, 7200, 1000204886016ULL, 0),
        ==, PULS_FORM_FACTOR_3_5_INCH);

    g_assert_cmpint (
        puls_detect_form_factor (NULL, "ST2000LM015-2E8174", "/dev/sdc",
                                 PULS_DRIVE_TYPE_SATA_HDD, PULS_TRANSPORT_SATA, 5400, 2000398934016ULL, 0),
        ==, PULS_FORM_FACTOR_2_5_INCH);

    g_assert_cmpint (
        puls_detect_form_factor (NULL, "Crucial CT1000MX500SSD1", "/dev/sdd",
                                 PULS_DRIVE_TYPE_SATA_SSD, PULS_TRANSPORT_SATA, 0, 1000204886016ULL, 0),
        ==, PULS_FORM_FACTOR_2_5_INCH);

    g_assert_cmpint (
        puls_detect_form_factor (NULL, "Samsung SSD 860 EVO mSATA 500GB", "/dev/sde",
                                 PULS_DRIVE_TYPE_SATA_SSD, PULS_TRANSPORT_SATA, 0, 500107862016ULL, 0),
        ==, PULS_FORM_FACTOR_MSATA);

    /* 4. PulsSmartData convenience wrapper */
    g_autoptr(PulsSmartData) sd = puls_smart_data_new ();
    puls_smart_data_set_device_path (sd, "/dev/nvme0n1");
    puls_smart_data_set_model_name (sd, "Samsung SSD 990 PRO 2TB");
    puls_smart_data_set_drive_type (sd, PULS_DRIVE_TYPE_NVME_SSD);
    puls_smart_data_set_transport_protocol (sd, PULS_TRANSPORT_PCIE);
    puls_smart_data_set_pcie_max_width (sd, 4);
    PulsFormFactor ff = puls_detect_form_factor_for_smart_data (sd);
    g_assert_cmpint (ff, ==, PULS_FORM_FACTOR_M2_2280);
    puls_smart_data_set_form_factor_enum (sd, ff);
    g_assert_cmpint (puls_smart_data_get_form_factor_enum (sd), ==, PULS_FORM_FACTOR_M2_2280);
}

static void
test_standby_sleep_detection (void)
{
    /* 1. Power state enum stringification & sleep helpers */
    g_assert_cmpstr (puls_power_state_to_string (PULS_POWER_STATE_ACTIVE),  ==, "Active");
    g_assert_cmpstr (puls_power_state_to_string (PULS_POWER_STATE_IDLE),    ==, "Idle");
    g_assert_cmpstr (puls_power_state_to_string (PULS_POWER_STATE_STANDBY), ==, "Standby");
    g_assert_cmpstr (puls_power_state_to_string (PULS_POWER_STATE_SLEEP),   ==, "Sleep");
    g_assert_cmpstr (puls_power_state_to_string (PULS_POWER_STATE_UNKNOWN), ==, "Unknown");

    g_assert_cmpint (puls_power_state_from_string ("ACTIVE or IDLE"), ==, PULS_POWER_STATE_ACTIVE);
    g_assert_cmpint (puls_power_state_from_string ("STANDBY"),        ==, PULS_POWER_STATE_STANDBY);
    g_assert_cmpint (puls_power_state_from_string ("SLEEP"),          ==, PULS_POWER_STATE_SLEEP);
    g_assert_cmpint (puls_power_state_from_string ("IDLE"),           ==, PULS_POWER_STATE_IDLE);
    g_assert_cmpint (puls_power_state_from_string (NULL),             ==, PULS_POWER_STATE_UNKNOWN);

    g_assert_true  (puls_power_state_is_sleeping (PULS_POWER_STATE_STANDBY));
    g_assert_true  (puls_power_state_is_sleeping (PULS_POWER_STATE_SLEEP));
    g_assert_false (puls_power_state_is_sleeping (PULS_POWER_STATE_ACTIVE));
    g_assert_false (puls_power_state_is_sleeping (PULS_POWER_STATE_IDLE));
    g_assert_false (puls_power_state_is_sleeping (PULS_POWER_STATE_UNKNOWN));

    /* 2. Verify -n standby flag is passed for rotational HDDs and omitted for SSDs/NVMe */
    gboolean hdd_standby = puls_should_use_standby_check_for_type ("/dev/sda", PULS_DRIVE_TYPE_SATA_HDD, TRUE);
    gboolean sas_standby = puls_should_use_standby_check_for_type ("/dev/sdb", PULS_DRIVE_TYPE_SAS_HDD, TRUE);
    gboolean sshd_standby = puls_should_use_standby_check_for_type ("/dev/sdc", PULS_DRIVE_TYPE_SSHD, TRUE);
    gboolean nvme_standby = puls_should_use_standby_check_for_type ("/dev/nvme0n1", PULS_DRIVE_TYPE_NVME_SSD, FALSE);
    gboolean ssd_standby = puls_should_use_standby_check_for_type ("/dev/sdd", PULS_DRIVE_TYPE_SATA_SSD, FALSE);
    gboolean virt_standby = puls_should_use_standby_check_for_type ("/dev/vda", PULS_DRIVE_TYPE_VIRTUAL, FALSE);

    g_assert_true  (hdd_standby);
    g_assert_true  (sas_standby);
    g_assert_true  (sshd_standby);
    g_assert_false (nvme_standby);
    g_assert_false (ssd_standby);
    g_assert_false (virt_standby);
    g_assert_false (puls_should_use_standby_check ("/dev/nvme0n1"));

    g_autofree gchar *hdd_cmd = puls_build_smartctl_read_command ("/dev/sda", hdd_standby);
    g_assert_cmpstr (hdd_cmd, ==, "smartctl -n standby -a -j /dev/sda");
    g_assert_nonnull (strstr (hdd_cmd, "-n standby"));

    g_autofree gchar *hdd_helper = puls_build_helper_read_command ("/dev/sda", hdd_standby);
    g_assert_cmpstr (hdd_helper, ==, "READ_SMART /dev/sda standby\n");

    g_autofree gchar *nvme_cmd = puls_build_smartctl_read_command ("/dev/nvme0n1", nvme_standby);
    g_assert_cmpstr (nvme_cmd, ==, "smartctl -a -j /dev/nvme0n1");
    g_assert_null (strstr (nvme_cmd, "-n standby"));

    g_autofree gchar *nvme_helper = puls_build_helper_read_command ("/dev/nvme0n1", nvme_standby);
    g_assert_cmpstr (nvme_helper, ==, "READ_SMART /dev/nvme0n1\n");

    /* 3. Parse smartctl -n standby JSON output */
    const gchar *standby_json =
        "{\n"
        "  \"device\": { \"name\": \"/dev/sda\", \"protocol\": \"ATA\" },\n"
        "  \"power_mode\": \"STANDBY\",\n"
        "  \"smartctl\": {\n"
        "    \"exit_status\": 2,\n"
        "    \"messages\": [ { \"string\": \"Device is in STANDBY mode, exit(2)\", \"severity\": \"information\" } ]\n"
        "  }\n"
        "}\n";

    g_assert_cmpint (puls_parse_smartctl_standby_json (standby_json), ==, PULS_POWER_STATE_STANDBY);

    GError *err = NULL;
    g_autoptr(PulsSmartData) parsed = puls_smart_parser_parse_json (standby_json, &err);
    g_assert_no_error (err);
    g_assert_nonnull (parsed);
    g_assert_cmpint (puls_smart_data_get_power_state (parsed), ==, PULS_POWER_STATE_STANDBY);

    const gchar *msg_only_sleep =
        "{ \"smartctl\": { \"messages\": [ { \"string\": \"Device is in SLEEP mode, exit(2)\" } ] } }";
    g_assert_cmpint (puls_parse_smartctl_standby_json (msg_only_sleep), ==, PULS_POWER_STATE_SLEEP);

    const gchar *active_json = "{ \"power_mode\": \"ACTIVE or IDLE\" }";
    g_assert_cmpint (puls_parse_smartctl_standby_json (active_json), ==, PULS_POWER_STATE_ACTIVE);

    /* 4. Parse hdparm -C output */
    g_assert_cmpint (puls_parse_hdparm_power_state ("/dev/sda:\n drive state is:  standby\n"),
                     ==, PULS_POWER_STATE_STANDBY);
    g_assert_cmpint (puls_parse_hdparm_power_state ("/dev/sdb:\n drive state is:  active/idle\n"),
                     ==, PULS_POWER_STATE_ACTIVE);
    g_assert_cmpint (puls_parse_hdparm_power_state ("/dev/sdc:\n drive state is:  sleeping\n"),
                     ==, PULS_POWER_STATE_SLEEP);
}

typedef struct {
    GMainLoop *loop;
    gboolean   done;
    gboolean   ok;
    GError    *error;
    guint      added_signals;
    gboolean   main_thread_ticked;
    GThread   *main_thread;
    gboolean   signals_on_main_thread;
} AsyncScanTestCtx;

static void
on_async_scan_disk_added (PulsDiskManager *mgr G_GNUC_UNUSED,
                          const gchar     *device_path G_GNUC_UNUSED,
                          gpointer         user_data)
{
    AsyncScanTestCtx *ctx = user_data;
    ctx->added_signals++;
    if (g_thread_self () != ctx->main_thread)
        ctx->signals_on_main_thread = FALSE;
}

static gboolean
on_async_main_thread_tick (gpointer user_data)
{
    AsyncScanTestCtx *ctx = user_data;
    ctx->main_thread_ticked = TRUE;
    return G_SOURCE_REMOVE;
}

static void
on_async_scan_test_done (GObject      *source_object,
                         GAsyncResult *result,
                         gpointer      user_data)
{
    AsyncScanTestCtx *ctx = user_data;
    PulsDiskManager *mgr = PULS_DISK_MANAGER (source_object);
    ctx->ok = puls_disk_manager_scan_finish (mgr, result, &ctx->error);
    ctx->done = TRUE;
    g_main_loop_quit (ctx->loop);
}

static void
test_async_scan_cancellation (void)
{
    /* 1. Asynchronous mock scan completes without blocking main thread */
    g_autoptr(PulsDiskManager) mgr_mock = puls_disk_manager_new ();
    puls_disk_manager_set_mock_mode (mgr_mock, TRUE);

    AsyncScanTestCtx ctx = {
        .loop = g_main_loop_new (NULL, FALSE),
        .main_thread = g_thread_self (),
        .signals_on_main_thread = TRUE,
    };

    g_signal_connect (mgr_mock, "disk-added", G_CALLBACK (on_async_scan_disk_added), &ctx);
    g_idle_add (on_async_main_thread_tick, &ctx);

    puls_disk_manager_scan_async (mgr_mock, NULL, on_async_scan_test_done, &ctx);
    g_assert_true (puls_disk_manager_is_scanning (mgr_mock));

    g_main_loop_run (ctx.loop);

    g_assert_true (ctx.done);
    g_assert_true (ctx.ok);
    g_assert_no_error (ctx.error);
    g_assert_true (ctx.main_thread_ticked);
    g_assert_true (ctx.signals_on_main_thread);
    g_assert_false (puls_disk_manager_is_scanning (mgr_mock));
    g_assert_cmpuint (ctx.added_signals, ==, 4);
    g_assert_cmpuint (puls_disk_manager_get_device_count (mgr_mock), ==, 4);
    g_main_loop_unref (ctx.loop);

    /* 2. Cancelled scan returns G_IO_ERROR_CANCELLED and does not mutate devices */
    g_autoptr(PulsDiskManager) mgr_cancel = puls_disk_manager_new ();
    puls_disk_manager_set_mock_mode (mgr_cancel, FALSE);
    g_autoptr(GCancellable) cancellable = g_cancellable_new ();
    g_cancellable_cancel (cancellable);

    AsyncScanTestCtx cancel_ctx = {
        .loop = g_main_loop_new (NULL, FALSE),
        .main_thread = g_thread_self (),
        .signals_on_main_thread = TRUE,
    };

    puls_disk_manager_scan_async (mgr_cancel, cancellable, on_async_scan_test_done, &cancel_ctx);
    g_main_loop_run (cancel_ctx.loop);

    g_assert_true (cancel_ctx.done);
    g_assert_false (cancel_ctx.ok);
    g_assert_error (cancel_ctx.error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
    g_clear_error (&cancel_ctx.error);
    g_assert_false (puls_disk_manager_is_scanning (mgr_cancel));
    g_assert_cmpuint (puls_disk_manager_get_device_count (mgr_cancel), ==, 0);
    g_main_loop_unref (cancel_ctx.loop);

    /* 3. Live non-mock async scan completes cleanly on host */
    g_autoptr(PulsDiskManager) mgr_live = puls_disk_manager_new ();
    puls_disk_manager_set_mock_mode (mgr_live, FALSE);

    AsyncScanTestCtx live_ctx = {
        .loop = g_main_loop_new (NULL, FALSE),
        .main_thread = g_thread_self (),
        .signals_on_main_thread = TRUE,
    };
    g_signal_connect (mgr_live, "disk-added", G_CALLBACK (on_async_scan_disk_added), &live_ctx);
    g_idle_add (on_async_main_thread_tick, &live_ctx);

    puls_disk_manager_scan_async (mgr_live, NULL, on_async_scan_test_done, &live_ctx);
    g_main_loop_run (live_ctx.loop);

    g_assert_true (live_ctx.done);
    g_assert_true (live_ctx.ok);
    g_assert_no_error (live_ctx.error);
    g_assert_true (live_ctx.main_thread_ticked);
    g_assert_true (live_ctx.signals_on_main_thread);
    g_assert_false (puls_disk_manager_is_scanning (mgr_live));
    if (g_file_test ("/sys/block/nvme0n1", G_FILE_TEST_IS_DIR)) {
        g_assert_nonnull (puls_disk_manager_get_smart_data (mgr_live, "/dev/nvme0n1"));
    }
    g_main_loop_unref (live_ctx.loop);

    /* 4. Fire-and-forget async scan (NULL callback) completes cleanly */
    g_autoptr(PulsDiskManager) mgr_ff = puls_disk_manager_new ();
    puls_disk_manager_set_mock_mode (mgr_ff, TRUE);
    puls_disk_manager_scan_async (mgr_ff, NULL, NULL, NULL);
    while (puls_disk_manager_is_scanning (mgr_ff))
        g_main_context_iteration (NULL, TRUE);
    g_assert_cmpuint (puls_disk_manager_get_device_count (mgr_ff), ==, 4);
}

static void
test_phase3_final_integration (void)
{
    /* 1. Full async enumeration of mock hardware configurations
     *    (M.2 NVMe, SATA SSD, RAID SATA HDD, USB external) */
    g_autoptr(PulsDiskManager) mgr = puls_disk_manager_new ();
    puls_disk_manager_set_mock_mode (mgr, TRUE);

    AsyncScanTestCtx scan_ctx = {
        .loop = g_main_loop_new (NULL, FALSE),
        .main_thread = g_thread_self (),
        .signals_on_main_thread = TRUE,
    };
    g_signal_connect (mgr, "disk-added", G_CALLBACK (on_async_scan_disk_added), &scan_ctx);
    puls_disk_manager_scan_async (mgr, NULL, on_async_scan_test_done, &scan_ctx);
    g_main_loop_run (scan_ctx.loop);
    g_main_loop_unref (scan_ctx.loop);

    g_assert_true (scan_ctx.ok);
    g_assert_no_error (scan_ctx.error);
    g_assert_true (scan_ctx.signals_on_main_thread);
    g_assert_cmpuint (puls_disk_manager_get_device_count (mgr), ==, 4);

    /* Validate M.2 NVMe topology & form factor */
    PulsSmartData *nvme = puls_disk_manager_get_smart_data (mgr, "/dev/nvme0n1");
    g_assert_nonnull (nvme);
    g_assert_cmpstr (puls_smart_data_get_controller_path (nvme), ==, "/dev/nvme0");
    g_assert_cmpuint (puls_smart_data_get_nvme_nsid (nvme), ==, 1);
    g_assert_cmpint (puls_smart_data_get_form_factor_enum (nvme), ==, PULS_FORM_FACTOR_M2_2280);
    g_assert_cmpstr (puls_smart_data_get_form_factor (nvme), ==, "M.2 2280");
    g_assert_false (puls_should_use_standby_check_for_type (
        "/dev/nvme0n1", puls_smart_data_get_drive_type (nvme), FALSE));

    /* Validate SATA HDD RAID membership, SCSI generic mapping, 3.5" form factor, & -n standby */
    PulsSmartData *hdd = puls_disk_manager_get_smart_data (mgr, "/dev/sdb");
    g_assert_nonnull (hdd);
    g_assert_cmpstr (puls_smart_data_get_scsi_generic_path (hdd), ==, "/dev/sg1");
    g_assert_cmpstr (puls_smart_data_get_raid_array_path (hdd), ==, "/dev/md0");
    g_assert_cmpstr (puls_smart_data_get_raid_level (hdd), ==, "RAID1");
    g_assert_cmpstr (puls_smart_data_get_raid_member_status (hdd), ==, "Member of /dev/md0 [RAID1]");
    g_assert_cmpint (puls_smart_data_get_form_factor_enum (hdd), ==, PULS_FORM_FACTOR_3_5_INCH);
    g_assert_true (puls_should_use_standby_check_for_type (
        "/dev/sdb", puls_smart_data_get_drive_type (hdd), TRUE));
    g_autofree gchar *hdd_cmd = puls_build_smartctl_read_command ("/dev/sdb", TRUE);
    g_assert_cmpstr (hdd_cmd, ==, "smartctl -n standby -a -j /dev/sdb");

    /* 2. Dynamic hotplug attach/detach & pseudo-device clutter filtering */
    HotplugSignalStats hp = { 0 };
    g_signal_connect (mgr, "disk-added", G_CALLBACK (on_test_disk_added), &hp);
    g_signal_connect (mgr, "disk-removed", G_CALLBACK (on_test_disk_removed), &hp);

    /* Clutter/pseudo uevents must be rejected */
    const gchar ev_loop[] = "ACTION=add\0SUBSYSTEM=block\0DEVTYPE=disk\0DEVNAME=loop12\0";
    const gchar ev_dm[]   = "ACTION=add\0SUBSYSTEM=block\0DEVTYPE=disk\0DEVNAME=dm-3\0";
    const gchar ev_ctrl[] = "ACTION=add\0SUBSYSTEM=block\0DEVTYPE=disk\0DEVNAME=nvme2\0";
    const gchar ev_part[] = "ACTION=add\0SUBSYSTEM=block\0DEVTYPE=partition\0DEVNAME=sdd1\0";
    g_assert_false (puls_disk_manager_handle_uevent (mgr, ev_loop, sizeof (ev_loop)));
    g_assert_false (puls_disk_manager_handle_uevent (mgr, ev_dm, sizeof (ev_dm)));
    g_assert_false (puls_disk_manager_handle_uevent (mgr, ev_ctrl, sizeof (ev_ctrl)));
    g_assert_false (puls_disk_manager_handle_uevent (mgr, ev_part, sizeof (ev_part)));
    g_assert_cmpuint (hp.added_count, ==, 0);

    /* Hotplug attach USB disk + NVMe namespace */
    const gchar ev_sdd_add[]   = "ACTION=add\0SUBSYSTEM=block\0DEVTYPE=disk\0DEVNAME=sdd\0";
    const gchar ev_nvme2_add[] = "ACTION=add\0SUBSYSTEM=block\0DEVTYPE=disk\0DEVNAME=nvme2n1\0";
    g_assert_true (puls_disk_manager_handle_uevent (mgr, ev_sdd_add, sizeof (ev_sdd_add)));
    g_assert_true (puls_disk_manager_handle_uevent (mgr, ev_nvme2_add, sizeof (ev_nvme2_add)));
    g_assert_cmpuint (hp.added_count, ==, 2);
    g_assert_cmpuint (puls_disk_manager_get_device_count (mgr), ==, 6);

    PulsSmartData *nvme2 = puls_disk_manager_get_smart_data (mgr, "/dev/nvme2n1");
    g_assert_nonnull (nvme2);
    g_assert_cmpstr (puls_smart_data_get_controller_path (nvme2), ==, "/dev/nvme2");
    g_assert_cmpuint (puls_smart_data_get_nvme_nsid (nvme2), ==, 1);

    /* Hotplug detach both */
    const gchar ev_sdd_rem[]   = "ACTION=remove\0SUBSYSTEM=block\0DEVTYPE=disk\0DEVNAME=sdd\0";
    const gchar ev_nvme2_rem[] = "ACTION=remove\0SUBSYSTEM=block\0DEVTYPE=disk\0DEVNAME=nvme2n1\0";
    g_assert_true (puls_disk_manager_handle_uevent (mgr, ev_sdd_rem, sizeof (ev_sdd_rem)));
    g_assert_true (puls_disk_manager_handle_uevent (mgr, ev_nvme2_rem, sizeof (ev_nvme2_rem)));
    g_assert_cmpuint (hp.removed_count, ==, 2);
    g_assert_cmpuint (puls_disk_manager_get_device_count (mgr), ==, 4);

    g_free (hp.last_added);
    g_free (hp.last_removed);
    g_free (hp.last_updated);

    /* 3. Live host async scan validates end-to-end pipeline without virtual clutter */
    g_autoptr(PulsDiskManager) mgr_live = puls_disk_manager_new ();
    puls_disk_manager_set_mock_mode (mgr_live, FALSE);

    AsyncScanTestCtx live_ctx = {
        .loop = g_main_loop_new (NULL, FALSE),
        .main_thread = g_thread_self (),
        .signals_on_main_thread = TRUE,
    };
    puls_disk_manager_scan_async (mgr_live, NULL, on_async_scan_test_done, &live_ctx);
    g_main_loop_run (live_ctx.loop);
    g_main_loop_unref (live_ctx.loop);

    g_assert_true (live_ctx.ok);
    g_assert_no_error (live_ctx.error);
    for (GList *l = puls_disk_manager_get_devices (mgr_live); l != NULL; l = l->next) {
        const gchar *path = l->data;
        g_assert_true (puls_disk_manager_is_real_disk_name (path));
        PulsSmartData *d = puls_disk_manager_get_smart_data (mgr_live, path);
        g_assert_nonnull (d);
        g_assert_cmpint (puls_smart_data_get_form_factor_enum (d), !=, PULS_FORM_FACTOR_UNKNOWN);
        if (puls_nvme_is_namespace_name (path)) {
            g_assert_nonnull (puls_smart_data_get_controller_path (d));
            g_assert_cmpuint (puls_smart_data_get_nvme_nsid (d), >=, 1);
        }
    }
}

static void
test_helper_protocol_handshake (void)
{
    /* 1. Client command formatting */
    g_autofree gchar *c_hello = puls_helper_format_hello (2);
    g_autofree gchar *c_read = puls_helper_format_read_smart ("/dev/nvme0", FALSE);
    g_autofree gchar *c_read_sb = puls_helper_format_read_smart ("/dev/sda", TRUE);
    g_autofree gchar *c_act = puls_helper_format_action ("/dev/sda", "short");
    g_autofree gchar *c_scan = puls_helper_format_stream_scan ("/dev/sda", 4096, 65536);

    g_assert_cmpstr (c_hello,   ==, "HELLO v2\n");
    g_assert_cmpstr (c_read,    ==, "READ_SMART /dev/nvme0\n");
    g_assert_cmpstr (c_read_sb, ==, "READ_SMART /dev/sda standby\n");
    g_assert_cmpstr (c_act,     ==, "ACTION /dev/sda short\n");
    g_assert_cmpstr (c_scan,    ==, "STREAM_SCAN /dev/sda 4096 65536\n");

    /* 2. Frame header parsing & bounds checks */
    gboolean ok = FALSE;
    gchar *err_code = NULL;
    gsize plen = 0;

    g_assert_true (puls_helper_parse_frame_header ("OK 128", &ok, &err_code, &plen));
    g_assert_true (ok);
    g_assert_null (err_code);
    g_assert_cmpuint (plen, ==, 128);

    g_assert_true (puls_helper_parse_frame_header ("ERR E_VERSION_MISMATCH 55", &ok, &err_code, &plen));
    g_assert_false (ok);
    g_assert_cmpstr (err_code, ==, "E_VERSION_MISMATCH");
    g_assert_cmpuint (plen, ==, 55);
    g_clear_pointer (&err_code, g_free);

    g_assert_false (puls_helper_parse_frame_header ("OK -5", &ok, &err_code, &plen));
    g_assert_false (puls_helper_parse_frame_header ("OK 999999999", &ok, &err_code, &plen));
    g_assert_false (puls_helper_parse_frame_header ("ERR 42", &ok, &err_code, &plen));
    g_assert_false (puls_helper_parse_frame_header ("GARBAGE", &ok, &err_code, &plen));
    g_assert_false (puls_helper_parse_frame_header ("", &ok, &err_code, &plen));

    /* 3. Live IPC handshake & version mismatch rejection against compiled helper */
#ifdef TEST_HELPER_BIN
    g_assert_true (g_file_test (TEST_HELPER_BIN, G_FILE_TEST_IS_EXECUTABLE));

    GError *err = NULL;
    const gchar *argv[] = { TEST_HELPER_BIN, NULL };
    g_autoptr(GSubprocessLauncher) launcher = g_subprocess_launcher_new (
        G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE);
    g_autoptr(GSubprocess) proc = g_subprocess_launcher_spawnv (launcher, argv, &err);
    g_assert_no_error (err);
    g_assert_nonnull (proc);

    GOutputStream *h_in = g_subprocess_get_stdin_pipe (proc);
    g_autoptr(GDataInputStream) h_out = g_data_input_stream_new (
        g_subprocess_get_stdout_pipe (proc));

    gchar *payload = NULL;

    /* Command before handshake must be rejected */
    g_assert_true (puls_helper_exchange_command (h_in, h_out, "READ_SMART /dev/sda\n",
                                                 &ok, &err_code, &payload, &err));
    g_assert_no_error (err);
    g_assert_false (ok);
    g_assert_cmpstr (err_code, ==, "E_HANDSHAKE_REQUIRED");
    g_clear_pointer (&err_code, g_free);
    g_clear_pointer (&payload, g_free);

    /* Version mismatches must be rejected */
    static const gchar * const bad_hellos[] = {
        "HELLO v1\n", "HELLO v3\n", "HELLO\n", "HELLO bogus\n"
    };
    for (guint i = 0; i < G_N_ELEMENTS (bad_hellos); i++) {
        g_assert_true (puls_helper_exchange_command (h_in, h_out, bad_hellos[i],
                                                     &ok, &err_code, &payload, &err));
        g_assert_no_error (err);
        g_assert_false (ok);
        g_assert_cmpstr (err_code, ==, "E_VERSION_MISMATCH");
        g_clear_pointer (&err_code, g_free);
        g_clear_pointer (&payload, g_free);
    }

    /* Valid v2 handshake succeeds */
    g_assert_true (puls_helper_exchange_command (h_in, h_out, "HELLO v2\n",
                                                 &ok, &err_code, &payload, &err));
    g_assert_no_error (err);
    g_assert_true (ok);
    g_assert_null (err_code);
    g_assert_nonnull (payload);
    g_assert_nonnull (strstr (payload, "\"protocol\":\"v2\""));
    g_assert_nonnull (strstr (payload, "\"version\":2"));
    g_clear_pointer (&payload, g_free);

    /* Post-handshake command validation */
    g_assert_true (puls_helper_exchange_command (h_in, h_out, "UNKNOWN_FOO\n",
                                                 &ok, &err_code, &payload, &err));
    g_assert_no_error (err);
    g_assert_false (ok);
    g_assert_cmpstr (err_code, ==, "E_UNKNOWN_CMD");
    g_clear_pointer (&err_code, g_free);
    g_clear_pointer (&payload, g_free);

    g_assert_true (puls_helper_exchange_command (h_in, h_out, "ACTION /dev/sda bogus\n",
                                                 &ok, &err_code, &payload, &err));
    g_assert_no_error (err);
    g_assert_false (ok);
    g_assert_cmpstr (err_code, ==, "E_INVALID_ACTION");
    g_clear_pointer (&err_code, g_free);
    g_clear_pointer (&payload, g_free);

    g_assert_true (puls_helper_exchange_command (h_in, h_out, "STREAM_SCAN /dev/sda 0 0\n",
                                                 &ok, &err_code, &payload, &err));
    g_assert_no_error (err);
    g_assert_false (ok);
    g_assert_cmpstr (err_code, ==, "E_INVALID_ARG");
    g_clear_pointer (&err_code, g_free);
    g_clear_pointer (&payload, g_free);

    g_assert_true (puls_helper_exchange_command (h_in, h_out, "READ_SMART /dev/invalid_xyz\n",
                                                 &ok, &err_code, &payload, &err));
    g_assert_no_error (err);
    g_assert_false (ok);
    g_assert_cmpstr (err_code, ==, "E_INVALID_DEVICE");
    g_clear_pointer (&err_code, g_free);
    g_clear_pointer (&payload, g_free);

    /* Clean QUIT exits helper with status 0 */
    g_assert_true (puls_helper_exchange_command (h_in, h_out, "QUIT\n",
                                                 &ok, &err_code, &payload, &err));
    g_assert_no_error (err);
    g_assert_true (ok);
    g_assert_cmpstr (payload, ==, "");
    g_clear_pointer (&payload, g_free);

    g_assert_true (g_subprocess_wait_check (proc, NULL, &err));
    g_assert_no_error (err);
#endif
}

static void
test_path_sanitization_anti_injection (void)
{
    /* 1. Directory traversal & path injection must be rejected */
    const gchar *traversal_paths[] = {
        "/dev/../etc/passwd",
        "/dev/sda/../../etc/shadow",
        "/dev/./sda",
        "../dev/sda",
        "/dev/..sda",
        "/dev/sda/..",
        "/dev//sda",
        "/dev/sda/",
        "dev/sda",
        "/tmp/sda",
        NULL
    };
    for (gsize i = 0; traversal_paths[i] != NULL; i++) {
        g_assert_false (puls_device_path_is_valid (traversal_paths[i]));
        g_assert_cmpint (puls_helper_is_valid_device_syntax (traversal_paths[i]), ==, 0);
    }

    /* 2. Shell metacharacter & command injection must be rejected */
    const gchar *injection_paths[] = {
        "/dev/sda;reboot",
        "/dev/sda|cat /etc/passwd",
        "/dev/sda&&id",
        "/dev/sda$(id)",
        "/dev/sda`id`",
        "/dev/sda > /tmp/pwn",
        "/dev/sda --help",
        "/dev/sda\nQUIT",
        "/dev/sda\tstandby",
        "/dev/sda'\"\\",
        NULL
    };
    for (gsize i = 0; injection_paths[i] != NULL; i++) {
        g_assert_false (puls_device_path_is_valid (injection_paths[i]));
        g_assert_cmpint (puls_helper_is_valid_device_syntax (injection_paths[i]), ==, 0);
    }

    /* 3. Pseudo-devices, partitions, bare prefixes, and uppercase must be rejected */
    const gchar *rejected_nodes[] = {
        NULL,
        "",
        "/dev/",
        "/dev/null",
        "/dev/zero",
        "/dev/urandom",
        "/dev/random",
        "/dev/tty",
        "/dev/mem",
        "/dev/kmsg",
        "/dev/loop0",
        "/dev/ram0",
        "/dev/zram0",
        "/dev/dm-0",
        "/dev/md0",
        "/dev/sda1",
        "/dev/sda12",
        "/dev/nvme0n1p1",
        "/dev/nvme0p1",
        "/dev/nvme0n0",
        "/dev/mmcblk0p1",
        "/dev/sg0p1",
        "/dev/sd",
        "/dev/hd",
        "/dev/vd",
        "/dev/xvd",
        "/dev/nvme",
        "/dev/mmcblk",
        "/dev/sg",
        "/dev/sga",
        "/dev/SDA",
        "/dev/NVME0N1",
        "/dev/sdabcde",
        NULL
    };
    for (gsize i = 0; rejected_nodes[i] != NULL || i == 0; i++) {
        g_assert_false (puls_device_path_is_valid (rejected_nodes[i]));
        g_assert_cmpint (puls_helper_is_valid_device_syntax (rejected_nodes[i]), ==, 0);
        if (i > 0 && rejected_nodes[i] == NULL)
            break;
    }

    /* 4. Valid whole-disk & controller device syntax must be accepted */
    const gchar *valid_paths[] = {
        "/dev/sda",
        "/dev/sdz",
        "/dev/sdab",
        "/dev/hda",
        "/dev/vda",
        "/dev/xvda",
        "/dev/nvme0",
        "/dev/nvme12",
        "/dev/nvme0n1",
        "/dev/nvme0n16",
        "/dev/nvme1c2n1",
        "/dev/mmcblk0",
        "/dev/mmcblk12",
        "/dev/sg0",
        "/dev/sg42",
        NULL
    };
    for (gsize i = 0; valid_paths[i] != NULL; i++) {
        g_assert_true (puls_device_path_is_valid (valid_paths[i]));
        g_assert_cmpint (puls_helper_is_valid_device_syntax (valid_paths[i]), ==, 1);
    }

    /* 5. Anti-symlink hijacking & non-device file spoofing verification */
    g_autoptr(GError) err = NULL;
    g_autofree gchar *tmp_dev = g_dir_make_tmp ("puls_test_dev_guard_XXXXXX", &err);
    g_assert_no_error (err);
    g_assert_nonnull (tmp_dev);

    /* 5a. Regular file named 'sda' inside dev_root must be rejected (not S_ISBLK/S_ISCHR) */
    g_autofree gchar *fake_file_sda = g_build_filename (tmp_dev, "sda", NULL);
    g_assert_true (g_file_set_contents (fake_file_sda, "fake_block_data", -1, &err));
    g_assert_no_error (err);

    g_assert_false (puls_validate_device_node_in_dir (tmp_dev, fake_file_sda, &err));
    g_assert_error (err, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error (&err);

    /* 5b. Symlink hijacking 'sdb -> /etc/passwd' or 'sdc -> /dev/null' must be rejected */
    g_autofree gchar *symlink_sdb = g_build_filename (tmp_dev, "sdb", NULL);
    g_assert_cmpint (symlink ("/etc/passwd", symlink_sdb), ==, 0);

    g_assert_false (puls_validate_device_node_in_dir (tmp_dev, symlink_sdb, &err));
    g_assert_error (err, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error (&err);

    g_autofree gchar *symlink_nvme = g_build_filename (tmp_dev, "nvme0n1", NULL);
    g_assert_cmpint (symlink ("/dev/null", symlink_nvme), ==, 0);

    g_assert_false (puls_validate_device_node_in_dir (tmp_dev, symlink_nvme, &err));
    g_assert_error (err, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error (&err);

    /* 5c. Directory traversal under tmp_dev must be rejected */
    g_autofree gchar *trav_in_tmp = g_strdup_printf ("%s/../etc/passwd", tmp_dev);
    g_assert_false (puls_validate_device_node_in_dir (tmp_dev, trav_in_tmp, &err));
    g_assert_error (err, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error (&err);

    unlink (fake_file_sda);
    unlink (symlink_sdb);
    unlink (symlink_nvme);
    rmdir (tmp_dev);

    /* 5d. If host has real /dev/nvme0 or /dev/nvme0n1 or /dev/sda, verify puls_validate_device_node accepts it */
    const gchar *host_candidates[] = { "/dev/nvme0", "/dev/nvme0n1", "/dev/sda", "/dev/vda", NULL };
    for (gsize i = 0; host_candidates[i] != NULL; i++) {
        struct stat st;
        if (lstat (host_candidates[i], &st) == 0 &&
            !S_ISLNK (st.st_mode) &&
            (S_ISBLK (st.st_mode) || S_ISCHR (st.st_mode))) {
            g_assert_true (puls_validate_device_node (host_candidates[i], &err));
            g_assert_no_error (err);
        }
    }

    /* 5e. Client sync smartctl wrappers must reject injection paths before spawning anything */
    g_autofree gchar *bad_out = puls_run_smartctl_sync ("/dev/sda;reboot", &err);
    g_assert_null (bad_out);
    g_assert_error (err, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error (&err);

    g_autofree gchar *bad_act = puls_run_smartctl_action_sync ("/dev/../etc/passwd", "short", &err);
    g_assert_null (bad_act);
    g_assert_error (err, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error (&err);

#ifdef TEST_HELPER_BIN
    /* 6. Live Privileged Helper IPC anti-injection test */
    g_autoptr(GSubprocess) proc = g_subprocess_new (
        G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
        &err, TEST_HELPER_BIN, NULL);
    g_assert_no_error (err);
    g_assert_nonnull (proc);

    GOutputStream *h_in = g_subprocess_get_stdin_pipe (proc);
    GInputStream *raw_out = g_subprocess_get_stdout_pipe (proc);
    g_autoptr(GDataInputStream) h_out = g_data_input_stream_new (raw_out);

    gboolean hs_ok = FALSE;
    g_autofree gchar *hs_err = NULL;
    g_autofree gchar *hs_payload = NULL;
    g_assert_true (puls_helper_exchange_command (h_in, h_out, "HELLO v2\n",
                                                 &hs_ok, &hs_err, &hs_payload, &err));
    g_assert_no_error (err);
    g_assert_true (hs_ok);

    const gchar *helper_attack_cmds[] = {
        "READ_SMART /dev/../etc/passwd\n",
        "READ_SMART /dev/sda;reboot\n",
        "READ_SMART /dev/sda|id\n",
        "READ_SMART /dev/null\n",
        "READ_SMART /dev/sda1\n",
        "ACTION /dev/../etc/passwd short\n",
        "ACTION /dev/sda;reboot short\n",
        "STREAM_SCAN /dev/../etc/passwd 0 512\n",
        "STREAM_SCAN /dev/sda;reboot 0 512\n",
        "STREAM_SCAN /dev/null 0 512\n",
        NULL
    };

    for (gsize i = 0; helper_attack_cmds[i] != NULL; i++) {
        gboolean ok = TRUE;
        g_autofree gchar *err_code = NULL;
        g_autofree gchar *payload = NULL;

        g_assert_true (puls_helper_exchange_command (h_in, h_out, helper_attack_cmds[i],
                                                     &ok, &err_code, &payload, &err));
        g_assert_no_error (err);
        g_assert_false (ok);
        g_assert_cmpstr (err_code, ==, "E_INVALID_DEVICE");
    }

    gboolean quit_ok = FALSE;
    g_autofree gchar *quit_err = NULL;
    g_autofree gchar *quit_payload = NULL;
    g_assert_true (puls_helper_exchange_command (h_in, h_out, "QUIT\n",
                                                 &quit_ok, &quit_err, &quit_payload, &err));
    g_assert_no_error (err);
    g_assert_true (quit_ok);
    g_assert_true (g_subprocess_wait_check (proc, NULL, &err));
    g_assert_no_error (err);
#endif
}

static void
test_helper_command_whitelist (void)
{
    /* 1. Shared pure-C protocol whitelist validators */
    g_assert_cmpint (puls_helper_is_valid_command ("HELLO"), ==, 1);
    g_assert_cmpint (puls_helper_is_valid_command ("READ_SMART"), ==, 1);
    g_assert_cmpint (puls_helper_is_valid_command ("ACTION"), ==, 1);
    g_assert_cmpint (puls_helper_is_valid_command ("STREAM_SCAN"), ==, 1);
    g_assert_cmpint (puls_helper_is_valid_command ("QUIT"), ==, 1);

    const gchar *bad_cmds[] = {
        NULL, "", "EXEC", "SHELL", "WRITE_SECTOR", "FORMAT",
        "SANITIZE", "HDPARM", "NVME_FORMAT", "DD", "SUDO",
        "read_smart", "action", "stream_scan", "quit", "PING", NULL
    };
    for (gsize i = 0; i == 0 || bad_cmds[i] != NULL; i++) {
        g_assert_cmpint (puls_helper_is_valid_command (bad_cmds[i]), ==, 0);
        if (i > 0 && bad_cmds[i] == NULL)
            break;
    }

    /* READ_SMART subcommand option whitelist: strictly NULL or "standby" */
    g_assert_cmpint (puls_helper_is_valid_read_option (NULL), ==, 1);
    g_assert_cmpint (puls_helper_is_valid_read_option ("standby"), ==, 1);
    g_assert_cmpint (puls_helper_is_valid_read_option ("short"), ==, 0);
    g_assert_cmpint (puls_helper_is_valid_read_option ("long"), ==, 0);
    g_assert_cmpint (puls_helper_is_valid_read_option ("abort"), ==, 0);
    g_assert_cmpint (puls_helper_is_valid_read_option ("STANDBY"), ==, 0);
    g_assert_cmpint (puls_helper_is_valid_read_option ("--all"), ==, 0);
    g_assert_cmpint (puls_helper_is_valid_read_option (""), ==, 0);

    /* ACTION self-test whitelist: strictly "short", "long", "abort" */
    g_assert_cmpint (puls_helper_is_valid_action ("short"), ==, 1);
    g_assert_cmpint (puls_helper_is_valid_action ("long"), ==, 1);
    g_assert_cmpint (puls_helper_is_valid_action ("abort"), ==, 1);
    g_assert_true (puls_helper_action_is_valid ("short"));
    g_assert_true (puls_helper_action_is_valid ("long"));
    g_assert_true (puls_helper_action_is_valid ("abort"));

    const gchar *bad_actions[] = {
        NULL, "", "standby", "conveyance", "offline", "select",
        "format", "sanitize", "write", "reset", "SHORT", "LONG",
        "ABORT", "--smart=off", "-t", "-X", "short;id", NULL
    };
    for (gsize i = 0; i == 0 || bad_actions[i] != NULL; i++) {
        g_assert_cmpint (puls_helper_is_valid_action (bad_actions[i]), ==, 0);
        g_assert_false (puls_helper_action_is_valid (bad_actions[i]));
        if (i > 0 && bad_actions[i] == NULL)
            break;
    }

    /* Single-shot CLI subcommand whitelist */
    g_assert_cmpint (puls_helper_is_valid_cli_subcommand (NULL), ==, 1);
    g_assert_cmpint (puls_helper_is_valid_cli_subcommand ("standby"), ==, 1);
    g_assert_cmpint (puls_helper_is_valid_cli_subcommand ("short"), ==, 1);
    g_assert_cmpint (puls_helper_is_valid_cli_subcommand ("long"), ==, 1);
    g_assert_cmpint (puls_helper_is_valid_cli_subcommand ("abort"), ==, 1);
    g_assert_cmpint (puls_helper_is_valid_cli_subcommand ("format"), ==, 0);
    g_assert_cmpint (puls_helper_is_valid_cli_subcommand ("conveyance"), ==, 0);

    /* 2. Client-side action guard rejects disallowed actions before spawning anything */
    g_autoptr(GError) err = NULL;
    for (gsize i = 0; i == 0 || bad_actions[i] != NULL; i++) {
        g_autofree gchar *out = puls_run_smartctl_action_sync ("/dev/sda", bad_actions[i], &err);
        g_assert_null (out);
        g_assert_error (err, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
        g_clear_error (&err);
        if (i > 0 && bad_actions[i] == NULL)
            break;
    }

#ifdef TEST_HELPER_BIN
    /* 3. Live Helper IPC granular command & subcommand whitelist checks */
    g_autoptr(GSubprocess) proc = g_subprocess_new (
        G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
        &err, TEST_HELPER_BIN, NULL);
    g_assert_no_error (err);
    g_assert_nonnull (proc);

    GOutputStream *h_in = g_subprocess_get_stdin_pipe (proc);
    g_autoptr(GDataInputStream) h_out = g_data_input_stream_new (
        g_subprocess_get_stdout_pipe (proc));

    gboolean ok = FALSE;
    g_autofree gchar *err_code = NULL;
    g_autofree gchar *payload = NULL;

    /* Handshake advertises granular read_options & actions whitelist */
    g_assert_true (puls_helper_exchange_command (h_in, h_out, "HELLO v2\n",
                                                 &ok, &err_code, &payload, &err));
    g_assert_no_error (err);
    g_assert_true (ok);
    g_assert_nonnull (strstr (payload, "\"actions\":[\"short\",\"long\",\"abort\"]"));
    g_assert_nonnull (strstr (payload, "\"read_options\":[\"standby\"]"));
    g_clear_pointer (&err_code, g_free);
    g_clear_pointer (&payload, g_free);

    /* 3a. Unrecognized top-level commands -> E_UNKNOWN_CMD */
    const gchar *unknown_cmds[] = {
        "EXEC /bin/sh\n",
        "SHELL id\n",
        "WRITE_SECTOR /dev/sda 0 512\n",
        "FORMAT /dev/sda\n",
        "SANITIZE /dev/sda\n",
        "HDPARM /dev/sda\n",
        "NVME_FORMAT /dev/nvme0n1\n",
        "DD /dev/sda\n",
        "read_smart /dev/sda\n",
        "action /dev/sda short\n",
        NULL
    };
    for (gsize i = 0; unknown_cmds[i] != NULL; i++) {
        g_assert_true (puls_helper_exchange_command (h_in, h_out, unknown_cmds[i],
                                                     &ok, &err_code, &payload, &err));
        g_assert_no_error (err);
        g_assert_false (ok);
        g_assert_cmpstr (err_code, ==, PULS_HELPER_ERR_UNKNOWN_CMD);
        g_clear_pointer (&err_code, g_free);
        g_clear_pointer (&payload, g_free);
    }

    /* 3b. Disallowed ACTION subcommands (including 'standby', 'conveyance', 'offline', flags) -> E_INVALID_ACTION */
    const gchar *disallowed_action_cmds[] = {
        "ACTION /dev/sda standby\n",
        "ACTION /dev/sda conveyance\n",
        "ACTION /dev/sda offline\n",
        "ACTION /dev/sda select\n",
        "ACTION /dev/sda format\n",
        "ACTION /dev/sda sanitize\n",
        "ACTION /dev/sda write\n",
        "ACTION /dev/sda reset\n",
        "ACTION /dev/sda SHORT\n",
        "ACTION /dev/sda --smart=off\n",
        "ACTION /dev/sda -t\n",
        NULL
    };
    for (gsize i = 0; disallowed_action_cmds[i] != NULL; i++) {
        g_assert_true (puls_helper_exchange_command (h_in, h_out, disallowed_action_cmds[i],
                                                     &ok, &err_code, &payload, &err));
        g_assert_no_error (err);
        g_assert_false (ok);
        g_assert_cmpstr (err_code, ==, PULS_HELPER_ERR_INVALID_ACTION);
        g_clear_pointer (&err_code, g_free);
        g_clear_pointer (&payload, g_free);
    }

    /* 3c. Disallowed READ_SMART subcommands -> E_INVALID_SUBCMD */
    const gchar *disallowed_read_subcmds[] = {
        "READ_SMART /dev/sda short\n",
        "READ_SMART /dev/sda long\n",
        "READ_SMART /dev/sda abort\n",
        "READ_SMART /dev/sda raw\n",
        "READ_SMART /dev/sda force\n",
        "READ_SMART /dev/sda STANDBY\n",
        "READ_SMART /dev/sda -x\n",
        "READ_SMART /dev/sda --all\n",
        NULL
    };
    for (gsize i = 0; disallowed_read_subcmds[i] != NULL; i++) {
        g_assert_true (puls_helper_exchange_command (h_in, h_out, disallowed_read_subcmds[i],
                                                     &ok, &err_code, &payload, &err));
        g_assert_no_error (err);
        g_assert_false (ok);
        g_assert_cmpstr (err_code, ==, PULS_HELPER_ERR_INVALID_SUBCMD);
        g_clear_pointer (&err_code, g_free);
        g_clear_pointer (&payload, g_free);
    }

    /* 3d. Arity violations, overflow bounds, and strict QUIT check -> E_INVALID_ARG */
    const gchar *invalid_arg_cmds[] = {
        "READ_SMART\n",
        "READ_SMART /dev/sda standby extra\n",
        "ACTION /dev/sda\n",
        "ACTION /dev/sda short extra\n",
        "STREAM_SCAN /dev/sda 0\n",
        "STREAM_SCAN /dev/sda 0 512 extra\n",
        "STREAM_SCAN /dev/sda -1 512\n",
        "STREAM_SCAN /dev/sda 0 99999999\n",
        "STREAM_SCAN /dev/sda 9223372036854775807 4096\n",
        "QUIT extra\n",
        "READ_SMART /dev/sda\x01\n",
        NULL
    };
    for (gsize i = 0; invalid_arg_cmds[i] != NULL; i++) {
        g_assert_true (puls_helper_exchange_command (h_in, h_out, invalid_arg_cmds[i],
                                                     &ok, &err_code, &payload, &err));
        g_assert_no_error (err);
        g_assert_false (ok);
        g_assert_cmpstr (err_code, ==, PULS_HELPER_ERR_INVALID_ARG);
        g_clear_pointer (&err_code, g_free);
        g_clear_pointer (&payload, g_free);
    }

    /* 3e. Embedded NUL byte in protocol line must be rejected with E_INVALID_ARG */
    static const char nul_cmd[] = "READ_SMART /dev/sda\0;id\n";
    gsize written = 0;
    g_assert_true (g_output_stream_write_all (h_in, nul_cmd, sizeof (nul_cmd) - 1,
                                              &written, NULL, &err));
    g_assert_no_error (err);
    g_autofree gchar *nul_hdr = g_data_input_stream_read_line (h_out, NULL, NULL, &err);
    g_assert_no_error (err);
    g_assert_nonnull (nul_hdr);
    gsize nul_plen = 0;
    g_assert_true (puls_helper_parse_frame_header (nul_hdr, &ok, &err_code, &nul_plen));
    g_assert_false (ok);
    g_assert_cmpstr (err_code, ==, PULS_HELPER_ERR_INVALID_ARG);
    if (nul_plen > 0) {
        g_autofree gchar *discard = g_malloc (nul_plen);
        gsize got = 0;
        g_assert_true (g_input_stream_read_all (G_INPUT_STREAM (h_out), discard, nul_plen,
                                                &got, NULL, &err));
        g_assert_no_error (err);
    }
    g_clear_pointer (&err_code, g_free);

    /* Clean QUIT still works after rejections */
    g_assert_true (puls_helper_exchange_command (h_in, h_out, "QUIT\n",
                                                 &ok, &err_code, &payload, &err));
    g_assert_no_error (err);
    g_assert_true (ok);
    g_clear_pointer (&payload, g_free);
    g_assert_true (g_subprocess_wait_check (proc, NULL, &err));
    g_assert_no_error (err);

    /* 4. Legacy single-shot CLI mode must reject unrecognized subcommands & excess args with exit code 5 */
    const gchar *cli_bad_1[] = { TEST_HELPER_BIN, "/dev/sda", "format", NULL };
    g_autoptr(GSubprocess) cli_p1 = g_subprocess_newv (
        cli_bad_1, G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE, &err);
    g_assert_no_error (err);
    g_assert_true (g_subprocess_wait (cli_p1, NULL, &err));
    g_assert_no_error (err);
    g_assert_true (g_subprocess_get_if_exited (cli_p1));
    g_assert_cmpint (g_subprocess_get_exit_status (cli_p1), ==, 5);

    const gchar *cli_bad_2[] = { TEST_HELPER_BIN, "/dev/sda", "short", "--extra", NULL };
    g_autoptr(GSubprocess) cli_p2 = g_subprocess_newv (
        cli_bad_2, G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE, &err);
    g_assert_no_error (err);
    g_assert_true (g_subprocess_wait (cli_p2, NULL, &err));
    g_assert_no_error (err);
    g_assert_true (g_subprocess_get_if_exited (cli_p2));
    g_assert_cmpint (g_subprocess_get_exit_status (cli_p2), ==, 5);
#endif
}

static void
test_polkit_policy_validation (void)
{
    g_autoptr(GError) err = NULL;

#ifdef TEST_POLICY_FILE
    /* 1. Validate the shipped io.github.word_sys.puls_diskinfo.policy file */
    g_autoptr(GPtrArray) actions = NULL;
    g_assert_true (puls_polkit_validate_policy_file (TEST_POLICY_FILE, &actions, &err));
    g_assert_no_error (err);
    g_assert_nonnull (actions);
    g_assert_cmpuint (actions->len, ==, 3);

    const PulsPolkitActionInfo *act_read = NULL;
    const PulsPolkitActionInfo *act_selftest = NULL;
    const PulsPolkitActionInfo *act_scan = NULL;

    for (guint i = 0; i < actions->len; i++) {
        const PulsPolkitActionInfo *a = g_ptr_array_index (actions, i);
        g_assert_nonnull (a->id);
        g_assert_nonnull (a->description_default);
        g_assert_nonnull (a->description_en);
        g_assert_nonnull (a->description_tr);
        g_assert_nonnull (a->message_default);
        g_assert_nonnull (a->message_en);
        g_assert_nonnull (a->message_tr);
        g_assert_cmpstr (a->allow_any, ==, "auth_admin");
        g_assert_cmpstr (a->allow_inactive, ==, "auth_admin");
        g_assert_cmpstr (a->allow_active, ==, "auth_admin_keep");

        if (g_strcmp0 (a->id, PULS_POLKIT_ACTION_SMART_READ) == 0)
            act_read = a;
        else if (g_strcmp0 (a->id, PULS_POLKIT_ACTION_SMART_SELFTEST) == 0)
            act_selftest = a;
        else if (g_strcmp0 (a->id, PULS_POLKIT_ACTION_SURFACE_SCAN) == 0)
            act_scan = a;
    }

    g_assert_nonnull (act_read);
    g_assert_nonnull (act_selftest);
    g_assert_nonnull (act_scan);

    /* Primary action binds pkexec to puls-diskinfo-helper with allow_gui=false */
    g_assert_cmpstr (act_read->exec_path, ==, "/usr/libexec/puls-diskinfo-helper");
    g_assert_cmpstr (act_read->allow_gui, ==, "false");
    g_assert_nonnull (strstr (act_read->description_tr, "S.M.A.R.T."));
    g_assert_nonnull (strstr (act_read->message_tr, "kimlik doğrulaması"));

    /* Secondary actions imply smart-read without duplicating exec.path */
    g_assert_null (act_selftest->exec_path);
    g_assert_cmpstr (act_selftest->imply, ==, PULS_POLKIT_ACTION_SMART_READ);
    g_assert_nonnull (strstr (act_selftest->description_tr, "S.M.A.R.T."));

    g_assert_null (act_scan->exec_path);
    g_assert_cmpstr (act_scan->imply, ==, PULS_POLKIT_ACTION_SMART_READ);
    g_assert_nonnull (strstr (act_scan->description_tr, "yüzey taraması"));
#endif

    /* 2. Negative security & schema validation tests */
    /* 2a. Malformed XML */
    g_assert_false (puls_polkit_validate_policy_xml ("<policyconfig><action></policyconfig>", NULL, &err));
    g_assert_nonnull (err);
    g_clear_error (&err);

    /* 2b. Wrong root tag */
    g_assert_false (puls_polkit_validate_policy_xml ("<notpolicy></notpolicy>", NULL, &err));
    g_assert_nonnull (err);
    g_clear_error (&err);

    /* 2c. Invalid Polkit <defaults> value */
    static const gchar *xml_bad_default =
        "<policyconfig><action id=\"io.github.word_sys.puls_diskinfo.smart-read\">"
        "<description>Read</description><description xml:lang=\"en\">Read</description>"
        "<description xml:lang=\"tr\">Oku</description>"
        "<message>Auth</message><message xml:lang=\"en\">Auth</message>"
        "<message xml:lang=\"tr\">Doğrula</message>"
        "<defaults><allow_any>auth_admin</allow_any><allow_inactive>auth_admin</allow_inactive>"
        "<allow_active>allow_everyone</allow_active></defaults>"
        "<annotate key=\"org.freedesktop.policykit.exec.path\">/usr/libexec/puls-diskinfo-helper</annotate>"
        "<annotate key=\"org.freedesktop.policykit.exec.allow_gui\">false</annotate>"
        "</action></policyconfig>";
    g_assert_false (puls_polkit_validate_policy_xml (xml_bad_default, NULL, &err));
    g_assert_nonnull (err);
    g_clear_error (&err);

    /* 2d. Passwordless <allow_active>yes</allow_active> rejected */
    static const gchar *xml_passwordless =
        "<policyconfig><action id=\"io.github.word_sys.puls_diskinfo.smart-read\">"
        "<description>Read</description><description xml:lang=\"en\">Read</description>"
        "<description xml:lang=\"tr\">Oku</description>"
        "<message>Auth</message><message xml:lang=\"en\">Auth</message>"
        "<message xml:lang=\"tr\">Doğrula</message>"
        "<defaults><allow_any>auth_admin</allow_any><allow_inactive>auth_admin</allow_inactive>"
        "<allow_active>yes</allow_active></defaults>"
        "<annotate key=\"org.freedesktop.policykit.exec.path\">/usr/libexec/puls-diskinfo-helper</annotate>"
        "<annotate key=\"org.freedesktop.policykit.exec.allow_gui\">false</annotate>"
        "</action></policyconfig>";
    g_assert_false (puls_polkit_validate_policy_xml (xml_passwordless, NULL, &err));
    g_assert_nonnull (err);
    g_clear_error (&err);

    /* 2e. Missing Turkish (xml:lang="tr") localization rejected */
    static const gchar *xml_no_tr =
        "<policyconfig><action id=\"io.github.word_sys.puls_diskinfo.smart-read\">"
        "<description>Read</description><description xml:lang=\"en\">Read</description>"
        "<message>Auth</message><message xml:lang=\"en\">Auth</message>"
        "<defaults><allow_any>auth_admin</allow_any><allow_inactive>auth_admin</allow_inactive>"
        "<allow_active>auth_admin_keep</allow_active></defaults>"
        "<annotate key=\"org.freedesktop.policykit.exec.path\">/usr/libexec/puls-diskinfo-helper</annotate>"
        "<annotate key=\"org.freedesktop.policykit.exec.allow_gui\">false</annotate>"
        "</action></policyconfig>";
    g_assert_false (puls_polkit_validate_policy_xml (xml_no_tr, NULL, &err));
    g_assert_nonnull (err);
    g_clear_error (&err);

    /* 2f. Duplicate action ID rejected */
    static const gchar *xml_dup_id =
        "<policyconfig>"
        "<action id=\"io.github.word_sys.puls_diskinfo.smart-read\">"
        "<description>A</description><description xml:lang=\"en\">A</description><description xml:lang=\"tr\">A</description>"
        "<message>M</message><message xml:lang=\"en\">M</message><message xml:lang=\"tr\">M</message>"
        "<defaults><allow_any>auth_admin</allow_any><allow_inactive>auth_admin</allow_inactive><allow_active>auth_admin_keep</allow_active></defaults>"
        "<annotate key=\"org.freedesktop.policykit.exec.path\">/usr/libexec/puls-diskinfo-helper</annotate>"
        "<annotate key=\"org.freedesktop.policykit.exec.allow_gui\">false</annotate>"
        "</action>"
        "<action id=\"io.github.word_sys.puls_diskinfo.smart-read\">"
        "<description>B</description><description xml:lang=\"en\">B</description><description xml:lang=\"tr\">B</description>"
        "<message>M</message><message xml:lang=\"en\">M</message><message xml:lang=\"tr\">M</message>"
        "<defaults><allow_any>auth_admin</allow_any><allow_inactive>auth_admin</allow_inactive><allow_active>auth_admin_keep</allow_active></defaults>"
        "</action>"
        "</policyconfig>";
    g_assert_false (puls_polkit_validate_policy_xml (xml_dup_id, NULL, &err));
    g_assert_nonnull (err);
    g_clear_error (&err);

    /* 2g. allow_gui=true on headless helper rejected */
    static const gchar *xml_allow_gui =
        "<policyconfig><action id=\"io.github.word_sys.puls_diskinfo.smart-read\">"
        "<description>Read</description><description xml:lang=\"en\">Read</description>"
        "<description xml:lang=\"tr\">Oku</description>"
        "<message>Auth</message><message xml:lang=\"en\">Auth</message>"
        "<message xml:lang=\"tr\">Doğrula</message>"
        "<defaults><allow_any>auth_admin</allow_any><allow_inactive>auth_admin</allow_inactive>"
        "<allow_active>auth_admin_keep</allow_active></defaults>"
        "<annotate key=\"org.freedesktop.policykit.exec.path\">/usr/libexec/puls-diskinfo-helper</annotate>"
        "<annotate key=\"org.freedesktop.policykit.exec.allow_gui\">true</annotate>"
        "</action></policyconfig>";
    g_assert_false (puls_polkit_validate_policy_xml (xml_allow_gui, NULL, &err));
    g_assert_nonnull (err);
    g_clear_error (&err);
}

static void
test_helper_crash_recovery_watchdog (void)
{
#ifdef TEST_HELPER_BIN
    g_autoptr(GError) err = NULL;
    PulsHelperClient *client = puls_helper_client_new (TEST_HELPER_BIN, FALSE);
    g_assert_nonnull (client);

    PulsHelperStats st = { 0 };
    puls_helper_client_get_stats (client, &st);
    g_assert_false (st.running);
    g_assert_cmpuint (st.launch_count, ==, 0);
    g_assert_cmpuint (st.crash_count, ==, 0);
    g_assert_cmpuint (st.reaped_count, ==, 0);

    /* 1. Initial launch & Protocol v2 handshake */
    g_assert_true (puls_helper_client_ensure_running (client, &err));
    g_assert_no_error (err);
    g_assert_true (puls_helper_client_watchdog_check (client));

    puls_helper_client_get_stats (client, &st);
    g_assert_true (st.running);
    g_assert_cmpint ((gint)st.pid, >, 0);
    g_assert_cmpuint (st.launch_count, ==, 1);
    GPid pid1 = st.pid;

    /* Normal query reuses the same live helper process */
    gboolean ok = TRUE;
    g_autofree gchar *err_code = NULL;
    g_autofree gchar *payload = NULL;
    g_assert_true (puls_helper_client_exchange (client, "READ_SMART /dev/invalid_xyz\n",
                                                &ok, &err_code, &payload, &err));
    g_assert_no_error (err);
    g_assert_false (ok);
    g_assert_cmpstr (err_code, ==, PULS_HELPER_ERR_INVALID_DEVICE);
    g_clear_pointer (&err_code, g_free);
    g_clear_pointer (&payload, g_free);

    puls_helper_client_get_stats (client, &st);
    g_assert_cmpint ((gint)st.pid, ==, (gint)pid1);
    g_assert_cmpuint (st.launch_count, ==, 1);

    /* 2. Intentionally SIGKILL helper -> watchdog detects termination & reaps zombie */
    g_assert_cmpint (kill ((pid_t)pid1, SIGKILL), ==, 0);

    gboolean still_alive = TRUE;
    for (guint i = 0; i < 100 && still_alive; i++) {
        g_usleep (5000);
        still_alive = puls_helper_client_watchdog_check (client);
    }
    g_assert_false (still_alive);

    puls_helper_client_get_stats (client, &st);
    g_assert_false (st.running);
    g_assert_cmpint ((gint)st.pid, ==, 0);
    g_assert_cmpuint (st.crash_count, ==, 1);
    g_assert_cmpuint (st.reaped_count, ==, 1);

    /* Verify pid1 is completely reaped (no zombie child lingering) */
    int wstatus = 0;
    errno = 0;
    g_assert_cmpint (waitpid ((pid_t)pid1, &wstatus, WNOHANG), ==, -1);
    g_assert_cmpint (errno, ==, ECHILD);

    /* Next query cleanly re-launches helper with a new PID */
    g_assert_true (puls_helper_client_exchange (client, "HELLO v2\n",
                                                &ok, &err_code, &payload, &err));
    g_assert_no_error (err);
    g_assert_true (ok);
    g_assert_nonnull (strstr (payload, "\"protocol\":\"v2\""));
    g_clear_pointer (&err_code, g_free);
    g_clear_pointer (&payload, g_free);

    puls_helper_client_get_stats (client, &st);
    g_assert_true (st.running);
    g_assert_cmpint ((gint)st.pid, >, 0);
    g_assert_cmpint ((gint)st.pid, !=, (gint)pid1);
    g_assert_cmpuint (st.launch_count, ==, 2);
    GPid pid2 = st.pid;

    /* 3. Kill helper directly before next query (NO manual watchdog_check call) */
    g_assert_cmpint (kill ((pid_t)pid2, SIGKILL), ==, 0);
    g_usleep (20000);

    /* Query transparently recovers, reaps pid2, avoids SIGPIPE, and succeeds */
    g_assert_true (puls_helper_client_exchange (client, "HELLO v2\n",
                                                &ok, &err_code, &payload, &err));
    g_assert_no_error (err);
    g_assert_true (ok);
    g_assert_nonnull (strstr (payload, "\"version\":2"));
    g_clear_pointer (&err_code, g_free);
    g_clear_pointer (&payload, g_free);

    puls_helper_client_get_stats (client, &st);
    g_assert_true (st.running);
    g_assert_cmpint ((gint)st.pid, !=, (gint)pid2);
    g_assert_cmpuint (st.launch_count, ==, 3);
    g_assert_cmpuint (st.crash_count, ==, 2);
    g_assert_cmpuint (st.reaped_count, ==, 2);
    GPid pid3 = st.pid;

    errno = 0;
    g_assert_cmpint (waitpid ((pid_t)pid2, &wstatus, WNOHANG), ==, -1);
    g_assert_cmpint (errno, ==, ECHILD);

    /* 4. Periodic GLib watchdog timer automatically reaps SIGTERM'd helper while idle */
    puls_helper_client_watchdog_start (client, 25);
    g_assert_cmpint (kill ((pid_t)pid3, SIGTERM), ==, 0);

    gint64 deadline = g_get_monotonic_time () + 2 * G_TIME_SPAN_SECOND;
    while (g_get_monotonic_time () < deadline) {
        puls_helper_client_get_stats (client, &st);
        if (!st.running && st.reaped_count >= 3)
            break;
        g_main_context_iteration (NULL, FALSE);
        g_usleep (5000);
    }
    puls_helper_client_watchdog_stop (client);

    puls_helper_client_get_stats (client, &st);
    g_assert_false (st.running);
    g_assert_cmpuint (st.crash_count, ==, 3);
    g_assert_cmpuint (st.reaped_count, ==, 3);

    errno = 0;
    g_assert_cmpint (waitpid ((pid_t)pid3, &wstatus, WNOHANG), ==, -1);
    g_assert_cmpint (errno, ==, ECHILD);

    /* Next query re-launches cleanly after periodic watchdog reap */
    g_assert_true (puls_helper_client_exchange (client, "ACTION /dev/sda bogus\n",
                                                &ok, &err_code, &payload, &err));
    g_assert_no_error (err);
    g_assert_false (ok);
    g_assert_cmpstr (err_code, ==, PULS_HELPER_ERR_INVALID_ACTION);
    g_clear_pointer (&err_code, g_free);
    g_clear_pointer (&payload, g_free);

    puls_helper_client_get_stats (client, &st);
    g_assert_true (st.running);
    g_assert_cmpuint (st.launch_count, ==, 4);
    GPid pid4 = st.pid;

    /* 5. Graceful shutdown sends QUIT and reaps without incrementing crash_count */
    puls_helper_client_shutdown (client);

    puls_helper_client_get_stats (client, &st);
    g_assert_false (st.running);
    g_assert_cmpuint (st.crash_count, ==, 3);
    g_assert_cmpuint (st.reaped_count, ==, 4);

    errno = 0;
    g_assert_cmpint (waitpid ((pid_t)pid4, &wstatus, WNOHANG), ==, -1);
    g_assert_cmpint (errno, ==, ECHILD);

    puls_helper_client_free (client);
#endif
}

static void
test_elevation_fallback_mechanisms (void)
{
    /* 1. Mode labels */
    g_assert_cmpstr (puls_elevation_mode_to_string (PULS_ELEVATION_NONE),         ==, "none");
    g_assert_cmpstr (puls_elevation_mode_to_string (PULS_ELEVATION_DIRECT_ROOT),  ==, "direct-root");
    g_assert_cmpstr (puls_elevation_mode_to_string (PULS_ELEVATION_DIRECT_GROUP), ==, "direct-group");
    g_assert_cmpstr (puls_elevation_mode_to_string (PULS_ELEVATION_PKEXEC),       ==, "pkexec");
    g_assert_cmpstr (puls_elevation_mode_to_string (PULS_ELEVATION_SUDO),         ==, "sudo");

    /* 2. Storage group access detector ('disk' / 'storage') */
    const gchar *groups_disk[]    = { "users", "wheel", "disk", NULL };
    const gchar *groups_storage[] = { "audio", "storage", "video", NULL };
    const gchar *groups_none[]    = { "users", "sudo", "disks", "storages", "adm", NULL };
    const gchar *groups_empty[]   = { NULL };

    g_assert_true  (puls_group_list_has_storage_access (groups_disk));
    g_assert_true  (puls_group_list_has_storage_access (groups_storage));
    g_assert_false (puls_group_list_has_storage_access (groups_none));
    g_assert_false (puls_group_list_has_storage_access (groups_empty));
    g_assert_false (puls_group_list_has_storage_access (NULL));

    /* 3. Deterministic elevation decision matrix across environments */
    /* 3a. Root user always uses direct-root */
    PulsElevationCaps caps_root = {
        .is_root = TRUE, .in_storage_group = TRUE,
        .polkit_available = TRUE, .sudo_available = TRUE
    };
    g_assert_cmpint (puls_resolve_elevation_mode (&caps_root), ==, PULS_ELEVATION_DIRECT_ROOT);

    /* 3b. User in 'disk' or 'storage' group bypasses Polkit & sudo completely */
    PulsElevationCaps caps_group = {
        .is_root = FALSE, .in_storage_group = TRUE,
        .polkit_available = TRUE, .sudo_available = TRUE
    };
    g_assert_cmpint (puls_resolve_elevation_mode (&caps_group), ==, PULS_ELEVATION_DIRECT_GROUP);

    /* 3c. User in 'disk'/'storage' group inside headless container/WSL2 */
    PulsElevationCaps caps_group_headless = {
        .is_root = FALSE, .in_storage_group = TRUE,
        .polkit_available = FALSE, .sudo_available = TRUE,
        .is_wsl = TRUE, .is_container = TRUE
    };
    g_assert_cmpint (puls_resolve_elevation_mode (&caps_group_headless), ==, PULS_ELEVATION_DIRECT_GROUP);

    /* 3d. Desktop Linux with Polkit available */
    PulsElevationCaps caps_desktop = {
        .is_root = FALSE, .in_storage_group = FALSE,
        .polkit_available = TRUE, .sudo_available = TRUE
    };
    g_assert_cmpint (puls_resolve_elevation_mode (&caps_desktop), ==, PULS_ELEVATION_PKEXEC);

    /* 3e. Non-desktop Linux / headless server / container without Polkit -> sudo fallback */
    PulsElevationCaps caps_headless_sudo = {
        .is_root = FALSE, .in_storage_group = FALSE,
        .polkit_available = FALSE, .sudo_available = TRUE,
        .is_container = TRUE
    };
    g_assert_cmpint (puls_resolve_elevation_mode (&caps_headless_sudo), ==, PULS_ELEVATION_SUDO);

    /* 3f. WSL2 forces sudo fallback even if polkit_available were spuriously set */
    PulsElevationCaps caps_wsl = {
        .is_root = FALSE, .in_storage_group = FALSE,
        .polkit_available = TRUE, .sudo_available = TRUE,
        .is_wsl = TRUE
    };
    g_assert_cmpint (puls_resolve_elevation_mode (&caps_wsl), ==, PULS_ELEVATION_SUDO);

    /* 3g. Fully unprivileged sandbox without Polkit, sudo, or storage group -> none */
    PulsElevationCaps caps_sandbox = {
        .is_root = FALSE, .in_storage_group = FALSE,
        .polkit_available = FALSE, .sudo_available = FALSE,
        .is_container = TRUE
    };
    g_assert_cmpint (puls_resolve_elevation_mode (&caps_sandbox), ==, PULS_ELEVATION_NONE);

    /* 4. Helper spawn argv construction per elevation mode */
    g_auto(GStrv) argv_pk = puls_helper_build_spawn_argv (PULS_ELEVATION_PKEXEC, "/usr/libexec/puls-diskinfo-helper");
    g_assert_cmpuint (g_strv_length (argv_pk), ==, 2);
    g_assert_cmpstr (argv_pk[0], ==, "pkexec");
    g_assert_cmpstr (argv_pk[1], ==, "/usr/libexec/puls-diskinfo-helper");

    g_auto(GStrv) argv_sudo = puls_helper_build_spawn_argv (PULS_ELEVATION_SUDO, "/usr/libexec/puls-diskinfo-helper");
    g_assert_cmpuint (g_strv_length (argv_sudo), ==, 3);
    g_assert_cmpstr (argv_sudo[0], ==, "sudo");
    g_assert_cmpstr (argv_sudo[1], ==, "-n");
    g_assert_cmpstr (argv_sudo[2], ==, "/usr/libexec/puls-diskinfo-helper");

    g_auto(GStrv) argv_grp = puls_helper_build_spawn_argv (PULS_ELEVATION_DIRECT_GROUP, "/usr/libexec/puls-diskinfo-helper");
    g_assert_cmpuint (g_strv_length (argv_grp), ==, 1);
    g_assert_cmpstr (argv_grp[0], ==, "/usr/libexec/puls-diskinfo-helper");

    g_auto(GStrv) argv_root = puls_helper_build_spawn_argv (PULS_ELEVATION_DIRECT_ROOT, "/usr/libexec/puls-diskinfo-helper");
    g_assert_cmpuint (g_strv_length (argv_root), ==, 1);
    g_assert_cmpstr (argv_root[0], ==, "/usr/libexec/puls-diskinfo-helper");

    /* 5. Live host capability probe runs cleanly */
    PulsElevationCaps live_caps;
    puls_detect_elevation_caps (&live_caps);
    PulsElevationMode live_mode = puls_resolve_elevation_mode (&live_caps);
    g_assert_nonnull (puls_elevation_mode_to_string (live_mode));
    g_assert_cmpint (puls_get_elevation_mode (), ==, live_mode);

#ifdef TEST_HELPER_BIN
    /* 6. Live group-access fallback query succeeds directly without pkexec/sudo */
    g_autoptr(GError) err = NULL;
    PulsHelperClient *grp_client = puls_helper_client_new_with_mode (
        TEST_HELPER_BIN, PULS_ELEVATION_DIRECT_GROUP);
    g_assert_nonnull (grp_client);

    gboolean ok = FALSE;
    g_autofree gchar *err_code = NULL;
    g_autofree gchar *payload = NULL;
    g_assert_true (puls_helper_client_exchange (grp_client, "HELLO v2\n",
                                                &ok, &err_code, &payload, &err));
    g_assert_no_error (err);
    g_assert_true (ok);
    g_assert_nonnull (strstr (payload, "\"protocol\":\"v2\""));
    g_clear_pointer (&err_code, g_free);
    g_clear_pointer (&payload, g_free);

    PulsHelperStats st = { 0 };
    puls_helper_client_get_stats (grp_client, &st);
    g_assert_true (st.running);
    g_assert_cmpint (st.elevation_mode, ==, PULS_ELEVATION_DIRECT_GROUP);
    g_assert_cmpuint (st.launch_count, ==, 1);

    /* Query a whitelisted command via the direct-group helper */
    g_assert_true (puls_helper_client_exchange (grp_client, "READ_SMART /dev/sdzz\n",
                                                &ok, &err_code, &payload, &err));
    g_assert_no_error (err);
    g_assert_false (ok);
    g_assert_cmpstr (err_code, ==, PULS_HELPER_ERR_NOT_FOUND);
    g_clear_pointer (&err_code, g_free);
    g_clear_pointer (&payload, g_free);

    puls_helper_client_free (grp_client);
#endif
}

typedef struct {
    guint             direct_calls;
    guint             helper_calls;
    gboolean          direct_perm_denied;
    PulsHelperClient *helper_client;
} ZeroElevTestCtx;

static gchar *
zero_elev_mock_direct_fn (const gchar  *device_path,
                          gboolean      check_standby G_GNUC_UNUSED,
                          gpointer      user_data,
                          GError      **error G_GNUC_UNUSED)
{
    ZeroElevTestCtx *ctx = (ZeroElevTestCtx *)user_data;
    ctx->direct_calls++;

    if (ctx->direct_perm_denied) {
        return g_strdup ("{\"smartctl\":{\"messages\":[{\"string\":\"Smartctl open device: failed: Permission denied\"}]}}\n");
    }

    return g_strdup_printf ("{\"source\":\"direct\",\"device\":\"%s\",\"smart_status\":{\"passed\":true}}\n",
                            device_path);
}

static gchar *
zero_elev_mock_helper_fn (const gchar  *device_path G_GNUC_UNUSED,
                          gboolean      check_standby G_GNUC_UNUSED,
                          gpointer      user_data,
                          GError      **error)
{
    ZeroElevTestCtx *ctx = (ZeroElevTestCtx *)user_data;
    ctx->helper_calls++;

    if (ctx->helper_client != NULL) {
        gboolean ok = FALSE;
        g_autofree gchar *err_code = NULL;
        gchar *payload = NULL;
        if (puls_helper_client_exchange (ctx->helper_client, "HELLO v2\n",
                                         &ok, &err_code, &payload, error) && ok) {
            return payload;
        }
        g_free (payload);
        return NULL;
    }

    return g_strdup ("{\"source\":\"helper\",\"smart_status\":{\"passed\":true}}\n");
}

static void
test_zero_elevation_mode (void)
{
    g_autoptr(GError) err = NULL;

    /* 1. Permission-denied output classifier */
    g_assert_true  (puls_smartctl_output_is_permission_denied (NULL));
    g_assert_true  (puls_smartctl_output_is_permission_denied (""));
    g_assert_true  (puls_smartctl_output_is_permission_denied ("open device: /dev/nvme0 failed: Permission denied"));
    g_assert_true  (puls_smartctl_output_is_permission_denied ("NVMe admin ioctl failed: Operation not permitted"));
    g_assert_true  (puls_smartctl_output_is_permission_denied ("error: EACCES"));
    g_assert_false (puls_smartctl_output_is_permission_denied ("{\"json_format_version\":[1,0],\"smart_status\":{\"passed\":true}}"));

    /* 2. Device-path security guard preserved on puls_device_supports_zero_elevation */
    g_assert_false (puls_device_supports_zero_elevation (NULL));
    g_assert_false (puls_device_supports_zero_elevation (""));
    g_assert_false (puls_device_supports_zero_elevation ("/dev/null"));
    g_assert_false (puls_device_supports_zero_elevation ("/dev/zero"));
    g_assert_false (puls_device_supports_zero_elevation ("/dev/../etc/passwd"));
    g_assert_false (puls_device_supports_zero_elevation ("/dev/sda;reboot"));

    /* 3. Real POSIX permission probing via puls_node_has_direct_rw_access() */
    g_autofree gchar *tmp_dir = g_dir_make_tmp ("puls_test_zero_elev_XXXXXX", &err);
    g_assert_no_error (err);
    g_assert_nonnull (tmp_dir);

    g_autofree gchar *rw_node = g_build_filename (tmp_dir, "sda", NULL);
    g_assert_true (g_file_set_contents (rw_node, "mock_dev", -1, &err));
    g_assert_no_error (err);

    /* 3a. Group/owner readable & writable (0660) -> direct access allowed */
    g_assert_cmpint (chmod (rw_node, 0660), ==, 0);
    g_assert_true (puls_node_has_direct_rw_access (rw_node));

    /* 3b. Read-only (0444) -> rejected (smartctl ioctls require W_OK) */
    g_assert_cmpint (chmod (rw_node, 0444), ==, 0);
    g_assert_false (puls_node_has_direct_rw_access (rw_node));

    /* 3c. Write-only (0200) -> rejected (missing R_OK) */
    g_assert_cmpint (chmod (rw_node, 0200), ==, 0);
    g_assert_false (puls_node_has_direct_rw_access (rw_node));

    /* 3d. No permissions (0000) -> rejected */
    g_assert_cmpint (chmod (rw_node, 0000), ==, 0);
    g_assert_false (puls_node_has_direct_rw_access (rw_node));

    /* 3e. Symlink to 0660 node -> rejected (anti-hijacking) */
    g_assert_cmpint (chmod (rw_node, 0660), ==, 0);
    g_autofree gchar *sym_node = g_build_filename (tmp_dir, "sdb", NULL);
    g_assert_cmpint (symlink (rw_node, sym_node), ==, 0);
    g_assert_false (puls_node_has_direct_rw_access (sym_node));
    g_assert_false (puls_node_has_direct_rw_access (NULL));
    g_assert_false (puls_node_has_direct_rw_access (""));

    /* 4. End-to-end Zero-Elevation orchestration (verifying helper is NOT invoked) */
    PulsHelperClient *real_helper = NULL;
#ifdef TEST_HELPER_BIN
    real_helper = puls_helper_client_new_with_mode (TEST_HELPER_BIN, PULS_ELEVATION_DIRECT_GROUP);
    g_assert_nonnull (real_helper);
#endif

    /* Case A: Node has R_OK | W_OK -> runs directly, helper is NEVER launched or invoked */
    g_assert_cmpint (chmod (rw_node, 0660), ==, 0);
    ZeroElevTestCtx ctx = { .helper_client = real_helper };
    gboolean used_direct = FALSE;
    g_autofree gchar *out_direct = puls_execute_smartctl_with_zero_elevation (
        rw_node,
        FALSE,
        puls_node_has_direct_rw_access (rw_node),
        zero_elev_mock_direct_fn,
        zero_elev_mock_helper_fn,
        &ctx,
        &used_direct,
        &err);
    g_assert_no_error (err);
    g_assert_nonnull (out_direct);
    g_assert_true (used_direct);
    g_assert_nonnull (strstr (out_direct, "\"source\":\"direct\""));
    g_assert_cmpuint (ctx.direct_calls, ==, 1);
    g_assert_cmpuint (ctx.helper_calls, ==, 0);

    if (real_helper != NULL) {
        PulsHelperStats st = { 0 };
        puls_helper_client_get_stats (real_helper, &st);
        g_assert_false (st.running);
        g_assert_cmpuint (st.launch_count, ==, 0);
    }

    /* Case B: Node lacks W_OK (0444) -> bypasses direct, invokes helper cleanly */
    g_assert_cmpint (chmod (rw_node, 0444), ==, 0);
    ctx.direct_calls = 0;
    ctx.helper_calls = 0;
    ctx.direct_perm_denied = FALSE;
    used_direct = TRUE;

    g_autofree gchar *out_helper = puls_execute_smartctl_with_zero_elevation (
        rw_node,
        FALSE,
        puls_node_has_direct_rw_access (rw_node),
        zero_elev_mock_direct_fn,
        zero_elev_mock_helper_fn,
        &ctx,
        &used_direct,
        &err);
    g_assert_no_error (err);
    g_assert_nonnull (out_helper);
    g_assert_false (used_direct);
    g_assert_cmpuint (ctx.direct_calls, ==, 0);
    g_assert_cmpuint (ctx.helper_calls, ==, 1);

    if (real_helper != NULL) {
        PulsHelperStats st = { 0 };
        puls_helper_client_get_stats (real_helper, &st);
        g_assert_true (st.running);
        g_assert_cmpuint (st.launch_count, ==, 1);
    }

    /* Case C: Node has 0660 RW, but kernel ioctl returns Permission denied -> falls back to helper */
    g_assert_cmpint (chmod (rw_node, 0660), ==, 0);
    ctx.direct_calls = 0;
    ctx.helper_calls = 0;
    ctx.direct_perm_denied = TRUE;
    used_direct = TRUE;

    g_autofree gchar *out_fb = puls_execute_smartctl_with_zero_elevation (
        rw_node,
        TRUE,
        puls_node_has_direct_rw_access (rw_node),
        zero_elev_mock_direct_fn,
        zero_elev_mock_helper_fn,
        &ctx,
        &used_direct,
        &err);
    g_assert_no_error (err);
    g_assert_nonnull (out_fb);
    g_assert_false (used_direct);
    g_assert_cmpuint (ctx.direct_calls, ==, 1);
    g_assert_cmpuint (ctx.helper_calls, ==, 1);

    if (real_helper != NULL)
        puls_helper_client_free (real_helper);

    chmod (rw_node, 0600);
    unlink (sym_node);
    unlink (rw_node);
    rmdir (tmp_dir);
}

typedef struct {
    guint   chunks_received;
    guint64 last_seq;
    guint64 last_bytes_read;
    gdouble last_progress;
    guint   cancel_after;
} StreamTestCtx;

static gboolean
stream_test_chunk_cb (guint64      seq,
                      const gchar *chunk_payload,
                      gpointer     user_data)
{
    StreamTestCtx *ctx = (StreamTestCtx *)user_data;

    PulsHelperStreamChunk parsed = { 0 };
    g_assert_true (puls_helper_parse_stream_chunk_json (chunk_payload, &parsed));
    g_assert_cmpuint (parsed.seq, ==, seq);
    g_assert_cmpuint (parsed.seq, ==, ctx->last_seq + 1);
    g_assert_cmpuint (parsed.bytes_read, >, ctx->last_bytes_read);
    g_assert_cmpfloat (parsed.progress_pct, >=, ctx->last_progress);
    g_assert_cmpfloat (parsed.mb_per_sec, >=, 0.0);

    ctx->chunks_received++;
    ctx->last_seq = parsed.seq;
    ctx->last_bytes_read = parsed.bytes_read;
    ctx->last_progress = parsed.progress_pct;

    if (ctx->cancel_after > 0 && ctx->chunks_received >= ctx->cancel_after)
        return FALSE; /* Trigger bidirectional mid-stream CANCEL */

    return TRUE;
}

static void
test_bidirectional_ipc_streaming (void)
{
    /* 1. Chunked command formatting & CHUNK frame header parser */
    g_autofree gchar *fmt = puls_helper_format_stream_scan_chunked ("/dev/nvme0n1", 0, 262144, 16384);
    g_assert_cmpstr (fmt, ==, "STREAM_SCAN /dev/nvme0n1 0 262144 16384\n");

    guint64 seq = 0;
    gsize plen = 0;
    g_assert_true  (puls_helper_parse_chunk_header ("CHUNK 1 128", &seq, &plen));
    g_assert_cmpuint (seq, ==, 1);
    g_assert_cmpuint (plen, ==, 128);
    g_assert_true  (puls_helper_parse_chunk_header ("CHUNK 42 0", &seq, &plen));
    g_assert_cmpuint (seq, ==, 42);
    g_assert_cmpuint (plen, ==, 0);

    g_assert_false (puls_helper_parse_chunk_header ("CHUNK 0 128", &seq, &plen));
    g_assert_false (puls_helper_parse_chunk_header ("CHUNK -1 128", &seq, &plen));
    g_assert_false (puls_helper_parse_chunk_header ("CHUNK 1 -5", &seq, &plen));
    g_assert_false (puls_helper_parse_chunk_header ("CHUNK 1 999999999", &seq, &plen));
    g_assert_false (puls_helper_parse_chunk_header ("OK 128", &seq, &plen));
    g_assert_false (puls_helper_parse_chunk_header ("ERR E_IO_ERROR 10", &seq, &plen));
    g_assert_false (puls_helper_parse_chunk_header ("", &seq, &plen));
    g_assert_false (puls_helper_parse_chunk_header (NULL, &seq, &plen));

    /* 2. Real bidirectional UNIX pipe streaming test (full completion + mid-stream CANCEL) */
    g_autoptr(GError) err = NULL;
    g_autofree gchar *tmp_dir = g_dir_make_tmp ("puls_test_stream_XXXXXX", &err);
    g_assert_no_error (err);
    g_autofree gchar *data_file = g_build_filename (tmp_dir, "disk.img", NULL);

    g_autofree gchar *zeros = g_malloc0 (16384);
    memset (zeros, 0x5A, 16384);
    g_assert_true (g_file_set_contents (data_file, zeros, 16384, &err));
    g_assert_no_error (err);

    /* 2a. Complete 4-chunk telemetry stream over live pipes */
    int c2h[2] = { -1, -1 };
    int h2c[2] = { -1, -1 };
    g_assert_cmpint (pipe (c2h), ==, 0);
    g_assert_cmpint (pipe (h2c), ==, 0);

    pid_t pid = fork ();
    g_assert_cmpint (pid, >=, 0);
    if (pid == 0) {
        close (c2h[1]);
        close (h2c[0]);

        /* Read incoming command line from client before streaming */
        char cmd_buf[128];
        size_t pos = 0;
        while (pos + 1 < sizeof (cmd_buf)) {
            char ch = '\0';
            if (read (c2h[0], &ch, 1) <= 0 || ch == '\n')
                break;
            cmd_buf[pos++] = ch;
        }

        int data_fd = open (data_file, O_RDONLY);
        FILE *out_fp = fdopen (h2c[1], "w");
        if (data_fd >= 0 && out_fp != NULL) {
            puls_helper_stream_read_fd (data_fd, c2h[0], out_fp,
                                        "/dev/sda", 0, 16384, 4096, 0);
            fclose (out_fp);
            close (data_fd);
        }
        close (c2h[0]);
        _exit (0);
    }

    close (c2h[0]);
    close (h2c[1]);

    g_autoptr(GOutputStream) p_in = g_unix_output_stream_new (c2h[1], TRUE);
    g_autoptr(GInputStream) raw_out = g_unix_input_stream_new (h2c[0], TRUE);
    g_autoptr(GDataInputStream) p_out = g_data_input_stream_new (raw_out);

    StreamTestCtx sctx = { 0 };
    gboolean ok = FALSE;
    g_autofree gchar *err_code = NULL;
    g_autofree gchar *final_payload = NULL;

    g_assert_true (puls_helper_exchange_stream (p_in, p_out,
                                                "STREAM_SCAN /dev/sda 0 16384 4096\n",
                                                stream_test_chunk_cb, &sctx,
                                                &ok, &err_code, &final_payload, &err));
    g_assert_no_error (err);
    g_assert_true (ok);
    g_assert_null (err_code);
    g_assert_cmpuint (sctx.chunks_received, ==, 4);
    g_assert_cmpuint (sctx.last_seq, ==, 4);
    g_assert_cmpuint (sctx.last_bytes_read, ==, 16384);
    g_assert_cmpfloat_with_epsilon (sctx.last_progress, 100.0, 0.01);
    g_assert_nonnull (strstr (final_payload, "\"status\":\"ok\""));
    g_assert_nonnull (strstr (final_payload, "\"chunks\":4"));
    g_assert_nonnull (strstr (final_payload, "\"bytes_read\":16384"));
    g_clear_pointer (&final_payload, g_free);

    int wstatus = 0;
    g_assert_cmpint (waitpid (pid, &wstatus, 0), ==, pid);

    /* 2b. Bidirectional mid-stream CANCEL (client cancels on chunk 2 of 8) */
    g_assert_cmpint (pipe (c2h), ==, 0);
    g_assert_cmpint (pipe (h2c), ==, 0);

    pid_t pid2 = fork ();
    g_assert_cmpint (pid2, >=, 0);
    if (pid2 == 0) {
        close (c2h[1]);
        close (h2c[0]);

        char cmd_buf[128];
        size_t pos = 0;
        while (pos + 1 < sizeof (cmd_buf)) {
            char ch = '\0';
            if (read (c2h[0], &ch, 1) <= 0 || ch == '\n')
                break;
            cmd_buf[pos++] = ch;
        }

        int data_fd = open (data_file, O_RDONLY);
        FILE *out_fp = fdopen (h2c[1], "w");
        if (data_fd >= 0 && out_fp != NULL) {
            /* 8 chunks of 2048 B with 15 ms pacing so CANCEL arrives cleanly mid-stream */
            puls_helper_stream_read_fd (data_fd, c2h[0], out_fp,
                                        "/dev/sda", 0, 16384, 2048, 15000);
            fclose (out_fp);
            close (data_fd);
        }
        close (c2h[0]);
        _exit (0);
    }

    close (c2h[0]);
    close (h2c[1]);

    g_autoptr(GOutputStream) p2_in = g_unix_output_stream_new (c2h[1], TRUE);
    g_autoptr(GInputStream) raw2_out = g_unix_input_stream_new (h2c[0], TRUE);
    g_autoptr(GDataInputStream) p2_out = g_data_input_stream_new (raw2_out);

    StreamTestCtx cancel_ctx = { .cancel_after = 2 };
    ok = TRUE;
    g_assert_true (puls_helper_exchange_stream (p2_in, p2_out,
                                                "STREAM_SCAN /dev/sda 0 16384 2048\n",
                                                stream_test_chunk_cb, &cancel_ctx,
                                                &ok, &err_code, &final_payload, &err));
    g_assert_no_error (err);
    g_assert_false (ok);
    g_assert_cmpstr (err_code, ==, PULS_HELPER_ERR_CANCELLED);
    g_assert_cmpuint (cancel_ctx.chunks_received, ==, 2);
    g_assert_nonnull (strstr (final_payload, "\"status\":\"cancelled\""));
    g_clear_pointer (&err_code, g_free);
    g_clear_pointer (&final_payload, g_free);

    g_assert_cmpint (waitpid (pid2, &wstatus, 0), ==, pid2);

    unlink (data_file);
    rmdir (tmp_dir);

#ifdef TEST_HELPER_BIN
    /* 3. Live compiled helper streaming capability advertisement, CANCEL, and chunk bounds */
    PulsHelperClient *client = puls_helper_client_new_with_mode (
        TEST_HELPER_BIN, PULS_ELEVATION_DIRECT_GROUP);
    g_assert_nonnull (client);

    g_assert_true (puls_helper_client_exchange (client, "HELLO v2\n",
                                                &ok, &err_code, &final_payload, &err));
    g_assert_no_error (err);
    g_assert_true (ok);
    g_assert_nonnull (strstr (final_payload, "\"streaming\":true"));
    g_assert_nonnull (strstr (final_payload, "\"chunk_frame\":\"CHUNK\""));
    g_clear_pointer (&final_payload, g_free);

    /* Idle CANCEL is idempotent and keeps connection synchronized */
    g_assert_true (puls_helper_client_exchange (client, "CANCEL\n",
                                                &ok, &err_code, &final_payload, &err));
    g_assert_no_error (err);
    g_assert_true (ok);
    g_clear_pointer (&final_payload, g_free);

    /* Invalid CANCEL arity & invalid chunk bounds rejected with E_INVALID_ARG */
    g_assert_true (puls_helper_client_exchange (client, "CANCEL extra\n",
                                                &ok, &err_code, &final_payload, &err));
    g_assert_no_error (err);
    g_assert_false (ok);
    g_assert_cmpstr (err_code, ==, PULS_HELPER_ERR_INVALID_ARG);
    g_clear_pointer (&err_code, g_free);
    g_clear_pointer (&final_payload, g_free);

    g_assert_true (puls_helper_client_stream (client, "STREAM_SCAN /dev/sda 0 65536 256\n",
                                              NULL, NULL,
                                              &ok, &err_code, &final_payload, &err));
    g_assert_no_error (err);
    g_assert_false (ok);
    g_assert_cmpstr (err_code, ==, PULS_HELPER_ERR_INVALID_ARG);
    g_clear_pointer (&err_code, g_free);
    g_clear_pointer (&final_payload, g_free);

    g_assert_true (puls_helper_client_stream (client, "STREAM_SCAN /dev/sda 0 65536 2097152\n",
                                              NULL, NULL,
                                              &ok, &err_code, &final_payload, &err));
    g_assert_no_error (err);
    g_assert_false (ok);
    g_assert_cmpstr (err_code, ==, PULS_HELPER_ERR_INVALID_ARG);
    g_clear_pointer (&err_code, g_free);
    g_clear_pointer (&final_payload, g_free);

    /* Non-existent device cleanly returns E_DEVICE_NOT_FOUND over streaming client API */
    g_assert_true (puls_helper_client_stream (client, "STREAM_SCAN /dev/sdzz 0 65536 4096\n",
                                              NULL, NULL,
                                              &ok, &err_code, &final_payload, &err));
    g_assert_no_error (err);
    g_assert_false (ok);
    g_assert_cmpstr (err_code, ==, PULS_HELPER_ERR_NOT_FOUND);
    g_clear_pointer (&err_code, g_free);
    g_clear_pointer (&final_payload, g_free);

    puls_helper_client_free (client);
#endif
}

static void
test_phase4_final_integration (void)
{
    g_autoptr(GError) err = NULL;

    /* 1. Polkit policy XML & localized action annotations verified */
#ifdef TEST_POLICY_FILE
    g_autoptr(GPtrArray) policy_actions = NULL;
    g_assert_true (puls_polkit_validate_policy_file (TEST_POLICY_FILE, &policy_actions, &err));
    g_assert_no_error (err);
    g_assert_cmpuint (policy_actions->len, ==, 3);
#endif

    /* 2. Default helper stats & elevation pipeline introspection */
    PulsHelperStats def_stats = { 0 };
    puls_helper_get_stats (&def_stats);
    g_assert_nonnull (puls_elevation_mode_to_string (def_stats.elevation_mode));

    /* 3. PulsUtils STREAM_SCAN entry guards enforce path & range sanitization */
    g_assert_false (puls_run_stream_scan_sync ("/dev/../etc/passwd", 0, 4096, 512,
                                               NULL, NULL, NULL, &err));
    g_assert_error (err, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error (&err);

    g_assert_false (puls_run_stream_scan_sync ("/dev/sda;reboot", 0, 4096, 512,
                                               NULL, NULL, NULL, &err));
    g_assert_error (err, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error (&err);

    g_assert_false (puls_run_stream_scan_sync ("/dev/sda", 0, 0, 512,
                                               NULL, NULL, NULL, &err));
    g_assert_error (err, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error (&err);

    g_assert_false (puls_run_stream_scan_sync ("/dev/sda", 0, 4096, 128,
                                               NULL, NULL, NULL, &err));
    g_assert_error (err, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error (&err);

#ifdef TEST_HELPER_BIN
    /* 4. Full End-to-End Phase 4 Pipeline against live compiled helper */
    PulsHelperClient *client = puls_helper_client_new_with_mode (
        TEST_HELPER_BIN, PULS_ELEVATION_DIRECT_GROUP);
    g_assert_nonnull (client);

    gboolean ok = FALSE;
    g_autofree gchar *err_code = NULL;
    g_autofree gchar *payload = NULL;

    /* 4a. Protocol v2 handshake & capability verification */
    g_assert_true (puls_helper_client_exchange (client, "HELLO v2\n",
                                                &ok, &err_code, &payload, &err));
    g_assert_no_error (err);
    g_assert_true (ok);
    g_assert_nonnull (strstr (payload, "\"protocol\":\"v2\""));
    g_assert_nonnull (strstr (payload, "\"streaming\":true"));
    g_assert_nonnull (strstr (payload, "\"actions\":[\"short\",\"long\",\"abort\"]"));
    g_clear_pointer (&payload, g_free);

    /* 4b. Anti-injection & command whitelist verification on same live session */
    g_assert_true (puls_helper_client_exchange (client, "READ_SMART /dev/../etc/passwd\n",
                                                &ok, &err_code, &payload, &err));
    g_assert_no_error (err);
    g_assert_false (ok);
    g_assert_cmpstr (err_code, ==, PULS_HELPER_ERR_INVALID_DEVICE);
    g_clear_pointer (&err_code, g_free);
    g_clear_pointer (&payload, g_free);

    g_assert_true (puls_helper_client_exchange (client, "ACTION /dev/sda standby\n",
                                                &ok, &err_code, &payload, &err));
    g_assert_no_error (err);
    g_assert_false (ok);
    g_assert_cmpstr (err_code, ==, PULS_HELPER_ERR_INVALID_ACTION);
    g_clear_pointer (&err_code, g_free);
    g_clear_pointer (&payload, g_free);

    g_assert_true (puls_helper_client_exchange (client, "READ_SMART /dev/sda raw\n",
                                                &ok, &err_code, &payload, &err));
    g_assert_no_error (err);
    g_assert_false (ok);
    g_assert_cmpstr (err_code, ==, PULS_HELPER_ERR_INVALID_SUBCMD);
    g_clear_pointer (&err_code, g_free);
    g_clear_pointer (&payload, g_free);

    g_assert_true (puls_helper_client_exchange (client, "EXEC /bin/sh\n",
                                                &ok, &err_code, &payload, &err));
    g_assert_no_error (err);
    g_assert_false (ok);
    g_assert_cmpstr (err_code, ==, PULS_HELPER_ERR_UNKNOWN_CMD);
    g_clear_pointer (&err_code, g_free);
    g_clear_pointer (&payload, g_free);

    /* 4c. Streaming command & idle CANCEL synchronization */
    g_assert_true (puls_helper_client_stream (client, "STREAM_SCAN /dev/sdzz 0 8192 4096\n",
                                              NULL, NULL, &ok, &err_code, &payload, &err));
    g_assert_no_error (err);
    g_assert_false (ok);
    g_assert_cmpstr (err_code, ==, PULS_HELPER_ERR_NOT_FOUND);
    g_clear_pointer (&err_code, g_free);
    g_clear_pointer (&payload, g_free);

    g_assert_true (puls_helper_client_exchange (client, "CANCEL\n",
                                                &ok, &err_code, &payload, &err));
    g_assert_no_error (err);
    g_assert_true (ok);
    g_clear_pointer (&payload, g_free);

    /* 4d. Crash recovery + zombie reaping + transparent re-launch */
    PulsHelperStats st = { 0 };
    puls_helper_client_get_stats (client, &st);
    GPid old_pid = st.pid;
    g_assert_cmpint ((gint)old_pid, >, 0);
    g_assert_cmpint (kill ((pid_t)old_pid, SIGKILL), ==, 0);
    g_usleep (20000);

    g_assert_true (puls_helper_client_exchange (client, "HELLO v2\n",
                                                &ok, &err_code, &payload, &err));
    g_assert_no_error (err);
    g_assert_true (ok);
    g_assert_nonnull (strstr (payload, "\"version\":2"));
    g_clear_pointer (&payload, g_free);

    puls_helper_client_get_stats (client, &st);
    g_assert_true (st.running);
    g_assert_cmpint ((gint)st.pid, !=, (gint)old_pid);
    g_assert_cmpuint (st.launch_count, ==, 2);
    g_assert_cmpuint (st.crash_count, ==, 1);
    g_assert_cmpuint (st.reaped_count, ==, 1);

    int wstatus = 0;
    errno = 0;
    g_assert_cmpint (waitpid ((pid_t)old_pid, &wstatus, WNOHANG), ==, -1);
    g_assert_cmpint (errno, ==, ECHILD);

    GPid final_pid = st.pid;
    puls_helper_client_free (client);

    errno = 0;
    g_assert_cmpint (waitpid ((pid_t)final_pid, &wstatus, WNOHANG), ==, -1);
    g_assert_cmpint (errno, ==, ECHILD);
#endif
}

int
main (int argc, char *argv[])
{
    gchar *tmp_config = g_dir_make_tmp ("puls_test_config_XXXXXX", NULL);
    if (tmp_config != NULL)
        g_setenv ("XDG_CONFIG_HOME", tmp_config, TRUE);

    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/basic/version", test_version);
    g_test_add_func ("/basic/format-utils", test_format_utils);
    g_test_add_func ("/basic/smart-data-model", test_smart_data_model);
    g_test_add_func ("/basic/mock-manager", test_mock_manager);
    g_test_add_func ("/basic/gresource-bundle", test_gresource_bundle);
    g_test_add_func ("/basic/structured-logging", test_structured_logging);
    g_test_add_func ("/basic/drive-types-and-transports", test_drive_types_and_transports);
    g_test_add_func ("/basic/extended-storage-metrics", test_extended_storage_metrics);
    g_test_add_func ("/basic/sysfs-helpers", test_sysfs_helpers);
    g_test_add_func ("/basic/hwmon-temperature", test_hwmon_temperature);
    g_test_add_func ("/basic/queue-attributes-trim", test_queue_attributes_trim);
    g_test_add_func ("/basic/pcie-link-inspector", test_pcie_link_inspector);
    g_test_add_func ("/basic/usb-speed-and-bridge", test_usb_speed_and_bridge_inspector);
    g_test_add_func ("/basic/partition-mount-mapper", test_partition_mount_mapper);
    g_test_add_func ("/basic/capacity-formatters", test_capacity_formatters);
    g_test_add_func ("/basic/phase2-final-integration", test_phase2_final_integration);
    g_test_add_func ("/basic/hotplug-monitoring", test_hotplug_monitoring);
    g_test_add_func ("/basic/nvme-topology-separation", test_nvme_topology_separation);
    g_test_add_func ("/basic/pseudo-device-filter", test_pseudo_device_filter);
    g_test_add_func ("/basic/scsi-generic-mapping", test_scsi_generic_mapping);
    g_test_add_func ("/basic/mdadm-raid-member-resolution", test_mdadm_raid_member_resolution);
    g_test_add_func ("/basic/zfs-pool-member-detection", test_zfs_pool_member_detection);
    g_test_add_func ("/basic/form-factor-detection-engine", test_form_factor_detection_engine);
    g_test_add_func ("/basic/standby-sleep-detection", test_standby_sleep_detection);
    g_test_add_func ("/basic/async-scan-cancellation", test_async_scan_cancellation);
    g_test_add_func ("/basic/phase3-final-integration", test_phase3_final_integration);
    g_test_add_func ("/basic/helper-protocol-handshake", test_helper_protocol_handshake);
    g_test_add_func ("/basic/path-sanitization-anti-injection", test_path_sanitization_anti_injection);
    g_test_add_func ("/basic/helper-command-whitelist", test_helper_command_whitelist);
    g_test_add_func ("/basic/polkit-policy-validation", test_polkit_policy_validation);
    g_test_add_func ("/basic/helper-crash-recovery-watchdog", test_helper_crash_recovery_watchdog);
    g_test_add_func ("/basic/elevation-fallback-mechanisms", test_elevation_fallback_mechanisms);
    g_test_add_func ("/basic/zero-elevation-mode", test_zero_elevation_mode);
    g_test_add_func ("/basic/bidirectional-ipc-streaming", test_bidirectional_ipc_streaming);
    g_test_add_func ("/basic/phase4-final-integration", test_phase4_final_integration);

    int result = g_test_run ();

    if (tmp_config != NULL) {
        g_autofree gchar *cfg_file = g_build_filename (tmp_config, "puls-diskinfo", "settings.json", NULL);
        unlink (cfg_file);
        g_autofree gchar *cfg_sub = g_build_filename (tmp_config, "puls-diskinfo", NULL);
        rmdir (cfg_sub);
        rmdir (tmp_config);
        g_free (tmp_config);
    }

    return result;
}
