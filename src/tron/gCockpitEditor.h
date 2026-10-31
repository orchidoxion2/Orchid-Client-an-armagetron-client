/*

*************************************************************************

Orchid Client -- HUD editor for Armagetron Advanced.

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

#ifndef ArmageTron_COCKPITEDITOR_H
#define ArmageTron_COCKPITEDITOR_H

#include "tString.h"

//! lays out the widgets of the HUD with the mouse and saves the result as the player's own HUD file
class gCockpitEditor
{
public:
    static void Open();                 //!< the editor screen
    static tString CustomFile();        //!< resource name of the player's own HUD
    static bool UsingCustom();          //!< the player's own HUD is in use
    static void UseCustom( bool use );  //!< switches between the player's own HUD and the default one
};

#endif
