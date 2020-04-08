/*
 * Distributed under the OSI-approved Apache License, Version 2.0.  See
 * accompanying file Copyright.txt for details.
 *
 * FileDrainer.cpp
 *
 *  Created on: April 1, 2020
 *      Author: Norbert Podhorszki <pnorbert@ornl.gov>
 */

#include "FileDrainer.h"

#include <chrono>
#include <cstring> // std::memcpy
#include <thread>  // std::this_thread::sleep_for

/// \cond EXCLUDE_FROM_DOXYGEN
#include <ios> //std::ios_base::failure
/// \endcond

namespace adios2
{
namespace burstbuffer
{

FileDrainOperation::FileDrainOperation(DrainOperation op,
                                       const std::string &fromFileName,
                                       const std::string &toFileName,
                                       size_t countBytes, size_t fromOffset,
                                       size_t toOffset, const void *data)
: op(op), fromFileName(fromFileName), toFileName(toFileName),
  countBytes(countBytes), fromOffset(fromOffset), toOffset(toOffset)
{
    if (data)
    {
        dataToWrite.resize(countBytes);
        std::memcpy(dataToWrite.data(), data, countBytes);
    };
}

/*FileDrainOperation::FileDrainOperation(std::string &toFileName,
                                       size_t countBytes, size_t toOffset, )
: op(DrainOperation::Write), fromFileName(""), toFileName(toFileName),
  countBytes(countBytes), append(false), fromOffset(0), toOffset(toOffset){

};*/

FileDrainer::FileDrainer() {}

FileDrainer::~FileDrainer() {}

void FileDrainer::AddOperation(FileDrainOperation &operation)
{
    std::lock_guard<std::mutex> lockGuard(operationsMutex);
    operations.push(operation);
}

void FileDrainer::AddOperation(DrainOperation op,
                               const std::string &fromFileName,
                               const std::string &toFileName, size_t fromOffset,
                               size_t toOffset, size_t countBytes,
                               const void *data)
{
    FileDrainOperation operation(op, fromFileName, toFileName, countBytes,
                                 fromOffset, toOffset, data);
    std::lock_guard<std::mutex> lockGuard(operationsMutex);
    operations.push(operation);
}

void FileDrainer::AddOperationSeekEnd(const std::string &toFileName)
{
    std::string emptyStr;
    AddOperation(DrainOperation::SeekEnd, emptyStr, toFileName, 0, 0, 0);
}
void FileDrainer::AddOperationCopyAt(const std::string &fromFileName,
                                     const std::string &toFileName,
                                     size_t fromOffset, size_t toOffset,
                                     size_t countBytes)
{
    AddOperation(DrainOperation::CopyAt, fromFileName, toFileName, fromOffset,
                 toOffset, countBytes);
}
void FileDrainer::AddOperationCopy(const std::string &fromFileName,
                                   const std::string &toFileName,
                                   size_t countBytes)
{
    AddOperation(DrainOperation::Copy, fromFileName, toFileName, 0, 0,
                 countBytes);
}

void FileDrainer::AddOperationWriteAt(const std::string &toFileName,
                                      size_t toOffset, size_t countBytes,
                                      const void *data)
{
    std::string emptyStr;
    AddOperation(DrainOperation::WriteAt, emptyStr, toFileName, 0, toOffset,
                 countBytes, data);
}

void FileDrainer::AddOperationWrite(const std::string &toFileName,
                                    size_t countBytes, const void *data)
{
    std::string emptyStr;
    AddOperation(DrainOperation::Write, emptyStr, toFileName, 0, 0, countBytes,
                 data);
}

void FileDrainer::AddOperationOpen(const std::string &toFileName, Mode mode)
{
    std::string emptyStr;
    if (mode == Mode::Write)
    {
        AddOperation(DrainOperation::Create, emptyStr, toFileName, 0, 0, 0);
    }
    else if (mode == Mode::Append)
    {
        AddOperation(DrainOperation::Open, emptyStr, toFileName, 0, 0, 0);
    }
    else
    {
        throw std::runtime_error(
            "ADIOS Coding ERROR: FileDrainer::AddOperationOpen() only supports "
            "Write and Append modes\n");
    }
}

InputFile FileDrainer::GetFileForRead(const std::string &path)
{
    auto it = m_InputFileMap.find(path);
    if (it != m_InputFileMap.end())
    {
        return it->second;
    }
    else
    {
        InputFile f = std::make_shared<std::ifstream>();
        m_InputFileMap.emplace(path, f);
        Open(f, path);
        return f;
    }
}

OutputFile FileDrainer::GetFileForWrite(const std::string &path, bool append)
{
    auto it = m_OutputFileMap.find(path);
    if (it != m_OutputFileMap.end())
    {
        return it->second;
    }
    else
    {
        OutputFile f = std::make_shared<std::ofstream>();
        m_OutputFileMap.emplace(path, f);
        Open(f, path, append);
        return f;
    }
}

void FileDrainer::Open(InputFile f, const std::string &path)
{

    f->rdbuf()->pubsetbuf(0, 0);
    f->open(path, std::ios::in);
}

void FileDrainer::Open(OutputFile f, const std::string &path, bool append)
{

    if (append)
    {
        f->rdbuf()->pubsetbuf(0, 0);
        f->open(path, std::ios::out | std::ios::app | std::ios::binary);
    }
    else
    {
        f->rdbuf()->pubsetbuf(0, 0);
        f->open(path, std::ios::out | std::ios::trunc | std::ios::binary);
    }
}

void FileDrainer::Close(InputFile f) { f->close(); }
void FileDrainer::Close(OutputFile f) { f->close(); }

bool FileDrainer::Good(InputFile f) { return (f->good()); }
bool FileDrainer::Good(OutputFile f) { return (f->good()); }

void FileDrainer::CloseAll()
{
    for (auto it = m_OutputFileMap.begin(); it != m_OutputFileMap.end(); ++it)
    {
        if (it->second->good())
        {
            Close(it->second);
        }
        m_OutputFileMap.erase(it);
    }
    for (auto it = m_InputFileMap.begin(); it != m_InputFileMap.end(); ++it)
    {
        if (it->second->good())
        {
            Close(it->second);
        }
        m_InputFileMap.erase(it);
    }
}

void FileDrainer::Seek(InputFile f, size_t offset, const std::string &path)
{
    f->seekg(offset, std::ios_base::beg);
}

void FileDrainer::Seek(OutputFile f, size_t offset, const std::string &path)
{
    f->seekp(offset, std::ios_base::beg);
}

void FileDrainer::SeekEnd(OutputFile f) { f->seekp(0, std::ios_base::end); }

std::pair<size_t, double> FileDrainer::Read(InputFile f, size_t count,
                                            char *buffer,
                                            const std::string &path)
{
    size_t totalRead = 0;
    double totalSlept = 0.0;
    const double sleepUnit = 0.01; // seconds
    while (count > 0)
    {
        f->read(buffer, static_cast<std::streamsize>(count));
        const auto readSize = f->gcount();

        if (readSize < static_cast<std::streamsize>(count))
        {
            if (f->eof())
            {
                std::chrono::duration<double> d(sleepUnit);
                std::this_thread::sleep_for(d);
                f->clear(f->rdstate() & ~std::fstream::eofbit);
                totalSlept += sleepUnit;
            }
            else
            {
                throw std::ios_base::failure(
                    "FileDrainer couldn't read from file " + path +
                    " count = " + std::to_string(count) + " bytes but only " +
                    std::to_string(readSize) + "\n");
                break;
            }
        }
        buffer += readSize;
        count -= readSize;
        totalRead += readSize;
    }
    return std::pair<size_t, double>(totalRead, totalSlept);
}

size_t FileDrainer::Write(OutputFile f, size_t count, const char *buffer,
                          const std::string &path)
{
    size_t totalWritten = 0;
    f->write(buffer, static_cast<std::streamsize>(count));

    if (f->bad())
    {
        throw std::ios_base::failure(
            "FileDrainer couldn't write to file " + path +
            " count = " + std::to_string(count) + " bytes\n");
    }

    return count;
}

void FileDrainer::SetVerbose(int verboseLevel, int rank)
{
    m_Verbose = verboseLevel;
    m_Rank = rank;
}

} // end namespace burstbuffer
} // end namespace adios2
