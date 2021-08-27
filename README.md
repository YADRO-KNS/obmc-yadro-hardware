# OpenBMC YADRO hardware services

This repository contain number of services to discover and manage some hardware on Yadro VEGMAN BMC platform.

## yadro-hw-manager

This is generic detection daemon that helps EntityManager to detect entities. Currently yadro-hw-manager provide information about chassis and fans:
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
Each node exposes corresponding dbus interface: `com.yadro.HWManager.Chassis` or `com.yadro.HWManager.Fan`
Interface definition can be found in `com` directory.

## yadro-storage-manager

Storage manager aimed to provide inventory information about storage devices and manage their state. As for now it only exposes Storage Drives Inventory:
```
~# busctl tree com.yadro.Storage
├─/com
│ └─/com/yadro
│   └─/com/yadro/storage
└─/xyz
  └─/xyz/openbmc_project
    └─/xyz/openbmc_project/inventory
      └─/xyz/openbmc_project/inventory/system
        └─/xyz/openbmc_project/inventory/system/chassis
          ├─/xyz/openbmc_project/inventory/system/chassis/drive_1
          ├─/xyz/openbmc_project/inventory/system/chassis/drive_2
```
Each drive object contains folowing interfaces:
* xyz.openbmc_project.Inventory.Item
* xyz.openbmc_project.Inventory.Item.Drive
* xyz.openbmc_project.Inventory.Decorator.Asset
* xyz.openbmc_project.State.Decorator.OperationalStatus

```
# busctl introspect com.yadro.Storage /xyz/openbmc_project/inventory/system/chassis/drive_1
NAME                                                  TYPE      SIGNATURE RESULT/VALUE                             FLAGS
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
.Type                                                 property  s         "xyz.openbmc_project.Inventory.Item.Dri… emits-change writable
xyz.openbmc_project.State.Decorator.OperationalStatus interface -         -                                        -
.Functional                                           property  b         true                                     emits-change writable
```

