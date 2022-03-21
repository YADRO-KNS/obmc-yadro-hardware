/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 YADRO.
 */

#pragma once

#include "com/yadro/HWManager/Chassis/server.hpp"
#include "com/yadro/HWManager/Fan/server.hpp"

using HWManagerChassisServer = sdbusplus::server::object_t<
    sdbusplus::com::yadro::HWManager::server::Chassis>;
class Chassis : HWManagerChassisServer
{
  public:
    Chassis(sdbusplus::bus::bus& bus, const std::string& aName,
            const std::string& aModel, const std::string& aPartNumber,
            const std::string& aSerial);
};

using HWManagerFanServer =
    sdbusplus::server::object_t<sdbusplus::com::yadro::HWManager::server::Fan>;
class Fan : HWManagerFanServer
{
  public:
    Fan(sdbusplus::bus::bus& bus, const std::string& aName,
        const std::string& aPrettyName, const std::string& aModel,
        const std::string& aPartNumber, const std::string& aZone,
        const std::string& aConnector, const uint32_t& aTachIndexA,
        const uint32_t& aTachIndexB, const uint32_t& aPwmIndex,
        const uint32_t& aPwmLimitMax);
};
