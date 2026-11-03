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


#include "cockpit/cKeyOverlay.h"

#include "tConfiguration.h"

bool sg_hudKeyOverlay = false;
static tConfItem< bool > sg_hudKeyOverlayConf( "HUD_KEY_OVERLAY", sg_hudKeyOverlay );

#ifndef DEDICATED

#include "cockpit/cCockpit.h"
#include "ePlayer.h"
#include "rFont.h"
#include "rGL.h"
#include "rRender.h"
#include "tSysTime.h"
#include "uInput.h"

#include <algorithm>

namespace
{
    // how long a press stays visible while it rises to the top
    double const ck_riseTime = 1.4;

    void ck_Rect( REAL l, REAL b, REAL r, REAL t )
    {
        BeginQuads();
        Vertex( l, b ); Vertex( r, b ); Vertex( r, t ); Vertex( l, t );
        RenderEnd();
    }

    void ck_Frame( REAL l, REAL b, REAL r, REAL t )
    {
        BeginLineLoop();
        Vertex( l, b ); Vertex( r, b ); Vertex( r, t ); Vertex( l, t );
        RenderEnd();
    }

    // a short label for a key: "Z", "Left", "Mouse 1"
    tString ck_Label( uInput * input )
    {
        std::string name = input->Name();
        if ( name.empty() )
            name = std::string( input->PersistentID() );
        if ( name.compare( 0, 9, "SCANCODE_" ) == 0 )
            name = name.substr( 9 );
        if ( name.size() > 6 )
            name = name.substr( 0, 6 );
        return tString( name );
    }
}

namespace cWidget {

bool KeyOverlay::Process(tXmlParser::node cur) {
    if ( WithCoordinates::Process(cur) )
        return true;
    DisplayError(cur);
    return false;
}

void KeyOverlay::Render() {
    if ( !sg_hudKeyOverlay )
        return;
    ePlayer * player = m_Cockpit ? m_Cockpit->GetPlayer() : NULL;
    if ( !player )
        return;

    // the keys bound to turning for this player: left ones first, then right ones, each in input order
    static uAction * const left = uAction::Find( "CYCLE_TURN_LEFT" );
    static uAction * const right = uAction::Find( "CYCLE_TURN_RIGHT" );
    std::vector< uInput * > keys;
    for ( uAction * action: { left, right } )
        for ( uInput * input: su_inputs )
        {
            uBind * bind = input ? input->GetBind() : NULL;
            if ( bind && action && bind->act == action && bind->CheckPlayer( player->ID() + 1 ) )
                keys.push_back( input );
        }
    if ( keys.empty() )
        return;

    // remember the presses
    double const now = tSysTimeFloat();
    for ( uInput * key: keys )
    {
        std::vector< Press > & presses = m_presses[ key->ID() ];
        bool const down = key->GetPressed() > .5f;
        bool const holding = !presses.empty() && presses.back().end < 0;
        if ( down && !holding )
            presses.push_back( Press{ now, -1 } );
        else if ( !down && holding )
            presses.back().end = now;
        // forget the ones that rose out of sight
        presses.erase( std::remove_if( presses.begin(), presses.end(), [&]( Press const & p ){ return p.end >= 0 && now - p.end > ck_riseTime; } ), presses.end() );
    }

    int const count = int( keys.size() );
    REAL const totalW = m_size.x, boxH = m_size.y;
    REAL const gap = totalW * .06f;
    REAL const boxW = ( totalW - gap * ( count - 1 ) ) / count;
    REAL const bottom = m_position.y, boxTop = bottom + boxH;
    REAL const areaBottom = boxTop + boxH * .2f, areaTop = areaBottom + boxH * 4;
    REAL const speed = ( areaTop - areaBottom ) / ck_riseTime;

    RenderEnd();
    glDisable( GL_TEXTURE_2D );
    glEnable( GL_BLEND );
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

    for ( int i = 0; i < count; ++i )
    {
        uInput * key = keys[i];
        REAL const l = m_position.x + i * ( boxW + gap ), r = l + boxW;
        bool const down = key->GetPressed() > .5f;

        // the presses: a bar that grows while held and rises once let go, fading out at the top
        for ( Press const & p: m_presses[ key->ID() ] )
        {
            REAL b = areaBottom + REAL( p.end < 0 ? 0 : now - p.end ) * speed;
            REAL t = areaBottom + REAL( now - p.start ) * speed;
            if ( b >= areaTop )
                continue;
            t = std::min( t, areaTop );
            if ( t - b < boxH * .08f )
                t = b + boxH * .08f;
            REAL const fade = 1 - std::max( REAL( 0 ), ( t - ( areaTop - boxH ) ) / boxH ) * .6f;
            Color( .55f, .55f, .58f, .75f * fade );
            ck_Rect( l + boxW * .08f, b, r - boxW * .08f, t );
        }

        // the key box: lit while pressed
        if ( down )
        {
            Color( 1, 1, 1, .9f );
            ck_Rect( l, bottom, r, boxTop );
        }
        else
        {
            Color( 0, 0, 0, .45f );
            ck_Rect( l, bottom, r, boxTop );
        }
        glLineWidth( 2 );
        Color( 1, 1, 1, .95f );
        ck_Frame( l, bottom, r, boxTop );
        glLineWidth( 1 );

        tString const label = ck_Label( key );
        REAL const h = std::min( boxH * .38f, boxW * .9f / std::max( 1, int( label.size() ) ) * 1.6f );
        rTextField::SetDefaultColor( tColor( 1, 1, 1, 1 ) );
        rTextField::SetBlendColor( down ? tColor( 0, 0, 0, 1 ) : tColor( 1, 1, 1, 1 ) );
        DisplayText( ( l + r ) * .5f, ( bottom + boxTop ) * .5f, h, label.c_str(), sr_fontCockpit, 0 );
        rTextField::SetBlendColor( tColor( 1, 1, 1, 1 ) );
        RenderEnd();
        glDisable( GL_TEXTURE_2D );
    }
    Color( 1, 1, 1, 1 );
}

}

#endif
