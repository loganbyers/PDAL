/******************************************************************************
* Copyright (c) 2015, Brad Chambers (brad.chambers@gmail.com)
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

#include "MapboxKernel.hpp"

#include <boost/program_options.hpp>

#include <pdal/BufferReader.hpp>
#include <pdal/Options.hpp>
#include <pdal/pdal_macros.hpp>
#include <pdal/PointBuffer.hpp>
#include <pdal/PointContext.hpp>
#include <pdal/Stage.hpp>
#include <pdal/StageFactory.hpp>

namespace pdal
{

static PluginInfo const s_info = PluginInfo(
    "kernels.mapbox",
    "Mapbox Kernel",
    "http://pdal.io/kernels/kernels.mapbox.html" );

CREATE_STATIC_PLUGIN(1, 0, MapboxKernel, Kernel, s_info)

std::string MapboxKernel::getName() const { return s_info.name; }

void MapboxKernel::validateSwitches()
{
    if (m_inputFile == "")
        throw app_usage_error("--input/-i required");
 
    if (m_outputFile == "")
        throw app_usage_error("--output/-o required");
}


void MapboxKernel::addSwitches()
{
    po::options_description* file_options = new po::options_description("file options");

    file_options->add_options()
    ("input,i", po::value<std::string>(&m_inputFile)->default_value(""), "input file name")
    ("output,o", po::value<std::string>(&m_outputFile)->default_value(""), "output file name")
    ;

    addSwitchSet(file_options);

    addPositionalSwitch("input", 1);
    addPositionalSwitch("output", 1);
}


int MapboxKernel::execute()
{
    PointContext ctx;

    Options readerOptions;
    setCommonOptions(readerOptions);
    std::cerr << m_inputFile << std::endl;
    Stage& reader(Kernel::makeReader(m_inputFile));
    reader.setOptions(readerOptions);
/*
    StageFactory f;
    Stage& filter = ownStage(f.createStage("filters.mortonorder"));
    filter.setInput(reader);
    filter.prepare(ctx);
    PointBufferSet pbSetSorted = filter.execute(ctx);

    BufferReader sortedBufferReader;
    sortedBufferReader.addBuffer(*pbSetSorted.begin());

    // insert decimation
*/
    // setup the Writer and write the results
    Options writerOptions;
    setCommonOptions(writerOptions);

    std::cerr << m_outputFile << std::endl;
    Stage& writer(Kernel::makeWriter(m_outputFile, reader));
    //Stage& writer(Kernel::makeWriter(m_outputFile, sortedBufferReader));
    writer.setOptions(writerOptions);

    std::vector<std::string> cmd = getProgressShellCommand();
    UserCallback *callback =
        cmd.size() ? (UserCallback *)new ShellScriptCallback(cmd) :
        (UserCallback *)new HeartbeatCallback();

    writer.setUserCallback(callback);

    for (const auto& pi: getExtraStageOptions())
    {
        std::string name = pi.first;
        Options options = pi.second;
        std::vector<Stage*> stages = writer.findStage(name);
        for (const auto& s : stages)
        {
            Options opts = s->getOptions();
            for (const auto& o : options.getOptions())
                opts.add(o);
            s->setOptions(opts);
        }
    }

    writer.prepare(ctx);

    // process the data, grabbing the PointBufferSet for visualization of the
    // resulting PointBuffer
    PointBufferSet pbSetOut = writer.execute(ctx);

    if (isVisualize())
        visualize(*pbSetOut.begin());
    
    return 0;
}

} // pdal

