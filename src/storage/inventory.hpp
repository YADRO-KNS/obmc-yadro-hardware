/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021, KNS Group LLC (YADRO)
 */

#pragma once

#include <xyz/openbmc_project/Inventory/Decorator/Asset/server.hpp>
#include <xyz/openbmc_project/Inventory/Item/Drive/server.hpp>
#include <xyz/openbmc_project/Inventory/Item/server.hpp>
#include <xyz/openbmc_project/State/Decorator/OperationalStatus/server.hpp>

class StorageDrive :
    sdbusplus::server::object::object<
        sdbusplus::xyz::openbmc_project::Inventory::server::Item>,
    sdbusplus::server::object::object<
        sdbusplus::xyz::openbmc_project::Inventory::Item::server::Drive>,
    sdbusplus::server::object::object<
        sdbusplus::xyz::openbmc_project::Inventory::Decorator::server::Asset>,
    sdbusplus::server::object::object<sdbusplus::xyz::openbmc_project::State::
                                          Decorator::server::OperationalStatus>
{
  public:
    StorageDrive(sdbusplus::bus::bus& bus, const std::string& aName,
                 const std::string& aPath, const std::string& aProto,
                 const std::string& aType, const std::string& aVendor,
                 const std::string& aModel, const std::string& aSerial,
                 const std::string& aSizeBytes);
};
