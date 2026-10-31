/*

*************************************************************************

Orchid Client -- path finder for Armagetron Advanced.

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

#ifndef ArmageTron_PATHFINDER_H
#define ArmageTron_PATHFINDER_H

class gCycle;

//! draws the safest route for the local player's cycle on the floor
class gPathFinder
{
public:
    static bool & Enabled();                 //!< the PATHFINDER setting
    static bool Active();                    //!< enabled, and not connected to a ranked server
    static void Render( gCycle const * cycle );
};

#endif
