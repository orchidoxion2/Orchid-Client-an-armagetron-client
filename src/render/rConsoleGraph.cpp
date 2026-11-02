/*

*************************************************************************

ArmageTron -- Just another Tron Lightcycle Game in 3D.
Copyright (C) 2000  Manuel Moos (manuel@moosnet.de)

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

#include "aa_config.h"

#ifndef DEDICATED

#include "rFont.h"
#include "rRender.h"
#include "tSysTime.h"
#include "rConsole.h"
#include "rSDL.h"
#include <cctype>
#include <cstring>
#include <vector>
#include "rSysdep.h"
#include "rScreen.h"
#include "rGL.h"
#include "tConfiguration.h"
#include "rDisplayList.h"

static tColoredString sr_centerString;
static REAL center_r,center_g,center_b,center_fadetime;

static REAL Time;

static rDisplayListAlphaSensitive sr_consoleDisplayList;

// flag memorizing whether the console already has been rendered this frame
static bool sr_alreadyDisplayed = false;

static void sr_ConsolePerFrame(){
    if (sr_con.autoDisplayAtSwap)
    {
        sr_con.Render();
    }
    sr_alreadyDisplayed = false;
}

static rPerFrameTask console_pf(&sr_ConsolePerFrame);


void rConsole::DisplayAtNewline(){
    bool sw=autoDisplayAtSwap;
    autoDisplayAtSwap=true;
    if (sr_glOut){
        rSysDep::SwapGL();
        rSysDep::ClearGL();
    }
    autoDisplayAtSwap=sw;
}

REAL centerMessageY=0;

static tConfItem<REAL> cmlocy("CM_LOCY",centerMessageY);

static int sr_columns = 0;
static tConfItem<int> sr_columnsConf("CONSOLE_COLUMNS",sr_columns);

static int sr_indent = 3;
static tConfItem<int> sr_indentConf("CONSOLE_INDENT",sr_indent);

float sr_chatLayer = 0.5;
static tConfItem<float> sr_chatLayerConf("CHAT_LAYER",sr_chatLayer);

bool sr_chatBackground = false;
static tConfItem<bool> sr_chatBackgroundConf("CHAT_BACKGROUND",sr_chatBackground);
int sr_chatBackgroundColor[3] = { 20, 10, 24 };
static tConfItem<int> sr_chatBackgroundRConf("CHAT_BACKGROUND_R",sr_chatBackgroundColor[0]);
static tConfItem<int> sr_chatBackgroundGConf("CHAT_BACKGROUND_G",sr_chatBackgroundColor[1]);
static tConfItem<int> sr_chatBackgroundBConf("CHAT_BACKGROUND_B",sr_chatBackgroundColor[2]);
float sr_chatBackgroundAlpha = 0.5;
static tConfItem<float> sr_chatBackgroundAlphaConf("CHAT_BACKGROUND_ALPHA",sr_chatBackgroundAlpha);

namespace
{
static tConfigMigration migrate([](tString const &savedInVersion){
    // default for sr_columns changed from 78 to 0 (fixed pixel size) on 0.2.9.1
    if(sr_columns == 78 && tConfigMigration::SavedBefore(savedInVersion, "0.2.9.1_alpha"))
    {
        sr_columns = 0;
    }
});
}

// links in the chat: where each drawn line is, to find the one under a click
namespace
{
    struct sr_ConsoleRow
    {
        REAL top, bottom;
        tString text;
    };
    std::vector< sr_ConsoleRow > sr_consoleRows;
    void ( * sr_linkOpener )( char const * url ) = 0;

    bool sr_IsHex( char c )
    {
        return ( c >= '0' && c <= '9' ) || ( c >= 'a' && c <= 'f' ) || ( c >= 'A' && c <= 'F' );
    }

    // characters that may be part of a link; quotes and anything unusual end it
    bool sr_LinkChar( char c )
    {
        return ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) ||
               strchr( "-._~:/?#[]@!$&()*+,;=%", c ) != 0;
    }

    // the links in s (start, length), found by http://, https:// or www. at the start of a word
    std::vector< std::pair< size_t, size_t > > sr_FindLinks( std::string const & s )
    {
        std::vector< std::pair< size_t, size_t > > links;
        std::string lower( s );
        for ( char & c: lower )
            c = tolower( c );
        size_t i = 0;
        while ( i < lower.size() )
        {
            size_t const http = lower.find( "http://", i ), https = lower.find( "https://", i ), www = lower.find( "www.", i );
            size_t start = std::min( http, std::min( https, www ) );
            if ( start == std::string::npos )
                break;
            // must begin a word (www. inside http://www. is part of that link)
            if ( start > 0 && sr_LinkChar( s[ start - 1 ] ) )
            {
                i = start + 1;
                continue;
            }
            size_t end = start;
            while ( end < s.size() && sr_LinkChar( s[end] ) )
            {
                // a color code (0x and six hex digits) ends the link
                if ( s[end] == '0' && end + 8 <= s.size() && ( s[end+1] == 'x' || s[end+1] == 'X' ) )
                {
                    bool hex = true;
                    for ( size_t m = 2; m < 8; ++m )
                        hex = hex && sr_IsHex( s[end+m] );
                    if ( hex )
                        break;
                }
                ++end;
            }
            // punctuation after a link belongs to the sentence
            while ( end > start && strchr( ".,;:!?)]'", s[ end - 1 ] ) )
                --end;
            if ( end > start + 8 )
                links.push_back( std::make_pair( start, end - start ) );
            i = end > start ? end : start + 1;
        }
        return links;
    }

    // the line with its links in a link color and underlined look, then back to the color before
    tString sr_HighlightLinks( tString const & line )
    {
        auto links = sr_FindLinks( line );
        if ( links.empty() )
            return line;
        tString out;
        size_t done = 0;
        for ( auto const & link: links )
        {
            // the color in effect where the link starts, to go back to after it
            std::string color = "0xRESETT";
            for ( size_t k = 0; k + 8 <= link.first; ++k )
                if ( line[k] == '0' && ( line[k+1] == 'x' || line[k+1] == 'X' ) )
                {
                    bool hex = true;
                    for ( size_t m = 2; m < 8; ++m )
                        hex = hex && sr_IsHex( line[k+m] );
                    if ( hex || line.substr( k + 2, 6 ) == "RESETT" )
                        color = line.substr( k, 8 );
                }
            out << line.substr( done, link.first - done ) << "0x5fb4ff" << line.substr( link.first, link.second ) << color;
            done = link.first + link.second;
        }
        out << line.substr( done );
        return out;
    }
}

void rConsole::SetLinkOpener( void ( * opener )( char const * url ) )
{
    sr_linkOpener = opener;
}

bool rConsole::ClickAt( REAL x, REAL y, bool copy )
{
#ifndef DEDICATED
    for ( auto const & row: sr_consoleRows )
    {
        if ( y > row.top || y < row.bottom )
            continue;
        tString const plain = tColoredString::RemoveColors( row.text );
        if ( copy )
        {
            // the line without its colors and line break
            std::string text( plain );
            while ( !text.empty() && ( text.back() == '\n' || text.back() == ' ' ) )
                text.pop_back();
            SDL_SetClipboardText( text.c_str() );
            DoCenterDisplay( tString( "Copied to the clipboard" ), 1.2f, .75f, .75f, .8f );
            return true;
        }
        auto links = sr_FindLinks( plain );
        if ( links.empty() || !sr_linkOpener )
            return false;
        std::string url = plain.substr( links[0].first, links[0].second );
        std::string lower( url );
        for ( char & c: lower )
            c = tolower( c );
        if ( lower.compare( 0, 4, "www." ) == 0 )
            url = "https://" + url;
        sr_linkOpener( url.c_str() );
        DoCenterDisplay( tString( "Opening " ) + tString( url ), 1.5f, .37f, .7f, 1 );
        return true;
    }
#endif
    return false;
}

void rConsole::Render(){
    if( sr_alreadyDisplayed )
    {
        return;
    }

    sr_alreadyDisplayed = true;

    if (!sr_glOut)
        return;

    static REAL lastBottom = -1.0;

    sr_ResetRenderState(true);

    REAL W=sr_screenWidth;
    REAL H=sr_screenHeight;

    // previous logic
    //REAL MW=400;
    //REAL MH=(MW*3)/4;
    //if(W>MW)
    //    W=MW;
    //if(H>MH)
    //    H=MH;
    // rCWIDTH_CON=10/W;
    // rCHEIGHT_CON=18/H;

    auto columns = sr_columns;
    if(columns == 0 && (W < 1280 || H < 720))
        columns = 78; // the old default for small screens

    if(columns > 0)
    {
        // the text field has an openGL coordinate with of 1.9; cram the specified number
        // of columns in it
        rCWIDTH_CON=1.9/columns;

        // get corresponding character height
        rCHEIGHT_CON=rCWIDTH_CON*W*9/(5*H);
    }
    else
    {
        rCHEIGHT_CON=26*2.0/H;
        rCWIDTH_CON=13*2.0/W;

        // but don't make it more than MAX_ROWS of text rows for the whole screen, more may be too small for hires small screens
        constexpr auto MAX_ROWS = 56;
        if(columns == 0 && rCHEIGHT_CON * MAX_ROWS < 2)
        {
            auto clamped_CHEIGHT = 2.0f/MAX_ROWS;
            rCWIDTH_CON *= clamped_CHEIGHT / rCHEIGHT_CON;
            rCHEIGHT_CON = clamped_CHEIGHT;
        }
    }

    if (sr_screen){
        Time=tSysTimeFloat();

        if (Time-center_fadetime<2){
            REAL alpha=center_fadetime-Time+1;
            if (alpha>1) alpha=1;
            if (alpha<0) alpha=0;
            rTextField::SetDefaultColor(tColor(center_r,center_g,center_b));
            rTextField::SetBlendColor(tColor(1,1,1,alpha));

            REAL width=rCWIDTH_CON*4;
            REAL height=rCHEIGHT_CON*4;
            REAL len=sr_centerString.LongestLine();
            REAL lines=(REAL)(sr_centerString.Count('\n')+1);

            REAL space = 1.6;
            REAL needed = width * len;
            if (needed > space) {
                width *= space/needed;
                height *= space/needed;
            }
            space = 0.9 + centerMessageY;
            needed = height*lines;
            if (needed > space) {
                width *= space/needed;
                height *= space/needed;
            }

            DisplayText(0,centerMessageY,height,sr_centerString,sr_fontCenterMessage);
            //std::cerr << "DisplayText(0," << centerMessageY << "," << (rCWIDTH_CON*4*fak) << "," << (rCHEIGHT_CON*4*fak) << "," <<sr_centerString << ");\n";
            RenderEnd();
            rTextField::SetDefaultColor(tColor(1,1,1));
            sr_ResetRenderState(true);
        }

        if (sr_textOut || rForceTextCallback::ForceText()){
            if (lastCustomTimeout<Time-5 &&
                    lastTimeout+timeout<Time && currentTop<currentIn){
                currentTop++;
                lastTimeout=Time;
            }

            rTextField out(rTextField::Pixelize(-.95f,W),rTextField::Pixelize(.99f,H),rCHEIGHT_CON, sr_fontConsole);//,&rFont::s_defaultFontSmall);

            static int lastTop = currentTop;
            static int lastIn  = currentIn;
            REAL predictBottom = lastBottom - ( lastTop - currentTop + currentIn - lastIn ) * out.GetCHeight();

            // the look of the chat changed in the settings: draw it again
            tString look;
            look << sr_chatBackground << ' ' << sr_chatBackgroundColor[0] << ' ' << sr_chatBackgroundColor[1] << ' '
                 << sr_chatBackgroundColor[2] << ' ' << sr_chatBackgroundAlpha << ' ' << sr_chatFont << ' ' << sr_chatLayer
                 << ' ' << sr_screenWidth << 'x' << sr_screenHeight;
            static tString lastLook = look;

            if ( lastTop != currentTop || lastIn != currentIn || lastLook != look )
            {
                lastTop = currentTop;
                lastIn  = currentIn;
                lastLook = look;
                sr_consoleDisplayList.Clear();
            }

            rTextField::SetDefaultColor( tColor(1,1,1) );

            if ( sr_consoleDisplayList.Call() )
            {
                return;
            }
            rDisplayListFiller filler( sr_consoleDisplayList );

            out.SetWidth(1.9f);
            out.EnableLineWrap();
            out.SetIndent(sr_indent);

            REAL const layer = sr_chatBackground ? sr_chatBackgroundAlpha : sr_chatLayer;
            if( sr_alphaBlend && layer > 0 && predictBottom < out.GetTop() )
            {
                RenderEnd();
                if ( sr_chatBackground )
                {
                    auto channel = []( int value ){ return ( value < 0 ? 0 : ( value > 255 ? 255 : value ) ) / 255.0f; };
                    glColor4f( channel( sr_chatBackgroundColor[0] ), channel( sr_chatBackgroundColor[1] ), channel( sr_chatBackgroundColor[2] ), layer );
                }
                else
                    glColor4f(0, 0, 0, layer);
                glRectf(-1,predictBottom-.4*out.GetCHeight(),1,1);
            }

            int i;
            sr_consoleRows.clear();
            for (i=currentTop;i<=currentIn && i<=currentTop+MaxHeight();i++)
                if (lines[i].Len()>1){
                    rTextField::SetDefaultColor( tColor(1,1,1) );
                    int const rowBefore = out.Row();
                    out << sr_HighlightLinks( lines[i] );
                    out.ResetColor();
                    // remember where the line went, for clicks on links
                    int const rowAfter = out.Row() > rowBefore ? out.Row() : rowBefore + 1;
                    sr_ConsoleRow row;
                    row.top = out.GetTop() - rowBefore * out.GetCHeight();
                    row.bottom = out.GetTop() - rowAfter * out.GetCHeight();
                    row.text = lines[i];
                    sr_consoleRows.push_back( row );
                }
            if (i<currentIn){
                // rTextField::SetDefaultColor( tColor(1,.8,.5) );
                out << tColoredString::ColorString( 1,.8,.5) << "       v   v   v   v   v   v   v   v   v\n";
            }


            int over=out.Lines()-Height();
            if (over>0 && (rSmallConsoleCallback::SmallColsole() || lastCustomTimeout<tSysTimeFloat()-15)){
                lastTimeout=Time;
                currentTop+=(over+1)/2;
            }
           
            // check for mispredictions of console height
            lastBottom = out.GetBottom();
            if( fabs(predictBottom - lastBottom) > .0001 )
            {
                sr_consoleDisplayList.Clear();
            }

        }

        rTextField::SetDefaultColor( tColor(1,1,1) );
    }
}


void CenterDisplay(const tString &s,REAL timeout,REAL r,REAL g,REAL b){
    rCenterDisplayCallback::CenterDisplay();

    sr_centerString=s;
    center_fadetime=timeout+tSysTimeFloat();
    center_r=r;
    center_g=g;
    center_b=b;
}

void rConsole::DoCenterDisplay(const tString &s,REAL timeout,REAL r,REAL g,REAL b){
    rCenterDisplayCallback::CenterDisplay();

    sr_centerString=s;
    center_fadetime=timeout+tSysTimeFloat();
    center_r=r;
    center_g=g;
    center_b=b;
}

bool rConsole::CenterDisplayActive()
{
    return tSysTimeFloat() - center_fadetime < 1.5;
}

// passes ladderlog output to external scripts (do nothing here)
void sr_InputForScripts( char const * input )
{}

#else
#include "rConsoleCout.cpp"
#endif
