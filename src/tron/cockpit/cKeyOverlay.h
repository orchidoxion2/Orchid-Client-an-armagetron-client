/*

*************************************************************************

Orchid Client -- key overlay for the Armagetron Advanced HUD.

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
//! @brief Shows the keys bound to turning left and right, lighting up while pressed

#ifndef ARMAGETRON_CKEYOVERLAY_H
#define ARMAGETRON_CKEYOVERLAY_H

#include "cockpit/cWidgetBase.h"

extern bool sg_hudKeyOverlay;   //!< HUD_KEY_OVERLAY: show the key overlay

#ifndef DEDICATED

#include <map>
#include <vector>

namespace cWidget {

//! One box per key bound to turning left, then one per key bound to turning right, each with the
//! recent presses rising above it as bars. Position is the bottom left corner of the key boxes;
//! Size is the total width and the height of a key box.
class KeyOverlay : public WithCoordinates {
public:
    virtual ~KeyOverlay() { };

    void Render();
    virtual bool Process(tXmlParser::node cur);

private:
    struct Press
    {
        double start, end;   // end < 0 while still held
    };
    std::map< int, std::vector< Press > > m_presses;  // by input ID
};

}

#endif
#endif
