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
#include "puls-settings.h"

#ifndef PULS_VERSION
#define PULS_VERSION "1.1.4"
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
