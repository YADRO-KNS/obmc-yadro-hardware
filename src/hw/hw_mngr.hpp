/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 YADRO.
 */

#pragma once

#include "options.hpp"

#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/bus.hpp>

struct ProductDescription;

enum class FanState
{
    uninit,
    init,
    detect,
    normal,
};

struct FanFeature
{
    uint32_t initialPwm{0};
    uint32_t maxInletRpm{0};
    uint32_t maxOutletRpm{0};
    std::string partNumber;
    std::string prettyName;
};

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

    HWManagerData() :
        desc(nullptr), chassisModel(), chassisPartNumber(), chassisSerial(),
        haveCPUFans(false), cpuPresence(), chassisFans()
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
    void onHostPowerOn();
    void onHostPowerOff();
    void runDetectFans();
    FanState getDetectFanState();
    void publish();

    HWManagerData config;

  private:
    void clear();
    void setFanSpeed();
    void setFanSpeedDelayed();
    void detectFansDelayed(uint32_t delaySecs);
    void processDetectState();
    bool processSystemFans();
    void updateFansInfo();

    FanState detectFansState{FanState::uninit};
    std::map<size_t, FanFeature> fanFeatures;
    uint32_t numErrorAttempts{0};

    boost::asio::io_service& io;
    sdbusplus::bus::bus& bus;
    std::vector<std::unique_ptr<sdbusplus::bus::match::match>> matches;
    HWManagerData configActive;
    std::vector<std::shared_ptr<Chassis>> chassis;
    std::vector<std::shared_ptr<Fan>> fans;
};
