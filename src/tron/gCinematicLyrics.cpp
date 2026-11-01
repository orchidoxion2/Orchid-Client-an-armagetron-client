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

#include "gCinematicLyrics.h"

#include "tConfiguration.h"

static bool sg_cinematicLyrics = false;
static tConfItem< bool > sg_cinematicLyricsConf( "CINEMATIC_LYRICS", sg_cinematicLyrics );
static int sg_cinematicLyricsPlacement = 0;
static tConfItem< int > sg_cinematicLyricsPlacementConf( "CINEMATIC_LYRICS_PLACEMENT", sg_cinematicLyricsPlacement );

static int sg_cinematicLyricsColor = 0;
static tConfItem< int > sg_cinematicLyricsColorConf( "CINEMATIC_LYRICS_COLOR", sg_cinematicLyricsColor );
static int sg_cinematicLyricsRGB[3] = { 214, 64, 159 };
static tConfItem< int > sg_cinematicLyricsRedConf( "CINEMATIC_LYRICS_RED", sg_cinematicLyricsRGB[0] );
static tConfItem< int > sg_cinematicLyricsGreenConf( "CINEMATIC_LYRICS_GREEN", sg_cinematicLyricsRGB[1] );
static tConfItem< int > sg_cinematicLyricsBlueConf( "CINEMATIC_LYRICS_BLUE", sg_cinematicLyricsRGB[2] );

bool & gCinematicLyrics::Enabled(){ return sg_cinematicLyrics; }
int & gCinematicLyrics::ColorMode(){ return sg_cinematicLyricsColor; }
int & gCinematicLyrics::Custom( int channel ){ return sg_cinematicLyricsRGB[ channel < 0 ? 0 : ( channel > 2 ? 2 : channel ) ]; }

tColor gCinematicLyrics::GlowColor( tColor const & cycle )
{
    if ( sg_cinematicLyricsColor == 1 )
    {
        REAL c[3];
        for ( int i = 0; i < 3; ++i )
            c[i] = sg_cinematicLyricsRGB[i] < 0 ? 0 : ( sg_cinematicLyricsRGB[i] > 255 ? 1 : sg_cinematicLyricsRGB[i] / 255.0f );
        return tColor( c[0], c[1], c[2] );
    }

    // a neon glow in the cycle's color, never too dark to see
    tColor glow( cycle.r_, cycle.g_, cycle.b_ );
    REAL const bright = glow.r_ > glow.g_ ? ( glow.r_ > glow.b_ ? glow.r_ : glow.b_ ) : ( glow.g_ > glow.b_ ? glow.g_ : glow.b_ );
    if ( bright < .6 )
    {
        REAL const lift = .6 - bright;
        glow = tColor( glow.r_ + lift, glow.g_ + lift, glow.b_ + lift );
    }
    return glow;
}
int & gCinematicLyrics::Placement(){ return sg_cinematicLyricsPlacement; }

#ifndef DEDICATED

#include "eAdvWall.h"
#include "gSpotify.h"
#include "rFont.h"
#include "rGL.h"
#include "rRender.h"
#include "rScreen.h"
#include "tColor.h"
#include "tSysTime.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace
{

// sizes in arena units
REAL const cl_wordHeight = 4.6;   // height of a normal word
REAL const cl_rowWidth   = 36;    // a line breaks into rows this wide
REAL const cl_rowGap     = 1.15;  // row distance, in word heights
REAL const cl_ahead      = 26;    // how far in front of the cycle the lyrics float
REAL const cl_depth      = .55;   // how thick the letters are
int  const cl_layers     = 7;     // slices that make up the thickness
REAL const cl_fontHeight = .2;    // font size the glyphs are made at, in screen units

REAL cl_Clamp( REAL v, REAL lo, REAL hi )
{
    return v < lo ? lo : ( v > hi ? hi : v );
}

REAL cl_Approach( REAL value, REAL target, REAL rate, REAL dt )
{
    return value + ( target - value ) * ( 1 - exp( -rate * dt ) );
}

//! a fixed pseudo random number in [0,1) for a word, so a line always looks the same
REAL cl_Random( unsigned int seed )
{
    seed ^= seed >> 16; seed *= 0x7feb352d;
    seed ^= seed >> 15; seed *= 0x846ca68b;
    seed ^= seed >> 16;
    return ( seed & 0xffffff ) / REAL( 0x1000000 );
}

REAL cl_EaseOutBack( REAL t )
{
    t = cl_Clamp( t, 0, 1 ) - 1;
    REAL const s = 1.7;
    return 1 + t * t * ( ( s + 1 ) * t + s );
}

struct cl_Word
{
    tString text;
    REAL x, y;      // left end of the baseline, in arena units from the center of the line
    REAL size;      // height in arena units
    REAL tilt;      // degrees
    REAL appear;    // seconds after the line starts
};

struct cl_Line
{
    int index = -1;
    int startMs = 0;
    REAL bottom = 0;  // lowest point of the letters, below the center
    std::vector< cl_Word > words;
};

//! splits a lyric line into words with their own size and tilt, in staggered rows
void cl_Layout( cl_Line & line, tString const & text, int index, int lengthMs )
{
    line.words.clear();
    line.index = index;
    line.bottom = 0;
    unsigned int const seed = 0x9e3779b9u * ( index + 1 );

    // the words with their sizes
    REAL const toWorld = sr_screenWidth / REAL( sr_screenHeight ) / cl_fontHeight;
    std::vector< REAL > widths;
    size_t start = 0;
    int n = 0;
    while ( start < text.size() )
    {
        size_t end = text.find( ' ', start );
        if ( end == std::string::npos )
            end = text.size();
        tString word( text.substr( start, end - start ) );
        start = end + 1;
        if ( word.empty() )
            continue;
        cl_Word w;
        w.text = word;
        REAL const r = cl_Random( seed + 31 * n );
        // short filler words come out smaller, like in a lyric video
        REAL const len = word.LenUtf8( 0, word.size() );
        w.size = cl_wordHeight * ( len <= 2 ? .72 : ( len <= 3 ? .82 : .9 ) ) * ( .9 + .35 * r );
        w.tilt = ( cl_Random( seed + 31 * n + 7 ) - .5 ) * 9;
        w.x = w.y = 0;
        widths.push_back( sr_LyricsTextWidth( word, cl_fontHeight ) * toWorld * w.size );
        line.words.push_back( w );
        ++n;
    }
    if ( line.words.empty() )
        return;

    // fill rows
    std::vector< std::vector< int > > rows( 1 );
    REAL used = 0;
    for ( int i = 0; i < n; ++i )
    {
        REAL const gap = cl_wordHeight * .32;
        if ( !rows.back().empty() && used + gap + widths[i] > cl_rowWidth )
        {
            rows.push_back( std::vector< int >() );
            used = 0;
        }
        used += ( rows.back().empty() ? 0 : gap ) + widths[i];
        rows.back().push_back( i );
    }

    // place them, each row shifted a bit sideways, the whole block centered
    REAL const rowStep = cl_wordHeight * cl_rowGap;
    REAL const top = ( rows.size() - 1 ) * rowStep * .5;
    for ( size_t r = 0; r < rows.size(); ++r )
    {
        REAL width = 0;
        for ( size_t k = 0; k < rows[r].size(); ++k )
            width += widths[ rows[r][k] ] + ( k ? cl_wordHeight * .32 : 0 );
        REAL const shift = rows.size() == 1 ? 0 : ( r % 2 ? 1 : -1 ) * cl_wordHeight * .9;
        REAL x = -width * .5 + shift;
        REAL const y = top - r * rowStep - cl_wordHeight * .35;
        for ( int i: rows[r] )
        {
            cl_Word & w = line.words[i];
            w.x = x;
            w.y = y + ( cl_Random( seed + 31 * i + 3 ) - .5 ) * cl_wordHeight * .25;
            x += widths[i] + cl_wordHeight * .32;
            line.bottom = std::min( line.bottom, w.y - w.size * .35f );
        }
    }

    // the words come in one by one, spread over most of the line, by length
    REAL const total = std::max( 300, std::min( lengthMs, 9000 ) ) * .001;
    REAL const span = std::min( total * .7, REAL( n ) * .45 );
    REAL letters = 0;
    for ( cl_Word const & w: line.words )
        letters += w.text.LenUtf8( 0, w.text.size() ) + 2;
    REAL done = 0;
    for ( cl_Word & w: line.words )
    {
        w.appear = .15 + ( n == 1 ? 0 : span * done / letters );
        done += w.text.LenUtf8( 0, w.text.size() ) + 2;
    }
}

//! how far in front of pos the lyrics can float before they would end up in or behind the arena wall
REAL cl_Ahead( tCoord const & pos, tCoord const & dir, REAL want )
{
    REAL const margin = cl_wordHeight * 1.5;
    if ( eWallRim::IsBound( pos + dir * want, margin ) )
        return want;
    REAL lo = 0, hi = want;
    for ( int i = 0; i < 12; ++i )
    {
        REAL const mid = ( lo + hi ) * .5;
        if ( eWallRim::IsBound( pos + dir * mid, margin ) )
            lo = mid;
        else
            hi = mid;
    }
    return lo;
}

//! where a line floats: the point under its middle and the direction the cycle looked
struct cl_Frame
{
    tCoord pos, dir;
};

struct cl_Fading
{
    cl_Line line;
    cl_Frame frame;
    REAL age = 0;
    REAL shown = 0;  // how long the line had been on screen
};

struct cl_View
{
    double lastUsed = -1;
    int trackVersion = -1;
    gSpotifyNowPlaying info;
    cl_Line current;
    REAL shown = 0;         // seconds the current line has been visible
    bool hasFrame = false;
    cl_Frame frame;
    std::vector< cl_Fading > fading;
};

std::map< void const *, cl_View > cl_views;

//! sets up a matrix with x to the right of dir, y forward (lying) or up (standing), z toward the viewer
void cl_Basis( cl_Frame const & f, bool standing, REAL lift, REAL bottom )
{
    tCoord const right = f.dir.Turn( 0, -1 );
    GLfloat m[16] = { 0 };
    m[0] = right.x; m[1] = right.y; m[2] = 0;
    if ( standing )
    {
        m[4] = 0; m[5] = 0; m[6] = 1;
        m[8] = -f.dir.x; m[9] = -f.dir.y; m[10] = 0;
        m[12] = f.pos.x; m[13] = f.pos.y; m[14] = .4 - bottom + lift;
    }
    else
    {
        // stretched along the way, so the words do not look squashed when seen at a low angle
        m[4] = f.dir.x * 1.5; m[5] = f.dir.y * 1.5; m[6] = 0;
        m[8] = 0; m[9] = 0; m[10] = 1;
        m[12] = f.pos.x; m[13] = f.pos.y; m[14] = .06 + cl_depth + lift;
    }
    m[15] = 1;
    glMultMatrixf( m );
}

//! draws one word: glow around it, the thick side, and the bright face on top
void cl_DrawWord( cl_Word const & w, REAL t, REAL fade, tColor const & glow )
{
    REAL const p = cl_Clamp( t / .45f, 0, 1 );
    if ( p <= 0 || fade <= .005 )
        return;
    REAL const alpha = cl_Clamp( t / .25f, 0, 1 ) * fade;
    REAL const grow = cl_EaseOutBack( p );        // pops up a little too far, then settles
    REAL const rise = 1 - ( 1 - p ) * ( 1 - p );  // grows out of the floor
    REAL const scale = w.size / cl_fontHeight * ( .55 + .45 * grow );
    REAL const aspect = sr_screenWidth / REAL( sr_screenHeight );

    glPushMatrix();
    glTranslatef( w.x, w.y - ( 1 - rise ) * w.size * .6, 0 );
    glRotatef( w.tilt * ( .4 + .6 * rise ), 0, 0, 1 );
    glScalef( scale * aspect, scale, 1 );

    REAL const depth = cl_depth * rise;

    // soft glow: the word spread out in a ring, added on top of what is there
    REAL const spread = .1 * cl_fontHeight;
    for ( int ring = 1; ring <= 2; ++ring )
        for ( int k = 0; k < 8; ++k )
        {
            REAL const a = k * M_PI / 4 + ring * .4;
            glColor4f( glow.r_, glow.g_, glow.b_, alpha * ( ring == 1 ? .16 : .08 ) );
            sr_LyricsTextRender( w.text, cl_fontHeight, cos( a ) * spread * ring / aspect, sin( a ) * spread * ring, true );
        }

    // thickness: slices from the back to the front, darker toward the back
    for ( int i = 0; i < cl_layers; ++i )
    {
        REAL const f = i / REAL( cl_layers - 1 );
        glPushMatrix();
        glTranslatef( 0, 0, -depth * ( 1 - f ) );
        REAL const shade = .25 + .45 * f;
        glColor4f( glow.r_ * shade, glow.g_ * shade, glow.b_ * shade, alpha );
        sr_LyricsTextRender( w.text, cl_fontHeight, 0, 0 );
        glPopMatrix();
    }

    // the face
    glColor4f( 1, 1, 1, alpha );
    sr_LyricsTextRender( w.text, cl_fontHeight, 0, 0 );

    glPopMatrix();
}

void cl_DrawLine( cl_Line const & line, cl_Frame const & frame, bool standing, REAL shown, REAL fade, REAL lift, tColor const & glow )
{
    glPushMatrix();
    cl_Basis( frame, standing, lift, line.bottom );
    for ( cl_Word const & w: line.words )
        cl_DrawWord( w, shown - w.appear, fade, glow );
    glPopMatrix();
}

}

void gCinematicLyrics::Render( void const * viewer, tCoord const & pos, tCoord const & dir, tColor const & color )
{
    if ( !sg_cinematicLyrics || !sr_glOut )
        return;

    double const now = tSysTimeFloat();

    // forget viewports that are gone
    for ( auto i = cl_views.begin(); i != cl_views.end(); )
        if ( now - i->second.lastUsed > 3 && i->first != viewer )
            i = cl_views.erase( i );
        else
            ++i;

    cl_View & v = cl_views[ viewer ];
    REAL const dt = v.lastUsed < 0 ? 0 : cl_Clamp( now - v.lastUsed, 0, .1 );
    v.lastUsed = now;

    if ( !gSpotify::NowPlaying( v.info, false ) )
    {
        v.current = cl_Line();
        v.fading.clear();
        v.trackVersion = -1;
        return;
    }
    if ( v.info.trackVersion != v.trackVersion || v.info.lines.empty() )
    {
        if ( v.info.trackVersion != v.trackVersion && v.current.index >= 0 && v.hasFrame )
        {
            cl_Fading f;
            f.line = v.current;
            f.frame = v.frame;
            f.shown = v.shown;
            v.fading.push_back( f );
            v.current = cl_Line();
        }
        v.trackVersion = v.info.trackVersion;
        gSpotify::NowPlaying( v.info, true );
    }

    // follow the cycle: float in front of it, swing around smoothly when it turns
    bool const standing = sg_cinematicLyricsPlacement == 1;
    tCoord d = dir;
    if ( d.NormSquared() < 1E-6 )
        d = tCoord( 1, 0 );
    d = d * ( 1 / sqrt( d.NormSquared() ) );
    cl_Frame target;
    target.dir = d;
    REAL const ahead = standing ? cl_ahead * 1.15 : cl_ahead;
    target.pos = pos + d * cl_Ahead( pos, d, ahead );
    if ( !v.hasFrame || ( v.frame.pos - target.pos ).NormSquared() > 200 * 200 )
    {
        v.frame = target;
        v.hasFrame = true;
    }
    else
    {
        v.frame.dir = v.frame.dir + ( target.dir - v.frame.dir ) * ( 1 - exp( -5 * dt ) );
        if ( v.frame.dir.NormSquared() < 1E-4 )
            v.frame.dir = target.dir;
        v.frame.dir = v.frame.dir * ( 1 / sqrt( v.frame.dir.NormSquared() ) );
        // where it should be with the smoothed direction, so it swings instead of cutting corners
        tCoord const want = pos + v.frame.dir * cl_Ahead( pos, v.frame.dir, ahead );
        v.frame.pos.x = cl_Approach( v.frame.pos.x, want.x, 14, dt );
        v.frame.pos.y = cl_Approach( v.frame.pos.y, want.y, 14, dt );
    }

    // which line is sung now
    int current = -1;
    for ( size_t i = 0; i < v.info.lines.size(); ++i )
        if ( v.info.lines[i].ms <= v.info.progressMs )
            current = int( i );
    if ( current != v.current.index )
    {
        if ( v.current.index >= 0 && !v.current.words.empty() )
        {
            cl_Fading f;
            f.line = v.current;
            f.frame = v.frame;
            f.shown = v.shown;
            v.fading.push_back( f );
        }
        v.current = cl_Line();
        v.shown = 0;
        if ( current >= 0 )
        {
            int const next = current + 1 < int( v.info.lines.size() ) ? v.info.lines[ current + 1 ].ms : v.info.durationMs;
            cl_Layout( v.current, v.info.lines[ current ].text, current, next - v.info.lines[ current ].ms );
            v.current.startMs = v.info.lines[ current ].ms;
            // joined in the middle of the line: show what was already sung
            v.shown = cl_Clamp( ( v.info.progressMs - v.current.startMs ) * .001, 0, 30 );
        }
    }
    else if ( v.info.playing )
        v.shown += dt;

    tColor const glow = GlowColor( color );

    RenderEnd();
    glDisable( GL_LIGHTING );
    glEnable( GL_BLEND );
    glEnable( GL_DEPTH_TEST );
    glDepthMask( GL_FALSE );
    glDisable( GL_CULL_FACE );

    // old lines sink into the floor where they were left
    for ( auto i = v.fading.begin(); i != v.fading.end(); )
    {
        i->age += dt;
        i->shown += dt;
        REAL const t = i->age / .45f;
        if ( t >= 1 )
        {
            i = v.fading.erase( i );
            continue;
        }
        cl_DrawLine( i->line, i->frame, standing, i->shown, ( 1 - t ) * ( 1 - t ), -t * t * cl_wordHeight * ( standing ? 1.5 : .8 ), glow );
        ++i;
    }

    if ( !v.current.words.empty() )
        cl_DrawLine( v.current, v.frame, standing, v.shown, 1, 0, glow );

    glDepthMask( GL_TRUE );
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    glDisable( GL_TEXTURE_2D );
    glColor4f( 1, 1, 1, 1 );
}

#endif
