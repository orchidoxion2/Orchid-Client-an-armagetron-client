/*

*************************************************************************

Orchid Client -- Spotify integration for Armagetron Advanced.

Code provided by Orchid Client
https://github.com/orchidoxion2/Orchid-Client-an-armagetron-client
Copyright (C) 2026 Orchid Client

If you copy or adapt this code, keep this notice (the GPL below requires
that), and please also credit "code provided by Orchid Client" where your
project shows its credits.

**************************************************************************

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

***************************************************************************

*/

//! @file
//! @brief The now playing card and the scrolling lyrics of the Spotify integration

#ifndef ARMAGETRON_CSPOTIFY_H
#define ARMAGETRON_CSPOTIFY_H

#include "cockpit/cWidgetBase.h"

#ifndef DEDICATED

#include "gSpotify.h"

#include <vector>

namespace cWidget {

//! Shows the song playing on Spotify: album cover, title, artist and progress, with the lyrics below.
//! Position is the top left corner; Size is the total width and the height of the album cover.
class Spotify : public WithCoordinates {
public:
    virtual ~Spotify() { };

    void Render();
    virtual bool Process(tXmlParser::node cur);

private:
    void RenderCard( gSpotifyNowPlaying const & info, REAL alpha );
    void RenderLyrics( gSpotifyNowPlaying const & info, REAL alpha, REAL dt );

    gSpotifyNowPlaying m_info;
    int m_trackVersion = -1;
    double m_trackStart = 0;
    double m_lastTime = -1;
    REAL m_scroll = 0;
    std::vector< REAL > m_emphasis;
    int m_current = -2;
};

}

#endif
#endif
