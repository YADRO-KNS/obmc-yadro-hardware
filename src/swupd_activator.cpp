/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022 YADRO.
 */

#include "dbus.hpp"
#include "xyz/openbmc_project/Software/Activation/server.hpp"

#include <getopt.h>

#include <filesystem>
#include <iostream>
#include <set>

namespace fs = std::filesystem;
using SoftwareRequestedActivations = sdbusplus::xyz::openbmc_project::Software::
    server::Activation::RequestedActivations;

// known limitation: the Status is common for all objects in the same Version,
// only last one will be displayed
struct Version
{
    std::string version;
    std::string status;
    std::set<std::string> targets;
    std::vector<std::pair<std::string, std::string>> objects;
};

static auto bus = sdbusplus::bus::new_default();
static bool yesMode = false;

static const std::regex yesno("^\\s*(y|n|yes|no)(\\s+.*)?$", std::regex::icase);

bool confirm(const char* prompt = "Do you want to continue?")
{
    std::string answer;
    std::smatch match;

    while (true)
    {
        printf("%s [y/N]: ", prompt);
        if (std::getline(std::cin, answer))
        {
            if (answer.empty())
            {
                break;
            }

            if (std::regex_match(answer, match, yesno))
            {
                const auto& c = match[1].str()[0];
                return (c == 'y' || c == 'Y');
            }
        }
        else
        {
            break;
        }
    }

    return false;
}

std::map<std::string, Version> getVersions(const std::string& versionId,
                                           const std::string& target)
{
    SubTreeType objects;
    auto getObjects =
        bus.new_method_call(dbus::mapper::busName, dbus::mapper::path,
                            dbus::mapper::interface, dbus::mapper::subtree);
    getObjects.append(
        dbus::software::path, 0,
        std::array<std::string, 1>{dbus::software::activationIface});

    try
    {
        bus.call(getObjects).read(objects);
    }
    catch (const sdbusplus::exception::SdBusError& ex)
    {
        fprintf(stderr, "Error while calling GetSubTree: %s\n", ex.what());
        return std::map<std::string, Version>();
    }

    std::map<std::string, Version> versions;
    for (const auto& [path, objDict] : objects)
    {
        const std::string& owner = objDict.begin()->first;
        const Interfaces& ifaces = objDict.begin()->second;

        auto it = std::find(ifaces.begin(), ifaces.end(),
                            dbus::software::filepathIface);
        if (it == ifaces.end())
        {
            continue;
        }

        std::string verId;
        std::string ver;
        std::string status;
        std::set<std::string> targets;
        std::string inventoryItem;

        // version path could be in two forms:
        // '/xyz/openbmc_project/software/<id>' or
        // '/xyz/openbmc_project/software/<id>/<device>'. Handle both variants.
        fs::path p(path);
        verId = p.filename();
        std::string parent = p.parent_path().filename();
        if (parent != "software")
        {
            verId = parent;
        }
        if ((!versionId.empty()) && (verId != versionId))
        {
            continue;
        }
        DbusProperties data;
        auto getProperties = bus.new_method_call(owner.c_str(), path.c_str(),
                                                 dbus::properties::interface,
                                                 dbus::properties::getAll);
        getProperties.append("");

        try
        {
            bus.call(getProperties).read(data);
            for (const auto& [prop, value] : data)
            {
                if (prop == dbus::software::properties::version)
                {
                    ver = std::get<std::string>(value);
                }
                // TODO: add handling Purpose here if needed:
                // if (prop == dbus::software::properties::purpose)
                // {
                //     ...
                // }
                if (prop == dbus::software::properties::activation)
                {
                    status = std::get<std::string>(value);
                    const size_t pos = status.find_last_of(".");
                    if (pos != std::string::npos)
                    {
                        status.erase(0, pos + 1);
                    }
                }
                if (prop == dbus::association::assoc)
                {
                    std::vector<Association> assoc;
                    assoc = std::get<std::vector<Association>>(value);
                    for (const auto& [fwd, rev, objPath] : assoc)
                    {
                        if (fwd == "inventory")
                        {
                            inventoryItem = fs::path(objPath).filename();
                            break;
                        }
                    }
                }
            }
        }
        catch (const sdbusplus::exception::exception& ex)
        {
            fprintf(stderr, "Error while reading version information: %s\n",
                    ex.what());
            return std::map<std::string, Version>();
        }

        if ((inventoryItem.empty()) ||
            ((!target.empty()) && (inventoryItem != target)))
        {
            continue;
        }
        versions[verId].version = ver;
        versions[verId].status = status;
        versions[verId].targets.emplace(inventoryItem);
        versions[verId].objects.emplace_back(owner, path);
    }
    return versions;
}

bool printVersionsList(const std::string& versionId, const std::string& target)
{
    auto versions = getVersions(versionId, target);
    fprintf(stdout, "Software package ID\t\tSoftware "
                    "version\t\t\tStatus\t\tTarget inventory items\n");
    for (const auto& [verId, ver] : versions)
    {
        std::string inventory;
        for (std::string item : ver.targets)
        {
            if (!inventory.empty())
            {
                inventory += ", ";
            }
            inventory += item;
        }
        if (ver.version.length() <= 35)
        {
            fprintf(stdout, "%-24s\t%-35s\t%-10s\t%s\n", verId.c_str(),
                    ver.version.c_str(), ver.status.c_str(), inventory.c_str());
        }
        else
        {
            fprintf(stdout, "%-24s\t%-.32s...\t%-10s\t%s\n", verId.c_str(),
                    ver.version.c_str(), ver.status.c_str(), inventory.c_str());
        }
    }

    return true;
}

bool activateVersions(const std::string& versionId, const std::string& target)
{
    auto versions = getVersions(versionId, target);
    if (versions.empty())
    {
        fprintf(stderr, "Software package not found.\n");
        return false;
    }
    if (versions.size() > 1)
    {
        fprintf(stderr, "There are more then one software packages available.\n"
                        "Specify package Software ID to activate.\n");
        return false;
    }

    auto& ver = versions.begin()->second;

    if (!yesMode)
    {
        fprintf(stdout,
                "Followed inventory items would be updated to version '%s':\n",
                ver.version.c_str());
        for (const auto& item : ver.targets)
        {
            fprintf(stdout, "\t%s\n", item.c_str());
        }
        fprintf(stdout, "\t(%d targets in total)\n", ver.objects.size());
        if (!confirm())
        {
            return false;
        }
    }

    fprintf(stdout, "Updating started...\n");
    for (const auto& [owner, path] : ver.objects)
    {
        fprintf(stdout, "set activation for %s\n", path.c_str());
        DbusPropVariant data = sdbusplus::message::details::convert_to_string(
            SoftwareRequestedActivations::Active);
        auto setProperty = bus.new_method_call(owner.c_str(), path.c_str(),
                                               dbus::properties::interface,
                                               dbus::properties::set);
        setProperty.append(dbus::software::activationIface,
                           dbus::software::properties::reqActivation, data);

        try
        {
            bus.call(setProperty);
        }
        catch (const sdbusplus::exception::SdBusError& ex)
        {
            fprintf(stderr,
                    "Failed to set Software Requested Activation at %s: %s\n",
                    path.c_str(), ex.what());
            return false;
        }
    }

    // TODO: Wait until update process is finished.
    fprintf(stdout,
            "Firmware is updating, please don't power off the system.\n");
    return true;
}

/**
 * @brief Show help message
 *
 * @param app       application name
 */
static void showUsage(const char* app)
{
    fprintf(stderr, R"(
Usage: %s -l|-a [-i <ID>] [-t <item>]
    Tool to work with uploaded software packages.
Options:
  -l, --list                    Print list of uploaded software packages.
  -a, --activate                Activate software package.
  -i, --version-id <ID>         Select only packages with specific IDs.
  -t, --target <item>           Select only packages suitable to specified
                                Inventory Item.
  -h, --help                    Show this help.
)",
            app);
}

enum class AppMode
{
    ModeList,
    ModeActivate
};

/**
 * @brief Application entry point
 *
 * @return exit code
 */
int main(int argc, char* argv[])
{
    AppMode mode = AppMode::ModeList;
    std::string versionId;
    std::string target;
    bool showhelp = false;
    int ret = EXIT_FAILURE;

    /* Disable buffering on stdout */
    setvbuf(stdout, NULL, _IONBF, 0);

    const struct option opts[] = {
        {"list", no_argument, nullptr, 'l'},
        {"activate", no_argument, nullptr, 'a'},
        {"version-id", required_argument, nullptr, 'i'},
        {"target", required_argument, nullptr, 't'},
        {"help", no_argument, nullptr, 'h'},
        // --- end of array ---
        {nullptr, 0, nullptr, '\0'}};
    int c;
    while ((c = getopt_long(argc, argv, "lai:t:h", opts, nullptr)) != -1)
    {
        switch (c)
        {
            case 'l':
                mode = AppMode::ModeList;
                break;
            case 'a':
                mode = AppMode::ModeActivate;
                break;
            case 'i':
                versionId = optarg;
                break;
            case 't':
                target = optarg;
                break;
            case 'h':
                showhelp = true;
                break;
            default:
                fprintf(stderr, "Unknown option found '%c'!\n", c);
                showhelp = true;
                break;
        }
    }
    if (showhelp)
    {
        showUsage(argv[0]);
        return EXIT_FAILURE;
    }

    if (optind < argc)
    {
        fprintf(stderr, "Can't parse option '%s'!\n", argv[optind]);
        showUsage(argv[0]);
        return EXIT_FAILURE;
    }

    if (mode == AppMode::ModeList)
    {
        if (printVersionsList(versionId, target))
        {
            ret = EXIT_SUCCESS;
        }
    }
    else if (mode == AppMode::ModeActivate)
    {
        if (activateVersions(versionId, target))
        {
            ret = EXIT_SUCCESS;
        }
    }

    return ret;
}
