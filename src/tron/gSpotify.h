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

#include <string>
#include <vector>

//! what the HUD shows: the current song and its lyrics
struct gSpotifyNowPlaying
{
    struct Line
    {
        int ms;
        tString text;
    };

    tString title, artist;
    int progressMs = 0, durationMs = 0;
    bool playing = false;
    bool showLyrics = false;
    enum LyricsState { LYRICS_SEARCHING, LYRICS_FOUND, LYRICS_NONE, LYRICS_UNREACHABLE };
    LyricsState lyricsState = LYRICS_SEARCHING;
    std::vector< Line > lines;  //!< synced lyrics, sorted by time; empty if there are none
    int trackVersion = 0;       //!< changes with every new song
};

//! shows the song playing on the player's Spotify account, with synced lyrics from LRCLIB
class gSpotify
{
public:
    //! fills in the current song; returns false if nothing is to be shown
    static bool NowPlaying( gSpotifyNowPlaying & info, bool withLines );

    //! the album cover as encoded image data; returns true and fills bytes if it changed since version
    static bool AlbumArt( int & version, std::string & bytes );

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
