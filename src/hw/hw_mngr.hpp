/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021, KNS Group LLC (YADRO)
 */

#pragma once

#include "product_registry.hpp"
#include "options.hpp"

#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/bus.hpp>

#include "common.hpp"

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
    uint32_t initialPwm{0};     // PWM value before detection start
    uint32_t maxInletRpm{0};    // RPM value of InletFAN on maximum PWM
    uint32_t maxOutletRpm{0};   // RPM value of OutletFAN on maximum PWM
    std::string partNumber;     // recognized FAN P/N, or empty
    std::string prettyName;     // recognized FAN pretty name, or empty
    FanPerformanceType type{FanPerformanceType::UNKNOWN};
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
    HWManager(boost::asio::io_service& io, sdbusplus::bus::bus& bus);
    void setProduct(const std::string& pname);
    bool setOption(const OptionType& optType, const int& instance,
                   const std::string& value);
    void onHostPowerChanged(bool powered);
    void runDetectFans();
    FanState getDetectFanState();
    void publish();

    HWManagerData config;

  private:
    void clear();
    void setFanSpeed();
    void setFanSpeedDelayed();
    void saveSystemFanFeatures();
    void loadSystemFanFeatures();
    void detectFansDelayed(uint32_t delaySecs);
    void processDetectState();
    bool processSystemFans();
    void publishSystemFans();
    void updateSystemFanFeatures();

    FanState detectFansState{FanState::uninit};
    std::map<size_t, FanFeature> fanFeatures;
    uint32_t numErrorAttempts{0};

    boost::asio::io_service& io;
    sdbusplus::bus::bus& bus;
    std::vector<std::unique_ptr<sdbusplus::bus::match::match>> matches;
    HWManagerData configActive;
    std::vector<std::shared_ptr<Chassis>> chassis;
    std::vector<std::shared_ptr<Fan>> fans;

    PowerState powerState;
};
