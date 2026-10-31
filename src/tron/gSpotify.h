/*

*************************************************************************

Orchid Client -- Spotify integration for Armagetron Advanced.

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

#ifndef ArmageTron_SPOTIFY_H
#define ArmageTron_SPOTIFY_H

#include "tString.h"

//! shows the song playing on the player's Spotify account, with synced lyrics from LRCLIB
class gSpotify
{
public:
    static bool & Enabled();     //!< the SPOTIFY_ENABLED setting
    static bool & ShowLyrics();  //!< the SPOTIFY_LYRICS setting

    static void Login();         //!< opens the Spotify login page in the browser
    static void Logout();        //!< forgets the login
    static bool LoggedIn();

    static tString Status();     //!< login state for the settings menu

    static tString Track();      //!< "title  artist" with color codes, empty if nothing plays
    static tString Lyric();      //!< the current lyric line, empty if there is none

    static void Shutdown();      //!< stops the background thread
};

#endif
