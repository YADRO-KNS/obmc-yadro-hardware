/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022, KNS Group LLC (YADRO).
 */

#include "common/mmapfile.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <system_error>

namespace common
{

MappedMem::~MappedMem()
{
    munmap(addr, length);
}

MappedMem MappedMem::open(const std::string& filePath)
{
    int fd = ::open(filePath.c_str(), O_RDONLY);
    if (fd == -1)
    {
        throw std::system_error(errno, std::system_category(), "open failed");
    }

    auto size = lseek(fd, 0, SEEK_END);
    if (size == -1)
    {
        auto lseekErr = errno;
        close(fd);
        throw std::system_error(lseekErr, std::system_category(),
                                "lseek failed");
    }

    auto addr = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    auto mmapErrNo = errno;
    close(fd);

    if (addr == MAP_FAILED)
    {
        throw std::system_error(mmapErrNo, std::system_category(),
                                "mmap failed");
    }

    return MappedMem(addr, size);
}

} // namespace common
