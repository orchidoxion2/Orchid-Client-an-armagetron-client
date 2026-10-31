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

#include "cockpit/cSpotify.h"

#ifndef DEDICATED

#include "rDisplayList.h"
#include "rFont.h"
#include "rGL.h"
#include "rRender.h"
#include "rScreen.h"
#include "tSysTime.h"

#include <SDL_image.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace
{

// the album cover, shared by all viewports
int const cs_artSize = 256;
GLuint cs_texture = 0;
int cs_artVersion = 0;
std::vector< unsigned char > cs_pixels;
bool cs_upload = false;

void cs_ForgetTexture()
{
    if ( cs_texture )
        glDeleteTextures( 1, &cs_texture );
    cs_texture = 0;
    cs_upload = !cs_pixels.empty();
}

rCallbackBeforeScreenModeChange cs_forget( &cs_ForgetTexture );

void cs_Decode( std::string const & bytes )
{
    cs_pixels.clear();
    cs_upload = true;
    if ( bytes.empty() )
        return;
    SDL_RWops * rw = SDL_RWFromConstMem( bytes.data(), int( bytes.size() ) );
    SDL_Surface * image = rw ? IMG_Load_RW( rw, 1 ) : NULL;
    if ( !image )
        return;
    SDL_Surface * scaled = SDL_CreateRGBSurface( 0, cs_artSize, cs_artSize, 32, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000 );
    SDL_Surface * converted = scaled ? SDL_ConvertSurface( image, scaled->format, 0 ) : NULL;
    if ( converted )
    {
        SDL_SetSurfaceBlendMode( converted, SDL_BLENDMODE_NONE );
        if ( SDL_BlitScaled( converted, NULL, scaled, NULL ) == 0 && SDL_LockSurface( scaled ) == 0 )
        {
            cs_pixels.resize( cs_artSize * cs_artSize * 4 );
            for ( int y = 0; y < cs_artSize; ++y )
                memcpy( &cs_pixels[ y * cs_artSize * 4 ], static_cast< unsigned char * >( scaled->pixels ) + y * scaled->pitch, cs_artSize * 4 );
            SDL_UnlockSurface( scaled );
        }
        SDL_FreeSurface( converted );
    }
    if ( scaled )
        SDL_FreeSurface( scaled );
    SDL_FreeSurface( image );
}

void cs_Upload()
{
    if ( !cs_upload )
        return;
    cs_upload = false;
    if ( cs_texture )
        glDeleteTextures( 1, &cs_texture );
    cs_texture = 0;
    if ( cs_pixels.empty() )
        return;
    rDisplayList::Cancel();
    glGenTextures( 1, &cs_texture );
    glBindTexture( GL_TEXTURE_2D, cs_texture );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
    gluBuild2DMipmaps( GL_TEXTURE_2D, GL_RGBA, cs_artSize, cs_artSize, GL_RGBA, GL_UNSIGNED_BYTE, &cs_pixels[0] );
}

//! the size of one screen pixel in the current viewport
struct cs_Pixel
{
    REAL x, y;
    cs_Pixel()
    {
        GLint viewport[4];
        glGetIntegerv( GL_VIEWPORT, viewport );
        x = 2.0 / std::max( 1, int( viewport[2] ) );
        y = 2.0 / std::max( 1, int( viewport[3] ) );
    }
};

REAL cs_Clamp( REAL v, REAL lo, REAL hi )
{
    return v < lo ? lo : ( v > hi ? hi : v );
}

REAL cs_Approach( REAL value, REAL target, REAL rate, REAL dt )
{
    return value + ( target - value ) * ( 1 - exp( -rate * dt ) );
}

void cs_RoundRect( REAL l, REAL b, REAL r, REAL t, REAL radiusPx, cs_Pixel const & px, bool textured )
{
    REAL const rx = std::min( radiusPx * px.x, ( r - l ) * REAL( .5 ) );
    REAL const ry = std::min( radiusPx * px.y, ( t - b ) * REAL( .5 ) );
    auto point = [&]( REAL x, REAL y )
    {
        if ( textured )
            TexCoord( ( x - l ) / ( r - l ), ( t - y ) / ( t - b ) );
        Vertex( x, y );
    };
    struct Corner { REAL x, y, angle; };
    Corner const corners[4] = { { r - rx, t - ry, 0 }, { l + rx, t - ry, 90 }, { l + rx, b + ry, 180 }, { r - rx, b + ry, 270 } };
    int const steps = 8;
    BeginTriangleFan();
    point( ( l + r ) * REAL( .5 ), ( b + t ) * REAL( .5 ) );
    for ( Corner const & c: corners )
        for ( int s = 0; s <= steps; ++s )
        {
            REAL const a = ( c.angle + 90.0 * s / steps ) * M_PI / 180;
            point( c.x + rx * cos( a ), c.y + ry * sin( a ) );
        }
    point( r, t - ry );
    RenderEnd();
}

void cs_Rect( REAL l, REAL b, REAL r, REAL t )
{
    BeginQuads();
    Vertex( l, b ); Vertex( r, b ); Vertex( r, t ); Vertex( l, t );
    RenderEnd();
}

void cs_Circle( REAL x, REAL y, REAL radiusPx, cs_Pixel const & px )
{
    int const steps = 20;
    BeginTriangleFan();
    Vertex( x, y );
    for ( int s = 0; s <= steps; ++s )
    {
        REAL const a = 2 * M_PI * s / steps;
        Vertex( x + radiusPx * px.x * cos( a ), y + radiusPx * px.y * sin( a ) );
    }
    RenderEnd();
}

REAL cs_Width( tString const & s, REAL h )
{
    return rTextField::GetTextLength( s, h, true );
}

tString cs_Fit( tString const & s, REAL h, REAL maxWidth )
{
    if ( maxWidth <= 0 || cs_Width( s, h ) <= maxWidth )
        return s;
    std::string t = s;
    while ( !t.empty() )
    {
        size_t cut = t.size() - 1;
        while ( cut > 0 && ( static_cast< unsigned char >( t[cut] ) & 0xC0 ) == 0x80 )
            --cut;
        t.erase( cut );
        tString candidate( t + "\xe2\x80\xa6" );
        if ( cs_Width( candidate, h ) <= maxWidth )
            return candidate;
    }
    return tString();
}

void cs_Text( REAL x, REAL y, REAL h, tString const & s, tColor const & color, int align = -1, REAL maxWidth = 0 )
{
    if ( color.a_ <= .005 )
        return;
    rTextField::SetDefaultColor( tColor( 1, 1, 1, 1 ) );
    rTextField::SetBlendColor( color );
    DisplayText( x, y, h, cs_Fit( s, h, maxWidth ).c_str(), sr_fontCockpit, align );
    rTextField::SetBlendColor( tColor( 1, 1, 1, 1 ) );
}

//! splits a string into its UTF-8 characters
std::vector< tString > cs_Chars( tString const & s )
{
    std::vector< tString > chars;
    for ( size_t i = 0; i < s.size(); )
    {
        size_t len = 1;
        while ( i + len < s.size() && ( static_cast< unsigned char >( s[i + len] ) & 0xC0 ) == 0x80 )
            ++len;
        chars.push_back( tString( s.substr( i, len ) ) );
        i += len;
    }
    return chars;
}

//! draws text with extra space between the letters
void cs_SpacedText( REAL x, REAL y, REAL h, tString const & s, tColor const & color, REAL spacing, REAL maxWidth )
{
    std::vector< tString > chars = cs_Chars( s );
    REAL const dots = cs_Width( tString( "\xe2\x80\xa6" ), h );
    for ( size_t i = 0; i < chars.size(); ++i )
    {
        REAL const w = cs_Width( chars[i], h );
        if ( x + w + ( i + 1 < chars.size() ? dots + spacing : 0 ) > maxWidth )
        {
            cs_Text( x, y, h, tString( "\xe2\x80\xa6" ), color );
            return;
        }
        cs_Text( x, y, h, chars[i], color );
        x += w + spacing;
    }
}

tString cs_Time( int ms )
{
    int const seconds = std::max( 0, ms / 1000 );
    char buffer[32];
    snprintf( buffer, sizeof( buffer ), "%d:%02d", seconds / 60, seconds % 60 );
    return tString( buffer );
}

std::vector< tString > cs_Wrap( tString const & text, REAL h, REAL maxWidth )
{
    std::vector< tString > rows;
    tString row;
    size_t start = 0;
    while ( start < text.size() )
    {
        size_t end = text.find( ' ', start );
        if ( end == std::string::npos )
            end = text.size();
        tString word( text.substr( start, end - start ) );
        start = end + 1;
        if ( word.empty() )
            continue;
        tString candidate = row;
        if ( !candidate.empty() )
            candidate << ' ';
        candidate << word;
        if ( !row.empty() && cs_Width( candidate, h ) > maxWidth )
        {
            rows.push_back( row );
            row = word;
        }
        else
            row = candidate;
    }
    if ( !row.empty() )
        rows.push_back( row );
    return rows;
}

}

namespace cWidget {

bool Spotify::Process(tXmlParser::node cur) {
    if ( WithCoordinates::Process(cur) )
        return true;
    DisplayError(cur);
    return false;
}

void Spotify::Render() {
    std::string bytes;
    if ( gSpotify::AlbumArt( cs_artVersion, bytes ) )
        cs_Decode( bytes );
    cs_Upload();

    if ( !gSpotify::NowPlaying( m_info, false ) )
    {
        m_trackVersion = -1;
        return;
    }

    double const now = tSysTimeFloat();
    if ( m_info.trackVersion != m_trackVersion )
    {
        m_trackVersion = m_info.trackVersion;
        m_trackStart = now;
        m_emphasis.clear();
        m_current = -2;
        m_info.lines.clear();
    }
    if ( m_info.lines.empty() )
        gSpotify::NowPlaying( m_info, true );

    REAL const dt = m_lastTime < 0 ? 0 : cs_Clamp( now - m_lastTime, 0, .1 );
    m_lastTime = now;
    REAL const alpha = cs_Clamp( ( now - m_trackStart ) / .35, 0, 1 );

    RenderEnd( true );
    glDisable( GL_TEXTURE_2D );
    glEnable( GL_BLEND );
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

    RenderCard( m_info, alpha );
    RenderLyrics( m_info, alpha, dt );

    glDisable( GL_TEXTURE_2D );
    Color( 1, 1, 1, 1 );
}

void Spotify::RenderCard( gSpotifyNowPlaying const & info, REAL alpha )
{
    cs_Pixel const px;
    REAL const A = m_size.y, Apx = A / px.y;
    REAL const L = m_position.x, T = m_position.y, R = m_position.x + m_size.x;
    REAL const artW = Apx * px.x;

    // album cover with rounded corners
    if ( cs_texture )
    {
        glEnable( GL_TEXTURE_2D );
        glBindTexture( GL_TEXTURE_2D, cs_texture );
        glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
        Color( 1, 1, 1, alpha );
        cs_RoundRect( L, T - A, L + artW, T, Apx * .13, px, true );
        glDisable( GL_TEXTURE_2D );
    }
    else
    {
        Color( .16, .09, .18, .85 * alpha );
        cs_RoundRect( L, T - A, L + artW, T, Apx * .13, px, false );
    }

    REAL const x0 = L + artW + Apx * .2 * px.x;
    REAL const title = info.playing ? 1 : .7;

    // title, drawn twice for a bolder look
    REAL const titleH = A * .34;
    cs_Text( x0, T - A * .3, titleH, info.title, tColor( 1, 1, 1, alpha * title ), -1, R - x0 );
    if ( Apx >= 60 )
        cs_Text( x0 + px.x, T - A * .3, titleH, info.title, tColor( 1, 1, 1, alpha * title ), -1, R - x0 - px.x );

    // time on the right, artist on the left
    REAL const rowY = T - A * .62, timeH = std::max( A * REAL( .19 ), 13 * px.y );
    tString time = cs_Time( info.progressMs );
    time << " / " << cs_Time( info.durationMs );
    REAL const timeW = cs_Width( time, timeH );
    cs_Text( R, rowY, timeH, time, tColor( .82, .78, .85, .85 * alpha ), 1 );

    REAL const sepX = R - timeW - Apx * .12 * px.x;
    glDisable( GL_TEXTURE_2D );
    Color( .82, .78, .85, .45 * alpha );
    cs_Rect( sepX - px.x * .5, rowY - timeH * .32, sepX + px.x * .5, rowY + timeH * .32 );

    REAL const artistH = std::max( A * REAL( .19 ), 13 * px.y );
    cs_SpacedText( x0, rowY, artistH, info.artist.ToUpper(), tColor( .86, .82, .89, .85 * alpha ),
                   artistH * .12 * px.x / px.y, sepX - Apx * .12 * px.x );

    // progress bar with a knob
    REAL const barY = T - A * .86;
    REAL const thick = std::max( REAL( 2 ), Apx * REAL( .022 ) ) * px.y * .5;
    REAL const done = info.durationMs > 0 ? cs_Clamp( REAL( info.progressMs ) / info.durationMs, 0, 1 ) : 0;
    REAL const knobX = x0 + ( R - x0 ) * done;
    glDisable( GL_TEXTURE_2D );
    Color( 1, 1, 1, .22 * alpha );
    cs_Rect( knobX, barY - thick, R, barY + thick );
    Color( 1, 1, 1, .9 * alpha );
    cs_Rect( x0, barY - thick, knobX, barY + thick );
    cs_Circle( knobX, barY, std::max( REAL( 3.5 ), Apx * REAL( .05 ) ), px );
}

void Spotify::RenderLyrics( gSpotifyNowPlaying const & info, REAL alpha, REAL dt )
{
    if ( !info.showLyrics )
        return;
    if ( info.lines.empty() )
    {
        // say why there is nothing to show
        tString message( info.lyricsState == gSpotifyNowPlaying::LYRICS_SEARCHING ? "Looking for lyrics\xe2\x80\xa6" :
                         info.lyricsState == gSpotifyNowPlaying::LYRICS_UNREACHABLE ? "Lyrics service unreachable" :
                         "No synced lyrics for this song" );
        if ( info.lyricsState == gSpotifyNowPlaying::LYRICS_UNREACHABLE )
        {
            if ( !info.lyricsError.empty() )
                message << " (" << info.lyricsError << ")";
            message << ", trying again soon";
        }
        REAL const A = m_size.y;
        cs_Text( m_position.x, m_position.y - A * 1.45f, A * .19f, message, tColor( .8, .75, .82, .55 * alpha ), -1, m_size.x );
        return;
    }

    cs_Pixel const px;
    REAL const A = m_size.y;
    REAL const L = m_position.x, W = m_size.x;
    REAL const small = A * .26, big = A * .36, lead = 1.18, gap = small * .45;
    REAL const top = m_position.y - A - A * .3;
    REAL const bottom = top - A * 2.5;

    // an empty line in front of the first lyric, so the intro shows the waiting dots
    bool const intro = info.lines.front().ms > 2500;
    int const count = int( info.lines.size() ) + ( intro ? 1 : 0 );
    auto timeOf = [&]( int i ) { return intro ? ( i == 0 ? 0 : info.lines[i - 1].ms ) : info.lines[i].ms; };
    auto textOf = [&]( int i ) { static tString const none; return intro ? ( i == 0 ? none : info.lines[i - 1].text ) : info.lines[i].text; };

    int current = 0;
    for ( int i = 0; i < count; ++i )
        if ( timeOf( i ) <= info.progressMs )
            current = i;

    bool const snap = m_current == -2 || int( m_emphasis.size() ) != count;
    if ( int( m_emphasis.size() ) != count )
        m_emphasis.assign( count, 0 );
    for ( int i = 0; i < count; ++i )
        m_emphasis[i] = snap ? ( i == current ? 1 : 0 ) : cs_Approach( m_emphasis[i], i == current ? 1 : 0, 9, dt );

    // lay the lines out from the top, each one as tall as its wrapped text
    std::vector< REAL > height( count ), size( count ), offset( count );
    std::vector< std::vector< tString > > rows( count );
    REAL y = 0;
    for ( int i = 0; i < count; ++i )
    {
        size[i] = small + ( big - small ) * m_emphasis[i];
        // wrapped at the largest size, so the line breaks stay put while a line grows
        rows[i] = cs_Wrap( textOf( i ), big, W );
        REAL const dots = small * ( .5 + .6 * m_emphasis[i] );
        height[i] = ( rows[i].empty() ? dots : rows[i].size() * size[i] * lead ) + gap;
        offset[i] = y;
        y += height[i];
    }

    // the current line sits one line below the top, the list glides to it
    REAL const target = offset[ current ];
    m_scroll = snap ? target : cs_Approach( m_scroll, target, 7, dt );
    m_current = current;
    REAL const anchor = top - small * lead - gap;

    for ( int i = 0; i < count; ++i )
    {
        REAL const lineTop = anchor + m_scroll - offset[i];
        REAL const lineBottom = lineTop - height[i];
        if ( lineBottom > top + small || lineTop < bottom - small )
            continue;

        int const d = i - current;
        REAL const dim = d == -1 ? .3 : ( d == 1 ? .5 : ( d == 2 ? .32 : ( d == 3 ? .18 : 0 ) ) );
        REAL a = dim + ( 1 - dim ) * m_emphasis[i];
        a *= cs_Clamp( 1 - ( lineTop - top ) / ( small * .8f ), 0, 1 );
        a *= cs_Clamp( ( lineBottom - bottom ) / ( small * 1.5 ) + 1, 0, 1 );
        a *= alpha;
        if ( a <= .01 )
            continue;

        REAL const e = m_emphasis[i];
        tColor const color( .93 + .07 * e, .85 + .15 * e, .91 + .09 * e, a );

        if ( rows[i].empty() )
        {
            // instrumental break: three dots that light up as it goes on
            if ( e < .05 )
                continue;
            int const next = i + 1 < count ? timeOf( i + 1 ) : info.durationMs;
            REAL const length = std::max( 1, next - timeOf( i ) );
            REAL const done = cs_Clamp( REAL( info.progressMs - timeOf( i ) ) / length, 0, 1 );
            REAL const pulse = 1 + .12 * sin( tSysTimeFloat() * 4 );
            REAL const radius = small * .16 / px.y * e * pulse;
            for ( int k = 0; k < 3; ++k )
            {
                REAL const lit = cs_Clamp( done * 3 - k, 0, 1 );
                glDisable( GL_TEXTURE_2D );
                Color( 1, 1, 1, a * ( .35 + .65 * lit ) );
                cs_Circle( L + radius * px.x + k * radius * 3.2 * px.x, lineTop - height[i] * .45, radius, px );
            }
            continue;
        }

        for ( size_t r = 0; r < rows[i].size(); ++r )
        {
            REAL const rowY = lineTop - size[i] * lead * ( r + .5 );
            cs_Text( L, rowY, size[i], rows[i][r], color, -1, W );
            if ( e > .5 && size[i] / px.y >= 22 )
                cs_Text( L + px.x, rowY, size[i], rows[i][r], tColor( color.r_, color.g_, color.b_, color.a_ * ( e - .5 ) * 2 ), -1, W - px.x );
        }
    }
}

}

#endif
