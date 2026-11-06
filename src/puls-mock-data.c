/*
 * puls-mock-data.c
 *
 * Mock storage device data simulator for testing and headless evaluation.
 *
 * Copyright (C) 2026 Barın Güzeldemirci
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "puls-mock-data.h"
#include <string.h>

static gboolean mock_mode_active = FALSE;

void
puls_set_mock_mode_enabled (gboolean enabled)
{
    mock_mode_active = enabled;
}

gboolean
puls_is_mock_mode_enabled (void)
{
    return mock_mode_active;
}

static void
add_attribute (PulsSmartData *data,
               guint8         id,
               const gchar   *name,
               gint           current,
               gint           worst,
               gint           thresh,
               guint64        raw)
{
    PulsSmartAttribute attr = { 0 };
    attr.id = id;
    attr.name = g_strdup (name);
    attr.current = current;
    attr.worst = worst;
    attr.threshold = thresh;
    attr.raw_value = raw;
    attr.raw_string = g_strdup_printf ("%" G_GUINT64_FORMAT, raw);
    attr.failing_now = (thresh > 0 && current <= thresh);
    attr.failed_past = FALSE;

    puls_smart_data_add_ata_attribute (data, &attr);
    g_free (attr.name);
    g_free (attr.raw_string);
}

static PulsSmartData *
create_mock_nvme_drive (void)
{
    PulsSmartData *d = puls_smart_data_new ();
    puls_smart_data_set_device_path (d, "/dev/nvme0n1");
    puls_smart_data_set_model_name (d, "Samsung SSD 980 PRO 1TB");
    puls_smart_data_set_serial_number (d, "S5GXNF0R123456K");
    puls_smart_data_set_firmware_version (d, "5B2QGXA7");
    puls_smart_data_set_interface_type (d, "NVMe");
    puls_smart_data_set_transfer_mode (d, "PCIe 4.0 x4 | PCIe 4.0 x4");
    puls_smart_data_set_standard (d, "NVM Express 1.4");
    puls_smart_data_set_form_factor (d, "M.2");
    puls_smart_data_set_capacity_bytes (d, 1000204886016ULL);
    puls_smart_data_set_drive_type (d, PULS_DRIVE_TYPE_NVME_SSD);
    puls_smart_data_set_transport_protocol (d, PULS_TRANSPORT_PCIE);
    puls_smart_data_set_health (d, PULS_HEALTH_GOOD);
    puls_smart_data_set_health_percent (d, 99);
    puls_smart_data_set_estimated_lifetime_days (d, 3650);
    puls_smart_data_set_temperature (d, 38);
    puls_smart_data_set_power_on_hours (d, 4210);
    puls_smart_data_set_power_cycle_count (d, 842);
    puls_smart_data_set_total_bytes_read (d, 12150000000000ULL);
    puls_smart_data_set_total_bytes_written (d, 15482000000000ULL);
    puls_smart_data_set_logical_sector_size (d, 512);
    puls_smart_data_set_physical_sector_size (d, 512);
    puls_smart_data_set_sector_format (d, PULS_SECTOR_FORMAT_512N);
    puls_smart_data_set_pcie_current_gen (d, PULS_PCIE_GEN_4);
    puls_smart_data_set_pcie_current_width (d, 4);
    puls_smart_data_set_pcie_max_gen (d, PULS_PCIE_GEN_4);
    puls_smart_data_set_pcie_max_width (d, 4);
    puls_smart_data_set_protocol_version (d, "NVMe 1.4");
    puls_smart_data_set_tbw_rating_bytes (d, 600000000000000ULL);
    puls_smart_data_set_wear_level_percent (d, 1);
    puls_smart_data_set_supports_trim (d, TRUE);
    puls_smart_data_set_supports_ncq (d, TRUE);

    PulsThermalSensor s1 = { (gchar *)"Composite", 38, 70, 80 };
    PulsThermalSensor s2 = { (gchar *)"Sensor 1 (NAND)", 42, 75, 85 };
    PulsThermalSensor s3 = { (gchar *)"Sensor 2 (Controller)", 55, 90, 105 };
    puls_smart_data_add_thermal_sensor (d, &s1);
    puls_smart_data_add_thermal_sensor (d, &s2);
    puls_smart_data_add_thermal_sensor (d, &s3);

    PulsNvmeHealth nvme = { 0 };
    nvme.critical_warning = 0;
    nvme.temperature = 38;
    nvme.available_spare = 100;
    nvme.available_spare_threshold = 10;
    nvme.percentage_used = 1;
    nvme.data_units_read = 24300000ULL;
    nvme.data_units_written = 30964000ULL;
    nvme.host_read_commands = 182000000ULL;
    nvme.host_write_commands = 210000000ULL;
    nvme.controller_busy_time = 240;
    nvme.power_cycles = 842;
    nvme.power_on_hours = 4210;
    nvme.unsafe_shutdowns = 12;
    nvme.media_errors = 0;
    nvme.error_log_entries = 0;
    puls_smart_data_set_nvme_health (d, &nvme);

    return d;
}

static PulsSmartData *
create_mock_sata_ssd (void)
{
    PulsSmartData *d = puls_smart_data_new ();
    puls_smart_data_set_device_path (d, "/dev/sda");
    puls_smart_data_set_model_name (d, "Crucial CT500MX500SSD1");
    puls_smart_data_set_serial_number (d, "2145E5F67890");
    puls_smart_data_set_firmware_version (d, "M3CR043");
    puls_smart_data_set_interface_type (d, "SATA");
    puls_smart_data_set_transfer_mode (d, "SATA/600 | SATA/600");
    puls_smart_data_set_standard (d, "ACS-3 Revision 5");
    puls_smart_data_set_form_factor (d, "2.5-inch");
    puls_smart_data_set_capacity_bytes (d, 500107862016ULL);
    puls_smart_data_set_drive_type (d, PULS_DRIVE_TYPE_SATA_SSD);
    puls_smart_data_set_transport_protocol (d, PULS_TRANSPORT_SATA);
    puls_smart_data_set_health (d, PULS_HEALTH_CAUTION);
    puls_smart_data_set_health_percent (d, 78);
    puls_smart_data_set_estimated_lifetime_days (d, 950);
    puls_smart_data_set_temperature (d, 41);
    puls_smart_data_set_power_on_hours (d, 18450);
    puls_smart_data_set_power_cycle_count (d, 1420);
    puls_smart_data_set_total_bytes_read (d, 38100000000000ULL);
    puls_smart_data_set_total_bytes_written (d, 45200000000000ULL);
    puls_smart_data_set_logical_sector_size (d, 512);
    puls_smart_data_set_physical_sector_size (d, 512);
    puls_smart_data_set_sector_format (d, PULS_SECTOR_FORMAT_512N);
    puls_smart_data_set_protocol_version (d, "ACS-3 Revision 5");
    puls_smart_data_set_tbw_rating_bytes (d, 180000000000000ULL);
    puls_smart_data_set_wear_level_percent (d, 22);
    puls_smart_data_set_supports_trim (d, TRUE);
    puls_smart_data_set_supports_ncq (d, TRUE);

    PulsThermalSensor s_sata = { (gchar *)"Drive Temperature", 41, 70, 80 };
    puls_smart_data_add_thermal_sensor (d, &s_sata);
    puls_smart_data_set_supports_devsleep (d, TRUE);
    puls_smart_data_set_supports_write_cache (d, TRUE);

    add_attribute (d, 0x05, "Reallocated Sectors Count", 98, 98, 10, 5);
    add_attribute (d, 0x09, "Power-On Hours", 90, 90, 0, 18450);
    add_attribute (d, 0x0C, "Power Cycle Count", 95, 95, 0, 1420);
    add_attribute (d, 0xC2, "Temperature", 59, 45, 0, 41);
    add_attribute (d, 0xC5, "Current Pending Sector Count", 100, 100, 0, 0);
    add_attribute (d, 0xC6, "Offline Uncorrectable Sector Count", 100, 100, 0, 0);
    add_attribute (d, 0xC7, "UltraDMA CRC Error Count", 100, 100, 0, 0);
    add_attribute (d, 0xCA, "Percentage Lifetime Used", 78, 78, 0, 22);

    return d;
}

static PulsSmartData *
create_mock_hdd (void)
{
    PulsSmartData *d = puls_smart_data_new ();
    puls_smart_data_set_device_path (d, "/dev/sdb");
    puls_smart_data_set_model_name (d, "Seagate ST8000VN004-2M2101");
    puls_smart_data_set_serial_number (d, "WSD1ABCD");
    puls_smart_data_set_firmware_version (d, "SC60");
    puls_smart_data_set_interface_type (d, "SATA");
    puls_smart_data_set_transfer_mode (d, "SATA/600 | SATA/600");
    puls_smart_data_set_standard (d, "ACS-4 Revision 10");
    puls_smart_data_set_form_factor (d, "3.5-inch");
    puls_smart_data_set_rotation_rpm (d, 7200);
    puls_smart_data_set_capacity_bytes (d, 8001563222016ULL);
    puls_smart_data_set_drive_type (d, PULS_DRIVE_TYPE_HDD);
    puls_smart_data_set_transport_protocol (d, PULS_TRANSPORT_SATA);
    puls_smart_data_set_health (d, PULS_HEALTH_GOOD);
    puls_smart_data_set_health_percent (d, 100);
    puls_smart_data_set_estimated_lifetime_days (d, 2100);
    puls_smart_data_set_temperature (d, 33);
    puls_smart_data_set_power_on_hours (d, 9800);
    puls_smart_data_set_power_cycle_count (d, 215);
    puls_smart_data_set_total_bytes_read (d, 31200000000000ULL);
    puls_smart_data_set_total_bytes_written (d, 24500000000000ULL);
    puls_smart_data_set_logical_sector_size (d, 512);
    puls_smart_data_set_physical_sector_size (d, 4096);
    puls_smart_data_set_sector_format (d, PULS_SECTOR_FORMAT_512E);
    puls_smart_data_set_protocol_version (d, "ACS-4 Revision 10");
    puls_smart_data_set_buffer_size_kb (d, 262144);
    puls_smart_data_set_supports_ncq (d, TRUE);
    puls_smart_data_set_supports_write_cache (d, TRUE);
    puls_smart_data_set_supports_trim (d, FALSE);

    PulsThermalSensor s_hdd = { (gchar *)"Internal Temperature", 33, 60, 70 };
    puls_smart_data_add_thermal_sensor (d, &s_hdd);

    add_attribute (d, 0x01, "Raw Read Error Rate", 82, 64, 6, 154320110);
    add_attribute (d, 0x03, "Spin-Up Time", 94, 94, 0, 5400);
    add_attribute (d, 0x04, "Start/Stop Count", 100, 100, 20, 215);
    add_attribute (d, 0x05, "Reallocated Sectors Count", 100, 100, 10, 0);
    add_attribute (d, 0x07, "Seek Error Rate", 85, 60, 45, 34812050);
    add_attribute (d, 0x09, "Power-On Hours", 89, 89, 0, 9800);
    add_attribute (d, 0x0A, "Spin Retry Count", 100, 100, 97, 0);
    add_attribute (d, 0x0C, "Power Cycle Count", 100, 100, 20, 215);
    add_attribute (d, 0xBB, "Reported Uncorrectable Errors", 100, 100, 0, 0);
    add_attribute (d, 0xC2, "Temperature", 33, 48, 0, 33);
    add_attribute (d, 0xC5, "Current Pending Sector Count", 100, 100, 0, 0);
    add_attribute (d, 0xC6, "Offline Uncorrectable Sector Count", 100, 100, 0, 0);
    add_attribute (d, 0xC7, "UltraDMA CRC Error Count", 200, 200, 0, 0);

    return d;
}

static PulsSmartData *
create_mock_usb_drive (void)
{
    PulsSmartData *d = puls_smart_data_new ();
    puls_smart_data_set_device_path (d, "/dev/sdc");
    puls_smart_data_set_model_name (d, "SanDisk Extreme 55AE");
    puls_smart_data_set_serial_number (d, "322109845612");
    puls_smart_data_set_firmware_version (d, "1012");
    puls_smart_data_set_interface_type (d, "USB");
    puls_smart_data_set_transfer_mode (d, "USB 3.2 (SuperSpeed++)");
    puls_smart_data_set_standard (d, "NVM Express 1.3 (via Bridge)");
    puls_smart_data_set_form_factor (d, "USB Drive / External");
    puls_smart_data_set_capacity_bytes (d, 1000204886016ULL);
    puls_smart_data_set_drive_type (d, PULS_DRIVE_TYPE_USB);
    puls_smart_data_set_transport_protocol (d, PULS_TRANSPORT_USB);
    puls_smart_data_set_health (d, PULS_HEALTH_GOOD);
    puls_smart_data_set_health_percent (d, 96);
    puls_smart_data_set_estimated_lifetime_days (d, 2800);
    puls_smart_data_set_temperature (d, 44);
    puls_smart_data_set_power_on_hours (d, 1250);
    puls_smart_data_set_power_cycle_count (d, 480);
    puls_smart_data_set_total_bytes_read (d, 6400000000000ULL);
    puls_smart_data_set_total_bytes_written (d, 8200000000000ULL);
    puls_smart_data_set_logical_sector_size (d, 512);
    puls_smart_data_set_physical_sector_size (d, 512);
    puls_smart_data_set_sector_format (d, PULS_SECTOR_FORMAT_512N);
    puls_smart_data_set_protocol_version (d, "USB 3.2 Gen 2 / UASP");
    puls_smart_data_set_tbw_rating_bytes (d, 500000000000000ULL);
    puls_smart_data_set_wear_level_percent (d, 4);

    PulsThermalSensor s_usb = { (gchar *)"Enclosure Temperature", 44, 70, 80 };
    puls_smart_data_add_thermal_sensor (d, &s_usb);

    return d;
}

void
puls_mock_data_populate_manager (PulsDiskManager *manager)
{
    g_return_if_fail (PULS_IS_DISK_MANAGER (manager));

    puls_disk_manager_add_mock_device (manager, "/dev/nvme0n1", create_mock_nvme_drive ());
    puls_disk_manager_add_mock_device (manager, "/dev/sda",     create_mock_sata_ssd ());
    puls_disk_manager_add_mock_device (manager, "/dev/sdb",     create_mock_hdd ());
    puls_disk_manager_add_mock_device (manager, "/dev/sdc",     create_mock_usb_drive ());
}
