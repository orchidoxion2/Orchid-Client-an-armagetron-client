/*

*************************************************************************

Orchid Client -- cinematic lyrics for Armagetron Advanced.

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

#ifndef ArmageTron_CINEMATIC_LYRICS_H
#define ArmageTron_CINEMATIC_LYRICS_H

#include "defs.h"
#include "tCoord.h"
#include "tColor.h"


//! Spotify lyrics drawn into the arena: big 3D handwritten words on the floor in front of the cycle,
//! appearing one word at a time. Replaces the lyrics in the HUD while it is on.
class gCinematicLyrics
{
public:
    static bool & Enabled();   //!< the CINEMATIC_LYRICS setting
    static int & Placement();  //!< the CINEMATIC_LYRICS_PLACEMENT setting: 0 lying on the floor, 1 standing up
    static bool & Stationary();  //!< the CINEMATIC_LYRICS_STATIONARY setting: lines stay where they appeared
    static int & ColorMode();  //!< the CINEMATIC_LYRICS_COLOR setting: 0 the cycle's color, 1 the custom color
    static int & Custom( int channel );  //!< CINEMATIC_LYRICS_RED/GREEN/BLUE, 0 to 255

    //! the glow color for lyrics following a cycle of the given color
    static tColor GlowColor( tColor const & cycle );

#ifndef DEDICATED
    //! draws the lyrics in front of the given cycle; the camera's world matrices must be set
    static void Render( void const * viewer, tCoord const & pos, tCoord const & dir, tColor const & color );
#endif
};

#endif
