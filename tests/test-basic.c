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

            /* Also test with /dev/ path prefix */
            gint path_temp = puls_sysfs_read_hwmon_temp ("/dev/nvme0n1");
            g_assert_cmpint (path_temp, ==, live_temp);

            GList *live_sensors = puls_sysfs_read_hwmon_sensors ("nvme0n1");
            g_assert_nonnull (live_sensors);
            g_list_free_full (live_sensors, (GDestroyNotify)puls_thermal_sensor_free);
        }
    }
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
    g_test_add_func ("/basic/extended-storage-metrics", test_extended_storage_metrics);
    g_test_add_func ("/basic/sysfs-helpers", test_sysfs_helpers);
    g_test_add_func ("/basic/hwmon-temperature", test_hwmon_temperature);

    return g_test_run ();
}
