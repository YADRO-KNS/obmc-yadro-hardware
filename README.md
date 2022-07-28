# OpenBMC YADRO hardware services

This repository contains a number of services to discover and manage some
hardware on Yadro VEGMAN BMC platform.

## yadro-hw-manager

This is a generic detection daemon that helps EntityManager to detect entities.
Currently yadro-hw-manager provides information about chassis and fans:

```
~# busctl tree com.yadro.HWManager
└─/com
  └─/com/yadro
    └─/com/yadro/hw_manager
      ├─/com/yadro/hw_manager/chassis
      │ └─/com/yadro/hw_manager/chassis/VEGMAN_N110
      └─/com/yadro/hw_manager/fan
        ├─/com/yadro/hw_manager/fan/Sys_Fan1
        ├─/com/yadro/hw_manager/fan/Sys_Fan2
        ├─/com/yadro/hw_manager/fan/Sys_Fan3
        ├─/com/yadro/hw_manager/fan/Sys_Fan4
        └─/com/yadro/hw_manager/fan/Sys_Fan5
```

Each node exposes a corresponding dbus interface: `com.yadro.HWManager.Chassis`
or `com.yadro.HWManager.Fan` Interface definition can be found in `com`
directory.

## yadro-storage-manager

Storage manager is aimed to provide inventory information about storage devices
and manage their state. It also communicates with backplanes to manage some
functions:

```
~# busctl tree com.yadro.Storage
├─/com
│ └─/com/yadro
│   └─/com/yadro/storage
│     └─/com/yadro/storage/backplane
│       ├─/com/yadro/storage/backplane/MCU_21_43
│       └─/com/yadro/storage/backplane/MCU_21_44
└─/xyz
  └─/xyz/openbmc_project
    └─/xyz/openbmc_project/inventory
      └─/xyz/openbmc_project/inventory/system
        └─/xyz/openbmc_project/inventory/system/drive
          ├─/xyz/openbmc_project/inventory/system/drive/drive_1
          ├─/xyz/openbmc_project/inventory/system/drive/drive_2
          ├─/xyz/openbmc_project/inventory/system/drive/drive_3
          └─/xyz/openbmc_project/inventory/system/drive/drive_4
```

Each drive object contains following interfaces to represent inventory
information:
* xyz.openbmc_project.Inventory.Item
* xyz.openbmc_project.Inventory.Item.Drive
* xyz.openbmc_project.Inventory.Decorator.Asset
* xyz.openbmc_project.State.Decorator.OperationalStatus

```
~# busctl introspect com.yadro.Storage /xyz/openbmc_project/inventory/system/drive/drive_1
NAME                                                  TYPE      SIGNATURE RESULT/VALUE                             FLAGS
[...]
xyz.openbmc_project.Inventory.Decorator.Asset         interface -         -                                        -
.BuildDate                                            property  s         ""                                       emits-change writable
.Manufacturer                                         property  s         ""                                       emits-change writable
.Model                                                property  s         "CT250MX500SSD4"                         emits-change writable
.PartNumber                                           property  s         ""                                       emits-change writable
.SerialNumber                                         property  s         "2014E299E2EB"                           emits-change writable
xyz.openbmc_project.Inventory.Item                    interface -         -                                        -
.Present                                              property  b         true                                     emits-change writable
.PrettyName                                           property  s         "SATA 250GB drive 1"                     emits-change writable
xyz.openbmc_project.Inventory.Item.Drive              interface -         -                                        -
.Capacity                                             property  t         250059350016                             emits-change writable
.Protocol                                             property  s         "xyz.openbmc_project.Inventory.Item.Dri… emits-change writable
.Type                                                 property  s         "xyz.openbmc_project.Inventory.Item.Dri… emits-change writable
xyz.openbmc_project.State.Decorator.OperationalStatus interface -         -                                        -
.Functional                                           property  b         true                                     emits-change writable
```

Storage manager resides at `/com/yadro/storage` and provides a number of
functions to locate storage drive in the backplane:

```
~# busctl introspect com.yadro.Storage /com/yadro/storage
NAME                                TYPE      SIGNATURE RESULT/VALUE FLAGS
com.yadro.HWManager.StorageManager  interface -         -            -
.FindDrive                          method    s         (ss)         -
.GetDriveLocationLED                method    s         b            -
.ResetDriveLocationLEDs             method    -         -            -
.SetDriveLocationLED                method    sb        -            -
com.yadro.Inventory.Manager         interface -         -            -
.Rescan                             method    -         -            -
[...]

```

* `SetDriveLocationLED` accepts drive serial number and requested LocateLED
  state (`true` to enable locate LED blinking and `false` - to disable).
* `GetDriveLocationLED` accepts drive serial number and return actual LocateLED
  state (`true` if locate LED blinking and `false` - if disabled).
* `ResetDriveLocationLEDs` disable all LocateLEDs
* `FindDrive` accepts drive serial number and return slot name where the drive
  is found (in form of ["Type", "Number"] pair)
* `Rescan` method in `com.yadro.Inventory.Manager` interface triggers Storage
  Manager to reread the list of drives received from BIOS.

Backplane controller objects contain information about firmware version and
channels state:

```
~# busctl introspect com.yadro.Storage /com/yadro/storage/backplane/MCU_21_44
NAME                                                  TYPE      SIGNATURE RESULT/VALUE                             FLAGS
com.yadro.HWManager.BackplaneMCU                      interface -         -                                        -
.Drives                                               property  a(sssb)   5 "Rear_05" "S438NC0R514253" "com.yadro… emits-change writable
.FirmwareVersion                                      property  s         "Jul 26 2022 18:17:24 ci system-firmwar… emits-change writable
org.freedesktop.DBus.Introspectable                   interface -         -                                        -
.Introspect                                           method    -         s                                        -
org.freedesktop.DBus.Peer                             interface -         -                                        -
.GetMachineId                                         method    -         s                                        -
.Ping                                                 method    -         -                                        -
org.freedesktop.DBus.Properties                       interface -         -                                        -
.Get                                                  method    ss        v                                        -
.GetAll                                               method    s         a{sv}                                    -
.Set                                                  method    ssv       -                                        -
.PropertiesChanged                                    signal    sa{sv}as  -                                        -
xyz.openbmc_project.State.Decorator.OperationalStatus interface -         -                                        -
.Functional                                           property  b         true                                     emits-change writable

~# busctl --verbose get-property com.yadro.Storage /com/yadro/storage/backplane/MCU_21_44 com.yadro.HWManager.BackplaneMCU Drives
ARRAY "(sssb)" {
        STRUCT "sssb" {
                STRING "Rear_05";
                STRING "S438NC0R514253";
                STRING "com.yadro.HWManager.BackplaneMCU.DriveInterface.NVMe";
                BOOLEAN true;
        };
        STRUCT "sssb" {
                STRING "Rear_06";
                STRING "";
                STRING "com.yadro.HWManager.BackplaneMCU.DriveInterface.NoDisk";
                BOOLEAN true;
        };
        STRUCT "sssb" {
                STRING "Rear_07";
                STRING "";
                STRING "com.yadro.HWManager.BackplaneMCU.DriveInterface.NoDisk";
                BOOLEAN true;
        };
        STRUCT "sssb" {
                STRING "Rear_08";
                STRING "7110A005T4B8";
                STRING "com.yadro.HWManager.BackplaneMCU.DriveInterface.NVMe";
                BOOLEAN true;
        };
        STRUCT "sssb" {
                STRING "Rear_09";
                STRING "SDM00005FDD8";
                STRING "com.yadro.HWManager.BackplaneMCU.DriveInterface.NVMe";
                BOOLEAN true;
        };
};
```

## yadro-network-adapter-manager

Network Adapter manager provides inventory information about network adapters,
connected to the host.

```
~# busctl tree com.yadro.NetworkAdapter
├─/com
│ └─/com/yadro
│   └─/com/yadro/network
│     └─/com/yadro/network/adapter
└─/xyz
  └─/xyz/openbmc_project
    └─/xyz/openbmc_project/inventory
      └─/xyz/openbmc_project/inventory/system
        └─/xyz/openbmc_project/inventory/system/network
          └─/xyz/openbmc_project/inventory/system/network/adapter
            ├─/xyz/openbmc_project/inventory/system/network/adapter/net0
            ├─/xyz/openbmc_project/inventory/system/network/adapter/net1
            ├─/xyz/openbmc_project/inventory/system/network/adapter/net2
            ├─/xyz/openbmc_project/inventory/system/network/adapter/net3
            ├─/xyz/openbmc_project/inventory/system/network/adapter/net4
            ├─/xyz/openbmc_project/inventory/system/network/adapter/net5
            ├─/xyz/openbmc_project/inventory/system/network/adapter/net6
            ├─/xyz/openbmc_project/inventory/system/network/adapter/net7
            ├─/xyz/openbmc_project/inventory/system/network/adapter/net8
            └─/xyz/openbmc_project/inventory/system/network/adapter/net9
```

Each network adapter object contains following interfaces to represent
inventory information:
* xyz.openbmc_project.Inventory.Item
* xyz.openbmc_project.Inventory.Item.NetworkInterface
* xyz.openbmc_project.Inventory.Decorator.Asset
* xyz.openbmc_project.State.Decorator.OperationalStatus
