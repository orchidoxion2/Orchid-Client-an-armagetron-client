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

#include "cockpit/cLeaderboard.h"

#include "tConfiguration.h"

bool sg_hudLeaderboard = false;
static tConfItem< bool > sg_hudLeaderboardConf( "HUD_LEADERBOARD", sg_hudLeaderboard );

#ifndef DEDICATED

#include "eNetGameObject.h"
#include "ePlayer.h"
#include "nNetwork.h"
#include "rFont.h"
#include "rGL.h"
#include "rRender.h"

#include <algorithm>
#include <vector>

namespace
{

int const cl_rows = 8;

void cl_Rect( REAL l, REAL b, REAL r, REAL t, REAL red, REAL green, REAL blue, REAL alpha )
{
    glDisable( GL_TEXTURE_2D );
    Color( red, green, blue, alpha );
    BeginQuads();
    Vertex( l, b ); Vertex( r, b ); Vertex( r, t ); Vertex( l, t );
    RenderEnd();
}

void cl_Text( REAL x, REAL y, REAL h, tString const & text, tColor const & color, int align )
{
    rTextField::SetDefaultColor( tColor( 1, 1, 1, 1 ) );
    rTextField::SetBlendColor( color );
    DisplayText( x, y, h, text.c_str(), sr_fontCockpit, align );
    rTextField::SetBlendColor( tColor( 1, 1, 1, 1 ) );
}

//! cuts a name with color codes down to the given width
tString cl_Fit( tString const & name, REAL h, REAL width )
{
    if ( rTextField::GetTextLength( name, h, true ) <= width )
        return name;
    std::string text = name;
    while ( !text.empty() )
    {
        size_t cut = text.size() - 1;
        while ( cut > 0 && ( static_cast< unsigned char >( text[cut] ) & 0xC0 ) == 0x80 )
            --cut;
        text.erase( cut );
        tColoredString candidate( text );
        candidate.RemoveTrailingColor();
        tString shown( candidate );
        shown << "0xffffff\xe2\x80\xa6";
        if ( rTextField::GetTextLength( shown, h, true ) <= width )
            return shown;
    }
    return tString();
}

}

namespace cWidget {

bool Leaderboard::Process(tXmlParser::node cur) {
    if ( WithCoordinates::Process(cur) )
        return true;
    DisplayError(cur);
    return false;
}

void Leaderboard::Render() {
    if ( !sg_hudLeaderboard )
        return;

    // everybody who plays, best score first
    std::vector< ePlayerNetID * > players;
    for ( int i = se_PlayerNetIDs.Len() - 1; i >= 0; --i )
    {
        ePlayerNetID * player = se_PlayerNetIDs(i);
        if ( player && player->IsActive() && !player->IsSpectating() && player->CurrentTeam() )
            players.push_back( player );
    }
    if ( players.empty() )
        return;
    std::stable_sort( players.begin(), players.end(), []( ePlayerNetID * a, ePlayerNetID * b ){ return a->TotalScore() > b->TotalScore(); } );

    // the local player always gets a row
    std::vector< int > shown;
    int me = -1;
    for ( size_t i = 0; i < players.size(); ++i )
    {
        if ( me < 0 && players[i]->Owner() == sn_myNetID && players[i]->pID >= 0 )
            me = int( i );
        if ( int( i ) < cl_rows )
            shown.push_back( int( i ) );
    }
    if ( me >= cl_rows )
        shown.back() = me;

    REAL const left = m_position.x, top = m_position.y, width = m_size.x, row = m_size.y;
    REAL const textH = row * .62;
    REAL const header = row * .9;
    REAL const bottom = top - header - row * shown.size();

    RenderEnd();
    glEnable( GL_BLEND );
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    cl_Rect( left, bottom - row * .15, left + width, top, .04, .05, .06, .78 );
    cl_Text( left + width * .04, top - header * .5, header * .5, tString( "LEADERBOARD" ), tColor( .55, .55, .6, 1 ), -1 );

    for ( size_t r = 0; r < shown.size(); ++r )
    {
        int const index = shown[r];
        ePlayerNetID const * player = players[ index ];
        REAL const rowTop = top - header - row * r;
        REAL const y = rowTop - row * .5;
        if ( index == me )
        {
            cl_Rect( left, rowTop - row, left + width, rowTop, 1, 1, 1, .08 );
            cl_Rect( left, rowTop - row, left + width * .012, rowTop, .84, .25, .62, 1 );
        }
        bool const alive = player->Object() && player->Object()->Alive();
        REAL const fade = alive ? 1 : .45;

        tString rank;
        rank << index + 1;
        cl_Text( left + width * .1, y, textH, rank, tColor( .55, .55, .6, fade ), 1 );

        tString score;
        score << player->TotalScore();
        cl_Text( left + width * .96, y, textH, score, tColor( 1, 1, 1, fade ), 1 );

        REAL const nameX = left + width * .15;
        REAL const nameWidth = left + width * .78 - nameX;
        cl_Text( nameX, y, textH, cl_Fit( tString( player->GetColoredName() ), textH, nameWidth ), tColor( 1, 1, 1, fade ), -1 );
    }

    glDisable( GL_TEXTURE_2D );
    Color( 1, 1, 1, 1 );
}

}

#endif
