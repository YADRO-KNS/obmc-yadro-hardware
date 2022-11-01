/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022, KNS Group LLC (YADRO)
 */

#include "common_i2c.hpp"

#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <phosphor-logging/log.hpp>

#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>

using namespace phosphor::logging;
static constexpr int retryCount = 3;

bool i2cDev::verbose = false;

i2cDev::i2cDev(std::string devPath, int addr, bool usePEC) :
    devFD(-1), i2cAddr(addr), ok(false)
{
    int res;
    std::stringstream ss;
    ss << devPath << ", 0x" << std::setfill('0') << std::setw(2) << std::hex
       << addr;
    deviceLabel = ss.str();

    devFD = open(devPath.c_str(), O_RDWR);
    if (devFD < 0)
    {
        log<level::ERR>("Failed to open I2C bus",
                        entry("PATH=%s", devPath.c_str()),
                        entry("ADDR=%d", addr));
        return;
    }

    // check i2c adapter capabilities
    unsigned long funcs = 0;
    res = ioctl(devFD, I2C_FUNCS, &funcs);
    if (res < 0)
    {
        log<level::ERR>("Error in I2C_FUNCS", entry("PATH=%s", devPath.c_str()),
                        entry("ADDR=%d", addr), entry("RESULT=%d", res),
                        entry("REASON=%s", std::strerror(-res)));
        return;
    }
    if (!((funcs & I2C_FUNC_SMBUS_BYTE_DATA) &&
          (funcs & I2C_FUNC_SMBUS_I2C_BLOCK) && (funcs & I2C_FUNC_SMBUS_PEC)))
    {
        log<level::ERR>("I2C bus does not support required operations",
                        entry("PATH=%s", devPath.c_str()),
                        entry("ADDR=%d", addr), entry("FUNC=%u", funcs));
        return;
    }

    // select i2c device on the bus
    res = ioctl(devFD, I2C_SLAVE, addr);
    if (res < 0)
    {
        log<level::ERR>("Error in select slave",
                        entry("PATH=%s", devPath.c_str()),
                        entry("ADDR=%d", addr), entry("RESULT=%d", res),
                        entry("REASON=%s", std::strerror(-res)));
        return;
    }

    // enable PEC
    if (usePEC)
    {
        res = ioctl(devFD, I2C_PEC, 1);
        if (res < 0)
        {
            log<level::ERR>("Could not set PEC",
                            entry("PATH=%s", devPath.c_str()),
                            entry("ADDR=%d", addr), entry("RESULT=%d", res),
                            entry("REASON=%s", std::strerror(-res)));
            return;
        }
    }
    ok = true;
}
i2cDev::~i2cDev()
{
    if (devFD >= 0)
    {
        close(devFD);
    }
}

int i2cDev::read_byte()
{
    int res = -1;
    for (int rtr = 0; (rtr < retryCount) && (res < 0); rtr++)
    {
        res = i2c_smbus_read_byte(devFD);
    }
    logTransfer(-1, nullptr, 0, &res, 1, res);
    return res;
}
int i2cDev::write_byte(uint8_t value)
{
    int res = -1;
    for (int rtr = 0; (rtr < retryCount) && (res < 0); rtr++)
    {
        res = i2c_smbus_write_byte(devFD, value);
    }
    logTransfer(-1, &value, 1, nullptr, 0, res);
    return res;
}
int i2cDev::read_byte_data(uint8_t command)
{
    int res = -1;
    for (int rtr = 0; (rtr < retryCount) && (res < 0); rtr++)
    {
        res = i2c_smbus_read_byte_data(devFD, command);
    }
    logTransfer(command, nullptr, 0, &res, 1, res);
    return res;
}
int i2cDev::write_byte_data(uint8_t command, uint8_t value)
{
    int res = -1;
    for (int rtr = 0; (rtr < retryCount) && (res < 0); rtr++)
    {
        res = i2c_smbus_write_byte_data(devFD, command, value);
    }
    logTransfer(command, &value, 1, nullptr, 0, res);
    return res;
}
int i2cDev::read_word_data(uint8_t command)
{
    int res = -1;
    for (int rtr = 0; (rtr < retryCount) && (res < 0); rtr++)
    {
        res = i2c_smbus_read_word_data(devFD, command);
    }
    logTransfer(command, nullptr, 0, &res, 2, res);
    return res;
}
int i2cDev::write_word_data(uint8_t command, uint16_t value)
{
    int res = -1;
    for (int rtr = 0; (rtr < retryCount) && (res < 0); rtr++)
    {
        res = i2c_smbus_write_word_data(devFD, command, value);
    }
    logTransfer(command, &value, 2, nullptr, 0, res);
    return res;
}
int i2cDev::read_i2c_block_data(uint8_t command, uint8_t length,
                                uint8_t* values)
{
    int res = -1;
    for (int rtr = 0; (rtr < retryCount) && (res < 0); rtr++)
    {
        res = i2c_smbus_read_i2c_block_data(devFD, command, length, values);
    }
    logTransfer(command, nullptr, 0, values, length, res);
    return res;
}

constexpr int i2cFlagWrite = 0;
constexpr int i2cFlagRead = 1;

int i2cDev::read_i2c_blob(uint8_t length, uint8_t* values)
{
    struct i2c_rdwr_ioctl_data i2c_req;
    struct i2c_msg messages[1];

    messages[0].addr = i2cAddr;
    messages[0].flags = i2cFlagRead;
    messages[0].len = length;
    messages[0].buf = values;

    i2c_req.msgs = messages;
    i2c_req.nmsgs = 1;

    int res = -1;
    for (int rtr = 0; (rtr < retryCount) && (res < 0); rtr++)
    {
        res = ioctl(devFD, I2C_RDWR, &i2c_req);
    }
    logTransfer(-1, nullptr, 0, values, length, res);
    return res;
}

int i2cDev::read_i2c_blob(uint8_t command, uint8_t length, uint8_t* values)
{
    struct i2c_rdwr_ioctl_data i2c_req;
    struct i2c_msg messages[2];
    unsigned char write_buf[1] = {command};

    messages[0].addr = i2cAddr;
    messages[0].flags = i2cFlagWrite;
    messages[0].len = 1;
    messages[0].buf = write_buf;

    messages[1].addr = i2cAddr;
    messages[1].flags = i2cFlagRead;
    messages[1].len = length;
    messages[1].buf = values;

    i2c_req.msgs = messages;
    i2c_req.nmsgs = 2;

    int res = -1;
    for (int rtr = 0; (rtr < retryCount) && (res < 0); rtr++)
    {
        res = ioctl(devFD, I2C_RDWR, &i2c_req);
    }
    logTransfer(command, nullptr, 0, values, length, res);
    return res;
}

int i2cDev::write_i2c_blob(uint8_t length, uint8_t* values)
{
    struct i2c_rdwr_ioctl_data i2c_req;
    struct i2c_msg messages[1];

    messages[0].addr = i2cAddr;
    messages[0].flags = i2cFlagWrite;
    messages[0].len = length;
    messages[0].buf = values;

    i2c_req.msgs = messages;
    i2c_req.nmsgs = 1;

    int res = -1;
    for (int rtr = 0; (rtr < retryCount) && (res < 0); rtr++)
    {
        res = ioctl(devFD, I2C_RDWR, &i2c_req);
    }
    logTransfer(-1, values, length, nullptr, 0, res);
    return res;
}

int i2cDev::write_i2c_blob(uint8_t command, uint8_t length, uint8_t* values)
{
    struct i2c_rdwr_ioctl_data i2c_req;
    struct i2c_msg messages[1];
    unsigned char write_buf[1 + length];
    write_buf[0] = command;
    std::memcpy(write_buf + 1, values, length);

    messages[0].addr = i2cAddr;
    messages[0].flags = i2cFlagWrite;
    messages[0].len = sizeof(write_buf);
    messages[0].buf = write_buf;

    i2c_req.msgs = messages;
    i2c_req.nmsgs = 1;

    int res = -1;
    for (int rtr = 0; (rtr < retryCount) && (res < 0); rtr++)
    {
        res = ioctl(devFD, I2C_RDWR, &i2c_req);
    }
    logTransfer(command, values, length, nullptr, 0, res);
    return res;
}

int i2cDev::i2c_transfer(uint8_t tx_len, uint8_t* tx_data, uint8_t rx_len,
                         uint8_t* rx_data)
{
    struct i2c_rdwr_ioctl_data i2c_req;
    struct i2c_msg messages[2];

    messages[0].addr = i2cAddr;
    messages[0].flags = i2cFlagWrite;
    messages[0].len = tx_len;
    messages[0].buf = tx_data;

    messages[1].addr = i2cAddr;
    messages[1].flags = i2cFlagRead;
    messages[1].len = rx_len;
    messages[1].buf = rx_data;

    i2c_req.msgs = messages;
    i2c_req.nmsgs = 2;

    int res = -1;
    for (int rtr = 0; (rtr < retryCount) && (res < 0); rtr++)
    {
        res = ioctl(devFD, I2C_RDWR, &i2c_req);
    }
    logTransfer(-1, tx_data, tx_len, rx_data, rx_len, res);
    return res;
}

bool i2cDev::isSpamingToLog(int res, std::stringstream& ss)
{
    const decltype(numLogErrors) maxLogErrors = 3;
    if (res > 0)
    {
        numLogErrors = 0;
        return false;
    }

    if (numLogErrors == maxLogErrors)
    {
        ss << "... (Detected multiple errors. Stoping spam to log.)";
        log<level::ERR>(ss.str().c_str());
    }

    if (numLogErrors < std::numeric_limits<decltype(numLogErrors)>::max())
    {
        numLogErrors++;
    }

    return (numLogErrors > maxLogErrors);
}

void i2cDev::logTransfer(int cmd, const void* txData, size_t txDataLen,
                         const void* rxData, size_t rxDataLen, int res)
{
    std::stringstream ss;
    ss << deviceLabel;
    if (res >= 0)
    {
        ss << " <ok>";
    }
    else
    {
        ss << " <FAILED (" << std::setw(1) << std::dec << res << ")!>";
    }

    //  stop spaming on multiple logs
    if (isSpamingToLog(res, ss))
    {
        return;
    }

    if (cmd >= 0)
    {
        ss << " CMD: " << std::setfill('0') << std::setw(2) << std::hex
           << static_cast<int>(cmd);
    }
    if (txData)
    {
        ss << " TX (" << std::setw(1) << std::dec << txDataLen << "):";
        for (const unsigned char* ptr =
                 reinterpret_cast<const unsigned char*>(txData);
             txDataLen > 0; txDataLen--, ptr++)
        {
            ss << " " << std::setfill('0') << std::setw(2) << std::hex
               << static_cast<int>(*ptr);
        }
    }
    if (rxData)
    {
        ss << " RX (" << std::setw(1) << std::dec << rxDataLen << "):";
        for (const unsigned char* ptr =
                 reinterpret_cast<const unsigned char*>(rxData);
             rxDataLen > 0; rxDataLen--, ptr++)
        {
            ss << " " << std::setfill('0') << std::setw(2) << std::hex
               << static_cast<int>(*ptr);
        }
    }

    if (res < 0)
    {
        log<level::ERR>(ss.str().c_str());
    }
    else if (i2cDev::verbose)
    {
        log<level::DEBUG>(ss.str().c_str());
    }
}
