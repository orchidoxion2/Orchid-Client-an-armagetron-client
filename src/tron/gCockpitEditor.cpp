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

#include "gCockpitEditor.h"

#include "aa_config.h"

tString gCockpitEditor::CustomFile()
{
    return tString( "Orchid/custom_hud-1.0.0.aacockpit.xml" );
}

#ifdef DEDICATED

void gCockpitEditor::Open(){}
bool gCockpitEditor::UsingCustom(){ return false; }
void gCockpitEditor::UseCustom( bool ){}

#else

#include "cockpit/cCockpit.h"
#include "rFont.h"
#include "rGL.h"
#include "rRender.h"
#include "rScreen.h"
#include "rSDL.h"
#include "tDirectories.h"
#include "tResourceManager.h"
#include "tSysTime.h"
#include "uMenu.h"

#include <libxml/parser.h>
#include <libxml/tree.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{

// *******************************************************************************
// the HUD file
// *******************************************************************************

xmlChar const * ce_X( char const * s )
{
    return reinterpret_cast< xmlChar const * >( s );
}

xmlNodePtr ce_Child( xmlNodePtr node, char const * name )
{
    for ( xmlNodePtr c = node ? node->children : NULL; c; c = c->next )
        if ( c->type == XML_ELEMENT_NODE && !xmlStrcmp( c->name, ce_X( name ) ) )
            return c;
    return NULL;
}

tString ce_Prop( xmlNodePtr node, char const * name )
{
    tString ret;
    if ( !node )
        return ret;
    xmlChar * value = xmlGetProp( node, ce_X( name ) );
    if ( value )
    {
        ret = reinterpret_cast< char const * >( value );
        xmlFree( value );
    }
    return ret;
}

REAL ce_Number( xmlNodePtr node, char const * name, REAL fallback )
{
    tString const value = ce_Prop( node, name );
    return value.empty() ? fallback : REAL( atof( value.c_str() ) );
}

void ce_SetNumber( xmlNodePtr node, char const * name, REAL value )
{
    char buffer[32];
    snprintf( buffer, sizeof( buffer ), "%.4f", value );
    // drop trailing zeros
    char * end = buffer + strlen( buffer ) - 1;
    while ( end > buffer && *end == '0' )
        *end-- = 0;
    if ( *end == '.' )
        *end = 0;
    xmlSetProp( node, ce_X( name ), ce_X( buffer ) );
}

void ce_Sources( xmlNodePtr node, std::vector< tString > & sources )
{
    for ( xmlNodePtr c = node ? node->children : NULL; c; c = c->next )
    {
        if ( c->type != XML_ELEMENT_NODE )
            continue;
        if ( !xmlStrcmp( c->name, ce_X( "AtomicData" ) ) )
            sources.push_back( ce_Prop( c, "source" ) );
        else
            ce_Sources( c, sources );
    }
}

tString ce_Friendly( tString const & source )
{
    static char const * const names[][2] = {
        { "current_framerate", "FPS" }, { "current_ping", "Ping" }, { "player_speed", "Speed" },
        { "player_rubber", "Rubber" }, { "player_brakes", "Brake" }, { "current_score_colored", "Score" },
        { "top_other_score_colored", "Best opponent" }, { "round_text", "Round" }, { "spotify_track", "Spotify song" },
        { "spotify_lyric", "Spotify lyric" }, { "current_colored_name", "Name" }, { "current_time", "Clock" } };
    for ( auto const & n: names )
        if ( source == n[0] )
            return tString( n[1] );
    tString ret( source );
    for ( size_t i = 0; i < ret.size(); ++i )
        if ( ret[i] == '_' )
            ret[i] = ' ';
    return ret;
}

struct ce_Widget
{
    xmlNodePtr node = NULL;
    tString type, label;
    REAL x = 0, y = 0, w = .1, h = .05;
    int align = -1;
};

struct ce_Box
{
    REAL l, b, r, t;
};

ce_Box ce_BoxOf( ce_Widget const & w )
{
    if ( w.type == "Label" )
    {
        REAL const width = std::max( REAL( .12 ), w.h * 4.5f );
        REAL const l = w.align < 0 ? w.x : ( w.align == 0 ? w.x - width * .5f : w.x - width );
        return ce_Box{ l, w.y - w.h * .5f, l + width, w.y + w.h * .5f };
    }
    if ( w.type == "Spotify" )
        return ce_Box{ w.x, w.y - w.h * 3.8f, w.x + w.w, w.y };
    if ( w.type == "Leaderboard" )
        return ce_Box{ w.x, w.y - w.h * 9.2f, w.x + w.w, w.y };
    if ( w.type == "KeyOverlay" )
        return ce_Box{ w.x, w.y, w.x + w.w, w.y + w.h * 5.2f };
    return ce_Box{ w.x - w.w, w.y - w.h, w.x + w.w, w.y + w.h };
}

class ce_Document
{
public:
    xmlDocPtr doc = NULL;
    xmlNodePtr cockpit = NULL;
    std::vector< ce_Widget > widgets;
    bool screenCoordinates = true;

    ~ce_Document()
    {
        Clear();
    }

    void Clear()
    {
        if ( doc )
            xmlFreeDoc( doc );
        doc = NULL;
        cockpit = NULL;
        widgets.clear();
    }

    bool Load( tString const & path )
    {
        Clear();
        if ( path.empty() )
            return false;
        // read it ourselves: the game routes libxml2's own file reading through its resource lookup
        std::string text;
        FILE * f = fopen( path.c_str(), "rb" );
        if ( f )
        {
            char buffer[4096];
            size_t got;
            while ( ( got = fread( buffer, 1, sizeof( buffer ), f ) ) > 0 )
                text.append( buffer, got );
            fclose( f );
        }
        doc = text.empty() ? NULL : xmlReadMemory( text.data(), int( text.size() ), path.c_str(), NULL, XML_PARSE_NOBLANKS );
        cockpit = doc ? ce_Child( xmlDocGetRootElement( doc ), "Cockpit" ) : NULL;
        if ( !cockpit )
        {
            Clear();
            return false;
        }
        screenCoordinates = ce_Prop( cockpit, "coordinates" ) == "screen";
        Collect();
        return true;
    }

    //! reads the widgets from the document
    void Collect()
    {
        widgets.clear();
        for ( xmlNodePtr c = cockpit ? cockpit->children : NULL; c; c = c->next )
        {
            if ( c->type != XML_ELEMENT_NODE || !xmlStrcmp( c->name, ce_X( "WidgetTemplate" ) ) )
                continue;
            ce_Widget w;
            w.node = c;
            w.type = reinterpret_cast< char const * >( c->name );
            xmlNodePtr const position = ce_Child( c, "Position" );
            xmlNodePtr const size = ce_Child( c, "Size" );
            w.x = ce_Number( position, "x", 0 );
            w.y = ce_Number( position, "y", 0 );
            w.w = ce_Number( size, "width", .1 );
            w.h = ce_Number( size, "height", .05 );
            tString const align = ce_Prop( ce_Child( c, "Align" ), "value" );
            w.align = align == "center" ? 0 : ( align == "right" ? 1 : -1 );

            std::vector< tString > sources;
            ce_Sources( ce_Child( c, "DataSet" ) ? c : NULL, sources );
            if ( w.type == "Map" )
                w.label = "Map";
            else if ( w.type == "Rectangle" )
                w.label = "Background box";
            else if ( w.type == "Camview" )
                w.label = "Camera view";
            else if ( w.type == "Spotify" || w.type == "Leaderboard" )
                w.label = w.type;
            else if ( w.type == "KeyOverlay" )
                w.label = "Key overlay";
            else if ( !sources.empty() )
            {
                for ( size_t s = 0; s < sources.size(); ++s )
                {
                    if ( s > 0 )
                        w.label << " / ";
                    w.label << ce_Friendly( sources[s] );
                }
            }
            else
            {
                tString caption = ce_Prop( ce_Child( ce_Child( c, "Caption" ), "Text" ), "value" );
                xmlNodePtr const face = ce_Child( c, "Face" );
                if ( face )
                {
                    // a fixed text like the SPEED caption
                    std::vector< xmlNodePtr > stack( 1, face );
                    while ( !stack.empty() )
                    {
                        xmlNodePtr n = stack.back();
                        stack.pop_back();
                        for ( xmlNodePtr k = n->children; k; k = k->next )
                        {
                            if ( k->type != XML_ELEMENT_NODE )
                                continue;
                            if ( !xmlStrcmp( k->name, ce_X( "Text" ) ) )
                                caption = ce_Prop( k, "value" );
                            else
                                stack.push_back( k );
                        }
                    }
                }
                w.label = tColoredString::RemoveColors( caption, false );
            }
            if ( w.label.empty() )
                w.label = w.type;
            widgets.push_back( w );
        }
    }

    //! writes the edited positions and sizes back into the document
    void Store()
    {
        for ( ce_Widget const & w: widgets )
        {
            xmlNodePtr position = ce_Child( w.node, "Position" );
            if ( !position )
                position = xmlNewChild( w.node, NULL, ce_X( "Position" ), NULL );
            ce_SetNumber( position, "x", w.x );
            ce_SetNumber( position, "y", w.y );
            xmlNodePtr size = ce_Child( w.node, "Size" );
            if ( !size )
                size = xmlNewChild( w.node, NULL, ce_X( "Size" ), NULL );
            ce_SetNumber( size, "width", w.w );
            ce_SetNumber( size, "height", w.h );
        }
    }

    bool Save( tString const & path )
    {
        if ( !doc || path.empty() )
            return false;
        Store();
        xmlNodePtr root = xmlDocGetRootElement( doc );
        xmlSetProp( root, ce_X( "author" ), ce_X( "Orchid" ) );
        xmlSetProp( root, ce_X( "name" ), ce_X( "custom_hud" ) );
        xmlSetProp( root, ce_X( "version" ), ce_X( "1.0.0" ) );
        return xmlSaveFormatFile( path.c_str(), doc, 1 ) >= 0;
    }
};

bool ce_Exists( tString const & path )
{
    FILE * f = path.empty() ? NULL : fopen( path.c_str(), "r" );
    if ( f )
        fclose( f );
    return f != NULL;
}

tString ce_CustomPath()
{
    return tDirectories::Resource().GetWritePath( gCockpitEditor::CustomFile() );
}

// *******************************************************************************
// drawing
// *******************************************************************************

struct ce_Color { REAL r, g, b, a; };
ce_Color const ce_bg        = { .071, .035, .086, 1 };
ce_Color const ce_panel     = { .102, .059, .122, 1 };
ce_Color const ce_screen    = { .02, .02, .03, 1 };
ce_Color const ce_grid      = { .2, .16, .24, .5 };
ce_Color const ce_border    = { .227, .125, .243, 1 };
ce_Color const ce_text      = { .969, .886, .937, 1 };
ce_Color const ce_textLow   = { .588, .463, .569, 1 };
ce_Color const ce_accent    = { .839, .251, .624, 1 };

void ce_Rect( REAL l, REAL b, REAL r, REAL t, ce_Color const & c, REAL alpha = 1 )
{
    glDisable( GL_TEXTURE_2D );
    Color( c.r, c.g, c.b, c.a * alpha );
    BeginQuads();
    Vertex( l, b ); Vertex( r, b ); Vertex( r, t ); Vertex( l, t );
    RenderEnd();
}

void ce_Frame( REAL l, REAL b, REAL r, REAL t, ce_Color const & c, REAL width = 1 )
{
    glDisable( GL_TEXTURE_2D );
    glLineWidth( width );
    Color( c.r, c.g, c.b, c.a );
    BeginLineLoop();
    Vertex( l, b ); Vertex( r, b ); Vertex( r, t ); Vertex( l, t );
    RenderEnd();
    glLineWidth( 1 );
}

void ce_Line( REAL x1, REAL y1, REAL x2, REAL y2, ce_Color const & c )
{
    glDisable( GL_TEXTURE_2D );
    Color( c.r, c.g, c.b, c.a );
    BeginLines();
    Vertex( x1, y1 ); Vertex( x2, y2 );
    RenderEnd();
}

REAL ce_TextWidth( tString const & s, REAL h )
{
    return rTextField::GetTextLength( s, h, true );
}

void ce_Text( REAL x, REAL y, REAL h, tString const & s, ce_Color const & c, int align = -1, REAL maxWidth = 0 )
{
    tString text = s;
    if ( maxWidth > 0 && ce_TextWidth( text, h ) > maxWidth )
    {
        while ( !text.empty() && ce_TextWidth( text + "\xe2\x80\xa6", h ) > maxWidth )
        {
            size_t cut = text.size() - 1;
            while ( cut > 0 && ( static_cast< unsigned char >( text[cut] ) & 0xC0 ) == 0x80 )
                --cut;
            text.erase( cut );
        }
        if ( text.empty() )
            return;
        text << "\xe2\x80\xa6";
    }
    rTextField::SetDefaultColor( tColor( 1, 1, 1, 1 ) );
    rTextField::SetBlendColor( tColor( c.r, c.g, c.b, c.a ) );
    DisplayText( x, y, h, text.c_str(), sr_fontServerBrowser, align );
    rTextField::SetBlendColor( tColor( 1, 1, 1, 1 ) );
}

// colors for the kinds of widgets
ce_Color ce_TypeColor( tString const & type )
{
    if ( type == "Label" )
        return ce_Color{ .35, .7, 1, 1 };
    if ( type == "Rectangle" )
        return ce_Color{ .6, .6, .65, 1 };
    if ( type == "Map" || type == "Camview" )
        return ce_Color{ .4, .9, .5, 1 };
    if ( type == "Spotify" || type == "Leaderboard" || type == "KeyOverlay" )
        return ce_Color{ .84, .25, .62, 1 };
    return ce_Color{ 1, .75, .3, 1 };
}

// *******************************************************************************
// the editor screen
// *******************************************************************************

enum ce_Action { CE_ADD, CE_DUPLICATE, CE_DELETE, CE_SAVE, CE_DEFAULT, CE_CLOSE, CE_PICK };

struct ce_Hit
{
    REAL l, b, r, t;
    ce_Action action;
    int param;
};

class ce_Item: public uMenuItem
{
public:
    explicit ce_Item( uMenu * menu ): uMenuItem( menu, "" ){}
    bool DisplayHelp( bool, REAL, REAL ) override { return false; }
};

class ce_Screen: public uMenu
{
public:
    ce_Screen(): uMenu( "", false ), item_( this )
    {
        // edit the player's own HUD if there is one, otherwise start from the HUD in use
        tString const custom = ce_CustomPath();
        tString const current = tResourceManager::locateResource( cCockpit::File() );
        loaded_ = ( ce_Exists( custom ) && doc_.Load( custom ) ) || doc_.Load( current );
        defaults_.Load( tResourceManager::locateResource( cCockpit::DefaultFile() ) );
        if ( !loaded_ )
            Status( "Could not open the HUD file." );
        else if ( !doc_.screenCoordinates )
            Status( "This HUD uses the classic layout space: positions are shown approximately." );
    }

protected:
    int ItemAt( int, int ) override { return -1; }

    void RenderItems() override
    {
        hits_.clear();
        ce_Rect( -1, -1, 1, 1, ce_bg );

        // tool bar
        REAL const barBottom = .84;
        ce_Rect( -1, barBottom, 1, 1, ce_panel );
        ce_Line( -1, barBottom, 1, barBottom, ce_border );
        ce_Text( -.96, .92, .05, tString( "HUD Editor" ), ce_text );
        REAL x = -.5;
        Button( x, barBottom, "Add widget", CE_ADD, !addOpen_ );
        Button( x, barBottom, "Duplicate", CE_DUPLICATE, selected_ >= 0 );
        Button( x, barBottom, "Delete", CE_DELETE, selected_ >= 0 );
        Button( x, barBottom, "Save & use", CE_SAVE, loaded_ );
        Button( x, barBottom, "Use default HUD", CE_DEFAULT, true );
        Button( x, barBottom, "Close", CE_CLOSE, true );

        // the screen, 16:9, with a grid
        REAL const top = .78, bottom = -.74;
        fh_ = top - bottom;
        fw_ = fh_ * 16 / 9 * ( sr_screenHeight > 0 ? REAL( sr_screenHeight ) / sr_screenWidth : 9 / 16.0f );
        if ( fw_ > 1.9 )
        {
            fh_ *= 1.9 / fw_;
            fw_ = 1.9;
        }
        fl_ = -fw_ * .5f;
        fb_ = ( top + bottom ) * .5f - fh_ * .5f;
        ce_Rect( fl_, fb_, fl_ + fw_, fb_ + fh_, ce_screen );
        for ( int g = -3; g <= 3; ++g )
        {
            REAL const gx = WinX( g * .25f ), gy = WinY( g * .25f );
            ce_Line( gx, fb_, gx, fb_ + fh_, ce_grid );
            ce_Line( fl_, gy, fl_ + fw_, gy, ce_grid );
        }
        ce_Frame( fl_, fb_, fl_ + fw_, fb_ + fh_, ce_border );

        // the widgets, the selected one on top
        for ( int i = 0; i < int( doc_.widgets.size() ); ++i )
            if ( i != selected_ )
                DrawWidget( doc_.widgets[i], false );
        if ( selected_ >= 0 && selected_ < int( doc_.widgets.size() ) )
            DrawWidget( doc_.widgets[ selected_ ], true );

        // the selected widget's numbers
        REAL const infoY = -.8;
        if ( selected_ >= 0 && selected_ < int( doc_.widgets.size() ) )
        {
            ce_Widget const & w = doc_.widgets[ selected_ ];
            char numbers[160];
            snprintf( numbers, sizeof( numbers ), "%s  -  %s     x %.3f   y %.3f   width %.3f   height %.3f",
                      w.type.c_str(), w.label.c_str(), w.x, w.y, w.w, w.h );
            ce_Text( -.96, infoY, .04, tString( numbers ), ce_text, -1, 1.92 );
        }
        else if ( loaded_ )
            ce_Text( -.96, infoY, .04, tString( "Click a widget to select it." ), ce_textLow );

        // status message and help line
        if ( !status_.empty() && tSysTimeFloat() < statusUntil_ )
            ce_Text( .96, infoY, .04, status_, ce_accent, 1 );
        else if ( dirty_ )
            ce_Text( .96, infoY, .04, tString( "Unsaved changes" ), ce_textLow, 1 );
        ce_Line( -1, -.87, 1, -.87, ce_border );
        ce_Text( -.96, -.93, .036, tString( "Drag to move   -   scroll or +/- to resize   -   arrows nudge (shift: more)   -   Del delete   -   D duplicate   -   S save & use   -   Esc close" ), ce_textLow, -1, 1.92 );

        if ( addOpen_ )
            RenderAddList( barBottom );
    }

    void HandleEvent( SDL_Event event ) override
    {
        switch ( event.type )
        {
        case SDL_MOUSEMOTION:
            MouseToMenu( event.motion.x, event.motion.y );
            if ( dragging_ && selected_ >= 0 )
            {
                ce_Widget & w = doc_.widgets[ selected_ ];
                w.x = startX_ + ( mouseX_ - grabX_ ) / fw_ * 2;
                w.y = startY_ + ( mouseY_ - grabY_ ) / fh_ * 2;
                dirty_ = true;
            }
            return;
        case SDL_MOUSEBUTTONDOWN:
            MouseToMenu( event.button.x, event.button.y );
            if ( event.button.button == SDL_BUTTON_LEFT )
                Click();
            else if ( event.button.button == SDL_BUTTON_RIGHT )
            {
                if ( addOpen_ )
                    addOpen_ = false;
                else
                    selected_ = -1;
            }
            return;
        case SDL_MOUSEBUTTONUP:
            dragging_ = false;
            return;
#if SDL_VERSION_ATLEAST(2,0,0)
        case SDL_MOUSEWHEEL:
            if ( addOpen_ )
                addScroll_ = std::max( 0, addScroll_ - event.wheel.y );
            else
                Scale( event.wheel.y > 0 ? 1.06f : 1 / 1.06f );
            return;
#endif
        case SDL_KEYDOWN:
            if ( HandleKey( event.key.keysym.sym, event.key.keysym.mod ) )
                return;
            break;
        default:
            break;
        }
        uMenu::HandleEvent( event );
    }

private:
    ce_Item item_;
    ce_Document doc_, defaults_;
    bool loaded_ = false;
    std::vector< ce_Hit > hits_;
    int selected_ = -1;
    bool dragging_ = false;
    REAL mouseX_ = -10, mouseY_ = -10, grabX_ = 0, grabY_ = 0, startX_ = 0, startY_ = 0;
    REAL fl_ = -1, fb_ = -1, fw_ = 2, fh_ = 2;
    bool addOpen_ = false;
    int addScroll_ = 0;
    bool dirty_ = false;
    tString status_;
    double statusUntil_ = 0;

    REAL WinX( REAL x ) const { return fl_ + ( x + 1 ) * .5f * fw_; }
    REAL WinY( REAL y ) const { return fb_ + ( y + 1 ) * .5f * fh_; }

    void Status( char const * message )
    {
        status_ = message;
        statusUntil_ = tSysTimeFloat() + 4;
    }

    bool Hover( REAL l, REAL b, REAL r, REAL t ) const
    {
        return mouseX_ >= l && mouseX_ <= r && mouseY_ >= b && mouseY_ <= t;
    }

    void MouseToMenu( int x, int y )
    {
        int width = sr_screenWidth, height = sr_screenHeight;
#if SDL_VERSION_ATLEAST(2,0,0)
        if ( sr_screen )
            SDL_GetWindowSize( sr_screen, &width, &height );
#endif
        if ( width > 0 && height > 0 )
        {
            mouseX_ = 2 * REAL( x ) / width - 1;
            mouseY_ = 1 - 2 * REAL( y ) / height;
        }
    }

    void Button( REAL & x, REAL barBottom, char const * label, ce_Action action, bool enabled )
    {
        REAL const h = .045, pad = .03;
        REAL const w = ce_TextWidth( tString( label ), h ) + pad * 2;
        REAL const b = barBottom + .03, t = .96;
        bool const hover = enabled && Hover( x, b, x + w, t );
        ce_Rect( x, b, x + w, t, hover ? ce_accent : ce_border, enabled ? 1 : .4f );
        ce_Text( x + w * .5f, ( b + t ) * .5f, h, tString( label ), ce_text, 0 );
        if ( enabled )
            hits_.push_back( ce_Hit{ x, b, x + w, t, action, 0 } );
        x += w + .02;
    }

    void DrawWidget( ce_Widget const & w, bool selected )
    {
        ce_Box const box = ce_BoxOf( w );
        REAL const l = WinX( box.l ), r = WinX( box.r ), b = WinY( box.b ), t = WinY( box.t );
        ce_Color const color = ce_TypeColor( w.type );
        ce_Rect( l, b, r, t, color, selected ? .35f : .18f );
        ce_Frame( l, b, r, t, selected ? ce_accent : color, selected ? 3 : 1 );
        REAL const h = std::min( REAL( .036 ), ( t - b ) * .8f );
        ce_Text( ( l + r ) * .5f, ( b + t ) * .5f, std::max( h, REAL( .025 ) ), w.label, ce_text, 0, std::max( r - l - .01f, REAL( .05 ) ) );
    }

    void RenderAddList( REAL barBottom )
    {
        int const rows = 14;
        REAL const rowH = .07, left = -.5, width = .9;
        REAL const top = barBottom - .02;
        int const count = int( defaults_.widgets.size() );
        addScroll_ = std::min( addScroll_, std::max( 0, count - rows ) );
        int const shown = std::min( rows, count );
        REAL const bottom = top - rowH * ( shown + 1 );
        ce_Rect( left, bottom, left + width, top, ce_panel );
        ce_Frame( left, bottom, left + width, top, ce_accent );
        ce_Text( left + .03, top - rowH * .5f, .04, tString( "Add a widget from the default HUD" ), ce_textLow );
        for ( int k = 0; k < shown; ++k )
        {
            int const index = k + addScroll_;
            ce_Widget const & w = defaults_.widgets[ index ];
            REAL const rt = top - rowH * ( k + 1 ), rb = rt - rowH;
            if ( Hover( left, rb, left + width, rt ) )
                ce_Rect( left, rb, left + width, rt, ce_border );
            ce_Rect( left + .03, rb + rowH * .3f, left + .045, rt - rowH * .3f, ce_TypeColor( w.type ) );
            tString text = w.label;
            text << "   (" << w.type << ")";
            ce_Text( left + .07, ( rb + rt ) * .5f, .042, text, ce_text, -1, width - .1 );
            hits_.push_back( ce_Hit{ left, rb, left + width, rt, CE_PICK, index } );
        }
        if ( count == 0 )
            ce_Text( left + .03, top - rowH * 1.5f, .04, tString( "The default HUD could not be read." ), ce_textLow );
    }

    void Click()
    {
        for ( auto it = hits_.rbegin(); it != hits_.rend(); ++it )
            if ( Hover( it->l, it->b, it->r, it->t ) )
            {
                Do( it->action, it->param );
                return;
            }
        if ( addOpen_ )
        {
            addOpen_ = false;
            return;
        }

        // the topmost widget under the mouse: the selected one first, then the last drawn
        selected_ = HitWidget();
        if ( selected_ >= 0 )
        {
            dragging_ = true;
            grabX_ = mouseX_;
            grabY_ = mouseY_;
            startX_ = doc_.widgets[ selected_ ].x;
            startY_ = doc_.widgets[ selected_ ].y;
        }
    }

    int HitWidget() const
    {
        auto inside = [&]( int i )
        {
            ce_Box const box = ce_BoxOf( doc_.widgets[i] );
            return Hover( WinX( box.l ), WinY( box.b ), WinX( box.r ), WinY( box.t ) );
        };
        if ( selected_ >= 0 && selected_ < int( doc_.widgets.size() ) && inside( selected_ ) )
            return selected_;
        for ( int i = int( doc_.widgets.size() ) - 1; i >= 0; --i )
            if ( inside( i ) )
                return i;
        return -1;
    }

    //! adds a copy of a widget node and selects it
    void Insert( xmlNodePtr source, REAL dx, REAL dy )
    {
        if ( !doc_.doc || !source )
            return;
        doc_.Store();
        xmlNodePtr copy = xmlDocCopyNode( source, doc_.doc, 1 );
        if ( !copy )
            return;
        xmlAddChild( doc_.cockpit, copy );
        doc_.Collect();
        for ( int i = 0; i < int( doc_.widgets.size() ); ++i )
            if ( doc_.widgets[i].node == copy )
            {
                selected_ = i;
                doc_.widgets[i].x += dx;
                doc_.widgets[i].y += dy;
            }
        dirty_ = true;
    }

    void Do( ce_Action action, int param )
    {
        switch ( action )
        {
        case CE_ADD:
            addOpen_ = true;
            addScroll_ = 0;
            break;
        case CE_PICK:
            addOpen_ = false;
            if ( param >= 0 && param < int( defaults_.widgets.size() ) )
                Insert( defaults_.widgets[ param ].node, 0, 0 );
            break;
        case CE_DUPLICATE:
            if ( selected_ >= 0 )
                Insert( doc_.widgets[ selected_ ].node, .03f, -.03f );
            break;
        case CE_DELETE:
            if ( selected_ >= 0 )
            {
                doc_.Store();
                xmlNodePtr node = doc_.widgets[ selected_ ].node;
                xmlUnlinkNode( node );
                xmlFreeNode( node );
                doc_.Collect();
                selected_ = -1;
                dirty_ = true;
            }
            break;
        case CE_SAVE:
            if ( doc_.Save( ce_CustomPath() ) )
            {
                cCockpit::UseFile( gCockpitEditor::CustomFile() );
                dirty_ = false;
                Status( "Saved. Your HUD is now in use." );
            }
            else
                Status( "Could not save the HUD file." );
            break;
        case CE_DEFAULT:
            cCockpit::UseFile( cCockpit::DefaultFile() );
            Status( "The default HUD is in use. Your own HUD is kept." );
            break;
        case CE_CLOSE:
            Exit();
            break;
        }
    }

    void Scale( REAL factor )
    {
        if ( selected_ < 0 )
            return;
        ce_Widget & w = doc_.widgets[ selected_ ];
        w.w *= factor;
        w.h *= factor;
        dirty_ = true;
    }

    bool HandleKey( SDL_Keycode key, Uint16 mod )
    {
        REAL const step = ( mod & KMOD_SHIFT ) ? .02f : .005f;
        ce_Widget * w = selected_ >= 0 && selected_ < int( doc_.widgets.size() ) ? &doc_.widgets[ selected_ ] : NULL;
        switch ( key )
        {
        case SDLK_ESCAPE:
            if ( addOpen_ )
            {
                addOpen_ = false;
                return true;
            }
            return false;
        case SDLK_LEFT:  if ( w ) { w->x -= step; dirty_ = true; } return true;
        case SDLK_RIGHT: if ( w ) { w->x += step; dirty_ = true; } return true;
        case SDLK_UP:    if ( w ) { w->y += step; dirty_ = true; } return true;
        case SDLK_DOWN:  if ( w ) { w->y -= step; dirty_ = true; } return true;
        case SDLK_PLUS:
        case SDLK_EQUALS:
        case SDLK_KP_PLUS:
            Scale( 1.06f );
            return true;
        case SDLK_MINUS:
        case SDLK_KP_MINUS:
            Scale( 1 / 1.06f );
            return true;
        case SDLK_DELETE:
        case SDLK_BACKSPACE:
            Do( CE_DELETE, 0 );
            return true;
        case SDLK_d:
            Do( CE_DUPLICATE, 0 );
            return true;
        case SDLK_s:
            Do( CE_SAVE, 0 );
            return true;
        default:
            return false;
        }
    }
};

}

void gCockpitEditor::Open()
{
    ce_Screen screen;
    screen.Enter();
}

bool gCockpitEditor::UsingCustom()
{
    return cCockpit::File() == CustomFile();
}

void gCockpitEditor::UseCustom( bool use )
{
    if ( !use )
    {
        cCockpit::UseFile( cCockpit::DefaultFile() );
        return;
    }

    // the first time, the player's own HUD starts as a copy of the default one
    tString const custom = ce_CustomPath();
    if ( !ce_Exists( custom ) )
    {
        ce_Document doc;
        if ( !doc.Load( tResourceManager::locateResource( cCockpit::DefaultFile() ) ) || !doc.Save( custom ) )
            return;
    }
    cCockpit::UseFile( CustomFile() );
}

#endif
