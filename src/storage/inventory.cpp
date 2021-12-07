/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 YADRO.
 */

#include "inventory.hpp"

#include "dbus.hpp"
#include "pcidb.hpp"

#include <phosphor-logging/log.hpp>

#include <charconv>
#include <fstream>

using namespace phosphor::logging;

static constexpr const char* inventorySubPath = "/system/drive/";

StorageDrive::StorageDrive(sdbusplus::bus::bus& bus, const std::string& aName,
                           const std::string& aPath, const std::string& aProto,
                           const std::string& aType, const std::string& aVendor,
                           const std::string& aModel,
                           const std::string& aSerial,
                           const std::string& aSizeBytes) :
    sdbusplus::server::object::object<
        sdbusplus::xyz::openbmc_project::Inventory::server::Item>(
        bus, dbusEscape(std::string(dbus::inventory::pathBase) +
                        inventorySubPath + aName)
                 .c_str()),
    sdbusplus::server::object::object<
        sdbusplus::xyz::openbmc_project::Inventory::Item::server::Drive>(
        bus, dbusEscape(std::string(dbus::inventory::pathBase) +
                        inventorySubPath + aName)
                 .c_str()),
    sdbusplus::server::object::object<
        sdbusplus::xyz::openbmc_project::Inventory::Decorator::server::Asset>(
        bus, dbusEscape(std::string(dbus::inventory::pathBase) +
                        inventorySubPath + aName)
                 .c_str()),
    sdbusplus::server::object::object<sdbusplus::xyz::openbmc_project::State::
                                          Decorator::server::OperationalStatus>(
        bus, dbusEscape(std::string(dbus::inventory::pathBase) +
                        inventorySubPath + aName)
                 .c_str())
{
    uint64_t sizeInt = 0;
    std::string sizeStr;
    std::string manuf;
    std::string name;
    // try to render drive size (assume 1KB = 1000B, which is common for storage
    // devices)
    if (!aSizeBytes.empty())
    {
        auto [p, ec] = std::from_chars(
            aSizeBytes.data(), aSizeBytes.data() + aSizeBytes.size(), sizeInt);
        if (ec != std::errc())
        {
            log<level::ERR>("failed to parse drive size",
                            entry("VALUE=%s", sizeStr.c_str()));
            sizeInt = 0;
        }

        if (sizeInt)
        {
            size_t order = aSizeBytes.size() / 3;
            if (aSizeBytes.size() == order * 3)
            {
                order--;
            }
            sizeStr = aSizeBytes.substr(0, aSizeBytes.size() - order * 3);
            // if only one digit left, add one more digit after decimal point
            if ((sizeStr.size() <= 1) && (aSizeBytes.size() > 1))
            {
                sizeStr += "." + aSizeBytes.substr(sizeStr.size(), 1);
            }

            switch (order)
            {
                case 0:
                    sizeStr += "B";
                    break;
                case 1:
                    sizeStr += "KB";
                    break;
                case 2:
                    sizeStr += "MB";
                    break;
                case 3:
                    sizeStr += "GB";
                    break;
                case 4:
                    sizeStr += "TB";
                    break;
                case 5:
                    sizeStr += "PB";
                    break;
                default:
                    sizeStr = std::string();
                    break;
            }
        }
    }
    // try to render drive manufacturer (use pci.ids database)
    if ((!aVendor.empty()) && (aProto == "NVMe"))
    {
        manuf = pciLookup(aVendor).first;
    }
    // assemble drive name
    if (!aProto.empty())
    {
        name += aProto + " ";
    }
    if (!sizeStr.empty())
    {
        name += sizeStr + " ";
    }
    name += aName;

    DriveProtocol proto = DriveProtocol::Unknown;
    if (aProto == "SATA")
    {
        proto = DriveProtocol::SATA;
    }
    else if (aProto == "SAS")
    {
        proto = DriveProtocol::SAS;
    }
    else if (aProto == "NVMe")
    {
        proto = DriveProtocol::NVMe;
    }
    DriveType driveType = DriveType::Unknown;
    if (aType == "SSD")
    {
        driveType = DriveType::SSD;
    }
    else if (aType == "HDD")
    {
        driveType = DriveType::HDD;
    }

    // xyz.openbmc_project.Inventory.Item
    prettyName(name);
    present(true);
    // xyz.openbmc_project.Inventory.Item.Drive
    capacity(sizeInt);
    type(driveType);
    protocol(proto);
    // xyz.openbmc_project.Inventory.Decorator.Asset
    serialNumber(aSerial);
    manufacturer(manuf);
    model(aModel);
    // xyz.openbmc_project.State.Decorator.OperationalStatus
    functional(true);
}
