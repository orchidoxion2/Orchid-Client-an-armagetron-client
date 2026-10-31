/*

*************************************************************************

Orchid Client -- leaderboard for the Armagetron Advanced HUD.

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
//! @brief A leaderboard of all players drawn in the HUD

#ifndef ARMAGETRON_CLEADERBOARD_H
#define ARMAGETRON_CLEADERBOARD_H

#include "cockpit/cWidgetBase.h"

extern bool sg_hudLeaderboard;   //!< HUD_LEADERBOARD: show the leaderboard

#ifndef DEDICATED

namespace cWidget {

//! The players sorted by score. Position is the top left corner; Size is the width and the height of one row.
class Leaderboard : public WithCoordinates {
public:
    virtual ~Leaderboard() { };

    void Render();
    virtual bool Process(tXmlParser::node cur);
};

}

#endif
#endif
