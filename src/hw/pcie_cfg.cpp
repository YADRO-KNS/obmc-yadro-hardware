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

static constexpr uint8_t bmcPcieBifurcateX4X4X4X4 = 0x44;
static constexpr uint8_t bmcPcieBifurcateX4X4XXX8 = 0x48;
static constexpr uint8_t bmcPcieBifurcateXXX8X4X4 = 0x84;
static constexpr uint8_t bmcPcieBifurcateXXX8XXX8 = 0x88;
static constexpr uint8_t bmcPcieBifurcateXXXXXX16 = 0x16;
static constexpr uint8_t bmcPcieBifurcate____X4X4 = 0xF4;
static constexpr uint8_t bmcPcieBifurcate____XXX8 = 0xF8;
static constexpr uint8_t bmcPcieBifurcateX4X4____ = 0x4F;
static constexpr uint8_t bmcPcieBifurcateXXX8____ = 0x8F;
static constexpr uint8_t bmcPcieBifurcateDisabled = 0xDD;

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

    // FIXME: we hardcode RADUNI address for now since currently we
    // only support Rx20 Gen1 and it is said that RADUNI will always be
    // connected to CPU0 port PE2 via J45 (B1)
    if (instance == 0xFFFF)
    {
        instance = 0x0002;
        value = bmcPcieBifurcate____XXX8;
    }
    // Translate old style constants to new
    switch (value)
    {
        case pcieBifurcateX4X4X4X4:
            value = bmcPcieBifurcateX4X4X4X4;
            break;
        case pcieBifurcateX4X4XXX8:
            value = bmcPcieBifurcateX4X4XXX8;
            break;
        case pcieBifurcateXXX8X4X4:
            value = bmcPcieBifurcateXXX8X4X4;
            break;
        case pcieBifurcateXXX8XXX8:
            value = bmcPcieBifurcateXXX8XXX8;
            break;
        case pcieBifurcateXXXXXX16:
            value = bmcPcieBifurcateXXXXXX16;
            break;
        case pcieBifurcateXXXXXXXX:
            value = bmcPcieBifurcateDisabled;
            break;
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
        else if (value == bmcPcieBifurcateDisabled)
        {
            // in case of conflict "Disabled" value have less priority
            return true;
        }
        else if (old_value == bmcPcieBifurcateDisabled)
        {
            // in case of conflict "Disabled" value have less priority
            bifurcationConfig[instance] = value;
            return true;
        }
        else if (((old_value & 0x0f) == 0x0f) && ((value & 0xf0) == 0xf0))
        {
            // merge High half of old value with Low half of current value
            bifurcationConfig[instance] = (old_value & 0xf0) | (value & 0x0f);
            return true;
        }
        else if (((value & 0x0f) == 0x0f) && ((old_value & 0xf0) == 0xf0))
        {
            // merge Low half of old value with High half of current value
            bifurcationConfig[instance] = (value & 0xf0) | (old_value & 0x0f);
            return true;
        }
        else
        {
            log<level::ERR>("Incompatible option values",
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
            case bmcPcieBifurcateX4X4X4X4:
                bifurcation = PCIe::BifurcationMode::x4x4x4x4;
                break;
            case bmcPcieBifurcateX4X4XXX8:
                bifurcation = PCIe::BifurcationMode::x4x4x8;
                break;
            case bmcPcieBifurcateXXX8X4X4:
                bifurcation = PCIe::BifurcationMode::x8x4x4;
                break;
            case bmcPcieBifurcateXXX8XXX8:
                bifurcation = PCIe::BifurcationMode::x8x8;
                break;
            case bmcPcieBifurcateXXXXXX16:
                bifurcation = PCIe::BifurcationMode::x16;
                break;
            case bmcPcieBifurcate____X4X4:
                bifurcation = PCIe::BifurcationMode::lo_x4x4;
                break;
            case bmcPcieBifurcate____XXX8:
                bifurcation = PCIe::BifurcationMode::lo_x8;
                break;
            case bmcPcieBifurcateX4X4____:
                bifurcation = PCIe::BifurcationMode::hi_x4x4;
                break;
            case bmcPcieBifurcateXXX8____:
                bifurcation = PCIe::BifurcationMode::hi_x8;
                break;
            case bmcPcieBifurcateDisabled:
                bifurcation = PCIe::BifurcationMode::disabled;
                break;
            default:
                log<level::ERR>("Unexpected bifurcation value",
                                entry("VALUE=%d", mode));
                continue;
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
