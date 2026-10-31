/*

*************************************************************************

ArmageTron -- Just another Tron Lightcycle Game in 3D.
Copyright (C) 2000  Manuel Moos (manuel@moosnet.de)
Copyright (C) 2004  Armagetron Advanced Team (http://sourceforge.net/projects/armagetronad/)

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
#   ifdef MACOSX_XCODE
#       include "uOSXPaste.h"
#       include <CoreFoundation/CoreFoundation.h>
#   elif !defined(MACOSX)
#       include "scrap.h"
#   endif
#endif

#include "tSysTime.h"
#include "uMenu.h"
#include "rSysdep.h"
#include "rScreen.h"
#include "rViewport.h"
#include "rTexture.h"
#include "tRecorder.h"
#include "tString.h"
#include "math.h"
#include "uInputQueue.h"
#include "rConsole.h"
#include "uInput.h"
#include "tDirectories.h"
//#include "tRecording.h"
#include "tToDo.h"
#include "tException.h"
#include "tRectangle.h"
#include <iterator>

#include "utf8.h"

#ifndef DEDICATED
#include "rRender.h"
#include "rSDL.h"
#endif

#include <vector>
#include <algorithm>

FUNCPTR  uMenu::idle(NULL);

bool uMenu::wrap=true;
uMenu::QuickExit uMenu::quickexit=uMenu::QuickExit_Off;
bool uMenu::exitToMain=false;

// *****************************************************

#ifdef SLOPPYLOCALE
uMenu::uMenu(const char *t="",bool exit_item)
        :exitFlag(0),spaceBelow(.4),title(t){
    if (exit_item) new uMenuItemExit(this);
    center=0;
    menuTop=.7;
    menuBot=-.7;
    yOffset=0;
    selected = 10000000;
}
#endif

uMenu::uMenu(const tOutput &t,bool exit_item)
        :exitFlag(0),spaceBelow(.4),title(t){
    if (exit_item) new uMenuItemExit(this);
    center=0;
    menuTop=.7;
    menuBot=-.7;
    yOffset=0;
    selected = 100000000;
}

uMenu::~uMenu(){
    if( selected >=0 && selected < items.Len() ) items[selected]->Deselect();
    for (int i=items.Len()-1;i>=0;i--)
        delete items[i];
}

void uMenu::ReverseItems(){
    tList<uMenuItem> dummy;
    dummy.Swap( items );

    for (int i=dummy.Len()-1; i>=0; i--){
        uMenuItem *x = dummy[i];
        dummy.Remove(x, x->idnum);
        items.Add  (x, x->idnum);
    }
}

//static REAL text_height=rCHEIGHT_NORMAL;
//static REAL text_width=rCWIDTH_NORMAL;

static REAL text_height=.11;

#ifndef DEDICATED
static REAL titlefac=1.2;
#endif
int menuentries=0;

REAL uMenu::YPos(int num){
    return yOffset-text_height*(menuentries-num);
}


#ifndef DEDICATED
static inline void arrow(REAL x,REAL y,REAL dy,REAL size){
    if (sr_glOut){
        BeginLineLoop();
        Vertex(x,y+2*dy*size);
        Vertex(x+size,y);
        Vertex(x+.3*size,y);
        Vertex(x+.3*size,y-2*dy*size);
        Vertex(x-.3*size,y-2*dy*size);
        Vertex(x-.3*size,y);
        Vertex(x-size,y);
        RenderEnd();
    }
}
#endif

static bool s_globalRepeat = false;

#ifndef DEDICATED
static bool disphelp=false;
static REAL lastkey;
#endif

// inhibit console newline display while in a menu, it causes flickering
static bool su_inMenu = false;
bool uMenu::MenuActive()
{
    return su_inMenu;
}
static rNoAutoDisplayAtNewlineCallback su_noNewline( uMenu::MenuActive );
// static rSmallConsoleCallback su_smallConsole( su_InMenu );

// *****************************************************
// *****************************************************

static bool     su_panelActive = false;
static uMenu *  su_panelBackdrop = nullptr;
static uMenu *  su_panelRoot = nullptr;
static int      su_panelDepth = 0;
static int      su_panelSection = -1;
static int      su_panelPending = -1;
static bool     su_panelClose = false;
static bool     su_renderingBackdrop = false;
static tString  su_panelSearch;
static bool     su_panelSearchFocus = false;
static REAL     su_panelMouseX = -10, su_panelMouseY = -10;
static REAL     su_panelAnimTime = 0;
static double   su_panelAnimLast = -1;

void uMenu::BeginPanel( uMenu * backdrop )
{
    su_panelActive = true;
    su_panelBackdrop = backdrop;
    su_panelRoot = nullptr;
    su_panelDepth = 0;
    su_panelSection = su_panelPending = -1;
    su_panelClose = false;
    su_panelSearch.Clear();
    su_panelSearchFocus = false;
    su_panelAnimTime = 0;
    su_panelAnimLast = -1;
}

void uMenu::EndPanel()
{
    BeginPanel( nullptr );
    su_panelActive = false;
}

class uPanelLevel
{
public:
    explicit uPanelLevel( uMenu * menu )
        : active_( su_panelActive && menu != su_panelBackdrop )
    {
        if ( !active_ )
            return;
        if ( su_panelDepth++ == 0 )
            su_panelRoot = menu;
        su_panelSearch.Clear();
        su_panelSearchFocus = false;
    }
    ~uPanelLevel()
    {
        if ( !active_ || !su_panelActive )
            return;
        if ( --su_panelDepth == 0 )
            su_panelRoot = nullptr;
        su_panelSearch.Clear();
        su_panelSearchFocus = false;
    }
private:
    bool active_;
};

#ifndef DEDICATED
class uMenuMouseGuard
{
public:
    uMenuMouseGuard(){ Free(); }
    ~uMenuMouseGuard()
    {
        if ( !sr_screen )
            return;
        bool fullscreen = currentScreensetting.fullscreen;
#if SDL_VERSION_ATLEAST(2,0,0)
        SDL_SetRelativeMouseMode( fullscreen ? SDL_TRUE : SDL_FALSE );
#endif
        SDL_ShowCursor( fullscreen ? SDL_DISABLE : SDL_ENABLE );
    }

    void Free()
    {
        if ( !sr_screen )
            return;
#if SDL_VERSION_ATLEAST(2,0,0)
        if ( SDL_GetRelativeMouseMode() )
            SDL_SetRelativeMouseMode( SDL_FALSE );
#endif
        if ( SDL_ShowCursor( SDL_QUERY ) != SDL_ENABLE )
            SDL_ShowCursor( SDL_ENABLE );
    }
};
#endif

void uMenu::OnEnter(){
#ifndef DEDICATED
    bool localRepeat = false;
    float nextrepeat = 0.0f;
    static const float repeatdelay = 0.2f;
    static const float repeatrateStart  = 0.2f;
    static const float repeatrateMin  = 0.05f;
    static float repeatrate  = repeatrateStart;
    SDL_Event tEventRepeat;
#else
    return;
#endif

    // delete stuck keys, maybe a menu item catches key release events.
    su_ClearKeys();

    uCallbackMenuEnter::MenuEnter();
    su_inMenu = true;

    if (items.Len()<=0)
        return;

#ifndef DEDICATED
    uMenuMouseGuard mouseGuard;
#endif
    uPanelLevel panelLevel( this );

    exitFlag=0;
    yOffset=menuTop;
    REAL lastt=0;
    REAL ts=0;
    bool snapScroll = false;

#ifndef DEDICATED
    lastkey=tSysTimeFloat();
    static const REAL timeout=.5;
#endif
    if( selected >= items.Len() )
    {
        // skip to actually selectable item
        bool wrapBack = wrap;
        wrap = true;
        selected = GetPrevSelectable(0);
        if( selected >=0 && selected < items.Len() ) items[selected]->Select();
        wrap = wrapBack;
    }
    while (!exitFlag && !quickexit && !exitToMain){
        st_DoToDo();
        tAdvanceFrame();

        ts=tSysTimeFloat()-lastt;
        lastt=tSysTimeFloat();
        if (ts>.2) ts=.2;

        if(snapScroll)
        {
            if(ts * 30 < 1)
                snapScroll = false;
        }
        else
        {
            if(ts * 15 > 1)
                snapScroll = true;
        }
        auto scrollBy = [this, snapScroll, ts](REAL delta)
        {
            if(snapScroll || fabsf(delta) < 1E-6)
            {
                yOffset += delta;
            }
            else
            {
                // almost standard exponential decay; the proximity factor makes it
                // approach the target position like t -> t^2 for negative t
                REAL proximity = std::min(1.0f, 10.0f * sqrtf(fabsf(delta)));
                REAL speed = std::min(1.0f, ts * 6 / proximity);
                yOffset += speed * delta;
            }
        };

        menuentries=items.Len();

        // clamp cursor
        if (selected < 0 )
            selected = 0;
        if ( selected >= items.Len())
            selected = items.Len()-1;

#ifndef DEDICATED
        {
            SDL_Event tEvent;
            uInputProcessGuard inputProcessGuard;
            while (!exitFlag && !quickexit && !exitToMain && su_GetSDLInput(tEvent))
            {
                REAL entertime = tSysTimeFloat();

                switch (tEvent.type)
                {
                case SDL_KEYDOWN:
                    if ( tEvent.key.keysym.sym == SDLK_UNKNOWN )
                    {
                        // don't repeat unknown syms. They come from multi-key compositions and
                        // don't send keyup events when released.
                        break;
                    }
                    localRepeat = s_globalRepeat = true;
                    memcpy( &tEventRepeat, &tEvent, sizeof( SDL_Event ) );
                    nextrepeat = tSysTimeFloat() + repeatdelay;
                    break;
                case SDL_KEYUP:
                    localRepeat = s_globalRepeat = false;
                    repeatrate = repeatrateStart;
                    break;
                }

                this->HandleEvent( tEvent );

                // quit shortcut
                if ( quickexit )
                    break;

                if ( tSysTimeFloat() - entertime > 1 )
                {
                    localRepeat = s_globalRepeat = false;
                }
            }

            if ( localRepeat && s_globalRepeat && tSysTimeFloat() > nextrepeat )
            {
                this->HandleEvent( tEventRepeat );
                nextrepeat = tSysTimeFloat() + repeatrate;
                repeatrate *= .71;
                if ( repeatrate < repeatrateMin )
                    repeatrate = repeatrateMin;
            }
        }

#ifndef DEDICATED
        mouseGuard.Free();
#endif

        if ( su_panelActive && su_panelRoot && this != su_panelBackdrop )
        {
            if ( su_panelClose || ( su_panelPending >= 0 && this != su_panelRoot ) )
                Exit();
            else if ( this == su_panelRoot && su_panelPending >= 0 )
            {
                int section = su_panelPending;
                su_panelPending = -1;
                if ( section < items.Len() )
                {
                    SetSelected( section );
                    EnterSelected();
                }
            }
        }

        // we're about to render, last chance to make changes to the menu
        OnRender();

#ifndef DEDICATED
        rSysDep::ClearGL();
#endif

        // clamp cursor
        if (selected < 0 )
            selected = 0;
        if ( selected >= items.Len())
            selected = items.Len()-1;
#endif
        // quit shortcut
        if ( quickexit )
            break;


        menuBot=-1+spaceBelow;

        const REAL border=.3;
        const REAL smallborder=.1;

        menuentries=items.Len();

        REAL ysel=YPos(selected);

        {
            REAL scrollUp = menuBot+border-ysel;
            if(scrollUp > 0)
                scrollBy(scrollUp);
        }
        {
            REAL scrollDown = menuTop-border-ysel;
            if(scrollDown < 0)
                scrollBy(scrollDown);
        }

        if (ysel<menuBot)
            yOffset+=(menuBot-ysel);

        if (ysel>menuTop-smallborder)
            yOffset+=(menuTop-smallborder-ysel);

        if (YPos(0)>menuBot+smallborder)
            yOffset+=menuBot+smallborder-YPos(0);

        if (YPos(menuentries-1)<menuTop-smallborder)
            yOffset+=menuTop-smallborder-YPos(menuentries-1);

#ifndef DEDICATED
        sr_ResetRenderState(true);
        items[selected]->RenderBackground();

        if (selected >= items.Len()) selected = items.Len()-1;
        if (items.Len() <= 0)
            return;

        if (sr_glOut && !exitFlag && !quickexit){
            RenderItems();

            REAL helpAlpha = tSysTimeFloat()-lastkey-timeout;
            if( helpAlpha > 1 )
            {
                helpAlpha = 1;
            }

            disphelp = helpAlpha > 0;
            if ( items[selected]->DisplayHelp( disphelp, menuBot, helpAlpha ) && !( su_panelActive && this != su_panelBackdrop ) )
            {
                if (sr_alphaBlend)
                    glColor4f(1,.8,.8, helpAlpha );
                else
                    Color(helpAlpha,
                          .8*helpAlpha,
                          .8*helpAlpha);

                rTextField c(-.95f,menuBot-.04f,rCHEIGHT_NORMAL, sr_fontMenu);
                c.SetWidth(1.9f-items[selected]->SpaceRight());
                c.EnableLineWrap();
                c << items[selected]->Help();
            }
        }
        else
#endif
            if ( !sr_glOut )
            {
                tDelay( 10000 );
            }

#ifndef DEDICATED
        rSysDep::SwapGL();
#endif
    }

    s_globalRepeat = false;

    uCallbackMenuLeave::MenuLeave();
    su_inMenu = false;
}

void uMenu::HandleEvent( SDL_Event event )
{
#ifndef DEDICATED
    bool panel = su_panelActive && this != su_panelBackdrop;
    if ( panel && su_panelSearchFocus && PanelEvent( event ) )
        return;

    if (!items[selected]->Event(event))
    {
        if ( panel && PanelEvent( event ) )
            return;

        // int newSelected = -1;
        switch (event.type){
        case SDL_KEYDOWN:
        {
            if (!disphelp)
                lastkey=tSysTimeFloat();
            switch (event.key.keysym.sym){

            case(SDLK_ESCAPE):
                s_globalRepeat = false;
                lastkey=tSysTimeFloat();
                Exit();
                break;

                case(SDLK_UP):
                    lastkey=tSysTimeFloat();
                    SetSelected(GetNextSelectable(selected));
                    break;
                case(SDLK_DOWN):
                    lastkey=tSysTimeFloat();
                    SetSelected(GetPrevSelectable(selected));
                    break;

            case(SDLK_LEFT):
                items[selected]->LeftRight(-1);
                break;
            case(SDLK_RIGHT):
                items[selected]->LeftRight(1);
                break;

            case(SDLK_SPACE):
                        case(SDLK_KP_ENTER):
                            case(SDLK_RETURN):
                EnterSelected();
                break;

            default:
                // let the input subsystem handle events for later processing
                su_HandleEvent( event, true );
                break;
            }
        }
        break;

        case SDL_MOUSEMOTION:
        {
            int item = ItemAt( event.motion.x, event.motion.y );
            if ( item >= 0 && item != selected )
            {
                lastkey=tSysTimeFloat();
                SetSelected( item );
            }
            su_HandleEvent( event, true );
        }
        break;
        case SDL_MOUSEBUTTONDOWN:
            if ( event.button.button == SDL_BUTTON_LEFT )
            {
                int item = ItemAt( event.button.x, event.button.y );
                if ( item >= 0 )
                {
                    SetSelected( item );
                    EnterSelected();
                    break;
                }
            }
            else if ( event.button.button == SDL_BUTTON_RIGHT )
            {
                s_globalRepeat = false;
                lastkey=tSysTimeFloat();
                Exit();
                break;
            }
#if !SDL_VERSION_ATLEAST(2,0,0)
            else if ( event.button.button == SDL_BUTTON_WHEELUP )
            {
                SetSelected(GetNextSelectable(selected));
                break;
            }
            else if ( event.button.button == SDL_BUTTON_WHEELDOWN )
            {
                SetSelected(GetPrevSelectable(selected));
                break;
            }
#endif
            su_HandleEvent( event, true );
            break;
#if SDL_VERSION_ATLEAST(2,0,0)
        case SDL_MOUSEWHEEL:
            lastkey=tSysTimeFloat();
            if ( event.wheel.y > 0 )
                SetSelected(GetNextSelectable(selected));
            else if ( event.wheel.y < 0 )
                SetSelected(GetPrevSelectable(selected));
            break;
#endif
        default:
            // let the input subsystem handle events for later processing
            su_HandleEvent( event, true );
            break;
        }
    }

    su_inMenu = true;
#endif
}

void uMenu::RenderItems()
{
#ifndef DEDICATED
    if ( su_panelActive && !su_renderingBackdrop && this != su_panelBackdrop )
    {
        RenderPanel();
        return;
    }

    const REAL smallborder=.1;

    items[selected]->Render(center,YPos(selected),1,true);

    for (int i=items.Len()-1;i>=0;i--)
        if (i!=selected){
            REAL y=YPos(i);
            REAL alpha=1;
            const REAL b=.1;
            if (y<menuBot+b)
                alpha=(y-menuBot)/b;
            if (y>menuTop-b)
                alpha=(menuTop-y)/b;
            if (y>menuBot && y<menuTop)
            {
                rTextField::SetDefaultColor( tColor(1,1,1,1) );
                rTextField::SetBlendColor( tColor(1,1,1,1) );
                items[i]->Render(center,y,alpha,false);
            }
        }

    rTextField::SetDefaultColor( tColor(1,1,1,1) );
    rTextField::SetBlendColor( tColor(1,1,1,1) );

    RenderTitle();

    glDisable(GL_TEXTURE_2D);
    //glDisable(GL_TEXTURE);
    Color(1,.2,.2,.5);
    if (YPos(0)<menuBot+smallborder && (int(tSysTimeFloat()))%2)
        arrow(.9,menuBot+.1,-1,.05);
    if (YPos(menuentries-1)>menuTop && (int(tSysTimeFloat())+1)%2)
        arrow(.9,menuTop,1,.05);
#endif
}

void uMenu::RenderTitle()
{
#ifndef DEDICATED
    rTextField::SetDefaultColor( tColor(1,1,1,1) );
    rTextField::SetBlendColor( tColor(1,1,1,1) );
    Color(.6,.6,1,1);
    ::DisplayText(0,menuTop+text_height*titlefac
                  ,text_height*titlefac,
                  title,sr_fontMenuTitle,0);
#endif
}

// *****************************************************
// *****************************************************

#ifndef DEDICATED
namespace
{
struct uPanelColor { REAL r, g, b, a; };
const uPanelColor pn_dim         = { .039, .016, .047, .6 };
const uPanelColor pn_rail        = { .078, .043, .098, 1 };
const uPanelColor pn_railHover   = { .125, .071, .149, 1 };
const uPanelColor pn_railActive  = { .173, .090, .196, 1 };
const uPanelColor pn_content     = { .110, .063, .129, 1 };
const uPanelColor pn_row         = { .157, .090, .180, 1 };
const uPanelColor pn_rowHover    = { .220, .122, .247, 1 };
const uPanelColor pn_chip        = { .071, .039, .086, 1 };
const uPanelColor pn_text        = { .969, .886, .937, 1 };
const uPanelColor pn_textDim     = { .769, .627, .729, 1 };
const uPanelColor pn_textLow     = { .588, .463, .569, 1 };
const uPanelColor pn_accent      = { .839, .251, .624, 1 };

const REAL pn_small = .040, pn_body = .046, pn_title = .075;

enum uPanelHitKind { HIT_PANEL, HIT_OUTSIDE, HIT_ROW, HIT_LEFT, HIT_RIGHT, HIT_RAIL, HIT_BACK, HIT_SEARCH };
struct uPanelHit { REAL l, b, r, t; uPanelHitKind kind; int item; };
std::vector< uPanelHit > pn_hits;

void pn_Hit( REAL l, REAL b, REAL r, REAL t, uPanelHitKind kind, int item = -1 )
{
    pn_hits.push_back( uPanelHit{ l, b, r, t, kind, item } );
}

uPanelHit const * pn_HitAt()
{
    for ( auto it = pn_hits.rbegin(); it != pn_hits.rend(); ++it )
        if ( su_panelMouseX >= it->l && su_panelMouseX <= it->r && su_panelMouseY >= it->b && su_panelMouseY <= it->t )
            return &*it;
    return nullptr;
}

bool pn_Hover( REAL l, REAL b, REAL r, REAL t )
{
    return su_panelMouseX >= l && su_panelMouseX <= r && su_panelMouseY >= b && su_panelMouseY <= t;
}

void pn_MouseToMenu( int x, int y )
{
    int width = sr_screenWidth, height = sr_screenHeight;
#if SDL_VERSION_ATLEAST(2,0,0)
    if ( sr_screen )
        SDL_GetWindowSize( sr_screen, &width, &height );
#endif
    if ( width > 0 && height > 0 )
    {
        su_panelMouseX = 2 * REAL(x) / width - 1;
        su_panelMouseY = 1 - 2 * REAL(y) / height;
    }
}

void pn_Rect( REAL l, REAL b, REAL r, REAL t, uPanelColor const & c )
{
    glDisable( GL_TEXTURE_2D );
    Color( c.r, c.g, c.b, c.a );
    BeginQuads();
    Vertex( l, b ); Vertex( r, b ); Vertex( r, t ); Vertex( l, t );
    RenderEnd();
}

void pn_Frame( REAL l, REAL b, REAL r, REAL t, uPanelColor const & c )
{
    glDisable( GL_TEXTURE_2D );
    Color( c.r, c.g, c.b, c.a );
    BeginLineLoop();
    Vertex( l, b ); Vertex( r, b ); Vertex( r, t ); Vertex( l, t );
    RenderEnd();
}

REAL pn_Height( REAL h )
{
    return sr_screenHeight > 0 ? std::max( h, REAL( 18.0 / sr_screenHeight ) ) : h;
}

REAL pn_Width( tString const & s, REAL h )
{
    return rTextField::GetTextLength( s, pn_Height( h ), true );
}

void pn_EraseLastChar( tString & s )
{
    if ( s.size() == 0 )
        return;
    size_t cut = s.size() - 1;
    while ( cut > 0 && ( static_cast<unsigned char>( s[cut] ) & 0xC0 ) == 0x80 )
        --cut;
    s.erase( cut );
}

tString pn_Fit( tString const & s, REAL h, REAL maxWidth )
{
    if ( maxWidth <= 0 || pn_Width( s, h ) <= maxWidth )
        return s;
    tString t = s;
    while ( t.size() > 0 )
    {
        pn_EraseLastChar( t );
        tString candidate( t + "\xe2\x80\xa6" );
        if ( pn_Width( candidate, h ) <= maxWidth )
            return candidate;
    }
    return tString();
}

void pn_Text( REAL x, REAL y, REAL h, tString const & s, uPanelColor const & c, int align = -1, REAL maxWidth = 0 )
{
    rTextField::SetDefaultColor( tColor( 1, 1, 1, 1 ) );
    rTextField::SetBlendColor( tColor( c.r, c.g, c.b, c.a ) );
    tString t = pn_Fit( s, h, maxWidth );
    DisplayText( x, y, h, t.c_str(), sr_fontMenu, align );
    rTextField::SetBlendColor( tColor( 1, 1, 1, 1 ) );
}

tString pn_Label( uMenuItem & item )
{
    tString label = tColoredString::RemoveColors( item.GetPanelLabel() );
    while ( label.size() > 0 && ( label[label.size()-1] == ':' || label[label.size()-1] == ' ' ) )
        label.erase( label.size() - 1 );
    return label;
}

bool pn_Matches( uMenuItem & item )
{
    if ( su_panelSearch.size() == 0 )
        return true;
    tString label = pn_Label( item ).ToLower();
    return label.size() > 0 && label.find( su_panelSearch.ToLower() ) != std::string::npos;
}
}

void uMenu::RenderPanel()
{
    pn_hits.clear();

    REAL const aspect = sr_screenHeight > 0 ? REAL( sr_screenWidth ) / sr_screenHeight : 4.0/3.0;
    REAL const width = std::min( REAL( 1.4 ), REAL( 1.65 / aspect ) );

    double const now = tSysTimeFloat();
    if ( su_panelAnimLast >= 0 )
        su_panelAnimTime += std::min( now - su_panelAnimLast, 1.0 / 30 );
    su_panelAnimLast = now;
    REAL const progress = std::min( REAL( 1 ), REAL( su_panelAnimTime / .25 ) );
    REAL const ease = 1 - ( 1 - progress ) * ( 1 - progress ) * ( 1 - progress );

    REAL const left = 1 - width * ease, railRight = left + width * .3;
    REAL const cl = railRight + .05, cr = .96;

    if ( su_panelBackdrop )
    {
        su_renderingBackdrop = true;
        su_panelBackdrop->RenderItems();
        su_renderingBackdrop = false;
    }
    pn_Rect( -1, -1, 1, 1, uPanelColor{ pn_dim.r, pn_dim.g, pn_dim.b, pn_dim.a * ease } );
    pn_Hit( -1, -1, left, 1, HIT_OUTSIDE );

    pn_Rect( left, -1, railRight, 1, pn_rail );
    pn_Rect( railRight, -1, 1, 1, pn_content );
    pn_Hit( left, -1, 1, 1, HIT_PANEL );

    REAL y = .87;
    pn_Text( left + .05, y, pn_small, tString( "S E T T I N G S" ), pn_textLow );
    y -= .1;
    if ( su_panelRoot )
    {
        for ( int i = su_panelRoot->items.Len()-1; i >= 0; --i )
        {
            uMenuItem & item = *su_panelRoot->items[i];
            uMenuItem::PanelKind kind = item.GetPanelKind();
            tString label = pn_Label( item );
            if ( kind == uMenuItem::PANEL_EXIT || kind == uMenuItem::PANEL_DIVIDER || label.size() == 0 )
                continue;
            REAL const b = y - .045, t = y + .045;
            bool active = this == su_panelRoot ? i == selected : i == su_panelSection;
            if ( active )
            {
                pn_Rect( left, b, railRight, t, pn_railActive );
                pn_Rect( left, b, left + .008, t, pn_accent );
            }
            else if ( pn_Hover( left, b, railRight, t ) )
                pn_Rect( left, b, railRight, t, pn_railHover );
            pn_Text( left + .05, y, pn_body, label, active ? pn_text : pn_textDim, -1, railRight - left - .07 );
            pn_Hit( left, b, railRight, t, HIT_RAIL, i );
            y -= .1;
        }
    }

    {
        REAL const b = -.96, t = -.84;
        bool hover = pn_Hover( left, b, railRight, t );
        pn_Text( ( left + railRight ) * .5, ( b + t ) * .5, pn_body, tString( "\xe2\x80\xb9  back" ), hover ? pn_text : pn_textDim, 0 );
        pn_Hit( left, b, railRight, t, HIT_BACK );
    }

    {
        REAL const b = .835, t = .925;
        pn_Rect( cl, b, cr, t, pn_chip );
        if ( su_panelSearchFocus )
            pn_Frame( cl, b, cr, t, pn_accent );
        REAL const cy = ( b + t ) * .5;
        if ( su_panelSearch.size() > 0 || su_panelSearchFocus )
        {
            tString shown = su_panelSearch;
            if ( su_panelSearchFocus && int( tSysTimeFloat() * 2 ) % 2 == 0 )
                shown << "|";
            pn_Text( cl + .025, cy, pn_body, shown, pn_text, -1, cr - cl - .12 );
        }
        else
            pn_Text( cl + .025, cy, pn_body, tString( "Search" ), pn_textLow );

        REAL const mx = cr - .045, my = cy + .006, mr = .014;
        glDisable( GL_TEXTURE_2D );
        glLineWidth( 2 );
        Color( pn_textDim.r, pn_textDim.g, pn_textDim.b, 1 );
        BeginLineLoop();
        for ( int i = 0; i < 16; ++i )
        {
            REAL a = 2 * 3.14159f * i / 16;
            Vertex( mx + mr * cosf( a ), my + mr * aspect * sinf( a ) );
        }
        RenderEnd();
        BeginLines();
        Vertex( mx + mr * .7, my - mr * .7 * aspect );
        Vertex( mx + mr * 1.6, my - mr * 1.6 * aspect );
        RenderEnd();
        glLineWidth( 1 );
        pn_Hit( cl, b, cr, t, HIT_SEARCH );
    }

    pn_Text( cl, .74, pn_title, tString( title ), pn_text, -1, cr - cl );

    REAL const listTop = .66, listBottom = -.70, rowH = .085, gap = .012, dividerH = .045;
    std::vector< int > rows;
    std::vector< REAL > rowTop;
    REAL total = 0;
    for ( int i = items.Len()-1; i >= 0; --i )
    {
        uMenuItem::PanelKind kind = items[i]->GetPanelKind();
        if ( kind == uMenuItem::PANEL_EXIT )
            continue;
        if ( su_panelSearch.size() > 0 && ( kind == uMenuItem::PANEL_DIVIDER || !pn_Matches( *items[i] ) ) )
            continue;
        rows.push_back( i );
        rowTop.push_back( total );
        total += ( kind == uMenuItem::PANEL_DIVIDER ? dividerH : rowH ) + gap;
    }

    if ( selected >= 0 && selected < items.Len() && items[selected]->GetPanelKind() == uMenuItem::PANEL_EXIT && !rows.empty() )
    {
        for ( int r: rows )
            if ( items[r]->IsSelectable() && items[r]->GetPanelKind() != uMenuItem::PANEL_DIVIDER )
            {
                SetSelected( r );
                break;
            }
    }

    REAL const view = listTop - listBottom;
    if ( selected != panelLastSelected_ )
    {
        for ( size_t r = 0; r < rows.size(); ++r )
        {
            if ( rows[r] != selected )
                continue;
            if ( rowTop[r] < panelScroll_ )
                panelScroll_ = rowTop[r];
            if ( rowTop[r] + rowH > panelScroll_ + view )
                panelScroll_ = rowTop[r] + rowH - view;
        }
        panelLastSelected_ = selected;
    }
    panelScroll_ = std::max( REAL( 0 ), std::min( panelScroll_, std::max( REAL( 0 ), total - gap - view ) ) );

    if ( rows.empty() )
        pn_Text( ( cl + cr ) * .5, listTop - .1, pn_body, tString( "Nothing found" ), pn_textLow, 0 );

    REAL const chipW = std::min( REAL( .42 ), REAL( ( cr - cl ) * .45 ) );
    for ( size_t r = 0; r < rows.size(); ++r )
    {
        int const i = rows[r];
        uMenuItem & item = *items[i];
        uMenuItem::PanelKind kind = item.GetPanelKind();
        REAL const t = listTop - ( rowTop[r] - panelScroll_ );
        REAL const b = t - ( kind == uMenuItem::PANEL_DIVIDER ? dividerH : rowH );
        if ( t > listTop + .001 || b < listBottom - .001 )
            continue;
        if ( kind == uMenuItem::PANEL_DIVIDER )
            continue;

        REAL const cy = ( b + t ) * .5;
        bool const sel = i == selected;
        pn_Rect( cl, b, cr, t, sel || pn_Hover( cl, b, cr, t ) ? pn_rowHover : pn_row );
        if ( sel )
            pn_Rect( cl, b, cl + .006, t, pn_accent );
        pn_Hit( cl, b, cr, t, HIT_ROW, i );

        tString label = pn_Label( item );
        REAL const labelX = cl + .03, valueR = cr - .02;
        switch ( kind )
        {
        case uMenuItem::PANEL_CHOICE:
        {
            REAL const vl = valueR - chipW;
            pn_Text( labelX, cy, pn_body, label, pn_text, -1, vl - labelX - .02 );
            pn_Rect( vl, b + .012, valueR, t - .012, pn_chip );
            bool hoverL = pn_Hover( vl, b, ( vl + valueR ) * .5, t ), hoverR = pn_Hover( ( vl + valueR ) * .5, b, valueR, t );
            pn_Text( vl + .025, cy, pn_body, tString( "\xe2\x80\xb9" ), hoverL ? pn_accent : pn_textDim, 0 );
            pn_Text( valueR - .025, cy, pn_body, tString( "\xe2\x80\xba" ), hoverR ? pn_accent : pn_textDim, 0 );
            pn_Text( ( vl + valueR ) * .5, cy, pn_body, item.GetPanelValue(), pn_text, 0, chipW - .1 );
            pn_Hit( vl, b, ( vl + valueR ) * .5, t, HIT_LEFT, i );
            pn_Hit( ( vl + valueR ) * .5, b, valueR, t, HIT_RIGHT, i );
            break;
        }
        case uMenuItem::PANEL_TOGGLE:
        {
            REAL const sw = .085, sh = .022 * aspect, sl = valueR - sw;
            bool on = item.GetPanelState();
            pn_Text( labelX, cy, pn_body, label, pn_text, -1, sl - labelX - .02 );
            pn_Rect( sl, cy - sh, valueR, cy + sh, on ? pn_accent : pn_chip );
            REAL const knob = sw * .42, inset = .006;
            if ( on )
                pn_Rect( valueR - knob - inset, cy - sh + inset * aspect, valueR - inset, cy + sh - inset * aspect, pn_text );
            else
                pn_Rect( sl + inset, cy - sh + inset * aspect, sl + knob + inset, cy + sh - inset * aspect, pn_textLow );
            break;
        }
        case uMenuItem::PANEL_TEXT:
        {
            REAL const tw = ( cr - cl ) * .5, vl = valueR - tw;
            pn_Text( labelX, cy, pn_body, label, pn_text, -1, vl - labelX - .02 );
            pn_Rect( vl, b + .012, valueR, t - .012, pn_chip );
            if ( sel )
                pn_Frame( vl, b + .012, valueR, t - .012, pn_accent );
            tString value = item.GetPanelValue();
            int cursor = item.GetPanelCursor();
            if ( sel && cursor >= 0 && int( tSysTimeFloat() * 2 ) % 2 == 0 )
                value.insert( std::min( size_t( cursor ), value.size() ), "|" );
            value = tColoredString::RemoveColors( value );
            while ( value.size() > 0 && pn_Width( value, pn_body ) > tw - .04 )
            {
                size_t cut = 1;
                while ( cut < value.size() && ( static_cast<unsigned char>( value[cut] ) & 0xC0 ) == 0x80 )
                    ++cut;
                value.erase( 0, cut );
            }
            pn_Text( vl + .02, cy, pn_body, value, pn_text );
            break;
        }
        case uMenuItem::PANEL_KEYS:
        {
            tString value = item.GetPanelValue();
            std::vector< tString > keys;
            size_t start = 0;
            while ( start <= value.size() )
            {
                size_t end = value.find( ", ", start );
                if ( end == std::string::npos )
                    end = value.size();
                if ( end > start )
                    keys.push_back( tString( value.substr( start, end - start ) ) );
                start = end + 2;
            }
            REAL x = valueR;
            REAL const minX = cl + ( cr - cl ) * .45;
            for ( auto k = keys.rbegin(); k != keys.rend(); ++k )
            {
                tString key = k->ToUpper();
                REAL const w = pn_Width( key, pn_small ) + .05;
                if ( x - w < minX )
                    break;
                pn_Rect( x - w, b + .015, x, t - .015, pn_chip );
                pn_Text( x - w * .5, cy, pn_small, key, pn_text, 0 );
                x -= w + .015;
            }
            pn_Text( labelX, cy, pn_body, label, pn_text, -1, x - labelX - .02 );
            break;
        }
        case uMenuItem::PANEL_BUTTON:
            pn_Text( labelX, cy, pn_body, label, pn_text, -1, valueR - labelX - .06 );
            pn_Text( valueR - .02, cy, pn_body, tString( "\xe2\x80\xba" ), sel ? pn_accent : pn_textDim, 0 );
            break;
        default:
            item.Render( ( cl + cr ) * .5, cy, 1, sel );
            break;
        }
    }

    if ( total - gap > view )
    {
        REAL const h = view * view / ( total - gap );
        REAL const top = listTop - ( view - h ) * panelScroll_ / ( total - gap - view );
        pn_Rect( cr + .012, top - h, cr + .02, top, pn_row );
    }

    if ( selected >= 0 && selected < items.Len() )
    {
        tString help = items[selected]->Help();
        if ( help.size() > 0 )
        {
            help = tColoredString::RemoveColors( help );
            REAL top = listBottom - .05;
            size_t start = 0;
            while ( start < help.size() && top > -.98 )
            {
                size_t end = help.find( '\n', start );
                if ( end == std::string::npos )
                    end = help.size();
                if ( end > start )
                {
                    rTextField::SetDefaultColor( tColor( 1, 1, 1, 1 ) );
                    rTextField::SetBlendColor( tColor( pn_textDim.r, pn_textDim.g, pn_textDim.b, 1 ) );
                    tString paragraph( help.substr( start, end - start ) );
                    rTextField c( cl, top, pn_Height( pn_small ), sr_fontMenu );
                    c.SetWidth( cr - cl );
                    c.EnableLineWrap();
                    c << paragraph;
                    int lines = 1 + int( pn_Width( paragraph, pn_small ) / ( ( cr - cl ) * .92 ) );
                    top -= lines * pn_Height( pn_small ) * 1.25 + .01;
                }
                start = end + 1;
            }
            rTextField::SetBlendColor( tColor( 1, 1, 1, 1 ) );
        }
    }
}

bool uMenu::PanelEvent( SDL_Event & event )
{
    if ( su_panelSearchFocus )
    {
        switch ( event.type )
        {
#if SDL_VERSION_ATLEAST(2,0,0)
        case SDL_TEXTINPUT:
            su_panelSearch << tString( event.text.text );
            panelScroll_ = 0;
            return true;
#endif
        case SDL_KEYDOWN:
            switch ( event.key.keysym.sym )
            {
            case SDLK_BACKSPACE:
                pn_EraseLastChar( su_panelSearch );
                break;
            case SDLK_ESCAPE:
                su_panelSearch.Clear();
                su_panelSearchFocus = false;
                break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                su_panelSearchFocus = false;
                break;
            default:
                break;
            }
            return true;
        default:
            break;
        }
    }

    switch ( event.type )
    {
    case SDL_MOUSEMOTION:
    {
        pn_MouseToMenu( event.motion.x, event.motion.y );
        uPanelHit const * hit = pn_HitAt();
        if ( hit && ( hit->kind == HIT_ROW || hit->kind == HIT_LEFT || hit->kind == HIT_RIGHT ) &&
             hit->item != selected && items[hit->item]->IsSelectable() )
        {
            lastkey = tSysTimeFloat();
            SetSelected( hit->item );
            panelLastSelected_ = selected;
        }
        return true;
    }
    case SDL_MOUSEBUTTONDOWN:
    {
        pn_MouseToMenu( event.button.x, event.button.y );
        if ( event.button.button == SDL_BUTTON_RIGHT )
        {
            Exit();
            return true;
        }
        if ( event.button.button != SDL_BUTTON_LEFT )
            return false;

        uPanelHit const * hit = pn_HitAt();
        su_panelSearchFocus = hit && hit->kind == HIT_SEARCH;
        if ( !hit )
            return true;
        int const item = hit->item;
        switch ( hit->kind )
        {
        case HIT_OUTSIDE:
            su_panelClose = true;
            Exit();
            break;
        case HIT_BACK:
            Exit();
            break;
        case HIT_RAIL:
            if ( this == su_panelRoot )
            {
                SetSelected( item );
                EnterSelected();
            }
            else
            {
                su_panelPending = item;
                Exit();
            }
            break;
        case HIT_LEFT:
        case HIT_RIGHT:
            SetSelected( item );
            items[item]->LeftRight( hit->kind == HIT_LEFT ? -1 : 1 );
            break;
        case HIT_ROW:
            if ( !items[item]->IsSelectable() )
                break;
            SetSelected( item );
            switch ( items[item]->GetPanelKind() )
            {
            case uMenuItem::PANEL_TOGGLE:
                items[item]->LeftRight( 1 );
                break;
            case uMenuItem::PANEL_CHOICE:
            case uMenuItem::PANEL_TEXT:
                break;
            default:
                EnterSelected();
                break;
            }
            break;
        default:
            break;
        }
        return true;
    }
#if SDL_VERSION_ATLEAST(2,0,0)
    case SDL_MOUSEWHEEL:
        panelScroll_ -= event.wheel.y * .15;
        return true;
#endif
    default:
        return false;
    }
}
#else
void uMenu::RenderPanel(){}
bool uMenu::PanelEvent( SDL_Event & ){ return false; }
#endif

void uMenu::EnterSelected()
{
#ifndef DEDICATED
    bool panelSection = su_panelActive && this == su_panelRoot;
    if ( panelSection )
        su_panelSection = selected;
    struct SectionReset
    {
        bool active;
        ~SectionReset(){ if ( active && su_panelActive ) su_panelSection = -1; }
    } sectionReset{ panelSection };

    s_globalRepeat = false;
    try
    {
        su_inMenu = false;
        items[selected]->Enter();
    }
    catch (tException const & e)
    {
        uMenu::SetIdle(NULL);

        // inform user of generic errors
        tConsole::Message( e.GetName(), e.GetDescription(), 20 );
    }
#ifdef _MSC_VER
#pragma warning ( disable : 4286 )
    // GRR. Visual C++ dones not handle generic exceptions with the above general statement.
    // A specialized version is needed. The best part: it warns about the code below being redundant.
    catch ( tGenericException const & e )
    {
        try
        {
            tConsole::Message( e.GetName(), e.GetDescription(), 20 );
        }
        catch (...)
        {
        }
    }
#endif

    su_inMenu = true;

    s_globalRepeat = false;
    lastkey=tSysTimeFloat();
#endif
}

int uMenu::ItemAt(int x, int y)
{
#ifndef DEDICATED
    if ( su_panelActive && this != su_panelBackdrop )
        return -1;

    int width = sr_screenWidth, height = sr_screenHeight;
#if SDL_VERSION_ATLEAST(2,0,0)
    if ( sr_screen )
        SDL_GetWindowSize( sr_screen, &width, &height );
#endif
    if ( height <= 0 )
        return -1;

    REAL menuY = 1 - 2 * REAL(y) / height;
    if ( menuY <= menuBot || menuY >= menuTop )
        return -1;

    for ( int i = items.Len()-1; i >= 0; --i )
    {
        if ( fabsf( menuY - YPos(i) ) <= text_height * .5 && items[i]->IsSelectable() )
            return i;
    }
#endif
    return -1;
}

// select the menu item below "start"
int uMenu::GetPrevSelectable(int start)
{
    int prev = start-1;
    while (prev!=start)
    {
        if (prev<0)
        {
            if (wrap)
                prev = items.Len()-1;
            else
                break;
        }
        if (items[prev]->IsSelectable())
        {
            return prev;
        }
        prev--;
    }
    return start;
}

#ifndef DEDICATED
static bool s_idleBackground = false;
#endif

void uMenu::SetSelected( int s )
{
    if( selected >= 0 && selected < items.Len() ) items[selected]->Deselect();
    selected = s;
    if( selected >= 0 && selected < items.Len() ) items[selected]->Select();
}

// select the menu item above "start"
int uMenu::GetNextSelectable(int start)
{
    int next = start+1;
    while (next!=start)
    {
        if (next>=items.Len())
        {
            if (this->wrap)
                next = 0;
            else
                break;
        }
        if (items[next]->IsSelectable())
        {
            return next;
        }
        next++;
    }
    return start;
}


// paints a nice background
void uMenu::GenericBackground(REAL top){
#ifndef DEDICATED
    if (idle)
    {
        s_idleBackground = true;

        try
        {
            // throw tGenericException("test"); // (test exception throw to see if error handling works right)
            (*idle)();

            // render the console so it appears behind the menu
            if( sr_con.autoDisplayAtSwap )
            {
                sr_con.Render();
            }

            // fade everything rendered so far to black
            if( sr_alphaBlend && sr_chatLayer > 0 )
            {
                sr_ResetRenderState(true);

                double time = tSysTimeFloat();
                static double lastTime = time - 100;
                static REAL alpha = 0.0f;
                double timePassed = time - lastTime;
                if( time - lastTime > 1.0 )
                {
                    alpha = 0.0f;
                }
                else
                {
                    alpha += timePassed;
                    REAL limit = sr_chatLayer;

                    if( alpha > limit )
                    {
                        alpha = limit;
                    }
                }
                lastTime = time;

                RenderEnd();
                glColor4f(0, 0, 0, alpha);
                glRectf(-1,-1,1,top);
            }
        }
        catch ( ... )
        {
            s_idleBackground = false;

            // the idle background function is broken. Disable it and rethrow.
            idle = 0;
            throw;
        }
        s_idleBackground = false;
    }
    else if (sr_glOut){
        uCallbackMenuBackground::MenuBackground();
    }
    else
        tDelay(100000);
#endif
    sr_ResetRenderState(true);
}

// marks the menu for exit
void uMenu::OnExit(){
    if( selected >=0 && selected < items.Len() ) items[selected]->Deselect();
    exitFlag=1;
}

//! called every frame before the menu is rendered
void uMenu::OnRender()
{
}

// *****************************************************

// *******************************************************************************************
// *
// *   SetColor
// *
// *******************************************************************************************
//!
//!        @param  selected    flag indicating whether the menu item is currently selected
//!        @param  alpha       transparency to use
//!
// *******************************************************************************************

void uMenuItem::SetColor( bool selected, REAL alpha )
{
    //   rTextField::SetBlendColor( tColor(.8+.2*sin(time),.3-.1*sin(time),.3-.1*sin(time),alpha) );
    rTextField::SetDefaultColor( tColor(1,1,1,alpha) );

    if (selected)
    {
        REAL time=tSysTimeFloat()*10;
        REAL intensity = 1+.3*sin(time);
        rTextField::SetDefaultColor( tColor(.8,.3,.3,alpha) );
        rTextField::SetBlendColor( tColor(intensity,intensity,intensity,alpha) );
    }
}

void uMenuItem::DisplayText(REAL x,REAL y,const char *text,
                            bool selected,REAL alpha,
                            int center,int c,int cp, rTextField::ColorMode colorMode, float maxWidth ){
#ifndef DEDICATED
    if (sr_glOut){
        SetColor( selected, alpha );

        REAL th = text_height;
        
        REAL availw = 1.9f;
        if (center < 0) availw = (.9f-x);
        if (center > 0) availw = (x + .9f);
        if (availw > maxWidth) availw = maxWidth;

        float usedwidth=rTextField::GetTextLength(tString(text), th, colorMode == rTextField::COLOR_USE);
        if (usedwidth > availw)
        {
            th *= availw/(usedwidth);
        }

        ::DisplayText(x,y,th,text,sr_fontMenu,center,c,cp, colorMode);
    }
#endif
}

void uMenuItem::DisplayTextSpecial(REAL x,REAL y,const char *text,
                                   bool selected,
                                   REAL alpha,int center){
    /*
     if(selected)
       glColor3f(.9,.3,.3);
     else
       glColor3f(.7,.7,1);

     ::DisplayText(x,y,text_width,text_height,text,center);
     */

    DisplayText(x,y,text,selected,alpha,center);
}

// *************************************

const tOutput& uMenuItemExit::ExitText()
{
    static tOutput exitText("$menuitem_exit_text");

    return exitText;
}

const tOutput& uMenuItemExit::ExitHelp()
{
    static tOutput exitHelp("$menuitem_exit_help");

    return exitHelp;
}

// *************************************

void uMenuItemToggle::NewChoice(uSelectItem<bool> *){}
void uMenuItemToggle::NewChoice(const char *,bool ){}

#ifdef SLOPPYLOCALE
uMenuItemToggle::uMenuItemToggle(uMenu *m,
                                 const char *tit,
                                 const char *help,
                                 bool &targ)
        :uMenuItemSelection<bool>(m,tit,help,targ){
    uMenuItemSelection<bool>::NewChoice("$menuitem_toggle_on","",true);
    uMenuItemSelection<bool>::NewChoice("$menuitem_toggle_off","",false);
}
#endif

uMenuItemToggle::uMenuItemToggle(uMenu *m,
                                 const tOutput& tit,
                                 const tOutput& help,
                                 bool &targ)
        :uMenuItemSelection<bool>(m,tit,help,targ){
    uMenuItemSelection<bool>::NewChoice("$menuitem_toggle_on","",true);
    uMenuItemSelection<bool>::NewChoice("$menuitem_toggle_off","",false);
}

uMenuItemToggle::~uMenuItemToggle(){}

void uMenuItemToggle::LeftRight(int){
    select=1-select;
    *target=!(*target);
}

void uMenuItemToggle::Enter(){
    LeftRight(0);
}
// *****************************************
//               Integer Choose
// *****************************************

#ifdef SLOPPYLOCALE
uMenuItemInt::uMenuItemInt
(uMenu *m,const char *tit,const char *help,int &targ,
 int mi,int ma,int step)
        :uMenuItem(m,help),title(tit),target(targ),Min(mi),Max(ma),
        Step(step){
    if (target<Min) target=Min;
    if (target>Max) target=Max;
}
#endif

uMenuItemInt::uMenuItemInt
(uMenu *m,const tOutput &tit,const tOutput &help,int &targ,
 int mi,int ma,int step)
        :uMenuItem(m,help),title(tit),target(targ),Min(mi),Max(ma),
        Step(step){
    if (target<Min) target=Min;
    if (target>Max) target=Max;
}


void uMenuItemInt::LeftRight(int dir){
    target+=dir*Step;
    if (target<Min) target=Min;
    if (target>Max) target=Max;
}

void uMenuItemInt::Render(REAL x,REAL y,REAL alpha,
                          bool selected){
    DisplayText(x-.02,y,title,selected,alpha,1);

    tString s;
    s << target;
    DisplayText(x+.02,y,s,selected,alpha,-1);
}

// *****************************************
//               Float Choose
// *****************************************

#ifdef SLOPPYLOCALE
uMenuItemReal::uMenuItemReal
(uMenu *m,const char *tit,const char *help,REAL &targ,
 REAL mi,REAL ma,REAL step)
        :uMenuItem(m,help),title(tit),target(targ),Min(mi),Max(ma),
        Step(step){
    if (target<Min) target=Min;
    if (target>Max) target=Max;
}
#endif

uMenuItemReal::uMenuItemReal
(uMenu *m,const tOutput &tit,const tOutput &help,REAL &targ,
 REAL mi,REAL ma,REAL step)
        :uMenuItem(m,help),title(tit),target(targ),Min(mi),Max(ma),
        Step(step){
    if (target<Min) target=Min;
    if (target>Max) target=Max;
}


void uMenuItemReal::LeftRight(int dir){
    target+=dir*Step;
    if (target<Min) target=Min;
    if (target>Max) target=Max;
}

void uMenuItemReal::Render(REAL x,REAL y,REAL alpha,
                          bool selected){
    DisplayText(x-.02,y,title,selected,alpha,1);

    tString s;
    s << target;
    DisplayText(x+.02,y,s,selected,alpha,-1);
}


// *****************************************************

uMenuItemString::uMenuItemString(uMenu *M,
                                 const tOutput& de,
                                 const tOutput& help,
                                 tString &c,
                                 int maxLength )
        :uMenuItem(M,help),description(de),content(&c),realCursorPos(0), maxLength_( maxLength ){
    // int len=content->Len();
    // if (len==0 || (*content)(len-1)!=0)
    //    (*content)[len]=0;
    realCursorPos=content->size();
    colorMode_ = rTextField::COLOR_SHOW;
}

void uMenuItemString::Render(REAL x,REAL y,
                             REAL alpha,bool selected){
#ifndef DEDICATED
    static int counter=0;
    counter++;

    int cmode=0;
    if (selected){
        cmode=1;
        if (counter & 32) cmode=2;
    }

    // unslected items with COLOR_SHOW should be rendered with COLOR_USE
    rTextField::ColorMode colorMode = colorMode_;
    if ( colorMode == rTextField::COLOR_SHOW && !selected )
        colorMode = rTextField::COLOR_USE;

    DisplayText(x-.02,y,description,selected,alpha,1);
    DisplayText(x+.02,y,*content,selected,alpha,-1,cmode,realCursorPos,colorMode);
#endif
}

bool uMenuItemString::Event(SDL_Event &e){
#ifndef DEDICATED
    bool ret =  false;
    if (e.type==SDL_KEYDOWN) {
        ret=true;
#if SDL_VERSION_ATLEAST(2,0,0)
        SDL_Keysym &c  = e.key.keysym;
        SDL_Keymod mod = static_cast<SDL_Keymod>(c.mod);
#else
        SDL_keysym &c = e.key.keysym;
        SDLMod mod    = c.mod;
#endif
        bool moveWordLeft, moveWordRight, deleteWordLeft, deleteWordRight, moveBeginning, moveEnd, killForwards;
        moveWordLeft = moveWordRight = deleteWordLeft = deleteWordRight = moveBeginning = moveEnd = killForwards = false;

#if defined (MACOSX)
        // For moving over/deleting words
        if (mod & KMOD_ALT) {
            if (c.sym == SDLK_LEFT) {
                moveWordLeft = true;
            }
            else if (c.sym == SDLK_RIGHT) {
                moveWordRight = true;
            }
            else if (c.sym == SDLK_DELETE) {
                deleteWordRight = true;
            }
            else if (c.sym == SDLK_BACKSPACE) {
                deleteWordLeft = true;
            }
        }
        // For moving to extremes of the line
#if SDL_VERSION_ATLEAST(2,0,0)
        else if (mod & KMOD_GUI) {
#else
        else if (mod & KMOD_META) {
#endif
            if (c.sym == SDLK_LEFT) {
                moveBeginning = true;
            }
            else if (c.sym == SDLK_RIGHT) {
                moveEnd = true;
            }
        }
        // Linux and Windows
#else
        // Word operations
        if (mod & KMOD_CTRL) {
            if (c.sym == SDLK_LEFT) {
                moveWordLeft = true;
            }
            else if (c.sym == SDLK_RIGHT) {
                moveWordRight = true;
            }
            else if (c.sym == SDLK_DELETE) {
                deleteWordRight = true;
            }
            else if (c.sym == SDLK_BACKSPACE) {
                deleteWordLeft = true;
            }
        }
        else if (c.sym == SDLK_HOME) {
            moveBeginning = true;
        }
        else if (c.sym == SDLK_END) {
            moveEnd = true;
        }
#endif
        // "bash" keys
        if (mod & KMOD_CTRL) {
            if (c.sym == SDLK_a) {
                moveBeginning = true;
            }
            else if (c.sym == SDLK_e) {
                moveEnd = true;
            }
            else if (c.sym == SDLK_k) {
                killForwards = true;
            }
        }
        // moveWordLeft = moveWordRight = deleteWordLeft = deleteWordRight = moveBeginning = moveEnd = killForwards

        if (moveWordLeft) {
            realCursorPos += content->PosWordLeft(realCursorPos);
        }
        else if (moveWordRight) {
            realCursorPos += content->PosWordRight(realCursorPos);
        }
        else if (deleteWordLeft) {
            realCursorPos += content->RemoveWordLeft(realCursorPos);
        }
        else if (deleteWordRight) {
            content->RemoveWordRight(realCursorPos);
        }
        else if (moveBeginning) {
            realCursorPos = 0;
        }
        else if (moveEnd) {
            realCursorPos = content->size();
        }
        else if (killForwards) {
            content->RemoveSubStr(realCursorPos,content->size()-realCursorPos);
        }
        else if (c.sym == SDLK_LEFT) {
            if (realCursorPos > 0) {
                while(((*content)[--realCursorPos]&0xc0) == 0x80) ;
            }
        }
        else if (c.sym == SDLK_RIGHT) {
            if ( realCursorPos < content->size() ) {
                while(++realCursorPos < content->size() && ((*content)[realCursorPos]&0xc0) == 0x80) ;
            }
        }
        else if (c.sym == SDLK_DELETE) {
            if (realCursorPos < content->size() ) {
                content->RemoveSubStrUtf8(realCursorPos,1);
            }
        }
        else if (c.sym == SDLK_BACKSPACE) {
            if (realCursorPos > 0) {
                realCursorPos -= content->RemoveSubStrUtf8(realCursorPos,-1);
            }
        }
        else if (c.sym == SDLK_KP_ENTER || c.sym == SDLK_RETURN || c.sym == SDLK_UP || c.sym == SDLK_DOWN || c.sym == SDLK_ESCAPE ) {
            ret = false;
//            c.sym = SDLK_DOWN;
        }
#ifdef MACOSX_XCODE
#if SDL_VERSION_ATLEAST(2,0,0)
        else if (c.sym == SDLK_v && mod & KMOD_GUI) {
#else
        else if (c.sym == SDLK_v && mod & KMOD_META) {
#endif
            CFDataRef data;
            if (su_OSXPastePasteboardData(data)) {
                const UInt8 *bytes = CFDataGetBytePtr(data);
                CFIndex bytesLength = CFDataGetLength(data);

                for (int i = 0; i < bytesLength; i++) {
                    if (!InsertChar(bytes[i], false))
                        break;
                }

                CFRelease(data);
            }
            else {
                ret = false;
            }
        }
#elif !defined(MACOSX)
        else if (c.sym == SDLK_v && mod & KMOD_CTRL) {
            char *scrap = 0;
            int scraplen;
            static bool initialized_scrap = false;

            ret = false;

            if(!initialized_scrap && init_scrap() >= 0) {
                initialized_scrap = true;
            }
            if(initialized_scrap) {
                get_scrap(SCRAP_TEXT, &scraplen, &scrap);
                if(scraplen > 0) {
                    bool convToUnicode = (!utf8::is_valid(scrap,scrap+scraplen));
                    if(!convToUnicode) std::cerr << "utf8 ";
                    std::cerr << "scrap: " << scrap << std::endl;
                    for(unsigned char *c = (unsigned char *)scrap; *c; ++c) {
                        if(!InsertChar(*c,convToUnicode)) {
                            break; // we hit a newline or were trying to
                                  // paste binary stuff
                        }
                        ret = true;
                    }
                    //free(scrap);
                }
            }
        }
#endif
#if SDL_VERSION_ATLEAST(2,0,0)
        else
        {
            // only the top row of keys counts as unhandled
            if(c.scancode >= SDL_SCANCODE_F1 && c.scancode <= SDL_SCANCODE_PAUSE)
                ret = false;
        }
    }
    else if (e.type==SDL_TEXTINPUT) {
        ret = Insert(tString(e.text.text)); // just insert input text as utf8 string
    }
    else if (e.type==SDL_TEXTEDITING) {
//        fprintf(stderr, "text editing \"%s\", selected range (%d, %d)\n",
//                e.edit.text, e.edit.start, e.edit.length);
    }
#else
        else {
            ret = InsertChar(c.unicode);
        }
    }
#endif
    if( realCursorPos > content->size()) {
        realCursorPos=content->size();
    }

    return ret;
#else
    return false;
#endif
}

void uMenuItemString::Select() {
#ifndef DEDICATED
#if SDL_VERSION_ATLEAST(2,0,0)
    SDL_StartTextInput();
#endif
#endif
}

void uMenuItemString::Deselect() {
#ifndef DEDICATED
#if SDL_VERSION_ATLEAST(2,0,0)
    SDL_StopTextInput();
#endif
#endif
}

bool uMenuItemString::Insert(const tString &insertion)
{
	// Len() includes the trailing \0
	if ( insertion.Len() > 0 && content->Len() + insertion.Len() <= maxLength_ + 2 )
    {
        *content = content->SubStr( 0, realCursorPos ) + insertion + content->SubStr( realCursorPos );
        realCursorPos += insertion.Len()-1;
        return true;
    }
    else
    {
        return false;
    }
}

inline bool IsReservedCodePoint(int unicode)
{
    bool reserved = unicode < 32;

#ifdef MACOSX
    /*
     Function keys code points. See the “Function-Key Unicodes” section.
     http://developer.apple.com/DOCUMENTATION/Cocoa/Reference/ApplicationKit/Classes/NSEvent_Class/Reference/Reference.html
     */

    reserved = reserved || (unicode >= 0xF700 && unicode <= 0xF747);
#endif

    return reserved;
}

bool uMenuItemString::InsertChar(int unicode, bool convert) {
#ifndef DEDICATED
    if (!IsReservedCodePoint(unicode))
    {
        // insert character if there is room
        if ( content->LenUtf8() < maxLength_ )
        {
            tString utf8string;

            if (convert)
            {
                unsigned short utf16string[1];
                utf16string[0] = unicode;
                utf8::utf16to8(utf16string, utf16string+1, back_inserter(utf8string));
            }
            else
            {
                utf8string.push_back(unicode);
            }

            content->insert(realCursorPos, utf8string);
            realCursorPos+=utf8string.size();
        }

        return true;
    }
    else {
        return false;
    }
#endif
    return false;
}

//! @param words a deque containing the possible words that can be used for completion
uAutoCompleter::uAutoCompleter(std::deque<tString> &words) :
        m_PossibleWords(words),
        m_LastCompletion(-1),
        m_ignorecase(true)
{}

//! @param string the string that should be tested
//! @param pos    the cursor position within the string
//! @return -1 if the cursor isn't at the end of a word, the number of characters to the previous space if it is
int uAutoCompleter::FindLengthOfLastWord(tString &string, unsigned pos) {
    int charright = ' ', charleft  = ' ';
    if(string.size() >= 1) {
        if (pos == string.size()) {
            charleft=string.at(pos-1);
        }
        else if (pos < string.size()) {
            charright=string.at(pos);
            if(pos>1) {
                charleft=string.at(pos-1);
            }
        }
    }
    if(charright != ' ' && charleft != ' ')
        return -1; //no completion possible
    if(charleft == ' ')
        return 0;
    else
        return pos - string.find_last_of(' ', pos - 1) - 1;
}

//! @param word       the part of the word that is searched for
//! @param results    the list where the possible completions will be saved
void uAutoCompleter::FindPossibleWords(tString word, std::deque<tString> &results) {
    if(m_ignorecase)
        word = Simplify(word);
    for(std::deque<tString>::iterator i=m_PossibleWords.begin(); i!=m_PossibleWords.end(); ++i) {
        size_t pos;
        if((pos = (m_ignorecase ? Simplify(*i) : *i).find(word)) != tString::npos) {
            if(isalpha((*i)[pos]) && pos != 0 && isalpha((*i)[pos-1]))
                continue; //both the char we're at and the one before is alphanumeric; we're in the middle of a word
            results.push_back(*i);
        }
    }
}

//! @param word       the part of the word that is searched for
//! @param results    the list of possible completions
//! @return the match, if any
tString uAutoCompleter::FindClosestMatch(tString &word, std::deque<tString> &results) {
    tString ret(m_ignorecase?Simplify(word):word);
    unsigned int len = ret.size();
    while(true) { // complete right side
        std::deque<tString>::iterator i;
        i = results.begin();
        bool found=true;
        tString::size_type pos=(m_ignorecase?Simplify(*i):*i).find(ret);
        if(pos!=tString::npos && pos+len < i->size()) {
            found=false;
            ret+=(m_ignorecase ? tolower(i->at(pos+len)) : i->at(pos+len));
            ++i;
            for(; i!=results.end(); ++i) {
                if((m_ignorecase?Simplify(*i):*i).find(ret) == tString::npos) {
                    found=true;
                    ret.erase(ret.length()-1);
                    break;
                }
            }
        }
        if (found) //we found a mismatch
            break;
        else
            len++;
    }
    while(true) { // something similar, but now for the left side of the word
        std::deque<tString>::iterator i;
        i = results.begin();
        bool found=true;
        tString::size_type pos=(m_ignorecase?Simplify(*i):*i).find(ret);
        if(pos!=tString::npos && pos > 0) {
            found=false;
            ret.insert(ret.begin(),(m_ignorecase ? tolower(i->at(pos-1)) : i->at(pos-1)));
            ++i;
            for(; i!=results.end(); ++i) {
                if((m_ignorecase?Simplify(*i):*i).find(ret) == tString::npos) {
                    found=true;
                    ret.erase(0,1);
                    break;
                }
            }
        }
        if (found)  //we found a mismatch
            break;
        else
            len++;
    }
    return ret;
}

//! @param results    the list of possible completions
//! @param word       the word that was already typed, it will be highlighted
void uAutoCompleter::ShowPossibilities(std::deque<tString> &results, tString &word) {
    if(results.size() > 10) {
        con << tOutput("$tab_completion_toomanyresults");
    }
    else {
        con << tOutput("$tab_completion_results");
        tString::size_type len=word.length();
        for(std::deque<tString>::iterator i=results.begin(); i!=results.end(); ++i) {
            tString result = m_ignorecase ? Simplify( *i ) : *i;
            tString::size_type pos= result.find(word);
            con << result.SubStr(0,pos)
            << "0xff8888"
            << result.SubStr(pos, len)
            << "0xffffff"
            << result.SubStr(pos+len)
            << "\n";
        }
    }
}

//! @param string     the string in which the completion should take place
//! @param pos        the position in strind where the replaced word ends
//! @param len        the length of the word that is already there
//! @param match      the string that will be inserted into the other one
//! @return the new cursor position
int uAutoCompleter::DoCompletion(tString &string, int pos, int len, tString &match) {
    string.erase(pos-len, len);
    string.insert(pos-len, match);
    return pos - len + match.size();
}

//! @param string     the string in which the completion should take place
//! @param pos        the position in string where the replaced word ends
//! @param len        the length of the word that is already there
//! @param match      the string that will be inserted into the other one
//! @return the new cursor position
int uAutoCompleter::DoFullCompletion(tString &string, int pos, int len, tString &match) {
    tString actualString = match + " ";
    return DoCompletion(string, pos, len, actualString);
}

//! @param string     the string in which the completion should take place
//! @param pos        the cursor position
//! @param len	      the length of the word
//! @return the new position on success (something was found), -1 on failure
int uAutoCompleter::TryCompletion(tString &string, unsigned pos, unsigned len) {
    tString word(string.SubStr(pos-len, len));
    std::deque<tString> results;
    FindPossibleWords(word, results);
    if(results.size() > 1){
        tString match(FindClosestMatch(word, results));
        if(match == word) { //no completion took place
            ShowPossibilities(results, word);
            return pos;
        }
        else { //do the completion
            return DoCompletion(string, pos, len, match);
        }
    }
    else if(!results.empty()){
        return DoFullCompletion(string, pos, len, results.front());
    }
    return -1;
}

//! @param string     the string in which the completion should take place
//! @param pos        the cursor position
//! @return the new cursor position
int uAutoCompleter::Complete(tString &string, unsigned pos) {
    if(m_LastCompletion != -1 && (unsigned)m_LastCompletion < pos) {
        int res = TryCompletion(string, pos, pos - m_LastCompletion);
        if(res != -1) return res;
    }
    int len = FindLengthOfLastWord(string, pos);
    if(len == -1) return pos; //in the middle of a word...
    int res = TryCompletion(string, pos, len);
    if(res != -1) {
        m_LastCompletion = pos - len;
        return res;
    }
    return pos;
}

//! @param ignorecase ignore upper and lower case?
void uAutoCompleter::SetIgnorecase(bool ignorecase) {
    m_ignorecase = ignorecase;
}

//! @param str the string to simplify
//! @returns the simplified string(in this case everything converted to lowercase)
tString uAutoCompleter::Simplify(tString const &str) {
    return str.ToLower();
}

//! @param M         passed on to uMenuItemString
//! @param desc      passed on to uMenuItemString
//! @param help      passed on to uMenuItemString
//! @param c         passed on to uMenuItemString
//! @param maxLength passed on to uMenuItemString
//! @param history   a reference to the history of commands
//! @param limit     the limit for the history's size
//! @param completer the autocompleter to be used, if 0, no completion will take place
uMenuItemStringWithHistory::uMenuItemStringWithHistory(uMenu *M,const tOutput& desc, const tOutput& help,tString &c, int maxLength, history_t &history, int limit, uAutoCompleter *completer ):
        uMenuItemString(M, desc,help,c, maxLength ),
        m_History(history),
        m_HistoryPos(0),
        m_HistoryLimit(limit),
        m_Completer(completer),
m_Searchmode(false) {
    m_History.push_front(tString());
}

//! This destructor will write the currently displayed string into the history if it isn't already in it
uMenuItemStringWithHistory::~uMenuItemStringWithHistory() {
    if(content->Len() > 1){
        for(history_t::iterator i=m_History.begin(); i!=m_History.end(); ++i) {
            if(*i == *content) {
                m_History.erase(i);
                break;
            }
        }
        m_History.front() = *content;
    } else {
        m_History.pop_front();
    }
    if(m_History.size() > m_HistoryLimit)
        m_History.pop_back();
}

//! @param e the event to process
/// @returns true if the event was handled, false if it wasn't
bool uMenuItemStringWithHistory::Event(SDL_Event &e){
#ifndef DEDICATED
    if (e.type==SDL_KEYDOWN &&
            (e.key.keysym.sym==SDLK_UP && !m_Searchmode )){
        if (m_History.size() - 1 > m_HistoryPos)
        {
            if(m_HistoryPos == 0)  //the new entry... save it before overwriting it
                m_History.front() = *content;
            m_HistoryPos++;
            *content = m_History[m_HistoryPos];
            realCursorPos = content->size();
        }

        return true;
    }
    else if (e.type==SDL_KEYDOWN &&
             (e.key.keysym.sym==SDLK_DOWN && !m_Searchmode)){
        if (m_HistoryPos > 0)
        {
            m_HistoryPos--;
            *content = m_History[m_HistoryPos];
            realCursorPos = content->size();
        }

        return true;
    }
    else if (e.type==SDL_KEYDOWN && e.key.keysym.mod & KMOD_CTRL && e.key.keysym.sym == SDLK_r ) {
        m_Searchmode = true;
        if(m_HistoryPos == 0 && !content->empty()) {  //the new entry... save it before overwriting it
            m_History.front() = *content;
            m_History.push_front(tString());
        }
        content->erase();
        realCursorPos=0;
        m_HistoryPos++;
        m_SearchFailing=false;
        return true;
    }
    else if (e.type==SDL_KEYDOWN &&
             (e.key.keysym.sym==SDLK_TAB && !m_Searchmode)){
        if(m_Completer != 0) {
            realCursorPos = m_Completer->Complete(*content, realCursorPos);
        }

        return true;
    }
    else if (e.type==SDL_KEYDOWN && m_Searchmode) {
        if(e.key.keysym.sym==SDLK_LEFT || e.key.keysym.sym==SDLK_RIGHT) {
            *content = m_History[m_HistoryPos];
            m_Searchmode = false;
            realCursorPos=0;
            return true;
        }
        bool ret = uMenuItemString::Event(e);
        tString searchstring = content->ToLower();
        unsigned int pos = 0;
        for(history_t::iterator iter = m_History.begin(); iter != m_History.end(); ++iter, ++pos) {
            if (iter->ToLower().find(searchstring) != tString::npos) {
                m_HistoryPos = pos;
                m_SearchFailing=false;
                return ret;
            }
        }
        m_SearchFailing=true;
        return ret;
    }

    else
        return uMenuItemString::Event(e);
#endif
    return false;
}

void uMenuItemStringWithHistory::Render(REAL x,REAL y,REAL alpha,bool selected) {
    if(m_Searchmode) {
        DisplayText(-.88, y-.1, ((m_SearchFailing ? "Failing reverse search: " : "Reverse search: ") + m_History[m_HistoryPos]).c_str(), selected, alpha, -1);
    }
    uMenuItemString::Render(x, y, alpha, selected);
}

//! @param filename the name of the file (in var/) to be loaded and saved to
uMenuItemStringWithHistory::history_t::history_t(char const *filename) : std::deque<tString>(), m_filename(filename) {
    tTextFileRecorder the_file ( tDirectories::Var(), m_filename );
    while(!the_file.EndOfFile()) {
        push_front(the_file.GetLine());
    }
}

uMenuItemStringWithHistory::history_t::~history_t() {
    std::ofstream the_file;
    if(tDirectories::Var().Open( the_file, m_filename)) {
        for(reverse_iterator i = rbegin(); i != rend(); ++i) {
            the_file << *i << "\n";
        }
    }
}


// *****************************************************
//  Submenu
// *****************************************************


uMenuItemSubmenu::uMenuItemSubmenu(uMenu *M,
                                   uMenu *s,
                                   const tOutput& help)
        :uMenuItem(M,help),submenu(s){}


void uMenuItemSubmenu::Render(REAL x,REAL y,REAL alpha,bool selected){
    DisplayTextSpecial(x,y,submenu->title,selected,alpha,0);
}

void uMenuItemSubmenu::Enter(){
    submenu->Enter();
}

tString uMenuItemSubmenu::GetPanelLabel(){
    return tString( submenu->title );
}

// *****************************************************
//  action
// *****************************************************


uMenuItemAction::uMenuItemAction(uMenu *M,
                                 const tOutput& n, const tOutput& help )
        :uMenuItem(M,help),name_(n){}


void uMenuItemAction::Render(REAL x,REAL y,REAL alpha,bool selected){
    DisplayTextSpecial(x,y,name_,selected,alpha,0);
}


void uMenuItemAction::Enter()
{
    tASSERT( 0 )
}


// *****************************************************
//  function
// *****************************************************


uMenuItemFunction::uMenuItemFunction(uMenu *M,
                                     const tOutput& n, const tOutput& help,
                                     FUNCPTR f)
        :uMenuItemAction(M,n,help),func(f){}

void uMenuItemFunction::Enter(){
    (*func)();
}



uMenuItemFunctionInt::uMenuItemFunctionInt(uMenu *M,
        const tOutput& n,
        const tOutput& help,
        INTFUNCPTR f,int a)
        :uMenuItemAction(M,n,help),func(f),arg(a){}


void uMenuItemFunctionInt::Enter(){
    (*func)(arg);
}

// *****************************************************
//  File Selection (added by k)
// *****************************************************

void uMenuItemFileSelection::NewChoice( uSelectItem<bool> * ) {}
void uMenuItemFileSelection::NewChoice( char *, bool ) {}

void uMenuItemFileSelection::Reload()
{
    Clear();
    if ( defaultFileName_.Len() > 1 && defaultFilePath_.Len() > 1 )
        AddFile( defaultFileName_, defaultFilePath_, formatName_ );
    LoadDirectory( dir_, fileSpec_, formatName_ );
}

void uMenuItemFileSelection::LoadDirectory( const char *dir, const char *fileSpec,
        bool formatName /*= true*/ )
{
    tArray <tString> files;
    tString filePath ( dir );
    tDirectories::GetFiles( tString( dir ), tString( fileSpec ), files, getFilesFlag_ );
    for ( int i = 0; i < files.Len(); i++ )
    {
        AddFile( files( i ), filePath + files( i ), formatName );
    }
}

void uMenuItemFileSelection::AddFile( const char *fileName, const char *filePath,
                                      bool formatName /*= true*/ )
{
    tString menuName ( fileName );
    if ( formatName )
        tDirectories::FileNameToMenuName( fileName, menuName );
    uMenuItemSelection<tString>::NewChoice( menuName, "", tString( filePath ) );
}

// *****************************************************
// Menu Enter/Leave-Callback
// *****************************************************

static tCallback *enter_anchor=NULL,*leave_anchor=NULL, *background_anchor=NULL;

uCallbackMenuEnter::uCallbackMenuEnter(AA_VOIDFUNC *f)
        :tCallback(enter_anchor,f){}

void uCallbackMenuEnter::MenuEnter(){
    Exec(enter_anchor);
}

uCallbackMenuLeave::uCallbackMenuLeave(AA_VOIDFUNC *f)
        :tCallback(leave_anchor,f){}

void uCallbackMenuLeave::MenuLeave(){
    Exec(leave_anchor);
}

uCallbackMenuBackground::uCallbackMenuBackground(AA_VOIDFUNC *f)
        :tCallback(background_anchor,f){}

void uCallbackMenuBackground::MenuBackground(){
    Exec(background_anchor);
}

// poll input, return true if ESC was pressed
bool uMenu::IdleInput( bool processInput )
{
#ifndef DEDICATED
    if( !processInput )
    {
        sr_LockSDL();
        SDL_PumpEvents();
        sr_UnlockSDL();
        return uMenu::quickexit != uMenu::QuickExit_Off;
    }

    SDL_Event event;
    uInputProcessGuard inputProcessGuard;
    while (!s_idleBackground && su_GetSDLInput(event))
    {
        switch (event.type)
        {
        case SDL_KEYDOWN:
            switch (event.key.keysym.sym)
            {
            case(SDLK_ESCAPE):
                s_globalRepeat = false;
                lastkey=tSysTimeFloat();
                return true;
                break;
            default:
                break;
            }
        default:
            break;
        }
    }

    return uMenu::quickexit != uMenu::QuickExit_Off;
#endif

    return false;
}

// ***************************************
// * uAnimation
// ***************************************

//! crude animation class
uAnimationFrame::uAnimationFrame( float duration_ )
: duration( duration_ )
{
}

uAnimationFrame::uAnimationFrame( char const * texture, float duration_ )
: duration( duration_ )
{
    textures.push_back( texture );
}

uAnimationFrame::uAnimationFrame( char const * texture1, char const * texture2, float duration_ )
: duration( duration_ )
{
    textures.push_back( texture1 );
    textures.push_back( texture2 );
}

uAnimationFrame::uAnimationFrame( char const * texture1, char const * texture2, char const * texture3, float duration_ )
: duration( duration_ )
{
    textures.push_back( texture1 );
    textures.push_back( texture2 );
    textures.push_back( texture3 );
}

uAnimationFrame::uAnimationFrame( char const * texture1, char const * texture2, char const * texture3, char const * texture4, float duration_ )
: duration( duration_ )
{
    textures.push_back( texture1 );
    textures.push_back( texture2 );
    textures.push_back( texture3 );
    textures.push_back( texture4 );
}

//! loads an animation from a file
bool uAnimationFrame::Load( std::vector< uAnimationFrame > & animation, char const * filename )
{
    std::ifstream f;
    if( !tDirectories::Data().Open( f, filename ) )
    {
        return false;
    }

    while( f.good() )
    {
        tString l;
        l.ReadLine( f );

        // ignore comments
        if( l[0]=='#' )
        {
            continue;
        }

        std::istringstream s(l);

        uAnimationFrame frame;
        s >> frame.duration;
        if( s.eof() )
        {
            continue;
        }
        tString texture;
        while( s.good() )
        {
            s >> texture;
            frame.textures.push_back( texture );
        }
        if( !s.eof() )
        {
            return false;
        }
        animation.push_back( frame );
    }
    if( !f.eof() )
    {
        return false;
    }

    return true;
}

#ifndef DEDICATED

// ***************************************
// * uAnimationPlayer
// ***************************************

uAnimationPlayer::uAnimationPlayer( std::vector< uAnimationFrame > const & animation )
: animation_( animation ), current_( -1 )
{
}

uAnimationPlayer::~uAnimationPlayer()
{
    ClearTextures();
}

void uAnimationPlayer::ClearTextures()
{
    for( std::vector< rITexture * >::iterator iter = textures_.begin();
         iter != textures_.end(); ++iter )
    {
        delete (*iter);
    }

    textures_.clear();
}

void uAnimationPlayer::Render( tRectangle & drawArea )
{
    // no animation? screw this.
    if( animation_.size() == 0 )
    {
        return;
    }

    bool change = false;

    // start from the beginning
    if( current_ < 0 )
    {
        current_ = 0;
        lastChange_ = tSysTimeFloat();
        change = true;
    }

    // advance
    REAL completion = ( tSysTimeFloat() - lastChange_ )/animation_[current_].duration;
    if( completion >= 1 )
    {
        current_++;
        if( current_ >= (int)animation_.size() )
        {
            current_ = 0;
        }
        lastChange_ = tSysTimeFloat();
        change = true;
        completion = 0;
    }
    if( completion < .01 )
        completion = 0.01;

    // get current frame
    uAnimationFrame const & frame = animation_[current_];

    // load textures
    if( change )
    {
        ClearTextures();
        for( std::vector< std::string >::const_iterator iter = frame.textures.begin();
             iter != frame.textures.end(); ++iter )
        {
            textures_.push_back( tNEW(rFileTexture)( rTextureGroups::TEX_FONT, (*iter).c_str(), false, false, true ) );
        }
    }

    REAL xl = drawArea.GetLow().x;
    REAL yl = drawArea.GetLow().y;
    REAL xh = drawArea.GetHigh().x;
    REAL yh = drawArea.GetHigh().y;

    glEnable(GL_ALPHA_TEST);
    glDisable(GL_BLEND);
    for( std::vector< rITexture * >::iterator iter = textures_.begin();
         iter != textures_.end(); ++iter )
    {
        (*iter)->Select(true);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,
                        GL_NEAREST);

        bool end = ( iter+1 == textures_.end() );
        if ( end )
        {
            glAlphaFunc(GL_GREATER,1-completion);

        }
        else
        {
            glAlphaFunc(GL_GREATER,0);
        }

        Color(1,1,1);
        BeginQuads();
        TexCoord(0,1);
        Vertex(xl,yl);
        TexCoord(0,0);
        Vertex(xl,yh);
        TexCoord(1,0);
        Vertex(xh,yh);
        TexCoord(1,1);
        Vertex(xh,yl);
        RenderEnd();
    }
    glAlphaFunc(GL_GREATER,0);
}

#endif

// print a big title, small description and animation
bool uMenu::Message(const tOutput& message, const tOutput& interpretation, REAL to, std::vector< uAnimationFrame > const & animation )
{
    bool ret = true;
#ifdef DEDICATED
    con << message << ":\n";
    con << interpretation << '\n';
#else
    uAnimationPlayer player( animation );

    // reload textures (just in case)
    rITexture::UnloadAll();

    tAdvanceFrame();

    bool textOutBack = sr_textOut;
    sr_textOut = false;

    FUNCPTR idle_back = idle;
    uMenu::SetIdle(NULL);

    rTextField::SetDefaultColor( tColor(1,1,1,1) );
    rTextField::SetBlendColor( tColor(1,1,1,1) );

    rSysDep::ClearGL();
    rSysDep::SwapGL();
    //    if (sr_glOut)
    //    {
    //        rFont::s_defaultFont.Select();
    //        rFont::s_defaultFontSmall.Select();
    //    }
    rSysDep::ClearGL();
    rSysDep::SwapGL();

    double timeout = tSysTimeFloat() + to;
    double ignoreInput = tSysTimeFloat() + .5;
    SDL_Event tEvent;

    // catch some keyboard input
    {
        uInputProcessGuard inputProcessGuard;
        while (su_GetSDLInput(tEvent)) ;
    }

    {
        uInputProcessGuard inputProcessGuard;

        unsigned offset = 0; //amount of scrolling taking place
        //convert to an array for scrolling
        tString interpretationString;
        interpretationString << interpretation << "\n";
        std::vector<tString> lines;
        int lastNewline = 0;
        for (int i = 0; i < interpretationString.Len() - 1; ++i) {
            if (interpretationString[i] == '\n' && i != 0) {
                lines.push_back(interpretationString.SubStr(lastNewline, i - lastNewline));
                lastNewline = i + 1;
            }
        }
        while (  !quickexit &&
                 (to < 0 || tSysTimeFloat() < timeout)){
            //while(  !quickexit && ( !su_GetSDLInput(tEvent) || tEvent.type!=SDL_KEYDOWN) &&
            //        (to < 0 || tSysTimeFloat() < timeout)){
            if ( su_GetSDLInput(tEvent) && tEvent.type==SDL_KEYDOWN) {
                switch (tEvent.key.keysym.sym) {
                case SDLK_UP:
                    if (offset > 0)
                        offset -= 1;
                    continue;
                case SDLK_DOWN:
                    offset += 1;
                    continue;
                case SDLK_ESCAPE:
                    ret = false;
                    break;
                default:
                    break;
                }
                if( tSysTimeFloat() > ignoreInput )
                {
                    // exit on any key
                    break;
                }
                else
                {
                    // esc may have been pressed in error
                    ret = true;
                }
            }
            if ( sr_glOut )
            {
                sr_ResetRenderState(true);
                rViewport::s_viewportFullscreen.Select();

                rSysDep::ClearGL();

                // GenericBackground();
                static rFileTexture background( rTextureGroups::TEX_FONT, "textures/message_background.png" );
                background.Select();

                Color(1,1,1);

                BeginQuads();
                TexCoord(0,0);
                Vertex(-1,1);
                TexCoord(1,0);
                Vertex(1,1);
                TexCoord(1,1);
                Vertex(1,-1);
                TexCoord(0,1);
                Vertex(-1,-1);
                RenderEnd();

                //16*3/640.0, 32*3/480.0
                REAL w=0.1*(REAL(sr_screenHeight)/sr_screenWidth),h=0.2;

                //REAL middle=-.6;

                tString m(message);
                int len = tColoredString::RemoveColors(m).Len();
                float maxWidth = 4.8;
                if (w * len > maxWidth)
                {
                    h = h * maxWidth / (w * len);
                    w = maxWidth / len;
                }

                Color(1,1,1);
                DisplayText(0,.8,w,message,sr_fontError);

                //16/640.0
                w = 1/30.0*(REAL(sr_screenHeight)/sr_screenWidth);
                h = 32/480.0;

                REAL center = .4;
                if (offset >= lines.size()) offset = lines.size() - 1;
                {
                    rTextField c(-.9,.6, h, sr_fontError);
                    c.EnableLineWrap();
                    c.SetWidth(1.8);

                    for (unsigned i = offset; i < lines.size(); ++i)
                        c << lines[i] << "\n";

                    center = (c.GetBottom()+1)/4;
                }

                // determite best display size for animation, asuming it is 64 pixels high
                {
                    int middleY = int( sr_screenHeight * center );
                    int maxHeight = middleY - 5 - sr_screenHeight/20;
                    int maxWidth = sr_screenWidth/2 - 10;
                    int height = 32;
                    int width = height*2;
                    int scale = 1;
                    while( ( scale + 1) * height + 1 < maxHeight && ( scale + 1 ) * width < maxWidth )
                    {
                        scale++;
                    }
                    height *= scale;
                    width  *= scale;
                    REAL wr = 2*width/REAL(sr_screenWidth);
                    REAL hr = 2*height/REAL(sr_screenHeight);
                    REAL c = (center*2)-1;

                    tRectangle ani = tRectangle( tCoord(-wr, c-hr), tCoord(wr, c+hr) );
                    player.Render( ani );
                }


            }
            rSysDep::SwapGL();
            tAdvanceFrame();
        }
    }

    // catch some keyboard input
    {
        uInputProcessGuard inputProcessGuard;
        while (su_GetSDLInput(tEvent)) ;
    }

    uMenu::SetIdle(idle_back);

    // reload textures (just in case)
    rITexture::UnloadAll();

    sr_textOut = textOutBack;
#endif

    return ret;
}

bool uMenu::Message(const tOutput& message, const tOutput& interpretation, REAL to, char const * animation )
{
    std::vector< uAnimationFrame > frames;
    uAnimationFrame::Load( frames, animation );
    return Message( message, interpretation, to, frames );
}

// return value: false only if the user pressed ESC
bool uMenu::Message(const tOutput& message, const tOutput& interpretation, REAL to)
{
    std::vector< uAnimationFrame > noAnimation;
    return Message( message, interpretation, to, noAnimation );
}

