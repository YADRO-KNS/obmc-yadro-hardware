/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021, KNS Group LLC (YADRO)
 */

#pragma once

#include <sdbusplus/bus.hpp>

#include <map>
#include <string>
#include <variant>
#include <vector>
#include <regex>

namespace dbus
{
namespace properties
{
constexpr const char* interface = "org.freedesktop.DBus.Properties";
constexpr const char* getAll = "GetAll";
constexpr const char* get = "Get";
constexpr const char* set = "Set";
} // namespace properties

namespace hwmgr
{
constexpr const char* busName = "com.yadro.HWManager";
constexpr const char* path = "/com/yadro/hw_manager";
} // namespace hwmgr

namespace stormgr
{
constexpr const char* busName = "com.yadro.Storage";
constexpr const char* path = "/com/yadro/storage";
} // namespace stormgr

namespace netadpmgr
{
constexpr const char* busName = "com.yadro.NetworkAdapter";
constexpr const char* path = "/com/yadro/network/adapter";
} // namespace netadpmgr

namespace objmgr
{
constexpr const char* interface = "org.freedesktop.DBus.ObjectManager";
constexpr const char* managedObjects = "GetManagedObjects";
} // namespace objmgr

namespace mapper
{
constexpr const char* busName = "xyz.openbmc_project.ObjectMapper";
constexpr const char* path = "/xyz/openbmc_project/object_mapper";
constexpr const char* interface = "xyz.openbmc_project.ObjectMapper";
constexpr const char* subtree = "GetSubTree";
} // namespace mapper

namespace inventory
{
constexpr const char* pathBase = "/xyz/openbmc_project/inventory";
constexpr const char* path =
    "/xyz/openbmc_project/inventory/system/chassis/motherboard";
constexpr const char* interface = "xyz.openbmc_project.Inventory.Item";
namespace properties
{
constexpr const char* PrettyName = "PrettyName";
constexpr const char* Present = "Present";
} // namespace properties
} // namespace inventory

namespace configuration
{
namespace bplmcu
{
constexpr const char* interface =
    "xyz.openbmc_project.Configuration.YadroBackplaneMCU";
namespace properties
{
constexpr const char* bus = "Bus";
constexpr const char* addr = "Address";
constexpr const char* channels = "ChannelNames";
constexpr const char* haveDriveI2C = "HaveDriveI2C";
constexpr const char* softwarePowerGood = "SoftwarePowerGood";
} // namespace properties
} // namespace bplmcu
} // namespace configuration

namespace fru
{
constexpr const char* busName = "xyz.openbmc_project.FruDevice";
constexpr const char* path = "/xyz/openbmc_project/FruDevice";
constexpr const char* interface = "xyz.openbmc_project.FruDevice";
} // namespace fru

namespace pid
{
constexpr const char* path = "/xyz/openbmc_project/inventory/system/board";
constexpr const char* interface = "xyz.openbmc_project.Configuration.Pid.Zone";
namespace properties
{
const constexpr char* MinThermalOutput = "MinThermalOutput";
} // namespace properties
} // namespace pid

namespace pcie_cfg
{
namespace properties
{
constexpr const char* bifurcation = "Bifurcation";
} // namespace properties
} // namespace pcie_cfg

namespace power
{
constexpr const char* busname = "xyz.openbmc_project.State.Host";
constexpr const char* interface = "xyz.openbmc_project.State.Host";
constexpr const char* path = "/xyz/openbmc_project/state/host0";
namespace properties
{
const static constexpr char* state = "CurrentHostState";
} // namespace properties
} // namespace power

namespace software
{
constexpr const char* path = "/xyz/openbmc_project/software";
constexpr const char* versionIface = "xyz.openbmc_project.Software.Version";
constexpr const char* filepathIface = "xyz.openbmc_project.Common.FilePath";
} // namespace software
} // namespace dbus

using Interface = std::string;
using PropertyName = std::string;
using DbusPropVariant = std::variant<uint32_t, uint64_t, bool, double,
                                     std::string, std::vector<std::string>>;
using DbusProperties = std::map<PropertyName, DbusPropVariant>;
using ManagedObjectType = std::map<sdbusplus::message::object_path,
                                   std::map<Interface, DbusProperties>>;

using ObjectPath = std::string;
using OwnerName = std::string;
using Interfaces = std::vector<Interface>;
using SubTreeType = std::map<ObjectPath, std::map<OwnerName, Interfaces>>;
using Association = std::tuple<std::string, std::string, std::string>;

static std::string dbusEscape(const std::string& str)
{
    static const std::regex dbusEscapePattern("[^a-zA-Z0-9_/]+");
    return std::regex_replace(str, dbusEscapePattern, "_");
}
