/******************************************************************************
 * Copyright (c) 2013, Bradley J Chambers (brad.chambers@gmail.com)
 * Copyright (c) 2016, Logan C Byers (logan.c.byers@gmail.com)
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

#include "SplitterFilter.hpp"

#include <cmath>
#include <iostream>
#include <limits>

#include <pdal/pdal_macros.hpp>
#include <pdal/util/ProgramArgs.hpp>
#include <pdal/util/Bounds.hpp>

namespace pdal
{

static PluginInfo const s_info = PluginInfo(
    "filters.splitter",
    "Split data based on a X/Y box length.",
    "http://pdal.io/stages/filters.splitter.html" );

CREATE_STATIC_PLUGIN(1, 0, SplitterFilter, Filter, s_info)

std::string SplitterFilter::getName() const { return s_info.name; }

void SplitterFilter::addArgs(ProgramArgs& args)
{
    args.add("length", "Edge length of cell", m_length, 1000.0);
    args.add("buffer", "Extra distance to grow or shrink cell", m_buffer, 0.0);
    args.add("origin_x", "X origin for a cell", m_xOrigin,
        std::numeric_limits<double>::quiet_NaN());
    args.add("origin_y", "Y origin for a cell", m_yOrigin,
        std::numeric_limits<double>::quiet_NaN());
}


//This used to be a lambda, but the VS compiler exploded, I guess.
typedef std::pair<int, int> Coord;
namespace
{
class CoordCompare
{
public:
    bool operator () (const Coord& c1, const Coord& c2) const
    {
        return c1.first < c2.first ? true :
            c1.first > c2.first ? false :
            c1.second < c2.second ? true :
            false;
    };
};
}

PointViewSet SplitterFilter::run(PointViewPtr inView)
{
    PointViewSet viewSet;
    if (!inView->size())
        return viewSet;

    CoordCompare compare;
    std::map<Coord, PointViewPtr, CoordCompare> viewMap(compare);

    // Ensure that origin is less/equal than minimum bound of the point cloud.
    // If origin not specified, use the minx-miny corner of the point cloud bounds.
    // (!= test == isnan(), which doesn't exist on windows)
    // If origin specified but greater than min bound, offset origin along grid.
    BOX2D bounds;
    inView->calculateBounds(bounds);
    if (m_xOrigin != m_xOrigin)
        m_xOrigin = bounds.minx;
    else if (m_xOrigin < bounds.minx)
    {
        int num_bins = (int)std::ceil((bounds.minx - m_xOrigin) / m_length);
        m_xOrigin = m_xOrigin - num_bins * m_length;
    }
    if (m_yOrigin != m_yOrigin)
        m_yOrigin = bounds.miny;

    else if (m_yOrigin < bounds.miny)
    {
        int num_bins = (int)std::ceil((bounds.miny - m_yOrigin) / m_length);
        m_yOrigin = m_yOrigin - num_bins * m_length;
    }

    // Calculate maximum grid coordinate assuming no buffer.
    int max_x_bin = (int)((bounds.maxx - m_xOrigin) / m_length);
    int max_y_bin = (int)((bounds.maxy - m_yOrigin) / m_length);

    // Overlay a grid of squares on the points (m_length sides).  Each square
    // corresponds to a new point buffer.  Place the points falling in the
    // each square in the corresponding point buffer.
    for (PointId idx = 0; idx < inView->size(); idx++)
    {

        double x = inView->getFieldAs<double>(Dimension::Id::X, idx);
        double y = inView->getFieldAs<double>(Dimension::Id::Y, idx);
        double local_x = x - m_xOrigin;
        double local_y = y - m_yOrigin;
        int bin_x = local_x / m_length;
        int bin_y = local_y / m_length;

        Coord bin0(bin_x, bin_y);
        PointViewPtr& outView = viewMap[bin0];
        if (!outView)
            outView = inView->makeNew();
        outView->appendPoint(*inView.get(), idx);

        if (m_buffer <= 0)
            continue;

        // Get number of adjacent bins the point certainly falls within.
        int num_adj_x_p = (int)std::floor(m_buffer / m_length);
        int num_adj_x_n = -num_adj_x_p;
        int num_adj_y_p = num_adj_x_p;
        int num_adj_y_n = -num_adj_y_p;

        // Get distances to bufferless bin edges.
        double x_resid_bin_min = local_x - bin_x * m_length;
        double y_resid_bin_min = local_y - bin_y * m_length;
        double x_resid_bin_max = (bin_x + 1) * m_length - local_x;
        double y_resid_bin_max = (bin_y + 1) * m_length - local_y;
        // Get length buffer overlaps with most-outside bin.
        double buffer_residual = std::fmod(m_buffer, m_length);
        // Adjust number of adjacent bins.
        if (x_resid_bin_min < buffer_residual)
            num_adj_x_n--;
        if (x_resid_bin_max >= buffer_residual)
            num_adj_x_p++;
        if (y_resid_bin_min < buffer_residual)
            num_adj_y_n--;
        if (y_resid_bin_max >= buffer_residual)
            num_adj_y_p++;

        // Loop through rectangle of bins, skipping bins outside cloud bounds.
        for (int ix=num_adj_x_n; ix<=num_adj_x_p; ix++)
        {
            if ( (bin_x + ix < 0) || (bin_x + ix > max_x_bin) )
                continue;
            for (int iy=num_adj_y_n; iy<=num_adj_y_p; iy++)
            {
                if ( (bin_y + iy < 0) || (bin_y + iy > max_y_bin) )
                    continue;
                if (ix==0 && iy==0) // bin_x, bin_y already known
                    continue;
                // add point to view
                Coord bin(bin_x + ix, bin_y + iy);
                PointViewPtr& outView1 = viewMap[bin];
                if (!outView1)
                    outView1 = inView->makeNew();
                outView1->appendPoint(*inView.get(), idx);
            }
        }
    }
    // Pull the point views out of the map and stick them in the standard
    // output set, setting the bounds as we go.
    for (auto bi = viewMap.begin(); bi != viewMap.end(); ++bi)
        viewSet.insert(bi->second);
    return viewSet;
}

} // pdal
