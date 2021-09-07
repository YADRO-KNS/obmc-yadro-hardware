/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 YADRO.
 */

#pragma once

#include "options.hpp"

#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/bus.hpp>

struct ProductDescription;

struct ChassisPIDZone
{
    std::vector<size_t> fanConnector;
    size_t fanMinSpeed;
    bool operator==(const ChassisPIDZone& right) const
    {
        if (fanConnector != right.fanConnector)
            return false;
        if (fanMinSpeed != right.fanMinSpeed)
            return false;
        return true;
    }
};

struct HWManagerData
{
    const ProductDescription* desc;
    std::string chassisModel;
    std::string chassisPartNumber;
    std::string chassisSerial;
    bool haveCPUFans;
    std::map<size_t, bool> cpuPresence;
    std::map<std::string, ChassisPIDZone> chassisFans;

    HWManagerData() : haveCPUFans(false)
    {}
    void reset()
    {
        desc = nullptr;
        chassisModel = "";
        chassisPartNumber = "";
        chassisSerial = "";
        haveCPUFans = false;
        cpuPresence.clear();
        chassisFans.clear();
    }
    bool operator==(const HWManagerData& right)
    {
        if (desc != right.desc)
            return false;
        if (chassisModel != right.chassisModel)
            return false;
        if (chassisPartNumber != right.chassisPartNumber)
            return false;
        if (chassisSerial != right.chassisSerial)
            return false;
        if (haveCPUFans != right.haveCPUFans)
            return false;
        if (cpuPresence != right.cpuPresence)
            return false;
        if (chassisFans != right.chassisFans)
            return false;
        return true;
    }
};

class Chassis;
class Fan;
class HWManager
{
  public:
    HWManager(boost::asio::io_service& io, sdbusplus::bus::bus& bus) :
        io(io), bus(bus)
    {}
    void setProduct(const std::string& pname);
    bool setOption(const OptionType& optType, const int& instance,
                   const std::string& value);
    void publish();

    HWManagerData config;

  private:
    void clear();
    void setFanSpeed();
    void setFanSpeedDelayed();

    boost::asio::io_service& io;
    sdbusplus::bus::bus& bus;
    std::vector<std::unique_ptr<sdbusplus::bus::match::match>> matches;
    HWManagerData configActive;
    std::vector<std::shared_ptr<Chassis>> chassis;
    std::vector<std::shared_ptr<Fan>> fans;
};
