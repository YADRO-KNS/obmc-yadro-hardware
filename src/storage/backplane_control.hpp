/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022, KNS Group LLC (YADRO)
 */

#pragma once

#include "com/yadro/HWManager/BackplaneMCU/server.hpp"

#include <xyz/openbmc_project/State/Decorator/OperationalStatus/server.hpp>

using BackplaneMCUServer = sdbusplus::server::object_t<
    sdbusplus::com::yadro::HWManager::server::BackplaneMCU>;
using OperationalStatusServer =
    sdbusplus::server::object_t<sdbusplus::xyz::openbmc_project::State::
                                    Decorator::server::OperationalStatus>;
struct BackplaneControllerConfig
{
    std::map<int, std::string>
        channels;           //!< map MCU channel index to drive slot names
    bool haveDriveI2C;      //!< try to lookup for drive i2c buses
    bool softwarePowerGood; //!< whether we have to send host power state
                            //!< information from BMC to MCU

    bool operator==(const BackplaneControllerConfig& right) const
    {
        if (channels != right.channels)
            return false;
        if (haveDriveI2C != right.haveDriveI2C)
            return false;
        if (softwarePowerGood != right.softwarePowerGood)
            return false;
        return true;
    }
};

class BackplaneController : BackplaneMCUServer, OperationalStatusServer
{
  public:
    BackplaneController(sdbusplus::bus::bus& bus, int i2cBus, int i2cAddr,
                        const BackplaneControllerConfig& config);

    void updateConfig(const BackplaneControllerConfig& config);
    bool refresh();
    std::string findChannelByDriveSN(const std::string& driveSN);
    void setDriveLocationLED(const std::string& chanName, bool assert);
    bool getDriveLocationLED(const std::string& chanName);
    void resetDriveLocationLEDs();
    void hostPowerChanged(bool powered);

  private:
    std::string i2cBusDev;
    int i2cAddr;
    BackplaneControllerConfig cfg;

    uint32_t cachedState; //!< cached value of MCU channels state (presence,
                          //!< failures)

    bool doRefresh();
    std::string readDriveSN(const std::string& chanName);
    int channelIndexByName(const std::string& chanName);
};
