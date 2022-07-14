/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022, KNS Group LLC (YADRO)
 */

#pragma once

#include <sdbusplus/bus.hpp>
#include <sdbusplus/bus/match.hpp>

#include <functional>
#include <map>
#include <string>

/**
 * @class PowerState
 *
 * This class provides an interface to check the current power state,
 * and to register a function that gets called when there is a power
 * state change.  A callback can be passed in using the constructor,
 * or can be added later using addCallback().
 *
 * @note the code is based on power state monitor from phosphor-fan-presence
 */
class PowerState
{
  public:
    using StateChangeFunc = std::function<void(bool)>;

    virtual ~PowerState() = default;
    PowerState(const PowerState&) = delete;
    PowerState& operator=(const PowerState&) = delete;
    PowerState(PowerState&&) = delete;
    PowerState& operator=(PowerState&&) = delete;

    /**
     * @brief Constructor
     *
     * @param[in] bus - The D-Bus bus connection object
     * @param[in] callback - The function that should be run when
     *                       the power state changes
     */
    PowerState(sdbusplus::bus::bus& aBus, StateChangeFunc callback);

    /**
     * @brief Constructor
     *
     * Callbacks can be added with addCallback().
     */
    PowerState(sdbusplus::bus::bus& aBus);

    /**
     * @brief Adds a function to call when the power state changes
     *
     * @param[in] - Any unique name, so the callback can be removed later
     *              if desired.
     * @param[in] callback - The function that should be run when
     *                       the power state changes
     */
    void addCallback(const std::string& name, StateChangeFunc callback);

    /**
     * @brief Remove the callback so it is no longer called
     *
     * @param[in] name - The name used when it was added.
     */
    void deleteCallback(const std::string& name);

    /**
     * @brief Says if power is on
     *
     * @return bool - The power state
     */
    bool isPowerOn() const;

  private:
    /**
     * @brief Called by derived classes to set the power state value
     *
     * Will call the callback functions if the state changed.
     *
     * @param[in] state - The new power state
     */
    void setPowerState(bool state);

    /**
     * @brief PropertiesChanged callback for the CurrentHostState property.
     *
     * Will call the registered callback function if necessary.
     *
     * @param[in] msg - The payload of the propertiesChanged signal
     */
    void hostStateChanged(sdbusplus::message::message& msg);

    /**
     * @brief Reads the CurrentHostState property from D-Bus and saves it.
     */
    void readHostState();

    /**
     * @brief Reference to the D-Bus connection object.
     */
    sdbusplus::bus::bus& bus;

    enum class systemPowerState
    {
        Off,
        On,
        Unknown
    };
    /**
     * @brief The power state value
     */
    systemPowerState powerState = systemPowerState::Unknown;

    /**
     * @brief The callback functions to run when the power state changes
     */
    std::map<std::string, StateChangeFunc> callbacks;

    /** @brief The propertiesChanged match */
    std::optional<sdbusplus::bus::match::match> match;
};

/**
 * @brief lookup I2C bus using ChannelName, defined from EntityManager
 *
 * EntityManager allow to give names for I2C mux channels and create
 * corresponding symbolic links. This function enumerate this links to find
 * device file for specified Channel Name.
 *
 * @param[in] chanName - name of the channel to lookup
 * @return device file name if channel found (e.g. "/dev/i2c-20"), empty string
 *         on error
 */
std::string getBusByChanName(const std::string& chanName);

/**
 * @brief remove trailing special symbols from string
 *
 * @param[in] str - input string
 * @param[in] chars - list of symbols to trim
 * @return trimmed string
 */
std::string& rtrim(std::string& str,
                   const std::string& chars = "\t\n\v\f\r \xFF");
