/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022, KNS Group LLC (YADRO).
 */
#include "backplane_mcu_driver.hpp"
#include "common/mmapfile.hpp"

#include <gpiod.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>

namespace fs = std::filesystem;

/**
 * @brief Check MCU and reflash if required
 *
 * @param bus      - i2c-bus that MCU is located on
 * @param addr     - MCU's i2c address
 * @param force    - Interpret the MCU absence as an error
 * @param firmware - Path to firmware image
 * @param version  - Required frimware version
 */
static void updateMCU(int bus, int addr, bool force, const fs::path& firmware,
                      const std::string& version)
{
    std::string dev = "/dev/i2c-" + std::to_string(bus);
    std::unique_ptr<BackplaneMCUDriver> mcu;
    try
    {
        mcu = backplaneMCU(dev, addr);
    }
    catch (const std::exception& e)
    {
        if (force)
        {
            fprintf(stderr, "MCU_%d_%02X: unable to init, %s\n", bus, addr,
                    e.what());
        }
        return;
    }

    try
    {
        auto devType = mcu->getBoardType();
        auto fwVer = mcu->getFwVersion();

        printf("MCU_%d_%02X: type='%s', ver='%s'\n", bus, addr, devType.c_str(),
               fwVer.c_str());

        if (!version.empty() && version == fwVer)
        {
            printf("MCU_%d_%02X is running on the same version, reflashing "
                   "skipped.\n",
                   bus, addr);
            return;
        }
    }
    catch (const std::exception& e)
    {
        fprintf(stderr, "MCU_%d_%02X: Unable to query, %s\n", bus, addr,
                e.what());
        return;
    }

    try
    {
        if (!firmware.empty())
        {
            auto fw = common::MappedMem::open(firmware);

            static constexpr size_t minImageSize =
                64; // image can't be less then 64 bytes (header size)
            static constexpr size_t maxImageSize =
                128 * 1024; // image can't be more then 128 Kbytes
            if ((fw.size() < minImageSize) || (fw.size() > maxImageSize))
            {
                fprintf(stderr, "Incorrect '%s' size\n", firmware.c_str());
                return;
            }

            // NOTE: On some old versions of MCU firmware, this operation may
            //       lead to full erasing of the MCU flash chip.
            //       Fortunately, the boot loader on MCU cleans the flash during
            //       the boot, so this operation can be safely skipped.
            // mcu->eraseFlash();
            // std::this_thread::sleep_for(std::chrono::seconds(2));

            static constexpr size_t chunkSize =
                128; // max chunk size is 255 bytes, but it shall be 4-byte
                     // aligned

            try
            {
                for (size_t offset = 0; offset < fw.size(); offset += chunkSize)
                {
                    mcu->writeFlash(reinterpret_cast<char*>(fw.data()) + offset,
                                    std::min(chunkSize, fw.size() - offset));
                }
            }
            catch (...)
            {
                // NOTE: Force MCU reboot to clean up the flash chip.
                mcu->reboot();
                throw;
            }

            bool done = false;
            mcu->reboot();
            static constexpr auto numAttempts = 20;
            for (auto cnt = 0; !done && cnt < numAttempts; ++cnt)
            {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                done = mcu->ping();
            }

            if (done)
            {
                printf("MCU_%d_%02X: Flashed with '%s'\n", bus, addr,
                       firmware.c_str());

                // create new object since protocol may changed in new firmware
                try
                {
                    mcu = backplaneMCU(dev, addr);
                    printf("MCU_%d_%02X: After reflash: type='%s', ver='%s'\n",
                           bus, addr, mcu->getBoardType().c_str(),
                           mcu->getFwVersion().c_str());
                }
                catch (const std::exception& e)
                {
                    fprintf(stderr, "MCU_%d_%02X: Unable to requery, %s\n", bus,
                            addr, e.what());
                }
            }
            else
            {
                fprintf(stderr,
                        "MCU_%d_%02X doesn't bring back after "
                        "rebooting in %d seconds.\n",
                        bus, addr, numAttempts);
            }
        }
    }
    catch (const std::exception& e)
    {
        fprintf(stderr, "MCU_%d_%02X: Unable to flash %s, %s\n", bus, addr,
                firmware.c_str(), e.what());
    }
}

/**
 * @brief Try to find i2c bus that the gpio chip is located on
 *
 * @param chip - gpio chip
 *
 * @return i2c bus number
 */
static int findBus(const std::string& chip)
{
    for (const auto& entry : fs::directory_iterator("/sys/bus/i2c/devices"))
    {
        if (fs::exists(entry.path() / chip))
        {
            try
            {
                // The filename contains bus and addr. For example: 21-0010
                return std::stoi(entry.path().filename().string());
            }
            catch (const std::exception&)
            {
                // pass
            }
        }
    }
    return -1;
}

static constexpr size_t numberOfPins = 8;

/**
 * @brief Calculate value encoded by bits
 *
 * NOTE: It uses the reverse format when 0-th bit is the leftmost.
 *
 */
inline uint8_t calcShred(const std::string& bits)
{
    uint8_t value = 0;
    for (size_t i = 0; i < bits.size() && i < numberOfPins; ++i)
    {
        if (bits[i] == '0' || bits[i] == '1')
        {
            value |= ((bits[i] - '0') << (numberOfPins - 1 - i));
        }
        else
        {
            fprintf(stderr, "Incorrect bit value in '%s' at %zu position\n",
                    bits.c_str(), i);
            return static_cast<uint8_t>(-1);
        }
    }

    return value;
}

/**
 * @brief Convert bit values to string format
 */
inline std::string toString(const std::vector<int>& bits)
{
    std::string ret;
    ret.reserve(bits.size() + 1);
    for (const auto& bit : bits)
    {
        ret.push_back('0' + (bit & 0x01));
    }
    return ret;
}

/**
 * @brief Compare key with wildcards and read shred value
 *
 * @param key  - key value
 * @param bits - shred real value
 *
 * @return true if matched
 */
static bool compareShred(const std::string& key, const std::string& bits)
{
    const size_t keySz = key.length();
    const size_t bitsSz = bits.length();
    if (keySz != bitsSz)
    {
        return false;
    }

    for (size_t i = 0; i < keySz; ++i)
    {
        if (key[i] != '*' && key[i] != bits[i])
        {
            return false;
        }
    }

    return true;
}

class Reflasher
{
    using Definition = std::tuple<fs::path, std::string, std::vector<int>>;
    using Shred = std::tuple<std::string, std::string, int>;
    using ShredList = std::vector<Shred>;

  public:
    /**
     * @brief Load configuration
     *
     * @param path - path to config file
     */
    void loadConfig(const fs::path& path)
    {
        try
        {
            std::ifstream is(path);
            auto json = nlohmann::json::parse(is);

            static constexpr auto shred = "shred";

            if (json.is_object() && json.contains(shred) &&
                json[shred].is_object())
            {
                for (const auto& [key, info] : json[shred].items())
                {
                    static constexpr auto firmware = "firmware";
                    static constexpr auto version = "version";
                    static constexpr auto mcus = "mcus";

                    fs::path fwPath;
                    if (info.contains(firmware))
                    {
                        const auto& fwName = info[firmware].get<std::string>();
                        fwPath = path.parent_path() / fwName;
                        if (!fs::exists(fwPath))
                        {
                            fprintf(stderr,
                                    "Definition '%s': Image '%s' "
                                    "doesn't exists!\n",
                                    key.c_str(), fwName.c_str());
                            fwPath.clear();
                        }
                    }

                    auto fwVer = info.contains(version)
                                     ? info[version].get<std::string>()
                                     : "";
                    std::vector<int> mcuAddrs;

                    if (info.contains(mcus) && info[mcus].is_array())
                    {
                        for (const auto& addr : info[mcus])
                        {
                            mcuAddrs.emplace_back(addr.get<int>());
                        }
                    }

                    definitions.emplace(
                        key, std::make_tuple(fwPath, fwVer, mcuAddrs));
                }
            }
        }
        catch (const std::exception& e)
        {
            fprintf(stderr, "Load '%s' failed, %s\n", path.c_str(), e.what());
            definitions.clear();
        }
    }

    /**
     * @brief Search all MCUs and try to update them
     */
    void scan(void)
    {
        for (const auto& [shred, chip, bus] : findShreds())
        {
            const auto& [fwPath, fwVersion, mcuAddrs] = findDefinition(shred);

            printf("Found shred '%s' (0x%02X), chip='%s', bus=i2c-%d, fw=%s\n",
                   shred.c_str(), calcShred(shred), chip.c_str(), bus,
                   fwPath.empty() ? "N/A" : fwPath.filename().c_str());

            for (const auto& addr : mcuAddrs)
            {
                updateMCU(bus, addr, true, fwPath, fwVersion);
            }

            if (mcuAddrs.empty())
            {
                // Try to scan all possible addresses
                for (const auto& addr : {0x2a, 0x2b, 0x2c})
                {
                    updateMCU(bus, addr, false, fwPath, fwVersion);
                }
            }
        }
    }

  protected:
    /**
     * @brief Find definition brought by config file.
     *
     * @param shred - shred bits in string format
     */
    const Definition& findDefinition(const std::string& shred) const
    {
        for (const auto& [name, def] : definitions)
        {
            if (compareShred(name, shred))
            {
                return def;
            }
        }

        static Definition empty;
        return empty;
    }

    /**
     * @brief Find shred through all GPIO lines
     *
     * @return List of shred bits and i2c-bus where it's located.
     */
    ShredList findShreds(void)
    {
        ShredList shreds;

        for (const auto& chip : gpiod::make_chip_iter())
        {
            gpiod::line_bulk bulk;
            for (const auto& line : gpiod::line_iter(chip))
            {
                if (line.name().find("_SHRED_") != std::string::npos)
                {
                    bulk.append(line);
                }
            }

            if (bulk.empty())
            {
                continue;
            }

            if (bulk.size() != numberOfPins)
            {
                fprintf(stderr, "On chip '%s' found %d/%zu items.\n",
                        chip.name().c_str(), bulk.size(), numberOfPins);
                continue;
            }

            std::vector<int> bits;
            try
            {
                static const gpiod::line_request req{
                    "yadro-mcu-reflash", gpiod::line_request::DIRECTION_INPUT,
                    0};
                bulk.request(req);
                bits = bulk.get_values();
                bulk.release();
            }
            catch (const std::exception& e)
            {
                fprintf(stderr, "Unable to get values from %s pins, %s\n",
                        chip.name().c_str(), e.what());
                continue;
            }

            int bus = findBus(chip.name());
            auto strBits = toString(bits);

            if (bus < 0)
            {
                fprintf(stderr,
                        "Shred '%s' found at '%s', but no i2c-bus determined\n",
                        strBits.c_str(), chip.name().c_str());
                continue;
            }

            shreds.emplace_back(std::make_tuple(strBits, chip.name(), bus));
        }

        return shreds;
    }

  private:
    std::map<std::string, Definition> definitions;
};

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[])
{
    Reflasher reflasher;

    if (argc > 1)
    {
        reflasher.loadConfig(argv[1]);
    }

    reflasher.scan();

    return 0;
}
