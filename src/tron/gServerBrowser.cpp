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

#include "gServerBrowser.h"
#include "gGame.h"
#include "gLogo.h"
#include "gServerFavorites.h"
#include "gFriends.h"

#include "nServerInfo.h"
#include "nNetwork.h"

#include "rSysdep.h"
#include "rScreen.h"
#include "rConsole.h"
#include "rRender.h"

#include "uMenu.h"
#include "uInputQueue.h"

#include "tMemManager.h"
#include "tSysTime.h"
#include "tToDo.h"

#include "tDirectories.h"
#include "tConfiguration.h"

#include <sstream>

int gServerBrowser::lowPort  = 4534;

int gServerBrowser::highPort = 4540;
static bool continuePoll = false;
static int sg_simultaneous = 20;
static tSettingItem< int > sg_simultaneousConf( "BROWSER_QUERIES_SIMULTANEOUS", sg_simultaneous );

static tOutput *sg_StartHelpText = NULL;

nServerInfo::QueryType sg_queryType = nServerInfo::QUERY_OPTOUT;
tCONFIG_ENUM( nServerInfo::QueryType );
static tSettingItem< nServerInfo::QueryType > sg_query_type( "BROWSER_QUERY_FILTER", sg_queryType );

class gServerMenuItem;

static nServerInfo::PrimaryKey sg_sortKey = nServerInfo::KEY_USERS;
tCONFIG_ENUM( nServerInfo::PrimaryKey );
static tConfItem< nServerInfo::PrimaryKey > sg_sortKeyConf( "BROWSER_SORT_KEY", sg_sortKey );

//  enabling this will cause friend's names to be case sensitive
static bool sg_enableFriendsCasing = true;
static tConfItem< bool > sg_enableFriendsCasingConf("ENABLE_FRIENDS_CASING", sg_enableFriendsCasing);

class gServerInfo: public nServerInfo
{
public:
    gServerMenuItem *menuItem;
    bool show; //for server browser hiding

    gServerInfo():menuItem(NULL), show(true)
    {
    }

    virtual ~gServerInfo();

    // during browsing, the whole server list consists of gServerInfos
    static gServerInfo * GetFirstServer()
    {
        return dynamic_cast< gServerInfo * >( nServerInfo::GetFirstServer() );
    }

    gServerInfo * Next()
    {
        return dynamic_cast< gServerInfo * >( nServerInfo::Next() );
    }
};

nServerInfo* CreateGServer()
{
    nServerInfo *ret = tNEW(gServerInfo);

    //if (!continuePoll)
    //{
    //    nServerInfo::StartQueryAll( sg_queryType );
    //    continuePoll = true;
    // }

    return ret;
}


class gServerMenu: public uMenu
{
public:
    virtual void OnRender();
    tString filter_string;

    void Update(); // sort the server view by score
    gServerMenu(const char *title);
    ~gServerMenu();

    virtual void HandleEvent( SDL_Event event );

    void Render(REAL y,
                const tString &servername, const tOutput &score,
                const tOutput &users     , const tOutput &ping);

    void Render(REAL y,
                const tString &servername, const tString &score,
                const tString &users     , const tString &ping);
    
    void Render(REAL x, REAL y, tString &c, int center, int cursor, int cursorPos);
};

class gBrowserMenuItem: public uMenuItem
{
protected:
    bool displayHelp_;
    REAL helpAlpha_;
    
    gBrowserMenuItem(uMenu *M,const tOutput &help): uMenuItem( M, help )
      , displayHelp_{false}
      , helpAlpha_(0.0f)
    {
    }

    // handles a key press
    virtual bool Event( SDL_Event& event );

    virtual void RenderBackground();

    virtual bool DisplayHelp( bool display, REAL y, REAL alpha )
    {
        helpAlpha_ = alpha;
        displayHelp_ = display;
        return false;
    }
};

class gServerFilterMenuItem: public uMenuItemString
{
public:
    gServerFilterMenuItem(gServerMenu *M)
        :uMenuItemString(M,"$network_master_filter","",M->filter_string)
    {}
        
    virtual ~gServerFilterMenuItem(){}
    
    virtual void Render(REAL x,REAL y,REAL alpha=1, bool selected=0);
    virtual bool Event( SDL_Event& event );
};

class gServerMenuItem: public gBrowserMenuItem
{
protected:
    gServerInfo *server;
    double      lastPing_; //!< the time of the last manual ping
    bool        favorite_; //!< flag indicating whether this is a favorite
public:
    void AddFavorite();
    void RemoveFavorite();
    void SetServer(nServerInfo *s);
    gServerInfo *GetServer();

    void Render(REAL x,REAL y,REAL alpha=1, bool selected=0) override;
    void RenderBackground() override;

    void Enter() override;
    void Select() override;

    // handles a key press
    bool Event( SDL_Event& event ) override;

    gServerMenuItem(gServerMenu *men);
    virtual ~gServerMenuItem();
};

class gServerStartMenuItem: public gBrowserMenuItem
{
public:
    virtual void Render(REAL x,REAL y,REAL alpha=1, bool selected=0);

    virtual void Enter();

    gServerStartMenuItem(gServerMenu *men);
    virtual ~gServerStartMenuItem();
};






static bool sg_RequestLANcontinuously = false;

void gServerBrowser::BrowseMaster()
{
    BrowseSpecialMaster(0,"");
}

// the currently active master
static nServerInfoBase * sg_currentMaster = 0;
nServerInfoBase * gServerBrowser::CurrentMaster()
{
    return sg_currentMaster;
}


void gServerBrowser::BrowseSpecialMaster( nServerInfoBase * master, char const * prefix )
{
    sg_currentMaster = master;

    sg_RequestLANcontinuously = false;

    sn_ServerInfoCreator *cback = nServerInfo::SetCreator(&CreateGServer);

    sr_con.autoDisplayAtNewline=true;
    sr_con.fullscreen=true;

#ifndef DEDICATED
    rSysDep::SwapGL();
    rSysDep::ClearGL();
    rSysDep::SwapGL();
    rSysDep::ClearGL();
#endif

    bool to=sr_textOut;
    sr_textOut=true;

    nServerInfo::DeleteAll();
    nServerInfo::GetFromMaster( master, prefix );
    nServerInfo::Save();

#ifdef SERVER_SURVEY
    // connect to all servers and log stats
    nServerInfo * info = nServerInfo::GetFirstServer();
    {
        std::ofstream o;
        if ( tDirectories::Var().Open(o, "serversurvey.txt", std::ios::app) )
        {
            o << "New survey\n";
        }
    }
    while( info )
    {
        ConnectToServer(info);
        info = info->Next();
    }
    return;
#endif

    //  gLogo::SetBig(true);
    //  gLogo::SetSpinning(false);

    sr_textOut = to;

    tOutput StartHelpTextInternet("$network_master_host_inet_help");
    sg_StartHelpText = &StartHelpTextInternet;
    sg_TalkToMaster = true;

    BrowseServers();

    nServerInfo::Save();

    sg_TalkToMaster = false;

    nServerInfo::SetCreator(cback);

    sg_currentMaster = master;
}

void gServerBrowser::BrowseLAN()
{
    // TODO: reacivate and see what happens. Done.
    sg_RequestLANcontinuously = true;
    //	sg_RequestLANcontinuously = false;

    sn_ServerInfoCreator *cback = nServerInfo::SetCreator(&CreateGServer);

    sr_con.autoDisplayAtNewline=true;
    sr_con.fullscreen=true;

#ifndef DEDICATED
    rSysDep::SwapGL();
    rSysDep::ClearGL();
    rSysDep::SwapGL();
    rSysDep::ClearGL();
#endif

    bool to=sr_textOut;
    sr_textOut=true;

    nServerInfo::DeleteAll();
    nServerInfo::GetFromLAN(lowPort, highPort);

    sr_textOut = to;

    tOutput StartHelpTextLAN("$network_master_host_lan_help");
    sg_StartHelpText = &StartHelpTextLAN;
    sg_TalkToMaster = false;

    BrowseServers();

    nServerInfo::SetCreator(cback);
}

#ifndef DEDICATED
static void sg_BrowseServerList(); // the full screen server list, below
#endif

void gServerBrowser::BrowseServers()
{
    if( nServerInfoAdmin::GetAdmin() )
    {
        nServerInfoAdmin::GetAdmin()->BeforeNewScan();
    }

    //nServerInfo::CalcScoreAll();
    //nServerInfo::Sort();
    nServerInfo::StartQueryAll( sg_queryType );
    continuePoll = true;

    // eat excess input the user made while the list was fetched
    SDL_Event ignore;
    REAL time;
    while(su_GetSDLInput(ignore, time)) ;

#ifndef DEDICATED
    sg_BrowseServerList();
#else
    gServerMenu browser("Server Browser");

    gServerStartMenuItem start(&browser);
    gServerFilterMenuItem filter(&browser);

    browser.Update();
    browser.Enter();
#endif

    nServerInfo::GetFromLANContinuouslyStop();

    //  gLogo::SetBig(false);
    //  gLogo::SetSpinning(true);
    // gLogo::SetDisplayed(true);
}

void gServerMenu::HandleEvent( SDL_Event event )
{
#ifndef DEDICATED
    // Ignore event when we have the filter menu item
    if(items.Len() - selected == 1)
    {
        return uMenu::HandleEvent( event );
    }
    
    switch (event.type)
    {
    case SDL_KEYDOWN:
        switch (event.key.keysym.sym)
        {
        case(SDLK_LEFT):
                        sg_sortKey = static_cast<nServerInfo::PrimaryKey>
                            ( ( sg_sortKey + nServerInfo::KEY_MAX-1 ) % nServerInfo::KEY_MAX );
            Update();
            return;
            break;
        case(SDLK_RIGHT):
                        sg_sortKey = static_cast<nServerInfo::PrimaryKey>
                            ( ( sg_sortKey + 1 ) % nServerInfo::KEY_MAX );
            Update();
            return;
            break;
        case(SDLK_m):
                        FriendsToggle();
            Update();
            return;
            break;
        case(SDLK_HOME):
            SetSelected(NumItems() - 1);
            Update();
            return;
        case(SDLK_END):
            SetSelected(0);
            Update();
            return;
        default:
            break;
        }
    }
#endif

    uMenu::HandleEvent( event );
}

void gServerMenu::OnRender()
{
    uMenu::OnRender();

    // next time the server list is to be resorted
    static double sg_serverMenuRefreshTimeout=-1E+32f;

    if (sg_serverMenuRefreshTimeout < tSysTimeFloat())
    {
        Update();
        sg_serverMenuRefreshTimeout = tSysTimeFloat()+2.0f;
    }
}

// priority of bookmarks in sorting
static nServerInfo::SortHelperPriority sg_bookmarkPriority[nServerInfo::KEY_MAX]=
{
    nServerInfo::PRIORITY_NONE,
    nServerInfo::PRIORITY_PRIMARY,
    nServerInfo::PRIORITY_SECONDARY,
    nServerInfo::PRIORITY_PRIMARY
};

tCONFIG_ENUM( nServerInfo::SortHelperPriority );
static tSettingItem< nServerInfo::SortHelperPriority > sgc_bookmarkPriorityName( "BROWSER_BOOKMARK_PRIORITY_NAME", sg_bookmarkPriority[nServerInfo::KEY_NAME] );
static tSettingItem< nServerInfo::SortHelperPriority > sgc_bookmarkPriorityPing( "BROWSER_BOOKMARK_PRIORITY_PING", sg_bookmarkPriority[nServerInfo::KEY_PING] );
static tSettingItem< nServerInfo::SortHelperPriority > sgc_bookmarkPriorityUsers( "BROWSER_BOOKMARK_PRIORITY_USERS", sg_bookmarkPriority[nServerInfo::KEY_USERS] );
static tSettingItem< nServerInfo::SortHelperPriority > sgc_bookmarkPriorityScore( "BROWSER_BOOKMARK_PRIORITY_SCORE", sg_bookmarkPriority[nServerInfo::KEY_SCORE] );


void gServerMenu::Update()
{
    // get currently selected server
    gServerMenuItem *item = NULL;
    if ( selected < items.Len() )
    {
        item = dynamic_cast<gServerMenuItem*>(items(selected));
    }
    gServerInfo* info = NULL;
    if ( item )
    {
        info = item->GetServer();
    }

    // keep the cursor position relative to the top, if possible
    int selectedFromTop = items.Len() - selected;

    ReverseItems();

    nServerInfo::CalcScoreAll();
    nServerInfo::Sort( nServerInfo::PrimaryKey( sg_sortKey ), &gServerFavorites::IsFavorite, sg_bookmarkPriority[sg_sortKey] );

    int mi = 2;
    gServerInfo *run = gServerInfo::GetFirstServer();
    bool oneFound = false; //so we can display all if none were found
    
    tString filteredFriends[MAX_FRIENDS];
    tString* friends = getFriends();
    int i;
    for (i = MAX_FRIENDS-1; i>=0; i--)
    {
        filteredFriends[i] = sg_enableFriendsCasing ? friends[i] : friends[i].ToLower();
    }

    while (run)
    {
        if(filter_string.Len() > 1)
        {
            run->show = false;
            oneFound = true;
            
            tString name;
            name << tColoredString::RemoveColors( run->GetName(), false );
            
            if(name.ToLower().find(filter_string.ToLower()) != std::string::npos)
            {
                run->show = true;
            }
        }
        //check friend filter
        else if (getFriendsEnabled())
        {
            run->show = false;
            tString userNames = sg_enableFriendsCasing ? run->UserNames() : run->UserNames().ToLower();
            tString globalIds = sg_enableFriendsCasing ? run->UserGlobalIDs() : run->UserGlobalIDs().ToLower();
            for (i = MAX_FRIENDS-1; i>=0; i--)
            {
                if (run->Users() > 0 && friends[i].Len() > 1 && (userNames.StrPos(filteredFriends[i]) >= 0 || globalIds.StrPos(filteredFriends[i]) >= 0))
                {
                    oneFound = true;
                    run->show = true;
                }
            }
        }
        run = run->Next();
    }

    run = gServerInfo::GetFirstServer();
    {
        while (run)
        {
            if (run->show || oneFound == false)
            {
                if (mi >= items.Len())
                    tNEW(gServerMenuItem)(this);

                gServerMenuItem *item = dynamic_cast<gServerMenuItem*>(items(mi));
                item->SetServer(run);
                mi++;
            }
            run = run->Next();
        }
    }

    if (items.Len() == 1)
        selected = 1;

    while(mi < items.Len() && items.Len() > 2)
    {
        uMenuItem *it = items(items.Len()-1);
        delete it;
    }

    ReverseItems();

    // keep the cursor position relative to the top, if possible ( calling function will handle the clamping )
    selected = items.Len() - selectedFromTop;

    // set cursor to currently selected server, if possible
    if ( info && info->menuItem )
    {
        selected = info->menuItem->GetID();
    }

    if (sg_RequestLANcontinuously)
    {
        static REAL timeout=-1E+32f;

        if (timeout < tSysTimeFloat())
        {
            nServerInfo::GetFromLANContinuously();
            if (!continuePoll)
            {
                nServerInfo::StartQueryAll( sg_queryType );
                continuePoll = true;
            }
            timeout = tSysTimeFloat()+10;
        }
    }
}

gServerMenu::gServerMenu(const char *title)
        : uMenu(title, false)
{
    nServerInfo *run = nServerInfo::GetFirstServer();
    
    while (run)
    {
        gServerMenuItem *item = tNEW(gServerMenuItem)(this);
        item->SetServer(run);
        run = run->Next();
    }

    ReverseItems();

    if (items.Len() <= 0)
    {
        selected = 1;
        tNEW(gServerMenuItem)(this);
    }
    else
        selected = items.Len();
}

gServerMenu::~gServerMenu()
{
    for (int i=items.Len()-1; i>=0; i--)
        delete items(i);
}

#ifndef DEDICATED
static REAL text_height=.05;

static REAL shrink = .6f;
static REAL displace = .15;

void gServerMenu::Render(REAL y,
                         const tString &servername, const tString &score,
                         const tString &users     , const tString &ping)
{
    if (sr_glOut)
    {
        DisplayText(-.9f, y, text_height, servername.c_str(), sr_fontServerBrowser, -1);
        DisplayText(.6f, y, text_height, ping.c_str(), sr_fontServerBrowser, 1);
        DisplayText(.75f, y, text_height, users.c_str(), sr_fontServerBrowser, 1);
        DisplayText(.9f, y, text_height, score.c_str(), sr_fontServerBrowser, 1);
    }
}

void gServerMenu::Render(REAL x,REAL y, tString &c, int center = 0, int cursor = 0, int cursorPos = 0)
{
    if (sr_glOut)
    {
        DisplayText(x, y, text_height, c.c_str(), sr_fontServerBrowser, -1, cursor, cursorPos);
    }
}

void gServerMenu::Render(REAL y,
                         const tString &servername, const tOutput &score,
                         const tOutput &users     , const tOutput &ping)
{
    tColoredString highlight, normal;
    highlight << tColoredString::ColorString( 1,.7,.7 );
    normal << tColoredString::ColorString( .7,.3,.3 );

    tString sn, s, u, p;

    sn << normal;
    s << normal;
    u << normal;
    p << normal;

    switch ( sg_sortKey )
    {
    case nServerInfo::KEY_NAME:
        sn = highlight;
        break;
    case nServerInfo::KEY_PING:
        p = highlight;
        break;
    case nServerInfo::KEY_USERS:
        u = highlight;
        break;
    case nServerInfo::KEY_SCORE:
        s = highlight;
        break;
    case nServerInfo::KEY_MAX:
        break;
    }

    sn << servername;// tColoredString::RemoveColors( servername );
    s  << score;
    u  << users;
    p  << ping;

    Render(y, sn, s, u, p);
}

#endif /* DEDICATED */
static bool sg_filterServernameColorStrings = true;
static tSettingItem< bool > removeServerNameColors("FILTER_COLOR_SERVER_NAMES", sg_filterServernameColorStrings);
static bool sg_filterServernameDarkColorStrings = true;
static tSettingItem< bool > removeServerNameDarkColors("FILTER_DARK_COLOR_SERVER_NAMES", sg_filterServernameDarkColorStrings);

void gServerMenuItem::Render(REAL x,REAL y,REAL alpha, bool selected)
{
#ifndef DEDICATED
    // REAL time=tSysTimeFloat()*10;

    SetColor( selected, alpha );

    gServerMenu *serverMenu = static_cast<gServerMenu*>(menu);

    if (server)
    {
        tColoredString name;
        tString score;
        tString users;
        tString ping;

        int p = static_cast<int>(server->Ping()*1000);
        if (p < 0)
            p = 0;
        if (p > 10000)
            p = 10000;

        int s = static_cast<int>(server->Score());
        if (server->Score() > 10000)
            s = 10000;
        if (server->Score() < -10000)
            s = -10000;

        if ( favorite_ )
        {
            score << "B ";
        }
        if (server->Polling())
        {
            score << tOutput("$network_master_polling");
        }
        else if (!server->Reachable())
        {
            score << tOutput("$network_master_unreachable");
        }
        else if ( nServerInfo::Compat_Ok != server->Compatibility() )
        {
            switch( server->Compatibility() )
            {
            case nServerInfo::Compat_Upgrade:
                score << tOutput( "$network_master_upgrage" );
                break;
            case nServerInfo::Compat_Downgrade:
                score << tOutput( "$network_master_downgrage" );
                break;
            default:
                score << tOutput( "$network_master_incompatible" );
                break;
            }
        }
        else if ( !favorite_ && server->GetClassification().noJoin_.Len() > 1 )
        {
            score << server->GetClassification().noJoin_;
        }
        else if ( server->Users() >= server->MaxUsers() )
        {
            score << tOutput( "$network_master_full" );
            score << " (" << server->Users() << "/" << server->MaxUsers() << ")";
        }
        else
        {
            score << s;
            users << server->Users() << "/" << server->MaxUsers();
            ping  << p;
        }

        if ( sg_filterServernameColorStrings )
            name << tColoredString::RemoveColors( server->GetName(), false );
	else if ( sg_filterServernameDarkColorStrings )
            name << tColoredString::RemoveColors( server->GetName(), true );
        else
        {
            name << server->GetName();
        }

        serverMenu->Render(y*shrink + displace,
                           name,
                           score, users, ping);
    }
    else
    {
        tOutput o("$network_master_noserver");
        tString s;
        s << o;
        serverMenu->Render(y*shrink + displace,
                           s,
                           tString(""), tString(""), tString(""));

    }
#endif
}

static REAL sg_menuBottom    = -.9;
static REAL sg_requestBottom = -.9;

void gServerMenuItem::RenderBackground()
{
#ifndef DEDICATED
    REAL helpTopReal = sg_requestBottom*shrink + displace - .05;;

    gBrowserMenuItem::RenderBackground();

    rTextField::SetDefaultColor( tColor(1,1,1) );

    rTextField players( -.9, helpTopReal, text_height, sr_fontServerDetails );
    players.EnableLineWrap();
    if ( server )
    {
        players << tOutput( "$network_master_players" );
        if ( server->UserNamesOneLine().Len() > 2 )
            players << server->UserNamesOneLine();
        else
            players << tOutput( "$network_master_players_empty" );
        players << "\n" << tColoredString::ColorString(1,1,1);
        tColoredString uri;
        uri << server->Url() << tColoredString::ColorString(1,1,1);
        tColoredString options;
        options << server->GetClassification().description_;
        options << server->Options();
        players << tOutput( "$network_master_serverinfo", server->Release(), uri, options );
    }

    {
        players << "\n";
        players.SetColor(tColor(1,1,1,displayHelp_ ? helpAlpha_ : 0));
        players << Help();
    }

    REAL helpSpace = players.GetTop() - players.GetBottom();
    REAL helpTop = -.85 + helpSpace;
    REAL helpTopScaled = ( helpTop - displace )/shrink;
    REAL helpTopMax = .25;
    REAL helpTopMin = -.9;
    if( helpTopScaled > helpTopMax )
    {
        helpTopScaled = helpTopMax;
    }
    if( helpTopScaled < helpTopMin )
    {
        helpTopScaled = helpTopMin;
    }
    sg_requestBottom = helpTopScaled;
#endif
}

#ifndef DEDICATED
static void Refresh()
{
    continuePoll = true;
    nServerInfo::StartQueryAll( sg_queryType );
}
#endif

bool gBrowserMenuItem::Event( SDL_Event& event )
{
#ifndef DEDICATED
    switch (event.type)
    {
    case SDL_KEYDOWN:
        switch (event.key.keysym.sym)
        {
        case SDLK_r:
            {
                static double lastRefresh = - 100; //!< the time of the last manual refresh
                if ( tSysTimeFloat() - lastRefresh > 2.0 )
                {
                    lastRefresh = tSysTimeFloat();
                    // trigger refresh
                    st_ToDo( Refresh );
                    return true;
                }
            }
            break;
        default:
            break;
        }
    }
#endif

    return uMenuItem::Event( event );
}

bool gServerMenuItem::Event( SDL_Event& event )
{
#ifndef DEDICATED
    switch (event.type)
    {
    case SDL_KEYDOWN:
        switch (event.key.keysym.sym)
        {
        case SDLK_p:
            continuePoll = true;
            if ( server && tSysTimeFloat() - lastPing_ > .5f )
            {
                lastPing_ = tSysTimeFloat();

                server->SetQueryType( nServerInfo::QUERY_ALL );
                server->QueryServer();
                server->ClearInfoFlags();
            }
            return true;
            break;
        default:
            break;
        }
        switch (event.key.keysym.sym)
        {
        case SDLK_KP_PLUS:
        case SDLK_PLUS:
            if ( server )
            {
                server->SetScoreBias( server->GetScoreBias() + 10 );
                server->CalcScore();
            }
            (static_cast<gServerMenu*>(menu))->Update();

            return true;
            break;
        case SDLK_KP_MINUS:
        case SDLK_MINUS:
            if ( server )
            {
                server->SetScoreBias( server->GetScoreBias() - 10 );
                server->CalcScore();
            }
            (static_cast<gServerMenu*>(menu))->Update();

            return true;
            break;
        case SDLK_b:
            if ( server )
            {
                if (favorite_ ) {
                    gServerFavorites::RemoveFavorite( server );
                    favorite_ = false;
                } else {
                    favorite_ = gServerFavorites::AddFavorite( server );
                }
            }
            (static_cast<gServerMenu*>(menu))->Update();

            return true;
            break;    
        default:
            break;
        }
    }
#endif

    return gBrowserMenuItem::Event( event );
}

void gBrowserMenuItem::RenderBackground()
{
    if( menu )
    {
        double now = tSysTimeFloat();
        static double lastTime = now;
        if( sg_menuBottom > sg_requestBottom )
        {
            sg_menuBottom -= now - lastTime;
        }
        lastTime = now;
        if( sg_menuBottom < sg_requestBottom )
        {
            sg_menuBottom = sg_requestBottom;
        }

        menu->SetBot( sg_menuBottom );
        sg_requestBottom = -.9;
    }

    sn_Receive();
    sn_SendPlanned();

    menu->GenericBackground();
    if (continuePoll)
    {
        continuePoll = nServerInfo::DoQueryAll(sg_simultaneous);
        sn_Receive();
        sn_SendPlanned();
    }

#ifndef DEDICATED
    rTextField::SetDefaultColor( tColor(.8,.3,.3,1) );

    tString sn2 = tString(tOutput("$network_master_servername"));
    if (getFriendsEnabled()) //display that friends filter is on
        sn2 << " - " << tOutput("$friends_enable");

    static_cast<gServerMenu*>(menu)->Render(.62,
                                            sn2,
                                            tOutput("$network_master_score"),
                                            tOutput("$network_master_users"),
                                            tOutput("$network_master_ping"));
#endif
}

void gServerMenuItem::Enter()
{
    nServerInfo::GetFromLANContinuouslyStop();

    menu->Exit();

    //  gLogo::SetBig(false);
    //  gLogo::SetSpinning(true);
    // gLogo::SetDisplayed(false);

    if (server)
        ConnectToServer(server);
}

void gServerMenuItem::Select()
{
    // reset help display state
    this->displayHelp_ = false;
    this->helpAlpha_ = 0.0f;
}

void gServerMenuItem::SetServer(nServerInfo *s)
{
    if (s == server)
        return;

    if (server)
        server->menuItem = NULL;

    server = dynamic_cast<gServerInfo*>(s);

    if (server)
    {
        if (server->menuItem)
            server->menuItem->SetServer(NULL);

        server->menuItem = this;
    }

    favorite_ = gServerFavorites::IsFavorite( server );
}

gServerInfo *gServerMenuItem::GetServer()
{
    return server;
}

static char const * sg_HelpText = "$network_master_browserhelp";

gServerMenuItem::gServerMenuItem(gServerMenu *men)
        :gBrowserMenuItem(men, sg_HelpText), server(NULL), lastPing_(-100), favorite_(false)
{}

gServerMenuItem::~gServerMenuItem()
{
    SetServer(NULL);

    // make sure the last entry in the array (the first menuitem)
    // stays the same
    uMenuItem* last = menu->Item(menu->NumItems()-1);
    menu->RemoveItem(last);
    menu->RemoveItem(this);
    menu->AddItem(last);
}


gServerInfo::~gServerInfo()
{
    if (menuItem)
        delete menuItem;
}


void gServerStartMenuItem::Render(REAL x,REAL y,REAL alpha, bool selected)
{
#ifndef DEDICATED
    // REAL time=tSysTimeFloat()*10;

    SetColor( selected, alpha );

    tString s;
    s << tOutput("$network_master_start");
    static_cast<gServerMenu*>(menu)->Render(y*shrink + displace,
                                            s,
                                            tString(), tString(), tString());
#endif
}



void gServerStartMenuItem::Enter()
{
    nServerInfo::GetFromLANContinuouslyStop();

    menu->Exit();

    //  gLogo::SetBig(false);
    //  gLogo::SetSpinning(true);
    // gLogo::SetDisplayed(false);

    sg_HostGameMenu();
}


void gServerFilterMenuItem::Render(REAL x,REAL y,REAL alpha, bool selected)
{
#ifndef DEDICATED
    SetColor( selected, alpha );
    
    tString s;
    s << description;
    
    x = -.9f;
    REAL x2 = s.Len() * 0.018 + x;
    
    int cMode = selected ? 1 : 0;
    
    static_cast<gServerMenu*>(menu)->Render(x, y*shrink + displace, s);
    static_cast<gServerMenu*>(menu)->Render(x2, y*shrink + displace, *content, 1, cMode, realCursorPos);
#endif
}

bool gServerFilterMenuItem::Event( SDL_Event& event )
{
#ifndef DEDICATED
    auto prev_filter_string = *content; // store current content for later comparison
    bool update = false; // do we need to update the server list?
    bool ret = false; // have we handled the event?

    if (event.type==SDL_KEYDOWN)
    {
        switch (event.key.keysym.sym)
        {
        case(SDLK_ESCAPE):
            // escape clears the filter
            if(!content->empty())
            {
                *content = "";

                update = true;
                ret = true;

                break;
            }

            // fall through
        default:
            // let base handle it
            ret = uMenuItemString::Event( event );

            break;
        }
    }
    else
    {
        // let base handle it
        ret = uMenuItemString::Event( event );
    }

    // update on change
    if(prev_filter_string != *content)
    {
        update = true;
    }

    if(update)
    {
        // update menu, filter has changed
        (static_cast<gServerMenu*>(menu))->Update();
    }

    return ret;
#else
    return false;
#endif
}

gServerStartMenuItem::gServerStartMenuItem(gServerMenu *men)
        :gBrowserMenuItem(men, *sg_StartHelpText)
{}

gServerStartMenuItem::~gServerStartMenuItem()
{
}

// *******************************************************************************
// *
// *   Server list screen: full screen server browser with sidebar filters,
// *   a server table and a details panel.
// *
// *******************************************************************************

#ifndef DEDICATED
#include "ePlayer.h"
#include "gMenus.h"
#include "rFont.h"
#include <algorithm>
#include <vector>

namespace
{
// palette
struct gSLColor { REAL r, g, b, a; };
const gSLColor sl_bg      = { .051, .067, .090, 1 };
const gSLColor sl_panel   = { .067, .086, .110, 1 };
const gSLColor sl_border  = { .130, .157, .192, 1 };
const gSLColor sl_rowSel  = { .102, .125, .157, 1 };
const gSLColor sl_rowHov  = { .082, .102, .129, 1 };
const gSLColor sl_textHi  = { .902, .929, .953, 1 };
const gSLColor sl_textMid = { .545, .580, .620, 1 };
const gSLColor sl_textLow = { .353, .388, .431, 1 };
const gSLColor sl_accent  = { .302, .847, .910, 1 };
const gSLColor sl_orange  = { .941, .627, .188, 1 };

const REAL sl_small = .040, sl_body = .046, sl_big = .075;

void sl_Rect( REAL l, REAL b, REAL r, REAL t, gSLColor const & c )
{
    glDisable( GL_TEXTURE_2D );
    Color( c.r, c.g, c.b, c.a );
    BeginQuads();
    Vertex( l, b ); Vertex( r, b ); Vertex( r, t ); Vertex( l, t );
    RenderEnd();
}

void sl_Frame( REAL l, REAL b, REAL r, REAL t, gSLColor const & c, REAL width = 1 )
{
    glDisable( GL_TEXTURE_2D );
    glLineWidth( width );
    Color( c.r, c.g, c.b, c.a );
    BeginLineLoop();
    Vertex( l, b ); Vertex( r, b ); Vertex( r, t ); Vertex( l, t );
    RenderEnd();
    glLineWidth( 1 );
}

void sl_Line( REAL x1, REAL y1, REAL x2, REAL y2, gSLColor const & c, REAL width = 1 )
{
    glDisable( GL_TEXTURE_2D );
    glLineWidth( width );
    Color( c.r, c.g, c.b, c.a );
    BeginLines();
    Vertex( x1, y1 ); Vertex( x2, y2 );
    RenderEnd();
    glLineWidth( 1 );
}

// text is never drawn smaller than 18 pixels, measure with the real height
REAL sl_RealHeight( REAL h )
{
    return sr_screenHeight > 0 ? std::max( h, REAL( 18.0 / sr_screenHeight ) ) : h;
}

REAL sl_TextWidth( tString const & s, REAL h )
{
    return rTextField::GetTextLength( s, sl_RealHeight( h ), true );
}

//! shortens the text with an ellipsis until it fits
tString sl_Fit( tString const & s, REAL h, REAL maxWidth )
{
    if ( maxWidth <= 0 || sl_TextWidth( s, h ) <= maxWidth )
        return s;
    std::string t = s;
    while ( !t.empty() )
    {
        // drop one UTF-8 character
        size_t cut = t.size() - 1;
        while ( cut > 0 && ( static_cast<unsigned char>( t[cut] ) & 0xC0 ) == 0x80 )
            --cut;
        t.erase( cut );
        tString candidate( t + "\xe2\x80\xa6" );
        if ( sl_TextWidth( candidate, h ) <= maxWidth )
            return candidate;
    }
    return tString();
}

//! align: -1 left at x, 0 centered on x, 1 right at x
void sl_Text( REAL x, REAL y, REAL h, tString const & s, gSLColor const & c, int align = -1, REAL maxWidth = 0 )
{
    // the color goes through the blend color: dark default colors get a backdrop
    rTextField::SetDefaultColor( tColor( 1, 1, 1, 1 ) );
    rTextField::SetBlendColor( tColor( c.r, c.g, c.b, c.a ) );
    tString t = sl_Fit( s, h, maxWidth );
    DisplayText( x, y, h, t.c_str(), sr_fontServerBrowser, align );
    rTextField::SetBlendColor( tColor( 1, 1, 1, 1 ) );
}

//! letter spaced, upper case label
tString sl_Spaced( tString const & s )
{
    tString r;
    for ( size_t i = 0; i < s.size(); ++i )
    {
        if ( i > 0 )
            r << ' ';
        r << s[i];
    }
    return r;
}

tString sl_Plain( tString const & s )
{
    return tColoredString::RemoveColors( s, false );
}

// game modes, guessed from the server name
enum gSLMode { MODE_FORT, MODE_SUMO, MODE_OTHER };

bool sl_Contains( tString const & lower, char const * what )
{
    return lower.find( what ) != std::string::npos;
}

tString sl_ModeTag( nServerInfo const & s )
{
    tString n = sl_Plain( s.GetName() ).ToLower();
    if ( sl_Contains( n, "fort" ) )
        return tString( "FORT" );
    if ( sl_Contains( n, "sumo" ) )
        return tString( "SUMO" );
    if ( sl_Contains( n, "ctf" ) || sl_Contains( n, "capture the flag" ) )
        return tString( "CTF" );
    if ( sl_Contains( n, "tdm" ) || sl_Contains( n, "team deathmatch" ) )
        return tString( "TDM" );
    if ( sl_Contains( n, "race" ) || sl_Contains( n, "racing" ) )
        return tString( "RACE" );
    if ( sl_Contains( n, "ffa" ) || sl_Contains( n, "free for all" ) || sl_Contains( n, "free-for-all" ) )
        return tString( "FFA" );
    if ( sl_Contains( n, "dogfight" ) )
        return tString( "DF" );
    if ( s.GetSettings().GetFlag( nServerInfo::SettingsDigest::Flags_TeamPlay ) )
        return tString( "TEAM" );
    return tString( "\xe2\x80\x93" );
}

gSLMode sl_Mode( nServerInfo const & s )
{
    tString tag = sl_ModeTag( s );
    if ( tag == "FORT" )
        return MODE_FORT;
    if ( tag == "SUMO" )
        return MODE_SUMO;
    return MODE_OTHER;
}

//! ranked servers are the ones hosted by the Retrocycles League (RCL)
bool sl_Ranked( nServerInfo const & s )
{
    tString n = sl_Plain( s.GetName() );
    if ( sl_Contains( n.ToLower(), "retrocycles league" ) )
        return true;
    for ( size_t pos = n.find( "RCL" ); pos != std::string::npos; pos = n.find( "RCL", pos + 1 ) )
    {
        bool letterBefore = pos > 0 && isalpha( static_cast<unsigned char>( n[pos-1] ) );
        bool letterAfter = pos + 3 < n.size() && isalpha( static_cast<unsigned char>( n[pos+3] ) );
        if ( !letterBefore && !letterAfter )
            return true;
    }
    return false;
}

//! can we connect at all? (full servers still count, to spectate)
bool sl_Joinable( nServerInfo const & s )
{
    return !s.Polling() && s.Reachable() && s.Compatibility() == nServerInfo::Compat_Ok;
}

const REAL sl_nearPing = .1; // seconds; servers below count as near

std::vector< tString > sl_PlayerNames( nServerInfo const & s )
{
    std::vector< tString > names;
    std::string all = s.UserNames();
    size_t start = 0;
    while ( start < all.size() )
    {
        size_t end = all.find( '\n', start );
        if ( end == std::string::npos )
            end = all.size();
        tString name = sl_Plain( tString( all.substr( start, end - start ) ) );
        if ( name.size() > 0 )
            names.push_back( name );
        start = end + 1;
    }
    return names;
}

// on-screen actions
enum gSLAction
{
    ACT_PROFILE, ACT_SEARCH, ACT_MODE, ACT_REGION, ACT_FILTER, ACT_REFRESH,
    ACT_SORT, ACT_ROW, ACT_JOIN, ACT_SPECTATE
};

struct gSLHit
{
    REAL l, b, r, t;
    gSLAction action;
    int param;
};

class gServerListScreen;

//! the single (invisible) item of the screen; keeps the network going
class gServerListItem: public uMenuItem
{
public:
    explicit gServerListItem( uMenu * menu ): uMenuItem( menu, "" ){}
    bool DisplayHelp( bool, REAL, REAL ) override { return false; }
    void RenderBackground() override;
};

class gServerListScreen: public uMenu
{
public:
    gServerListScreen(): uMenu( "", false ), item_( this ) {}

    void OnRender() override
    {
        uMenu::OnRender();

        // resort regularly, new answers arrive all the time
        if ( resortTime_ < tSysTimeFloat() || resort_ )
        {
            nServerInfo::CalcScoreAll();
            nServerInfo::Sort( sortKey_ );
            resortTime_ = tSysTimeFloat() + 2;
            resort_ = false;
        }

        BuildList();
    }

protected:
    int ItemAt( int, int ) override { return -1; }

    void RenderItems() override
    {
        hits_.clear();
        REAL const top = .88, bottom = -.93, sideRight = -.62, detailLeft = .46;

        // frame: top bar, bottom bar, sidebar, details panel
        sl_Rect( -1, -1, 1, 1, sl_bg );
        sl_Rect( -1, bottom, sideRight, top, sl_panel );
        sl_Rect( detailLeft, bottom, 1, top, sl_panel );
        sl_Line( -1, top, 1, top, sl_border );
        sl_Line( -1, bottom, 1, bottom, sl_border );
        sl_Line( sideRight, bottom, sideRight, top, sl_border );
        sl_Line( detailLeft, bottom, detailLeft, top, sl_border );

        RenderTopBar( top );
        RenderSidebar( sideRight, top, bottom );
        RenderList( sideRight, detailLeft, top, bottom );
        RenderDetails( detailLeft, top, bottom );
        RenderBottomBar( bottom );
    }

    void HandleEvent( SDL_Event event ) override
    {
        switch ( event.type )
        {
        case SDL_MOUSEMOTION:
            MouseToMenu( event.motion.x, event.motion.y );
            return;
        case SDL_MOUSEBUTTONDOWN:
            MouseToMenu( event.button.x, event.button.y );
            if ( event.button.button == SDL_BUTTON_LEFT )
                Click();
            else if ( event.button.button == SDL_BUTTON_RIGHT )
                Exit();
#if !SDL_VERSION_ATLEAST(2,0,0)
            else if ( event.button.button == SDL_BUTTON_WHEELUP )
                Scroll( -3 );
            else if ( event.button.button == SDL_BUTTON_WHEELDOWN )
                Scroll( 3 );
#endif
            return;
#if SDL_VERSION_ATLEAST(2,0,0)
        case SDL_MOUSEWHEEL:
            Scroll( event.wheel.y > 0 ? -3 : 3 );
            return;
        case SDL_TEXTINPUT:
            if ( searching_ )
            {
                tString text( event.text.text );
                if ( ignoreSlash_ && text == "/" )
                    ignoreSlash_ = false;
                else
                    search_ << text;
            }
            return;
#endif
        case SDL_KEYDOWN:
            if ( HandleKey( event ) )
                return;
            break;
        default:
            break;
        }
        uMenu::HandleEvent( event );
    }

private:
    gServerListItem item_;

    // view state
    nServerInfo::PrimaryKey sortKey_ = nServerInfo::KEY_PING;
    bool resort_ = true;
    double resortTime_ = 0;
    int mode_ = -1;                 // -1: all, else gSLMode
    int region_ = 2;                // 0 near, 1 far, 2 all
    bool hideEmpty_ = false, hideFull_ = false, rankedOnly_ = false;
    tString search_;
    bool searching_ = false, ignoreSlash_ = false;
    int scroll_ = 0;
    nServerInfo * selected_ = nullptr;
    double lastClickTime_ = 0;
    nServerInfo * lastClicked_ = nullptr;

    // per frame data
    std::vector< nServerInfo * > list_;
    int modeCount_[4] = { 0, 0, 0, 0 };    // all, fort, sumo, others
    int totalServers_ = 0, totalPlayers_ = 0;
    std::vector< gSLHit > hits_;
    REAL mouseX_ = -10, mouseY_ = -10;
    int visibleRows_ = 10;

    bool Hover( REAL l, REAL b, REAL r, REAL t ) const
    {
        return mouseX_ >= l && mouseX_ <= r && mouseY_ >= b && mouseY_ <= t;
    }

    void AddHit( REAL l, REAL b, REAL r, REAL t, gSLAction action, int param = 0 )
    {
        hits_.push_back( gSLHit{ l, b, r, t, action, param } );
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
            mouseX_ = 2 * REAL(x) / width - 1;
            mouseY_ = 1 - 2 * REAL(y) / height;
        }
    }

    // **************** filtering ****************

    bool PassesFilters( nServerInfo const & s, bool checkMode ) const
    {
        if ( checkMode && mode_ >= 0 && sl_Mode( s ) != mode_ )
            return false;
        if ( region_ == 0 && !( s.Ping() < sl_nearPing ) )
            return false;
        if ( region_ == 1 && s.Ping() < sl_nearPing )
            return false;
        if ( hideEmpty_ && s.Users() <= 0 )
            return false;
        if ( hideFull_ && s.MaxUsers() > 0 && s.Users() >= s.MaxUsers() )
            return false;
        if ( rankedOnly_ && !sl_Ranked( s ) )
            return false;
        if ( search_.size() > 0 )
        {
            tString needle = search_.ToLower();
            if ( !sl_Contains( sl_Plain( s.GetName() ).ToLower(), needle.c_str() ) &&
                 !sl_Contains( sl_Plain( s.UserNames() ).ToLower(), needle.c_str() ) )
                return false;
        }
        return true;
    }

    void BuildList()
    {
        list_.clear();
        for ( int & c: modeCount_ )
            c = 0;
        totalServers_ = totalPlayers_ = 0;
        bool selectedFound = false;
        for ( nServerInfo * s = nServerInfo::GetFirstServer(); s; s = s->Next() )
        {
            ++totalServers_;
            if ( s->Reachable() && !s->Polling() )
                totalPlayers_ += std::max( 0, s->Users() );
            if ( PassesFilters( *s, false ) )
            {
                ++modeCount_[0];
                ++modeCount_[ 1 + sl_Mode( *s ) ];
            }
            if ( PassesFilters( *s, true ) )
            {
                list_.push_back( s );
                if ( s == selected_ )
                    selectedFound = true;
            }
        }
        if ( !selectedFound )
            selected_ = list_.empty() ? nullptr : list_[0];
        if ( lastClicked_ && std::find( list_.begin(), list_.end(), lastClicked_ ) == list_.end() )
            lastClicked_ = nullptr;
    }

    int SelectedIndex() const
    {
        for ( size_t i = 0; i < list_.size(); ++i )
            if ( list_[i] == selected_ )
                return int(i);
        return -1;
    }

    void Scroll( int rows )
    {
        scroll_ += rows;
        ClampScroll();
    }

    void ClampScroll()
    {
        int maxScroll = std::max( 0, int( list_.size() ) - visibleRows_ );
        scroll_ = std::max( 0, std::min( scroll_, maxScroll ) );
    }

    void MoveSelection( int delta )
    {
        if ( list_.empty() )
            return;
        int i = std::max( 0, std::min( int( list_.size() ) - 1, SelectedIndex() + delta ) );
        selected_ = list_[i];
        if ( i < scroll_ )
            scroll_ = i;
        if ( i >= scroll_ + visibleRows_ )
            scroll_ = i - visibleRows_ + 1;
        ClampScroll();
    }

    // **************** actions ****************

    void Click()
    {
        // later hits are drawn on top
        for ( auto it = hits_.rbegin(); it != hits_.rend(); ++it )
        {
            if ( Hover( it->l, it->b, it->r, it->t ) )
            {
                Do( it->action, it->param );
                return;
            }
        }
        searching_ = false;
    }

    void Do( gSLAction action, int param )
    {
        if ( action != ACT_SEARCH )
            searching_ = false;
        switch ( action )
        {
        case ACT_PROFILE:
            sg_PlayerMenu( 0 );
            break;
        case ACT_SEARCH:
            searching_ = true;
            break;
        case ACT_MODE:
            mode_ = param;
            scroll_ = 0;
            break;
        case ACT_REGION:
            region_ = param;
            scroll_ = 0;
            break;
        case ACT_FILTER:
            if ( param == 0 ) hideEmpty_ = !hideEmpty_;
            if ( param == 1 ) hideFull_ = !hideFull_;
            if ( param == 2 ) rankedOnly_ = !rankedOnly_;
            scroll_ = 0;
            break;
        case ACT_REFRESH:
            Refresh();
            break;
        case ACT_SORT:
            sortKey_ = nServerInfo::PrimaryKey( param );
            resort_ = true;
            break;
        case ACT_ROW:
            if ( param >= 0 && param < int( list_.size() ) )
            {
                nServerInfo * s = list_[param];
                double now = tSysTimeFloat();
                bool doubleClick = s == lastClicked_ && now - lastClickTime_ < .4;
                selected_ = lastClicked_ = s;
                lastClickTime_ = now;
                if ( doubleClick )
                    Join( false );
            }
            break;
        case ACT_JOIN:
            Join( false );
            break;
        case ACT_SPECTATE:
            Join( true );
            break;
        }
    }

    void Refresh()
    {
        // ask every listed server again for its players and ping
        if ( nServerInfoAdmin::GetAdmin() )
            nServerInfoAdmin::GetAdmin()->BeforeNewScan();
        nServerInfo::StartQueryAll( sg_queryType );
        continuePoll = true;
        resort_ = true;
    }

    void Join( bool spectate )
    {
        nServerInfo * server = selected_;
        if ( !server || !sl_Joinable( *server ) )
            return;

        nServerInfo::GetFromLANContinuouslyStop();
        Exit();

        // spectating: all local players watch for this connection only
        std::vector< bool > spectating;
        for ( int i = 0; i < MAX_PLAYERS; ++i )
        {
            ePlayer * p = ePlayer::PlayerConfig( i );
            spectating.push_back( p ? p->spectate : false );
            if ( p && spectate )
                p->spectate = true;
        }
        struct Restore
        {
            std::vector< bool > const & flags;
            ~Restore()
            {
                for ( int i = 0; i < MAX_PLAYERS; ++i )
                    if ( ePlayer * p = ePlayer::PlayerConfig( i ) )
                        p->spectate = flags[i];
            }
        } restore{ spectating };

        ConnectToServer( server );
    }

    bool HandleKey( SDL_Event const & event )
    {
        SDL_Keycode key = event.key.keysym.sym;
        if ( searching_ )
        {
            switch ( key )
            {
            case SDLK_BACKSPACE:
                if ( search_.size() > 0 )
                {
                    // remove one UTF-8 character
                    size_t cut = search_.size() - 1;
                    while ( cut > 0 && ( static_cast<unsigned char>( search_[cut] ) & 0xC0 ) == 0x80 )
                        --cut;
                    search_.erase( cut );
                }
                return true;
            case SDLK_ESCAPE:
                search_.clear();
                searching_ = false;
                return true;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                searching_ = false;
                return true;
            case SDLK_UP:
            case SDLK_DOWN:
                break;
            default:
                return true; // the text arrives as text input
            }
        }

        switch ( key )
        {
        case SDLK_UP:       MoveSelection( -1 ); return true;
        case SDLK_DOWN:     MoveSelection( 1 ); return true;
        case SDLK_PAGEUP:   MoveSelection( -visibleRows_ ); return true;
        case SDLK_PAGEDOWN: MoveSelection( visibleRows_ ); return true;
        case SDLK_RETURN:
        case SDLK_KP_ENTER: Join( false ); return true;
        case SDLK_s:        Join( true ); return true;
        case SDLK_r:        Refresh(); return true;
        case SDLK_SLASH:
            searching_ = true;
            ignoreSlash_ = true;
            return true;
        case SDLK_ESCAPE:
            Exit();
            return true;
        default:
            return false;
        }
    }

    // **************** drawing ****************

    void RenderTopBar( REAL top )
    {
        REAL const y = ( 1 + top ) * .5;

        // logo
        sl_Line( -.955, y - .025, -.955, y + .02, sl_accent, 2 );
        sl_Line( -.955, y + .02, -.93, y + .02, sl_accent, 2 );
        sl_Line( -.94, y - .025, -.94, y + .005, sl_accent, 2 );
        tString title;
        title << tOutput( "$game_name" );
        REAL x = -.55; // tabs
        sl_Text( -.91, y, sl_body, sl_Spaced( title.ToUpper() ), sl_textHi, -1, x - .05 + .91 );

        sl_Text( x, y, sl_body, tString( "Servers" ), sl_textHi );
        REAL w = sl_TextWidth( tString( "Servers" ), sl_body );
        sl_Line( x - .01, top + .003, x + w + .01, top + .003, sl_accent, 2 );
        x += w + .07;
        REAL pw = sl_TextWidth( tString( "Profile" ), sl_body );
        bool hov = Hover( x - .02, top, x + pw + .02, 1 );
        sl_Text( x, y, sl_body, tString( "Profile" ), hov ? sl_textHi : sl_textMid );
        AddHit( x - .02, top, x + pw + .02, 1, ACT_PROFILE );

        // search box
        REAL const sl = .24, sr = .72, sb = y - .035, st = y + .035;
        sl_Rect( sl, sb, sr, st, sl_panel );
        sl_Frame( sl, sb, sr, st, searching_ ? sl_accent : sl_border );
        if ( search_.size() > 0 || searching_ )
        {
            tString shown = search_;
            if ( searching_ && int( tSysTimeFloat() * 2 ) % 2 == 0 )
                shown << "|";
            sl_Text( sl + .02, y, sl_small, shown, sl_textHi, -1, sr - sl - .04 );
        }
        else
            sl_Text( sl + .02, y, sl_small, tString( "Search servers, players\xe2\x80\xa6" ), sl_textLow, -1, sr - sl - .04 );
        AddHit( sl, sb, sr, st, ACT_SEARCH );

        // local player
        tString name;
        if ( ePlayer * p = ePlayer::PlayerConfig( 0 ) )
            name = sl_Plain( p->name );
        sl_Rect( .77, y - .008, .782, y + .008, sl_accent );
        bool hovName = Hover( .76, top, 1, 1 );
        sl_Text( .80, y, sl_small, name, hovName ? sl_textHi : sl_textMid, -1, .18 );
        AddHit( .76, top, 1, 1, ACT_PROFILE );
    }

    void RenderSidebar( REAL right, REAL top, REAL bottom )
    {
        REAL const l = -.96, r = right - .04;
        REAL y = top - .09;

        // mode
        sl_Text( l, y, sl_small, sl_Spaced( tString( "MODE" ) ), sl_textLow );
        y -= .075;
        char const * modes[] = { "All", "Fortress", "Sumo", "Others" };
        for ( int i = 0; i < 4; ++i )
        {
            REAL b = y - .035, t = y + .035;
            bool sel = mode_ == i - 1;
            if ( sel )
            {
                sl_Rect( l - .01, b, r, t, sl_rowSel );
                sl_Line( l - .01, b, l - .01, t, sl_accent, 2 );
            }
            else if ( Hover( l - .01, b, r, t ) )
                sl_Rect( l - .01, b, r, t, sl_rowHov );
            sl_Text( l + .01, y, sl_body, tString( modes[i] ), sel ? sl_textHi : sl_textMid );
            tString count;
            count << modeCount_[i];
            sl_Text( r - .01, y, sl_small, count, sl_textLow, 1 );
            AddHit( l - .01, b, r, t, ACT_MODE, i - 1 );
            y -= .075;
        }

        // region
        y -= .04;
        sl_Text( l, y, sl_small, sl_Spaced( tString( "REGION" ) ), sl_textLow );
        y -= .08;
        char const * regions[] = { "NEAR", "FAR", "ALL" };
        REAL const bw = ( r - l - .02 ) / 3;
        for ( int i = 0; i < 3; ++i )
        {
            REAL bl = l + i * ( bw + .01 ), br = bl + bw, b = y - .035, t = y + .035;
            bool sel = region_ == i;
            sl_Rect( bl, b, br, t, sel ? sl_rowSel : ( Hover( bl, b, br, t ) ? sl_rowHov : sl_panel ) );
            sl_Frame( bl, b, br, t, sel ? sl_textLow : sl_border );
            sl_Text( ( bl + br ) * .5, y, sl_small, tString( regions[i] ), sel ? sl_textHi : sl_textMid, 0 );
            AddHit( bl, b, br, t, ACT_REGION, i );
        }

        // filters
        y -= .12;
        sl_Text( l, y, sl_small, sl_Spaced( tString( "FILTER" ) ), sl_textLow );
        y -= .075;
        char const * filters[] = { "Hide empty", "Hide full", "Ranked only" };
        bool const states[] = { hideEmpty_, hideFull_, rankedOnly_ };
        for ( int i = 0; i < 3; ++i )
        {
            // square check box, whatever the screen shape
            REAL const box = .012;
            REAL const boxY = sr_screenHeight > 0 ? box * sr_screenWidth / sr_screenHeight : box;
            REAL const bx = l + box;
            if ( states[i] )
            {
                sl_Rect( bx - box, y - boxY, bx + box, y + boxY, sl_accent );
                sl_Line( bx - box * .55, y, bx - box * .1, y - boxY * .55, sl_bg, 2 );
                sl_Line( bx - box * .1, y - boxY * .55, bx + box * .6, y + boxY * .6, sl_bg, 2 );
            }
            else
                sl_Frame( bx - box, y - boxY, bx + box, y + boxY, sl_textLow );
            bool hov = Hover( l - .01, y - .035, r, y + .035 );
            sl_Text( l + box * 2 + .025, y, sl_body, tString( filters[i] ), hov ? sl_textHi : sl_textMid );
            AddHit( l - .01, y - .035, r, y + .035, ACT_FILTER, i );
            y -= .07;
        }

        // totals
        tString players, servers;
        players << totalPlayers_ << " players online";
        servers << totalServers_ << " servers listed";
        sl_Text( l, bottom + .11, sl_small, players, sl_textLow );
        sl_Text( l, bottom + .055, sl_small, servers, sl_textLow );
    }

    void RenderList( REAL left, REAL right, REAL top, REAL bottom )
    {
        REAL const l = left + .05, r = right - .04;
        REAL y = top - .09;

        // title and subtitle
        sl_Text( l, y, sl_big, tString( "Servers" ), sl_textHi );
        tString sub;
        char const * modeNames[] = { "All", "Fortress", "Sumo", "Others" };
        char const * regionNames[] = { "Near", "Far", "All regions" };
        char const * sortNames[] = { "name", "ping", "players", "score" };
        sub << modeNames[ mode_ + 1 ] << " \xc2\xb7 " << regionNames[ region_ ] << " \xc2\xb7 sorted by " << sortNames[ sortKey_ ];
        REAL tw = sl_TextWidth( tString( "Servers" ), sl_big );
        sl_Text( l + tw + .04, y - .005, sl_small, sub, sl_textLow, -1, r - l - tw - .32 );

        // refresh button
        REAL const rl = r - .22, rb = y - .04, rt = y + .04;
        bool hovRefresh = Hover( rl, rb, r, rt );
        sl_Rect( rl, rb, r, rt, hovRefresh ? sl_rowSel : sl_bg );
        sl_Frame( rl, rb, r, rt, sl_border );
        sl_Text( ( rl + r ) * .5, y, sl_small, tString( "\xe2\x86\xbb Refresh" ), sl_textHi, 0 );
        AddHit( rl, rb, r, rt, ACT_REFRESH );

        // columns
        REAL const xName = l + .04, xMode = l + .60, xPlayers = l + .72, xBar = l + .82, barW = .09, xPing = r - .01;
        y -= .11;
        struct Column { char const * label; REAL x; int align; int key; };
        Column const columns[] = {
            { "SERVER", xName, -1, nServerInfo::KEY_NAME },
            { "MODE", xMode, -1, -1 },
            { "PLAYERS", xPlayers, -1, nServerInfo::KEY_USERS },
            { "PING", xPing, 1, nServerInfo::KEY_PING } };
        for ( auto const & c: columns )
        {
            tString label = sl_Spaced( tString( c.label ) );
            REAL w = sl_TextWidth( label, sl_small );
            REAL cl = c.align < 0 ? c.x : c.x - w;
            bool active = c.key == sortKey_;
            bool hov = c.key >= 0 && Hover( cl - .01, y - .03, cl + w + .01, y + .03 );
            sl_Text( c.x, y, sl_small, label, active || hov ? sl_textMid : sl_textLow, c.align );
            if ( c.key >= 0 )
                AddHit( cl - .01, y - .03, cl + w + .01, y + .03, ACT_SORT, c.key );
        }
        y -= .045;
        sl_Line( l, y, r, y, sl_border );

        // rows
        REAL const rowH = .075;
        REAL const listTop = y - .01;
        visibleRows_ = std::max( 1, int( ( listTop - ( bottom + .02 ) ) / rowH ) );
        ClampScroll();
        if ( list_.empty() )
        {
            tString none = totalServers_ > 0 ? tString( "No servers match the filters." ) : tString( "No servers found." );
            sl_Text( ( l + r ) * .5, listTop - rowH, sl_body, none, sl_textLow, 0 );
        }
        for ( int row = 0; row < visibleRows_ && scroll_ + row < int( list_.size() ); ++row )
        {
            int index = scroll_ + row;
            nServerInfo & s = *list_[index];
            REAL const t = listTop - row * rowH, b = t - rowH + .008, cy = ( t + b ) * .5;
            bool sel = &s == selected_;
            if ( sel )
            {
                sl_Rect( l, b, r, t, sl_rowSel );
                sl_Frame( l, b, r, t, sl_border );
            }
            else if ( Hover( l, b, r, t ) )
                sl_Rect( l, b, r, t, sl_rowHov );
            AddHit( l, b, r, t, ACT_ROW, index );

            bool full = s.MaxUsers() > 0 && s.Users() >= s.MaxUsers();
            bool waiting = s.Polling() || !s.Reachable() || s.Compatibility() != nServerInfo::Compat_Ok;

            // status dot
            gSLColor const & dot = waiting ? sl_textLow : ( full ? sl_orange : sl_accent );
            sl_Rect( l + .012, cy - .008, l + .024, cy + .008, dot );

            // name and ranked badge
            tString name = sl_Plain( s.GetName() );
            bool ranked = sl_Ranked( s );
            tString badge = sl_Spaced( tString( "RANKED" ) );
            REAL bw = ranked ? sl_TextWidth( badge, sl_small * .8 ) + .03 : 0;
            REAL nameMax = xMode - xName - .03 - ( ranked ? bw + .02 : 0 );
            tString shown = sl_Fit( name, sl_body, nameMax );
            sl_Text( xName, cy, sl_body, shown, sl_textHi );
            if ( ranked )
            {
                REAL bl = xName + sl_TextWidth( shown, sl_body ) + .02;
                sl_Frame( bl, cy - .026, bl + bw, cy + .026, sl_accent );
                sl_Text( bl + bw * .5, cy, sl_small * .8, badge, sl_accent, 0 );
            }

            // mode
            sl_Text( xMode, cy, sl_small, sl_ModeTag( s ), sl_textMid );

            // players and fill bar
            if ( waiting )
            {
                tString state = s.Polling() ? tString( "polling" ) : ( s.Reachable() ? tString( "incompatible" ) : tString( "unreachable" ) );
                sl_Text( xPlayers, cy, sl_small, state, sl_textLow );
            }
            else
            {
                tString players;
                players << s.Users() << "/" << s.MaxUsers();
                sl_Text( xPlayers, cy, sl_small, players, sl_textHi );
                REAL fill = s.MaxUsers() > 0 ? std::min( 1.0f, REAL( s.Users() ) / s.MaxUsers() ) : 0;
                sl_Rect( xBar, cy - .005, xBar + barW, cy + .005, sl_border );
                sl_Rect( xBar, cy - .005, xBar + barW * fill, cy + .005, full ? sl_orange : sl_textMid );

                // ping
                int ping = int( s.Ping() * 1000 );
                tString pingText;
                pingText << ping;
                sl_Text( xPing, cy, sl_small, pingText, ping < 50 ? sl_accent : ( ping < 100 ? sl_textHi : sl_orange ), 1 );
            }
        }

        // scroll indicator
        if ( int( list_.size() ) > visibleRows_ )
        {
            REAL const trackT = listTop, trackB = bottom + .02;
            REAL const h = ( trackT - trackB ) * visibleRows_ / list_.size();
            REAL const t = trackT - ( trackT - trackB - h ) * scroll_ / std::max( 1, int( list_.size() ) - visibleRows_ );
            sl_Rect( r + .012, t - h, r + .02, t, sl_border );
        }
    }

    void RenderDetails( REAL left, REAL top, REAL bottom )
    {
        REAL const l = left + .04, r = .96;
        nServerInfo * s = selected_;
        if ( !s )
        {
            sl_Text( ( l + r ) * .5, ( top + bottom ) * .5, sl_body, tString( "Select a server" ), sl_textLow, 0 );
            return;
        }

        REAL y = top - .09;
        tString kicker;
        kicker << sl_ModeTag( *s ) << " \xc2\xb7 " << ( s->Ping() < sl_nearPing ? "NEAR" : "FAR" );
        sl_Text( l, y, sl_small, sl_Spaced( kicker ), sl_accent );

        // name, wrapped over at most two lines
        y -= .08;
        REAL const nameH = .062;
        std::string name = sl_Plain( s->GetName() );
        std::string first = name, rest;
        if ( sl_TextWidth( tString( name ), nameH ) > r - l )
        {
            size_t cut = name.size();
            while ( cut != std::string::npos && cut > 0 )
            {
                cut = name.rfind( ' ', cut - 1 );
                if ( cut != std::string::npos && sl_TextWidth( tString( name.substr( 0, cut ) ), nameH ) <= r - l )
                    break;
            }
            if ( cut != std::string::npos && cut > 0 )
            {
                first = name.substr( 0, cut );
                rest = name.substr( cut + 1 );
            }
        }
        sl_Text( l, y, nameH, tString( first ), sl_textHi, -1, r - l );
        if ( !rest.empty() )
        {
            y -= .075;
            sl_Text( l, y, nameH, tString( rest ), sl_textHi, -1, r - l );
        }
        y -= .065;
        tString address;
        address << s->GetConnectionName() << ":" << s->GetPort();
        sl_Text( l, y, sl_small, address, sl_textLow, -1, r - l );

        // stats
        y -= .07;
        REAL const sb = y - .13;
        sl_Frame( l, sb, r, y, sl_border );
        REAL const cw = ( r - l ) / 3;
        tString players, ping, teams;
        if ( s->Polling() || !s->Reachable() )
            players << "\xe2\x80\x93", ping << "\xe2\x80\x93";
        else
            players << s->Users() << "/" << s->MaxUsers(), ping << int( s->Ping() * 1000 );
        teams << ( s->GetSettings().GetFlag( nServerInfo::SettingsDigest::Flags_TeamPlay ) ? "yes" : "no" );
        char const * labels[] = { "PLAYERS", "PING", "TEAMS" };
        tString const * values[] = { &players, &ping, &teams };
        for ( int i = 0; i < 3; ++i )
        {
            REAL cl = l + i * cw;
            if ( i > 0 )
                sl_Line( cl, sb, cl, y, sl_border );
            sl_Text( cl + .02, y - .035, sl_small * .85, sl_Spaced( tString( labels[i] ) ), sl_textLow );
            sl_Text( cl + .02, y - .09, sl_body * 1.2, *values[i], sl_textHi, -1, cw - .03 );
        }
        y = sb - .07;

        // settings
        sl_Text( l, y, sl_small, sl_Spaced( tString( "SETTINGS" ) ), sl_textMid );
        y -= .06;
        nServerInfo::SettingsDigest const & settings = s->GetSettings();
        if ( !settings.GetFlag( nServerInfo::SettingsDigest::Flags_SettingsDigestSent ) )
        {
            sl_Text( l, y, sl_small, tString( "not reported by this server" ), sl_textLow );
            y -= .05;
        }
        else
        {
            auto number = []( REAL v ) { std::ostringstream o; o.precision( 2 ); o << std::fixed << v; return tString( o.str() ); };
            tString wall = settings.wallsLength_ <= 0 || settings.wallsLength_ > 1000 ? tString( "\xe2\x88\x9e" ) : number( settings.wallsLength_ ) + tString( " s" );
            struct Row { char const * key; tString value; };
            Row const rows[] = {
                { "acceleration", number( settings.acceleration_ ) },
                { "rubber", number( settings.rubberWallHump_ ) },
                { "wall length", wall },
                { "map", tString( settings.GetFlag( nServerInfo::SettingsDigest::Flags_NondefaultMap ) ? "custom" : "default" ) },
                { "login", tString( settings.GetFlag( nServerInfo::SettingsDigest::Flags_AuthenticationRequired ) ? "required" : "optional" ) } };
            for ( auto const & row: rows )
            {
                sl_Text( l, y, sl_small, tString( row.key ), sl_textLow );
                sl_Text( r, y, sl_small, row.value, sl_textHi, 1 );
                y -= .048;
            }
        }

        // buttons
        REAL const bb = bottom + .06, bt = bottom + .16, jr = r - .12;
        bool canJoin = sl_Joinable( *s );
        bool hovJoin = canJoin && Hover( l, bb, jr, bt );
        gSLColor joinColor = canJoin ? sl_accent : sl_border;
        if ( hovJoin )
            joinColor = gSLColor{ .45, .9, .95, 1 };
        sl_Rect( l, bb, jr, bt, joinColor );
        sl_Text( ( l + jr ) * .5, ( bb + bt ) * .5, sl_body, sl_Spaced( tString( "JOIN SERVER" ) ), sl_bg, 0 );
        AddHit( l, bb, jr, bt, ACT_JOIN );

        REAL const el = jr + .025;
        bool hovEye = canJoin && Hover( el, bb, r, bt );
        sl_Rect( el, bb, r, bt, hovEye ? sl_rowSel : sl_panel );
        sl_Frame( el, bb, r, bt, sl_border );
        RenderEye( ( el + r ) * .5, ( bb + bt ) * .5, canJoin ? sl_textMid : sl_border );
        AddHit( el, bb, r, bt, ACT_SPECTATE );

        // players on the server
        y -= .03;
        sl_Text( l, y, sl_small, sl_Spaced( tString( "PLAYERS" ) ), sl_textMid );
        y -= .06;
        std::vector< tString > names = sl_PlayerNames( *s );
        if ( names.empty() )
            sl_Text( l, y, sl_small, tString( s->Polling() ? "waiting for answer\xe2\x80\xa6" : "nobody playing" ), sl_textLow );
        for ( size_t i = 0; i < names.size(); ++i )
        {
            if ( y < bt + .06 )
            {
                tString more;
                more << "+" << ( names.size() - i ) << " more";
                sl_Text( l, y, sl_small, more, sl_textLow );
                break;
            }
            tString number;
            number << i + 1;
            sl_Text( l, y, sl_small * .9, number, sl_textLow );
            sl_Rect( l + .045, y - .016, l + .05, y + .016, i % 2 ? sl_orange : sl_accent );
            sl_Text( l + .07, y, sl_small, names[i], sl_textHi, -1, r - l - .07 );
            y -= .048;
        }
    }

    void RenderEye( REAL x, REAL y, gSLColor const & c )
    {
        REAL const aspect = sr_screenHeight > 0 ? REAL( sr_screenWidth ) / sr_screenHeight : 1;
        REAL const w = .025, h = .016 * aspect;
        glDisable( GL_TEXTURE_2D );
        glLineWidth( 2 );
        Color( c.r, c.g, c.b, c.a );
        BeginLineLoop();
        for ( int i = 0; i <= 16; ++i )
        {
            REAL t = REAL( i ) / 16;
            Vertex( x - w + 2 * w * t, y + h * sinf( 3.14159f * t ) );
        }
        for ( int i = 1; i < 16; ++i )
        {
            REAL t = REAL( i ) / 16;
            Vertex( x + w - 2 * w * t, y - h * sinf( 3.14159f * t ) );
        }
        RenderEnd();
        glLineWidth( 1 );
        BeginTriangleFan();
        Vertex( x, y );
        for ( int i = 0; i <= 16; ++i )
        {
            REAL a = 2 * 3.14159f * i / 16;
            Vertex( x + .008 * cosf( a ), y + .008 * aspect * sinf( a ) );
        }
        RenderEnd();
    }

    void RenderBottomBar( REAL bottom )
    {
        REAL const y = ( -1 + bottom ) * .5;
        REAL x = -.96;
        char const * hints[][2] = { { "ENTER", "join" }, { "S", "spectate" }, { "R", "refresh" }, { "/", "search" }, { "ESC", "back" } };
        for ( auto const & hint: hints )
        {
            sl_Text( x, y, sl_small, tString( hint[0] ), sl_textMid );
            x += sl_TextWidth( tString( hint[0] ), sl_small ) + .02;
            sl_Text( x, y, sl_small, tString( hint[1] ), sl_textLow );
            x += sl_TextWidth( tString( hint[1] ), sl_small ) + .07;
        }

        tString status;
        if ( !sg_TalkToMaster )
            status << "local network";
        else if ( totalServers_ > 0 )
            status << "master server \xc2\xb7 ok";
        else
            status << "master server \xc2\xb7 no servers";
        sl_Text( .96, y, sl_small, status, sl_textLow, 1 );
    }
};

void gServerListItem::RenderBackground()
{
    sn_Receive();
    sn_SendPlanned();
    if ( continuePoll )
    {
        continuePoll = nServerInfo::DoQueryAll( sg_simultaneous );
        sn_Receive();
        sn_SendPlanned();
    }

    if ( sg_RequestLANcontinuously )
    {
        static REAL timeout = -1E+32f;
        if ( timeout < tSysTimeFloat() )
        {
            nServerInfo::GetFromLANContinuously();
            if ( !continuePoll )
            {
                nServerInfo::StartQueryAll( sg_queryType );
                continuePoll = true;
            }
            timeout = tSysTimeFloat() + 10;
        }
    }

    sr_ResetRenderState( true );
}
} // namespace

static void sg_BrowseServerList()
{
    gServerListScreen screen;
    screen.Enter();
}
#endif
