/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 YADRO.
 */

#include "pcie_cfg.h"

#include "dbus.hpp"
#include "options.hpp"

#include <phosphor-logging/log.hpp>
#include <xyz/openbmc_project/Control/PCIe/server.hpp>

#include <charconv>

using namespace phosphor::logging;

using namespace sdbusplus::xyz::openbmc_project::Control::server;
using BifurcationConfiguration =
    std::vector<std::tuple<uint8_t, uint8_t, PCIe::BifurcationMode>>;

// PCIe Bifurcation mode
static constexpr uint8_t pcieBifurcateX4X4X4X4 = 0;
static constexpr uint8_t pcieBifurcateX4X4XXX8 = 1;
static constexpr uint8_t pcieBifurcateXXX8X4X4 = 2;
static constexpr uint8_t pcieBifurcateXXX8XXX8 = 3;
static constexpr uint8_t pcieBifurcateXXXXXX16 = 4;
static constexpr uint8_t pcieBifurcateXXXXXXXX = 0xF;

/**
 * @brief Lookup dbus service for interface
 *
 * @param[in] interface     DBus interface to lookup
 * @return service name and resource path
 */
static std::tuple<std::string, std::string>
    dbusGetSetviceAndPath(sdbusplus::bus::bus& bus,
                          const std::string& interface)
{
    auto getObjects =
        bus.new_method_call(dbus::mapper::busName, dbus::mapper::path,
                            dbus::mapper::interface, dbus::mapper::subtree);
    getObjects.append("/", 0, std::array<std::string, 1>{interface});

    SubTreeType objects;
    bus.call(getObjects).read(objects);

    if (objects.size() != 1)
    {
        throw sdbusplus::exception::SdBusError(-EINVAL,
                                               "unexpected objects count");
    }
    if (objects.begin()->second.size() != 1)
    {
        throw sdbusplus::exception::SdBusError(-EINVAL,
                                               "unexpected services count");
    }

    auto path = objects.begin()->first;
    auto service = objects.begin()->second.begin()->first;
    return std::make_tuple(path, service);
}

bool pcieCfg::addBifurcationConfig(const int& socket,
                                   const std::string& optValue)
{
    uint16_t instance = 0;
    uint8_t value = 0;
    if (optValue.size() < 4)
    {
        log<level::ERR>("Invalid PCIe configuration option format",
                        entry("VALUE=%s", optValue.c_str()));
        return false;
    }

    std::from_chars(optValue.data() + 0, optValue.data() + 2, instance, 16);
    std::from_chars(optValue.data() + 2, optValue.data() + 4, value, 16);
    instance |= (socket & 0xFF) << 8;
    if (optValue.size() > 4)
    { // TODO: implement slot description parsing
        std::string slots = optValue.substr(4);
        log<level::DEBUG>("PCIe configuration option contain slot description",
                          entry("VALUE=%s", slots.c_str()));
    }

    // Check if another hardware component has already claimed this port and try
    // to merge the configurations. For that purpose we assume here that a port
    // can only be split across two components by halves (8 lanes), and that for
    // an unused half a component always requests 'x8' mode.
    auto cfg = bifurcationConfig.find(instance);
    if (cfg != bifurcationConfig.end())
    {
        // Get the config previously set for the port by another hardware
        // component
        uint8_t old_value = cfg->second;
        if (value == old_value)
        {
            return true;
        }
        else if (((old_value == pcieBifurcateX4X4XXX8) ||
                  (old_value == pcieBifurcateXXX8X4X4)) &&
                 (value == pcieBifurcateXXX8XXX8))
        {
            // We want to set our half to 'x8', while other component split it's
            // half into 'x4x4' - keep old value, since it has already set 'x8'
            // for us
            return true;
        }
        else if ((old_value == pcieBifurcateXXX8XXX8) &&
                 ((value == pcieBifurcateX4X4XXX8) ||
                  (value == pcieBifurcateXXX8X4X4)))
        {
            // We want to split our half into 'x4x4', while other component uses
            // it's half as 'x8' - set our value
            bifurcationConfig[instance] = value;
            return true;
        }
        else if (((old_value == pcieBifurcateX4X4XXX8) &&
                  (value == pcieBifurcateXXX8X4X4)) ||
                 ((old_value == pcieBifurcateXXX8X4X4) &&
                  (value == pcieBifurcateX4X4XXX8)))
        {
            // We both want to split our halves into 'x4x4' - resulting
            // configuration is 'x4x4x4x4'
            bifurcationConfig[instance] = pcieBifurcateX4X4X4X4;
            return true;
        }
        else
        {
            log<level::ERR>("incompatible option values",
                            entry("VALUE=%d", value),
                            entry("OLD_VALUE=%d", old_value));
            return false;
        }
    }
    else
    {
        bifurcationConfig.emplace(instance, value);
    }
    return true;
}

pcieCfg::~pcieCfg()
{
    BifurcationConfiguration config;
    std::string settingsPath, settingsService;
    for (auto& [addr, mode] : bifurcationConfig)
    {
        const uint8_t socket = (addr >> 8) & 0xff;
        const uint8_t iouNumber = addr & 0xff;
        PCIe::BifurcationMode bifurcation = PCIe::BifurcationMode::disabled;
        switch (mode)
        {
            case pcieBifurcateX4X4X4X4:
                bifurcation = PCIe::BifurcationMode::x4x4x4x4;
                break;
            case pcieBifurcateX4X4XXX8:
                bifurcation = PCIe::BifurcationMode::x4x4x8;
                break;
            case pcieBifurcateXXX8X4X4:
                bifurcation = PCIe::BifurcationMode::x8x4x4;
                break;
            case pcieBifurcateXXX8XXX8:
                bifurcation = PCIe::BifurcationMode::x8x8;
                break;
            case pcieBifurcateXXXXXX16:
                bifurcation = PCIe::BifurcationMode::x16;
                break;
        }
        config.emplace_back(std::make_tuple(socket, iouNumber, bifurcation));
    }

    try
    {
        std::tie(settingsPath, settingsService) =
            dbusGetSetviceAndPath(bus, PCIe::interface);
    }
    catch (const sdbusplus::exception::exception& ex)
    {
        log<level::ERR>("Settings lookup error", entry("VALUE=%s", ex.what()));
        return;
    }

    auto setProp =
        bus.new_method_call(settingsService.c_str(), settingsPath.c_str(),
                            dbus::properties::interface, dbus::properties::set);
    setProp.append(PCIe::interface, dbus::pcie_cfg::properties::bifurcation,
                   std::variant<BifurcationConfiguration>(config));

    try
    {
        bus.call(setProp);
    }
    catch (const sdbusplus::exception::exception& ex)
    {
        log<level::ERR>("Set configuration error",
                        entry("VALUE=%s", ex.what()));
        return;
    }
}
