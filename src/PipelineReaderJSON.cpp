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

#include <pdal/PipelineReaderJSON.hpp>

#include <pdal/Filter.hpp>
#include <pdal/PipelineManager.hpp>
#include <pdal/Options.hpp>
#include <pdal/util/FileUtils.hpp>
#include <pdal/util/Utils.hpp>

#include <json/json.h>
#include <json/json-forwards.h>

#include <memory>
#include <vector>

#ifndef _WIN32
#include <wordexp.h>
#endif

namespace pdal
{

// ------------------------------------------------------------------------

// this class helps keep tracks of what child nodes we've seen, so we
// can keep all the error checking in one place
class PipelineReaderJSON::StageParserContext
{
public:
    enum Cardinality { None, One, Many };

    StageParserContext()
        : m_numTypes(0)
        , m_cardinality(One)
        , m_numStages(0)
    {}

    void setCardinality(Cardinality cardinality)
    {
        m_cardinality = cardinality;
    }

    void addType()
    {
        ++m_numTypes;
    }

    int getNumTypes()
    {
        return m_numTypes;
    }

    void addStage()
    {
        ++m_numStages;
    }

    void addUnknown(const std::string& name)
    {
        throw pdal_error("unknown child of element: " + name);
    }

    void validate()
    {
        if (m_numTypes == 0)
            throw pdal_error("PipelineReaderJSON: expected Type element missing");
        if (m_numTypes > 1)
            throw pdal_error("PipelineReaderJSON: extra Type element found");

        if (m_cardinality == None)
        {
            if (m_numStages != 0)
                throw pdal_error("PipelineReaderJSON: found child stages where "
                                 "none were expected");
        }
        if (m_cardinality == One)
        {
            if (m_numStages == 0)
                throw pdal_error("PipelineReaderJSON: "
                                 "expected child stage missing");
            if (m_numStages > 1)
                throw pdal_error("PipelineReaderJSON: extra child stages found");
        }
        if (m_cardinality == Many)
        {
            if (m_numStages == 0)
                throw pdal_error("PipelineReaderJSON: expected child stage "
                                 "missing");
        }
    }

private:
    int m_numTypes;
    Cardinality m_cardinality; // num child stages allowed
    int m_numStages;
};


PipelineReaderJSON::PipelineReaderJSON(PipelineManager& manager, bool isDebug,
                               uint32_t verboseLevel) :
    m_manager(manager) , m_isDebug(isDebug) , m_verboseLevel(verboseLevel)
{
    if (m_isDebug)
    {
        Option opt("debug", true);
        m_baseOptions.add(opt);
    }
    if (m_verboseLevel)
    {
        Option opt("verbose", m_verboseLevel);
        m_baseOptions.add(opt);
    }
}


Option PipelineReaderJSON::parseElement_Option(const std::string& name, const Json::Value& tree)
{
    // cur is an option element, such as this:
    // {
    //   "options": [
    //     {
    //       <option_name>: <option_value>
    //     },
    //     ...
    //   ]
    // }
    // this function will process the element and return an Option from it

    // here we make an assumption that we will never get here with a number of members != 1
    std::string value = tree[name].asString();

    Utils::trim(value);
    Option option(name, value);

    // filenames in the JSON are fixed up as follows:
    //   - if absolute path, leave it alone
    //   - if relative path, make it absolute using the JSON file's directory
    // The toAbsolutePath function does exactly that magic for us.
    if (option.getName() == "filename")
    {
        std::string path = option.getValue<std::string>();
#ifndef _WIN32
        wordexp_t result;
        if (wordexp(path.c_str(), &result, 0) == 0)
        {
            if (result.we_wordc == 1)
                path = result.we_wordv[0];
        }
        wordfree(&result);
#endif
        if (!FileUtils::isAbsolutePath(path))
        {
            std::string abspath = FileUtils::toAbsolutePath(m_inputJSONFile);
            std::string absdir = FileUtils::getDirectory(abspath);
            path = FileUtils::toAbsolutePath(path, absdir);

            assert(FileUtils::isAbsolutePath(path));
        }
        option.setValue(path);
    }
    else if (option.getName() == "plugin")
    {
        PluginManager::loadPlugin(option.getValue<std::string>());
    }
    return option;
}

Stage *PipelineReaderJSON::parseReaderByFilename(const std::string& filename)
{
    Options options(m_baseOptions);

    StageParserContext context;
    std::string type;

    try
    {
        type = StageFactory::inferReaderDriver(filename);
        if (!type.empty())
        {
            context.addType();
        }

        // std::string path(filename);
        // if (!FileUtils::isAbsolutePath(path))
        // {
        //     std::string abspath = FileUtils::toAbsolutePath(filename);
        //     std::string absdir = FileUtils::getDirectory(abspath);
        //     path = FileUtils::toAbsolutePath(path, absdir);
        //
        //     assert(FileUtils::isAbsolutePath(path));
        // }
        Option opt("filename", filename);
        options += opt;
    }
    catch (Option::not_found)
    {}

    context.setCardinality(StageParserContext::None);
    context.validate();

    Stage& reader(m_manager.addReader(type));
    reader.setOptions(options);

    return &reader;
}

Stage *PipelineReaderJSON::parseElement_Reader(const Json::Value& tree)
{
    Options options(m_baseOptions);

    StageParserContext context;

    std::string type = tree["type"].asString();
    if (!type.empty())
        context.addType();
    Json::Value opts = tree["options"];
    Json::Value::Members members(opts.getMemberNames());
    for (auto const& m : members)
    {
        Option option = parseElement_Option(m, opts);
        options.add(option);
    }

    // If we aren't provided a type, try to infer the type from the filename
    // #278
    if (context.getNumTypes() == 0)
    {
        try
        {
            const std::string filename = options.getValueOrThrow<std::string>("filename");
            type = StageFactory::inferReaderDriver(filename);
            if (!type.empty())
            {
                context.addType();
            }
        }
        catch (Option::not_found)
        {}
    }

    context.setCardinality(StageParserContext::None);
    context.validate();

    Stage& reader(m_manager.addReader(type));
    reader.setOptions(options);

    return &reader;
}


Stage *PipelineReaderJSON::parseElement_Filter(const Json::Value& tree)
{
    Options options(m_baseOptions);

    StageParserContext context;

    std::vector<Stage*> prevStages;

    std::string type = tree["type"].asString();
    context.addType();

    Json::Value opts = tree["options"];
    Json::Value::Members members(opts.getMemberNames());
    for (auto const& m : members)
    {
        Option option = parseElement_Option(m, opts);
        options.add(option);
    }

    context.setCardinality(StageParserContext::None);
    context.validate();

    Stage& filter(m_manager.addFilter(type));
    filter.setOptions(options);

    return &filter;
}

Stage *PipelineReaderJSON::parseWriterByFilename(const std::string& filename)
{
    Options options(m_baseOptions);

    StageFactory f;
    StageParserContext context;
    std::string type;

    try
    {
        type = f.inferWriterDriver(filename);
        if (type.empty())
            throw pdal_error("Cannot determine output file type of " +
                filename);

        // std::string path(filename);
        // if (!FileUtils::isAbsolutePath(path))
        // {
        //     std::string abspath = FileUtils::toAbsolutePath(filename);
        //     std::string absdir = FileUtils::getDirectory(abspath);
        //     path = FileUtils::toAbsolutePath(path, absdir);
        //
        //     assert(FileUtils::isAbsolutePath(path));
        // }

        options += f.inferWriterOptionsChanges(filename);
        context.addType();
    }
    catch (Option::not_found)
    {}

    context.setCardinality(StageParserContext::None);
    context.validate();

    Stage& writer(m_manager.addWriter(type));
    writer.setOptions(options);

    return &writer;
}

Stage *PipelineReaderJSON::parseElement_Writer(const Json::Value& tree)
{
    Options options(m_baseOptions);
    StageParserContext context;

    std::string type = tree["type"].asString();
    context.addType();

    Json::Value opts = tree["options"];
    Json::Value::Members members(opts.getMemberNames());
    for (auto const& m : members)
    {
        Option option = parseElement_Option(m, opts);
        options.add(option);
    }

    context.setCardinality(StageParserContext::None);
    context.validate();

    Stage& writer(m_manager.addWriter(type));
    writer.setOptions(options);

    return &writer;
}

bool PipelineReaderJSON::parseElement_Pipeline(const Json::Value& tree)
{
  bool isWriter = false;
    StageFactory f;
    std::map<std::string, Stage*> tags;
    std::vector<Stage*> stages;

    size_t i = 0;
    for (auto const& node : tree)
    {
      Stage* stage = NULL;

      // strings are assumed to be filenames
      if (node.isString())
      {
          std::string filename = node.asString();
          // all filenames assumed to be readers...
          if (i < tree.size()-1)
          {
              stage = parseReaderByFilename(filename);
              // prevStages.push_back(reader);
              // stage = reader;
          }
          // ...except the last
          else
          {
              stage = parseWriterByFilename(filename);
              // writer->setInput(*stage);
              // stage = writer;
              isWriter = true;
          }
      }
      else
      {
        std::string type, filename, tag;
        if (node.isMember("type"))
            type = node["type"].asString();
        if (node.isMember("filename"))
            filename = node["filename"].asString();
        if (node.isMember("tag"))
            tag = node["tag"].asString();
        std::vector<std::string> inputs =
            node["inputs"].getMemberNames();

        if (!type.empty())
        {
            std::cerr << "Type specified as " << type << std::endl;

            if (i < tree.size()-1)
            {
                stage = &m_manager.addReader(type);
            }
            else
            {
                stage = &m_manager.addWriter(type);
                isWriter = true;
            }
        }
        else if (!filename.empty())
        {
            std::cerr << "Filename specified as " << filename << std::endl;

            if (i < tree.size()-1)
            {
                stage = parseReaderByFilename(filename);
            }
            else
            {
                stage = parseWriterByFilename(filename);
                isWriter = true;
            }
        }

        if (!tag.empty())
        {
            if (tags[tag])
                throw pdal_error("Duplicate tag " + tag);

            tags[tag] = stage;
        }

      Options options(m_baseOptions);
        for (auto const& kate : node.getMemberNames())
        {
          if (kate.compare("filename") == 0)
              continue;
          if (kate.compare("type") == 0)
              continue;
          if (kate.compare("inputs") == 0)
              continue;
          if (kate.compare("tag") == 0)
              continue;
          std::cerr << "Found option " << kate << ":" << node[kate].asString() << std::endl;

          Option opt(kate, node[kate].asString());
          options.add(opt);
        }

        stage->addOptions(options);
      }

      stages.push_back(stage);

      if (i)
          stage->setInput(*stages[i-1]);

      // stage->setOptions(options);

      i++;
    }

    return isWriter;
}

bool PipelineReaderJSON::parseElement_PipelineOriginal(const Json::Value& tree)
{
    assert(tree.isArray());

    Stage *stage = NULL;
    Stage *reader = NULL;
    Stage *filter = NULL;
    Stage *writer = NULL;
    std::vector<Stage*> prevStages;

    bool isWriter = false;

    size_t num_stages = tree.size();

    // for each stage object in the pipeline array
    size_t i = 0;
    for (auto const& s : tree)
    {
        // strings are assumed to be filenames
        if (s.isString())
        {
            // all filenames assumed to be readers...
            if (i < num_stages-1)
            {
                reader = parseReaderByFilename(s.asString());
                prevStages.push_back(reader);
                stage = reader;
            }
            // ...except the last
            else
            {
                writer = parseWriterByFilename(s.asString());
                writer->setInput(*stage);
                isWriter = true;
            }
        }
        // otherwise, we can parse as a generic stage
        else
        {
            /*
            A stage object may have a member with the name tag whose value is a string. The purpose of the tag is to cross-reference this stage within other stages. Each tag must be unique.

            A stage object may have a member with the name inputs whose value is an array of strings. Each element in the array is the tag of another stage. If inputs is not specified, the previous stage in the array will be used as input. Reader stages will disregard the inputs member.

            A tag mentioned as input to one stage must have been previously defined in the pipeline array.

            A reader or writer stage object may have a member with the name type whose value is a string. The type must specify a valid PDAL reader or writer name.

            A filter stage object must have a member with the name type whose value is a string. The type must specify a valid PDAL filter name.

            A stage object may have additional members with names corresponding to stage-specific option names and their respective values.
            */

            Json::Value::Members members = s.getMemberNames();
            Options opts;
            for (auto const& n : members)
            {
                Stage *innerStage = NULL;

                // if filename, then reader/writer
                if (n.compare("filename") == 0)
                {
                    std::cerr << "Adding reader with filename = " << s[n].asString() << std::endl;

                    // Option opt(n, s[n].asString());
                    // opts += opt;

                    reader = parseReaderByFilename(s[n].asString());
                    prevStages.push_back(reader);
                    innerStage = reader;
                }
                /*
                else if (n.compare("tag") == 0)
                {}
                else if (n.compare("type") == 0)
                {
                    std::cerr << "Adding filter of type " << s[n].asString() << std::endl;

                    StageParserContext context;
                    context.addType();
                    context.setCardinality(StageParserContext::None);
                    context.validate();

                    Stage& filter(m_manager.addFilter(s[n].asString()));
                    for (auto r : prevStages)
                    {
                        std::cerr << r->getName() << std::endl;
                        filter.setInput(*r);
                    }

                    innerStage = &filter;
                }
                else if (n.compare("inputs") == 0)
                {}
                */
                else
                {
                    std::cerr << "Setting option " << n << " = " << s[n].asString() << std::endl;
                    Option opt(n, s[n].asString());
                    opts += opt;
                }
                // if filename + type, then reader/writer
                // if type and not filename, then filter
                // if tag (use in map)
                // if inputs, use tags, not previous stage
                // everything else is an option

                stage = innerStage;
                // stage->setOptions(opts);
            }
        }
        i++;
    }
    return isWriter;
    //
    // Json::Value filters = tree["Filters"];
    // bool firstFilter = true;
    // for (auto const& f : filters)
    // {
    //     filter = parseElement_Filter(f);
    //     if (firstFilter)
    //     {
    //         for (auto r : prevStages)
    //             filter->setInput(*r);
    //         firstFilter = false;
    //     }
    //     else
    //     {
    //         filter->setInput(*stage);
    //     }
    //     stage = filter;
    // }
}

bool PipelineReaderJSON::readPipeline(std::istream& input)
{
    Json::Value root;
    Json::Reader jsonReader;
    if (!jsonReader.parse(input, root))
        throw pdal_error("PipelineReaderJSON: unable to parse pipeline");

    Json::Value subtree = root["pipeline"];
    if (!subtree)
        throw pdal_error("PipelineReaderJSON: root element is not a Pipeline");
    return parseElement_Pipeline(subtree);
}


bool PipelineReaderJSON::readPipeline(const std::string& filename)
{
    m_inputJSONFile = filename;

    std::istream* input = FileUtils::openFile(filename);

    bool isWriter = false;

    try
    {
        isWriter = readPipeline(*input);
    }
    catch (const pdal_error& error)
    {
        throw error;
    }
    catch (...)
    {
        FileUtils::closeFile(input);
        std::ostringstream oss;
        oss << "Unable to process pipeline file \"" << filename << "\"." <<
            "  JSON is invalid.";
        throw pdal_error(oss.str());
    }

    FileUtils::closeFile(input);

    m_inputJSONFile = "";

    return isWriter;
}


} // namespace pdal
