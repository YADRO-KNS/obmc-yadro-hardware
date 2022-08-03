/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021, KNS Group LLC (YADRO)
 */

#include "adapter.hpp"

#include "dbus.hpp"
#include "pcidb.hpp"

#include <phosphor-logging/log.hpp>

#include <charconv>
#include <fstream>

using namespace phosphor::logging;

const std::string NetworkAdapter::inventoryPath =
    dbus::inventory::pathBase + std::string("/system/network/adapter/");

NetworkAdapter::NetworkAdapter(sdbusplus::bus::bus& bus,
                               const std::string& name,
                               const std::string& vendor,
                               const std::string& device,
                               const std::string& macAddress) :
    InventoryItemServer(bus, dbusEscape(inventoryPath + name).c_str()),
    NetworkInterfaceServer(bus, dbusEscape(inventoryPath + name).c_str()),
    DecoratorAssetServer(bus, dbusEscape(inventoryPath + name).c_str()),
    OperationalStatusServer(bus, dbusEscape(inventoryPath + name).c_str())
{
    // try to render adapter manufacturer/model (use pci.ids database)
    auto [vendorName, modelName] = pciLookup(vendor, device);
    // xyz.openbmc_project.Inventory.Item
    prettyName(name);
    present(true);
    // xyz.openbmc_project.Inventory.Item.NetworkInterface
    mACAddress(macAddress);
    // xyz.openbmc_project.Inventory.Decorator.Asset
    manufacturer(vendorName);
    model(modelName);
    // xyz.openbmc_project.State.Decorator.OperationalStatus
    functional(true);
}
