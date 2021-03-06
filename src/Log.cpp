/******************************************************************************
* Copyright (c) 2011, Michael P. Gerlek (mpg@flaxen.com)
*
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following
* conditions are met:
*
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above copyright
*       notice, this list of conditions and the following disclaimer in
*       the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of Hobu, Inc. or Flaxen Geo Consulting nor the
*       names of its contributors may be used to endorse or promote
*       products derived from this software without specific prior
*       written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
* "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
* FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
* COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
* BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
* OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
* AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
* OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
* OF SUCH DAMAGE.
****************************************************************************/

#include <pdal/Log.hpp>
#include <pdal/PDALUtils.hpp>

#include <fstream>
#include <ostream>

namespace pdal
{

Log::Log(std::string const& leaderString,
         std::string const& outputName)
    : m_level(LogLevel::Error)
    , m_deleteStreamOnCleanup(false)
{

    makeNullStream();
    if (Utils::iequals(outputName, "stdlog"))
        m_log = &std::clog;
    else if (Utils::iequals(outputName, "stderr"))
        m_log = &std::cerr;
    else if (Utils::iequals(outputName, "stdout"))
        m_log = &std::cout;
    else if (Utils::iequals(outputName, "devnull"))
        m_log = m_nullStream;
    else
    {
        m_log = Utils::createFile(outputName);
        m_deleteStreamOnCleanup = true;
    }
    m_leaders.push(leaderString);
}


Log::Log(std::string const& leaderString,
         std::ostream* v)
    : m_level(LogLevel::Error)
    , m_deleteStreamOnCleanup(false)
{
    m_log = v;
    makeNullStream();
    m_leaders.push(leaderString);
}


Log::~Log()
{

    if (m_deleteStreamOnCleanup)
    {
        m_log->flush();
        delete m_log;
    }
    delete m_nullStream;
}


void Log::makeNullStream()
{
#ifdef _WIN32
    std::string nullFilename = "nul";
#else
    std::string nullFilename = "/dev/null";
#endif

    m_nullStream = new std::ofstream(nullFilename);
}


void Log::floatPrecision(int level)
{
    m_log->setf(std::ios_base::fixed, std::ios_base::floatfield);
    m_log->precision(level);
}


void Log::clearFloat()
{
    m_log->unsetf(std::ios_base::fixed);
    m_log->unsetf(std::ios_base::floatfield);
}


std::ostream& Log::get(LogLevel level)
{
    const auto incoming(Utils::toNative(level));
    const auto stored(Utils::toNative(m_level));
    const auto nativeDebug(Utils::toNative(LogLevel::Debug));
    if (incoming <= stored)
    {
        *m_log << "(" << leader() << " "<< getLevelString(level) <<": " <<
            incoming << "): " <<
            std::string(incoming < nativeDebug ? 0 : incoming - nativeDebug,
                    '\t');
        return *m_log;
    }
    return *m_nullStream;

}


std::string Log::getLevelString(LogLevel level) const
{
    switch (level)
    {
        case LogLevel::Error:
            return "Error";
            break;
        case LogLevel::Warning:
            return "Warning";
            break;
        case LogLevel::Info:
            return "Info";
            break;
        default:
            return "Debug";
    }
}

} // namespace
