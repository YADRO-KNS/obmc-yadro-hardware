/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021, KNS Group LLC (YADRO)
 */

#include "backplane_control.hpp"
#include "com/yadro/HWManager/StorageManager/server.hpp"
#include "com/yadro/Inventory/Manager/server.hpp"
#include "common.hpp"
#include "common_i2c.hpp"
#include "common_swupd.hpp"
#include "dbus.hpp"
#include "inventory.hpp"
#include "xyz/openbmc_project/Common/error.hpp"
#include "xyz/openbmc_project/Software/Version/server.hpp"

#include <getopt.h>

#include <boost/algorithm/string.hpp>
#include <phosphor-logging/log.hpp>
#include <sdbusplus/server.hpp>
#include <sdeventplus/clock.hpp>
#include <sdeventplus/event.hpp>
#include <sdeventplus/exception.hpp>
#include <sdeventplus/source/event.hpp>
#include <sdeventplus/source/signal.hpp>
#include <sdeventplus/utility/timer.hpp>

#include <filesystem>
#include <fstream>
#include <streambuf>
#include <string>

using namespace phosphor::logging;
using namespace sdbusplus::xyz::openbmc_project::Common::Error;
namespace sdbusRule = sdbusplus::bus::match::rules;
namespace fs = std::filesystem;

constexpr auto clockId = sdeventplus::ClockId::RealTime;
using Timer = sdeventplus::utility::Timer<clockId>;

const std::chrono::seconds readConfigDelay(5);

static constexpr const char* storageDataFile = "/var/lib/inventory/storage.csv";

using InventoryManagerServer = sdbusplus::server::object_t<
    sdbusplus::com::yadro::Inventory::server::Manager>;
using StorageManagerServer = sdbusplus::server::object_t<
    sdbusplus::com::yadro::HWManager::server::StorageManager>;

class Manager : InventoryManagerServer, StorageManagerServer
{
  public:
    Manager(sdbusplus::bus::bus& bus, sdeventplus::Event& event);
    // com.yadro.Inventory.Manager
    void rescan();
    // com.yadro.HWManager.StorageManager
    std::tuple<std::string, std::string> findDrive(std::string driveSN);
    void setDriveLocationLED(std::string driveSN, bool assert);
    bool getDriveLocationLED(std::string driveSN);
    void resetDriveLocationLEDs();

    void applyConfiguration();
    void refresh();

  private:
    sdbusplus::bus::bus& bus;
    sdeventplus::Event& event;
    std::vector<std::unique_ptr<sdbusplus::bus::match_t>> matches;
    sdeventplus::utility::Timer<sdeventplus::ClockId::Monotonic> readDelayTimer;
    sdeventplus::utility::Timer<sdeventplus::ClockId::Monotonic> refreshTimer;
    void softwareAdded(sdbusplus::message::message& msg);

    std::vector<std::shared_ptr<StorageDrive>> drives;
    std::map<std::string, std::shared_ptr<BackplaneController>> bplMCUs;
    std::map<std::string, std::shared_ptr<SoftwareObject>> software;

    void hostPowerChanged(bool powered);
    PowerState powerState;
};

enum Fields
{
    path = 0,
    proto,
    type,
    vendor,
    model,
    serial,
    sizeBytes,
    count
};

/**
 * @brief Constructor
 *
 */
Manager::Manager(sdbusplus::bus::bus& bus, sdeventplus::Event& event) :
    InventoryManagerServer(bus, dbus::stormgr::path),
    StorageManagerServer(bus, dbus::stormgr::path), bus(bus), event(event),
    powerState(bus),
    readDelayTimer(event,
                   std::bind(std::mem_fn(&Manager::applyConfiguration), this)),
    refreshTimer(event, std::bind(std::mem_fn(&Manager::refresh), this),
                 std::chrono::seconds(10))
{
    matches.emplace_back(std::make_unique<sdbusplus::bus::match_t>(
        bus,
        sdbusRule::type::signal() + sdbusRule::member("PropertiesChanged") +
            sdbusRule::path_namespace(dbus::inventory::pathBase) +
            sdbusRule::argN(0, dbus::configuration::bplmcu::interface) +
            sdbusRule::interface(dbus::properties::interface),
        [this](sdbusplus::message::message&) {
            readDelayTimer.restartOnce(readConfigDelay);
        }));
    matches.emplace_back(std::make_unique<sdbusplus::bus::match_t>(
        bus,
        sdbusRule::interfacesAdded() + sdbusRule::path(dbus::software::path),
        [this](sdbusplus::message::message& msg) { softwareAdded(msg); }));
}

void Manager::applyConfiguration()
{
    bool softwarePowerGoodRequested = false;
    SubTreeType objects;
    auto getObjects =
        bus.new_method_call(dbus::mapper::busName, dbus::mapper::path,
                            dbus::mapper::interface, dbus::mapper::subtree);
    getObjects.append(
        dbus::inventory::pathBase, 0,
        std::array<std::string, 1>{dbus::configuration::bplmcu::interface});

    try
    {
        log<level::DEBUG>(
            "Calling GetSubTree for YadroBackplaneMCU configuration");
        bus.call(getObjects /*, dbusTimeout*/).read(objects);
        log<level::DEBUG>("GetSubTree call done");
    }
    catch (const sdbusplus::exception::SdBusError& ex)
    {
        log<level::ERR>("Error while calling GetSubTree",
                        entry("WHAT=%s", ex.what()));
        return;
    }

    for (const auto& [path, objDict] : objects)
    {
        const std::string& owner = objDict.begin()->first;
        DbusProperties data;
        auto getProperties = bus.new_method_call(owner.c_str(), path.c_str(),
                                                 dbus::properties::interface,
                                                 dbus::properties::getAll);
        getProperties.append(dbus::configuration::bplmcu::interface);

        try
        {
            log<level::DEBUG>("Calling GetAll for YadroBackplaneMCU object");
            bus.call(getProperties /*, dbusTimeout*/).read(data);
            log<level::DEBUG>("GetAll call done");
        }
        catch (const sdbusplus::exception::exception& ex)
        {
            log<level::ERR>(
                "Error while calling GetAll",
                entry("SERVICE=%s", owner.c_str()),
                entry("PATH=%s", path.c_str()),
                entry("INTERFACE=%s", dbus::configuration::bplmcu::interface),
                entry("WHAT=%s", ex.description()));
            return;
        }

        uint64_t i2cBus(std::numeric_limits<uint64_t>::max());
        uint64_t i2cAddr(std::numeric_limits<uint64_t>::max());
        std::map<int, std::string> channels;
        bool haveDriveI2C(false);
        bool softwarePowerGood(false);

        for (const auto& [prop, value] : data)
        {
            if (prop == dbus::configuration::bplmcu::properties::bus)
            {
                i2cBus = std::get<uint64_t>(value);
            }
            else if (prop == dbus::configuration::bplmcu::properties::addr)
            {
                i2cAddr = std::get<uint64_t>(value);
            }
            else if (prop == dbus::configuration::bplmcu::properties::channels)
            {
                std::vector<std::string> tmp;
                tmp = std::get<std::vector<std::string>>(value);
                int chanIndex = 0;
                for (const auto& chan : tmp)
                {
                    // skip channel if the name is not specified in config
                    if (!chan.empty())
                    {
                        channels[chanIndex] = chan;
                    }
                    chanIndex++;
                }
            }
            else if (prop ==
                     dbus::configuration::bplmcu::properties::haveDriveI2C)
            {
                haveDriveI2C = std::get<bool>(value);
            }
            else if (prop ==
                     dbus::configuration::bplmcu::properties::softwarePowerGood)
            {
                softwarePowerGood = std::get<bool>(value);
                if (softwarePowerGood)
                {
                    softwarePowerGoodRequested = true;
                }
            }
        }

        if ((i2cBus == std::numeric_limits<uint64_t>::max()) ||
            (i2cAddr == std::numeric_limits<uint64_t>::max()))
        {
            log<level::ERR>(
                "Required fields not specified for backplane MCU",
                entry("SERVICE=%s", owner.c_str()),
                entry("PATH=%s", path.c_str()),
                entry("INTERFACE=%s", dbus::configuration::bplmcu::interface),
                entry("BUS=%llu", i2cBus), entry("ADDR=%llu", i2cAddr));
            continue;
        }
        BackplaneControllerConfig config = {.channels = channels,
                                            .haveDriveI2C = haveDriveI2C,
                                            .softwarePowerGood =
                                                softwarePowerGood};
        std::ostringstream ss;
        ss << "MCU_" << i2cBus << "_" << std::hex << i2cAddr;
        const std::string name = ss.str();

        auto it = bplMCUs.find(name);
        if (it == bplMCUs.end())
        {
            fs::path p(path);
            bplMCUs[name] = std::make_shared<BackplaneController>(
                bus, i2cBus, i2cAddr, name, config, p.parent_path().string());
        }
        else
        {
            it->second->updateConfig(config);
        }
    }
    if (softwarePowerGoodRequested)
    {
        powerState.addCallback(
            "manager",
            std::bind(&Manager::hostPowerChanged, this, std::placeholders::_1));
    }
}

void Manager::refresh()
{
    try
    {
        for (const auto& [_, mcu] : bplMCUs)
        {
            mcu->refresh();
        }
    }
    catch (...)
    {}
}

/**
 * @brief Callback to be called when new firmware images placed to the system
 *
 */
void Manager::softwareAdded(sdbusplus::message::message& msg)
{
    using SVersion = sdbusplus::xyz::openbmc_project::Software::server::Version;
    using VersionPurpose = SVersion::VersionPurpose;

    sdbusplus::message::object_path objPath;
    auto purpose = VersionPurpose::Unknown;
    std::string version;
    std::map<std::string,
             std::map<std::string, sdbusplus::message::variant<std::string>>>
        interfaces;
    msg.read(objPath, interfaces);
    std::string path(std::move(objPath));
    std::string filePath;

    for (const auto& intf : interfaces)
    {
        if (intf.first == dbus::software::versionIface)
        {
            for (const auto& property : intf.second)
            {
                if (property.first == "Purpose")
                {
                    purpose = SVersion::convertVersionPurposeFromString(
                        std::get<std::string>(property.second));
                }
                else if (property.first == "Version")
                {
                    version = std::get<std::string>(property.second);
                }
            }
        }
        else if (intf.first == dbus::software::filepathIface)
        {
            for (const auto& property : intf.second)
            {
                if (property.first == "Path")
                {
                    filePath = std::get<std::string>(property.second);
                }
            }
        }
    }

    if (version.empty() || filePath.empty() ||
        !(purpose == VersionPurpose::Other ||
          purpose == VersionPurpose::System))
    {
        return;
    }

    // Version id is the last item in the path
    auto pos = path.rfind("/");
    if (pos == std::string::npos)
    {
        log<level::ERR>("No version id found in object path",
                        entry("OBJPATH=%s", path.c_str()));
        return;
    }

    auto versionId = path.substr(pos + 1);
    fs::path firmwareDir(filePath);

    std::vector<std::string> images;
    std::regex search("(.+)\\.bin$");
    std::smatch match;
    for (auto const& p : fs::directory_iterator{firmwareDir})
    {
        fs::path path = p.path();
        std::string filename = path.filename().string();
        if (std::regex_match(filename, match, search))
        {
            std::ssub_match image = match[1];
            images.emplace_back(image.str());
        }
    }

    for (const auto& [name, mcu] : bplMCUs)
    {
        std::string mcuType = mcu->getType();
        auto it = std::find(images.begin(), images.end(), mcuType);
        if (it != images.end())
        {
            software[versionId + "_" + name] = std::make_unique<SoftwareObject>(
                bus,
                dbusEscape(std::string(dbus::software::path) + "/" + versionId +
                           "/" + name),
                filePath, version, mcuType, purpose, mcu);
        }
    }
}

/**
 * @brief Find storage drives information and create corresponding dbus objects
 *
 */
void Manager::rescan()
{
    std::ifstream dataFile(storageDataFile);
    std::string line;
    if (!dataFile.is_open())
    {
        log<level::ERR>("failed to open file",
                        entry("VALUE=%s", storageDataFile));
        return;
    }

    size_t index = 1;
    drives.clear();
    while (std::getline(dataFile, line))
    {
        std::vector<std::string> entryFields;
        rtrim(line);
        boost::split(entryFields, line, boost::is_any_of(";"));
        if (entryFields.size() == 1)
        {
            continue;
        }
        else if (entryFields.size() < Fields::count)
        {
            log<level::ERR>("file format error",
                            entry("VALUE=%s", line.c_str()));
            return;
        }
        drives.emplace_back(std::make_shared<StorageDrive>(
            bus, "drive " + std::to_string(index), entryFields[path],
            entryFields[proto], entryFields[type], entryFields[vendor],
            entryFields[model], entryFields[serial], entryFields[sizeBytes]));
        index++;
    }
}

std::tuple<std::string, std::string> Manager::findDrive(std::string driveSN)
{
    if (driveSN.empty())
    {
        throw InvalidArgument();
    }
    for (const auto& [_, mcu] : bplMCUs)
    {
        auto chanName = mcu->findChannelByDriveSN(driveSN);
        if (!chanName.empty())
        {
            auto found = chanName.find('_');
            if (found != std::string::npos)
            {
                std::string type = chanName.substr(0, found);
                std::string name = chanName.substr(found + 1);
                return std::make_tuple(type, name);
            }
            else
            {
                return std::make_tuple(std::string(), chanName);
            }
        }
    }
    throw ResourceNotFound();
}

void Manager::setDriveLocationLED(std::string driveSN, bool assert)
{
    if (driveSN.empty())
    {
        throw InvalidArgument();
    }
    for (const auto& [_, mcu] : bplMCUs)
    {
        auto chanName = mcu->findChannelByDriveSN(driveSN);
        if (!chanName.empty())
        {
            mcu->setDriveLocationLED(chanName, assert);
            return;
        }
    }
    throw ResourceNotFound();
}

bool Manager::getDriveLocationLED(std::string driveSN)
{
    if (driveSN.empty())
    {
        throw InvalidArgument();
    }
    for (const auto& [_, mcu] : bplMCUs)
    {
        auto chanName = mcu->findChannelByDriveSN(driveSN);
        if (!chanName.empty())
        {
            return mcu->getDriveLocationLED(chanName);
        }
    }
    throw ResourceNotFound();
    return false;
}

void Manager::resetDriveLocationLEDs()
{
    for (const auto& [_, mcu] : bplMCUs)
    {
        mcu->resetDriveLocationLEDs();
    }
}

void Manager::hostPowerChanged(bool powered)
{
    for (const auto& [chan, mcu] : bplMCUs)
    {
        mcu->hostPowerChanged(powered);
    }
}

static void signalHandler(sdeventplus::source::Signal& source,
                          const struct signalfd_siginfo*)
{
    source.get_event().exit(EXIT_SUCCESS);
}

static void showUsage(char* appName)
{
    fprintf(stderr, R"(Usage: %s [options]
Options:
  -v, --verbose  Enable output debug messages.
  -h, --help     Show this help
)",
            appName);
}

/**
 * @brief Application entry point
 *
 * @return exit status
 */
int main(int argc, char** argv)
{
    const struct option opts[] = {
        {"verbose", no_argument, nullptr, 'v'},
        {"help", no_argument, nullptr, 'h'},
        {nullptr, 0, nullptr, '\0'}};
    int c;
    while ((c = getopt_long(argc, argv, "v:h", opts, nullptr)) != -1)
    {
        switch (c)
        {
            case 'd':
                i2cDev::verbose = true;
                break;
            case 'h':
                showUsage(argv[0]);
                return 0;
            default:
                break;
        }
    }

    sigset_t ss;

    if (sigemptyset(&ss) < 0 || sigaddset(&ss, SIGTERM) < 0 ||
        sigaddset(&ss, SIGINT) < 0 || sigaddset(&ss, SIGCHLD) < 0)
    {
        log<level::ERR>("ERROR: Failed to setup signal handlers",
                        entry("REASON=%d", strerror(errno)));
        return EXIT_FAILURE;
    }

    if (sigprocmask(SIG_BLOCK, &ss, nullptr) < 0)
    {
        log<level::ERR>("ERROR: Failed to block signals",
                        entry("REASON=%d", strerror(errno)));
        return EXIT_FAILURE;
    }

    auto bus = sdbusplus::bus::new_default();
    auto event = sdeventplus::Event::get_default();

    sdeventplus::source::Signal sigterm(event, SIGTERM, signalHandler);
    sdeventplus::source::Signal sigint(event, SIGINT, signalHandler);
    bus.attach_event(event.get(), SD_EVENT_PRIORITY_NORMAL);

    sdbusplus::server::manager_t objManager(bus, "/");

    Manager storageManager(bus, event);
    storageManager.rescan();
    storageManager.applyConfiguration();

    bus.request_name(dbus::stormgr::busName);
    return event.loop();
}
