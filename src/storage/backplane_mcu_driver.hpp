/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022, KNS Group LLC (YADRO)
 */

#pragma once
#include "common_i2c.hpp"

#include <memory>

class BackplaneMCUDriver;
std::unique_ptr<BackplaneMCUDriver> backplaneMCU(std::string devPath, int addr);

enum class DriveTypes
{
    Unknown,
    NoDisk,
    SATA_SAS,
    NVMe
};

/**
 * @class BackplaneMCUDriver
 *
 * This class provides an interface to backplane MCUs. This is the base class
 * and should be subclassed with the actual implementation of the MCU protocol.
 */
class BackplaneMCUDriver
{
  public:
    BackplaneMCUDriver(const BackplaneMCUDriver&) = delete;
    BackplaneMCUDriver& operator=(const BackplaneMCUDriver&) = delete;
    BackplaneMCUDriver(BackplaneMCUDriver&&) = delete;
    BackplaneMCUDriver& operator=(BackplaneMCUDriver&&) = delete;

    /**
     * @brief Constructor
     *
     * The constructor initializes communication descriptor and test if device
     * accessibly.
     * @param[in] dev - I2C device to communicate with
     */
    BackplaneMCUDriver(std::unique_ptr<i2cDev> device) : dev(std::move(device))
    {}

    virtual std::string getFwVersion() = 0;
    virtual bool drivePresent(int chanIndex) = 0;
    virtual bool driveFailured(int chanIndex) = 0;
    virtual DriveTypes driveType(int chanIndex) = 0;
    virtual void setDriveLocationLED(int chanIndex, bool assert) = 0;
    virtual bool getDriveLocationLED(int chanIndex) = 0;
    virtual void resetDriveLocationLEDs() = 0;
    virtual void setHostPowerState(bool powered) = 0;
    virtual bool ifStateChanged(uint32_t& cache) = 0;

    static constexpr int maxChannelsNumber = 8;

  protected:
    std::unique_ptr<i2cDev> dev;
};

class MCUProtoV0 : public BackplaneMCUDriver
{
  public:
    MCUProtoV0(std::unique_ptr<i2cDev> device) :
        BackplaneMCUDriver(std::move(device))
    {}
    static uint8_t ident();

    std::string getFwVersion();
    bool drivePresent(int chanIndex);
    bool driveFailured(int chanIndex);
    DriveTypes driveType(int chanIndex);
    void setDriveLocationLED(int chanIndex, bool assert);
    bool getDriveLocationLED(int chanIndex);
    void resetDriveLocationLEDs();
    void setHostPowerState(bool powered);
    bool ifStateChanged(uint32_t& cache);

  private:
    void getDrivesPresence();
    void getDrivesFailures();

    int dPresence = -1;
    int dFailures = -1;
};

class MCUProtoV1 : public BackplaneMCUDriver
{
  public:
    MCUProtoV1(std::unique_ptr<i2cDev> device) :
        BackplaneMCUDriver(std::move(device))
    {}
    static uint8_t ident();

    std::string getFwVersion();
    bool drivePresent(int chanIndex);
    bool driveFailured(int chanIndex);
    DriveTypes driveType(int chanIndex);
    void setDriveLocationLED(int chanIndex, bool assert);
    bool getDriveLocationLED(int chanIndex);
    void resetDriveLocationLEDs();
    void setHostPowerState(bool powered);
    bool ifStateChanged(uint32_t& cache);

  private:
    void getDrivesPresence();
    void getDrivesFailures();
    void getDrivesType();
    uint8_t getDrivesLocate();

    int dPresence = -1;
    int dFailures = -1;
    int dTypes = -1;
};
