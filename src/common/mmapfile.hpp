/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022, KNS Group LLC (YADRO).
 */
#pragma once

#include <cstddef>
#include <string>

namespace common
{

/**
 * @brief RAII wrapper for mmap()/munmap()
 */
struct MappedMem
{
    MappedMem() = delete;
    MappedMem(const MappedMem&) = delete;
    MappedMem& operator=(const MappedMem&) = delete;
    MappedMem(MappedMem&&) = default;
    MappedMem& operator=(MappedMem&&) = default;

    virtual ~MappedMem();

    inline void* data() const
    {
        return addr;
    }
    inline size_t size() const
    {
        return length;
    }
    inline operator bool() const
    {
        return addr != nullptr;
    }

    /**
     * @brief Map specified file into memory
     *
     * @param filePath - path to file
     *
     * @return MappedMem object with file content.
     */
    static MappedMem open(const std::string& filePath);

  protected:
    MappedMem(void* addr, size_t length) : addr(addr), length(length)
    {}

  private:
    void* addr = nullptr;
    size_t length = 0;
};

} // namespace common
