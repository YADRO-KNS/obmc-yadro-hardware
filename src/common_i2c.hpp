/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022, KNS Group LLC (YADRO)
 */

#pragma once

#include <string>

extern "C"
{
#include <i2c/smbus.h>
}

/**
 * @class i2cDev
 *
 * This class implements low level communication with I2C device
 */
class i2cDev
{
  public:
    i2cDev(const i2cDev&) = delete;
    i2cDev& operator=(const i2cDev&) = delete;
    i2cDev(i2cDev&&) = default;
    i2cDev& operator=(i2cDev&&) = default;

    /**
     * @brief Constructor
     *
     * The constructor initializes communication descriptor.
     * @param[in] devPath - I2C bus device file path (e.g. "/dev/i2c-20")
     * @param[in] addr - 7-bit I2C device address
     * @param[in] usePEC - If we should use PEC for communication
     */
    i2cDev(std::string devPath, int addr, bool usePEC = false);

    /**
     * @brief Destructor
     *
     * The destructor closes communication descriptor
     */
    ~i2cDev();

    /**
     * @brief If communication descriptor initialized successfully
     *
     * @return true if ok, false if any error appears during initialization
     */
    bool isOk() const
    {
        return ok;
    }

    int read_byte();
    int write_byte(uint8_t value);
    int read_byte_data(uint8_t command);
    int write_byte_data(uint8_t command, uint8_t value);
    int read_word_data(uint8_t command);
    int write_word_data(uint8_t command, uint16_t value);
    int read_i2c_block_data(uint8_t command, uint8_t length, uint8_t* values);
    int read_i2c_blob(uint8_t command, uint8_t length, uint8_t* values);

    std::string getDevLabel()
    {
        return deviceLabel;
    }

    static constexpr int i2cBlockSize = I2C_SMBUS_BLOCK_MAX;

  private:
    int devFD;
    int i2cAddr;
    bool ok;
    std::string deviceLabel;

    /**
     * @brief Log transaction with device
     *
     * @param[in] cmd - register address / smbus command
     * @param[in] txData - data, transferred to i2c device
     * @param[in] txDataLen - number of bytes transferred
     * @param[in] rxData - data, received from i2c device
     * @param[in] rxDataLen - number of bytes received
     * @param[in] res - result code
     */
    void logTransfer(uint8_t cmd, const void* txData, size_t txDataLen,
                     const void* rxData, size_t rxDataLen, int res);
};