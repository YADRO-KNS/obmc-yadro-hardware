/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022 YADRO.
 */

#include "backplane_mcu_driver.hpp"
#include "dbus.hpp"

#include <getopt.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <thread>

#define EXIT_UPDATE_FAILED 10
static bool showProgress = false;
static bool forceErase = false;

/**
 * @brief Invoke backplane MCU firmware update
 *
 * The update function will read the file and write it to MCU flash. After the
 * image uploaded the function sends MCU reset command to apply new firmware.
 * When MCU ready to operate again, new version and device type would be tested
 * to ensure Firmware operation succeed. If \p expectedVersion is not provided
 * (empty string), the check is skipped.
 *
 * @param imagePath         path to firmware image binary file, which will be
 *                          written to the MCU
 * @param i2cBusDev         path to device file for I2C bus
 * @param i2cAddr           device address on I2C bus
 * @param expectedVersion   version of the new firmware
 * @return firmware update status
 */

bool runImageUpdate(std::string& imagePath, std::string& i2cBusDev, int i2cAddr,
                    std::string& expectedVersion)
{
    try
    {
        size_t imageSize = std::filesystem::file_size(imagePath);
        static constexpr size_t minImageSize =
            64; // image can't be less then 64 bytes (header size)
        static constexpr size_t maxImageSize =
            128 * 1024; // image can't be more then 128 Kbytes
        if ((imageSize < minImageSize) || (imageSize > maxImageSize))
        {
            fprintf(stderr, "Incorrect firmware image size: %d bytes\n",
                    imageSize);
            return false;
        }
        std::ifstream file(imagePath, std::ios::binary);
        if (!file.good())
        {
            fprintf(stderr, "Failed to open firmware file: %s\n",
                    imagePath.c_str());
            return false;
        }
        auto mcu = backplaneMCU(i2cBusDev, i2cAddr);
        auto fwVer = mcu->getFwVersion();
        auto devType = mcu->getBoardType();

        if (showProgress)
        {

            fprintf(stdout, R"(
  Device type:              %s
  Current firmware version: %s
  New firmware version:     %s
  Firmware image path:      %s
)",
                    devType.c_str(), fwVer.c_str(), expectedVersion.c_str(),
                    imagePath.c_str());
        }

        if (forceErase)
        {
            if (showProgress)
            {
                fprintf(stdout, "Erase MCU fw update flash area...\n");
            }
            mcu->eraseFlash();
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }

        static constexpr size_t chunkSize =
            128; // max chunk size is 255 bytes, but it shall be 4-byte aligned
        std::array<char, chunkSize> buf;
        size_t bytesRead = 0;
        try
        {
            while (file.good() && !file.eof())
            {
                file.read(buf.data(), buf.size());
                size_t bytes = file.gcount();
                if (bytes == 0)
                {
                    break;
                }
                mcu->writeFlash(buf.data(), bytes);
                bytesRead += bytes;
                float progress = (bytesRead * 100.0) / imageSize;
                if (showProgress)
                {
                    fprintf(stdout,
                            "wrote %.2f%% (%d of %d bytes, chunk size %d)\n",
                            progress, bytesRead, imageSize, bytes);
                }
            }
        }
        catch (...)
        {
            if (!forceErase)
            {
                // NOTE: Enforces the MCU's boot loader to clean the flash up.
                mcu->reboot();
            }
            throw;
        }

        mcu->reboot();
        for (int cnt = 0; cnt < 20; cnt++)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (mcu->ping())
            {
                break;
            }
        }
        // create new object since protocol may changed in new firmware
        mcu = backplaneMCU(i2cBusDev, i2cAddr);
        fwVer = mcu->getFwVersion();
        devType = mcu->getBoardType();

        if (showProgress)
        {
            fprintf(stdout, R"(
  Device type:              %s
  Firmware version:         %s
)",
                    devType.c_str(), fwVer.c_str());
        }
        if (fwVer.empty() || devType.empty())
        {
            fprintf(stderr, "Can not read device information\n");
            return false;
        }
        if ((!expectedVersion.empty()) && (fwVer != expectedVersion))
        {
            fprintf(stderr,
                    "Firmware version mismatched: expected '%s', read '%s'\n",
                    expectedVersion.c_str(), fwVer.c_str());
            return false;
        }

        /// TODO: add cleanup (software objects removal) to software manager
    }
    catch (std::exception const& ex)
    {
        fprintf(stderr, "%s\n", ex.what());
        return false;
    }
    catch (...)
    {
        return false;
    }
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
Usage: %s [-pE] -f <path> -b <device path> -a <addr> [-v <version>] [-d <object>]
    Update backplane MCU firmware.
Options:
  -f, --file <path>         Firmware image binary file path.
  -b, --bus <device path>   Path to I2C bus device (e.g. /dev/i2c-1).
  -a, --addr <addr>         I2C device address of the target MCU.
  -E, --force-erase         Send erase-flash command to MCU.
  -v, --version <version>   Version of the new software image. If specified
                            will be compared after flashing to ensure update
                            succeed.
  -p, --progress            Print firmware update progress.
  -h, --help                Show this help.
)",
            app);
}

/**
 * @brief Application entry point
 *
 * @return exit code
 */
int main(int argc, char* argv[])
{
    std::string firmwareFile;
    std::string i2cBusDev;
    int i2cAddr = 0;
    std::string expectedVersion;
    bool showhelp = false;
    int ret = EXIT_FAILURE;

    const struct option opts[] = {{"file", required_argument, nullptr, 'f'},
                                  {"bus", required_argument, nullptr, 'b'},
                                  {"addr", required_argument, nullptr, 'a'},
                                  {"force-erase", no_argument, nullptr, 'E'},
                                  {"version", required_argument, nullptr, 'v'},
                                  {"progress", no_argument, nullptr, 'p'},
                                  {"help", no_argument, nullptr, 'h'},
                                  // --- end of array ---
                                  {nullptr, 0, nullptr, '\0'}};
    int c;
    while ((c = getopt_long(argc, argv, "f:b:a:v:eph", opts, nullptr)) != -1)
    {
        switch (c)
        {
            case 'f':
                firmwareFile = optarg;
                break;
            case 'b':
                i2cBusDev = optarg;
                break;
            case 'a':
                i2cAddr = std::stoi(optarg, nullptr, 0);
                if ((i2cAddr == 0) || (i2cAddr > 0x7f))
                {
                    fprintf(stderr,
                            "\nAddress should be between 0x01 and 0x7F, but "
                            "given 0x%02X\n",
                            i2cAddr);
                    showhelp = true;
                }
                break;
            case 'E':
                forceErase = true;
                break;
            case 'v':
                expectedVersion = optarg;
                break;
            case 'p':
                showProgress = true;
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
    if (firmwareFile.empty())
    {
        fprintf(stderr, "--file option must be specified!\n");
        return EXIT_FAILURE;
    }
    if (i2cBusDev.empty())
    {
        fprintf(stderr, "--bus option must be specified!\n");
        return EXIT_FAILURE;
    }
    if (i2cAddr == 0)
    {
        fprintf(stderr, "--addr option must be specified!\n");
        return EXIT_FAILURE;
    }

    printf("Update firmware in MCU at %s, addr 0x%02X\n", i2cBusDev.c_str(),
           i2cAddr);
    if (runImageUpdate(firmwareFile, i2cBusDev, i2cAddr, expectedVersion))
    {
        fprintf(stdout, "Firmware updated!\n");
        ret = EXIT_SUCCESS;
    }
    else
    {
        fprintf(stderr, "Firmware update failed!\n");
        ret = EXIT_UPDATE_FAILED;
    }

    return ret;
}
