/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 YADRO.
 */

#pragma once

#include <xyz/openbmc_project/Inventory/Decorator/Asset/server.hpp>
#include <xyz/openbmc_project/Inventory/Item/NetworkInterface/server.hpp>
#include <xyz/openbmc_project/Inventory/Item/server.hpp>
#include <xyz/openbmc_project/State/Decorator/OperationalStatus/server.hpp>

using namespace sdbusplus::xyz::openbmc_project;

using InventoryItemServer =
    sdbusplus::server::object::object<Inventory::server::Item>;
using NetworkInterfaceServer = sdbusplus::server::object::object<
    Inventory::Item::server::NetworkInterface>;
using DecoratorAssetServer =
    sdbusplus::server::object::object<Inventory::Decorator::server::Asset>;
using OperationalStatusServer = sdbusplus::server::object::object<
    State::Decorator::server::OperationalStatus>;

struct NetworkAdapter :
    public InventoryItemServer,
    public NetworkInterfaceServer,
    public DecoratorAssetServer,
    public OperationalStatusServer
{
    NetworkAdapter(sdbusplus::bus::bus& bus, const std::string& name,
                   const std::string& vendor, const std::string& device,
                   const std::string& macAddress);

  private:
    static const std::string inventoryPath;
};
