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

#include "gSpotify.h"

#include "aa_config.h"
#include "tConfiguration.h"
#include "tString.h"

#ifndef DEDICATED

#include "gStuff.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <winhttp.h>
typedef SOCKET sp_Socket;
static sp_Socket const sp_noSocket = INVALID_SOCKET;
static void sp_CloseSocket( sp_Socket s ){ closesocket( s ); }
static int const sp_sendFlags = 0;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int sp_Socket;
static sp_Socket const sp_noSocket = -1;
static void sp_CloseSocket( sp_Socket s ){ close( s ); }
static int const sp_sendFlags = MSG_NOSIGNAL;
#ifdef LIBCURL_PROTOCOL_HTTP
#include <curl/curl.h>
#endif
#endif

#endif // DEDICATED

static bool sg_spotifyEnabled = false;
static bool sg_spotifyLyrics = true;
static tString sg_spotifyRefreshToken;

static tConfItem< bool > sg_spotifyEnabledConf( "SPOTIFY_ENABLED", sg_spotifyEnabled );
static tConfItem< bool > sg_spotifyLyricsConf( "SPOTIFY_LYRICS", sg_spotifyLyrics );
static tConfItemLine sg_spotifyRefreshTokenConf( "SPOTIFY_REFRESH_TOKEN", sg_spotifyRefreshToken );

bool & gSpotify::Enabled(){ return sg_spotifyEnabled; }
bool & gSpotify::ShowLyrics(){ return sg_spotifyLyrics; }

#ifdef DEDICATED

bool gSpotify::NowPlaying( gSpotifyNowPlaying &, bool ){ return false; }
bool gSpotify::AlbumArt( int &, std::string & ){ return false; }
void gSpotify::Login(){}
void gSpotify::Logout(){}
bool gSpotify::LoggedIn(){ return false; }
tString gSpotify::Status(){ return tString(); }
tString gSpotify::Track(){ return tString(); }
tString gSpotify::Lyric(){ return tString(); }
void gSpotify::Shutdown(){}

#else

static char const * const sp_clientID = "d30ef2099a1941d4a4ca10ac216906ff";
static int const sp_port = 43821;
static char const * const sp_redirect = "http://127.0.0.1:43821/callback";
static char const * const sp_scopes = "user-read-currently-playing user-read-playback-state";
static char const * const sp_userAgent = "Orchid Client (https://github.com/orchidoxion2/Orchid-Client-an-armagetron-client)";

// the addresses can be pointed at a local test server through the environment
static std::string sp_Base( char const * variable, char const * fallback )
{
    char const * value = getenv( variable );
    return value && *value ? value : fallback;
}

static std::string sp_AccountsBase(){ return sp_Base( "ORCHID_SPOTIFY_ACCOUNTS", "https://accounts.spotify.com" ); }
static std::string sp_ApiBase(){ return sp_Base( "ORCHID_SPOTIFY_API", "https://api.spotify.com" ); }
static std::string sp_LyricsBase(){ return sp_Base( "ORCHID_LRCLIB", "https://lrclib.net" ); }

// *******************************************************************************
// text helpers
// *******************************************************************************

static std::string sp_UrlEncode( std::string const & in )
{
    static char const * const hex = "0123456789ABCDEF";
    std::string out;
    for ( unsigned char c: in )
    {
        if ( isalnum( c ) || c == '-' || c == '_' || c == '.' || c == '~' )
            out += char( c );
        else
        {
            out += '%';
            out += hex[ c >> 4 ];
            out += hex[ c & 15 ];
        }
    }
    return out;
}

static std::string sp_UrlDecode( std::string const & in )
{
    std::string out;
    for ( size_t i = 0; i < in.size(); ++i )
    {
        if ( in[i] == '%' && i + 2 < in.size() )
        {
            out += char( strtol( in.substr( i + 1, 2 ).c_str(), NULL, 16 ) );
            i += 2;
        }
        else
            out += in[i] == '+' ? ' ' : in[i];
    }
    return out;
}

static std::string sp_RandomString( size_t length )
{
    static char const * const chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
    std::random_device device;
    std::mt19937 generator( device() ^ unsigned( std::chrono::steady_clock::now().time_since_epoch().count() ) );
    std::uniform_int_distribution< int > pick( 0, 65 );
    std::string out;
    for ( size_t i = 0; i < length; ++i )
        out += chars[ pick( generator ) ];
    return out;
}

static std::string sp_Sha256( std::string const & message )
{
    static uint32_t const k[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
    uint32_t h[8] = { 0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19 };

    std::string data = message;
    uint64_t const bits = uint64_t( message.size() ) * 8;
    data += char( 0x80 );
    while ( data.size() % 64 != 56 )
        data += char( 0 );
    for ( int i = 7; i >= 0; --i )
        data += char( ( bits >> ( i * 8 ) ) & 0xff );

    auto rotr = []( uint32_t x, int n ){ return ( x >> n ) | ( x << ( 32 - n ) ); };
    for ( size_t chunk = 0; chunk < data.size(); chunk += 64 )
    {
        uint32_t w[64];
        for ( int i = 0; i < 16; ++i )
        {
            unsigned char const * p = reinterpret_cast< unsigned char const * >( data.data() + chunk + i * 4 );
            w[i] = ( uint32_t( p[0] ) << 24 ) | ( uint32_t( p[1] ) << 16 ) | ( uint32_t( p[2] ) << 8 ) | p[3];
        }
        for ( int i = 16; i < 64; ++i )
        {
            uint32_t s0 = rotr( w[i-15], 7 ) ^ rotr( w[i-15], 18 ) ^ ( w[i-15] >> 3 );
            uint32_t s1 = rotr( w[i-2], 17 ) ^ rotr( w[i-2], 19 ) ^ ( w[i-2] >> 10 );
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for ( int i = 0; i < 64; ++i )
        {
            uint32_t S1 = rotr( e, 6 ) ^ rotr( e, 11 ) ^ rotr( e, 25 );
            uint32_t ch = ( e & f ) ^ ( ~e & g );
            uint32_t t1 = hh + S1 + ch + k[i] + w[i];
            uint32_t S0 = rotr( a, 2 ) ^ rotr( a, 13 ) ^ rotr( a, 22 );
            uint32_t maj = ( a & b ) ^ ( a & c ) ^ ( b & c );
            uint32_t t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    std::string digest;
    for ( uint32_t v: h )
        for ( int i = 3; i >= 0; --i )
            digest += char( ( v >> ( i * 8 ) ) & 0xff );
    return digest;
}

static std::string sp_Base64Url( std::string const & in )
{
    static char const * const chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    size_t i = 0;
    for ( ; i + 2 < in.size(); i += 3 )
    {
        uint32_t n = ( uint32_t( uint8_t( in[i] ) ) << 16 ) | ( uint32_t( uint8_t( in[i+1] ) ) << 8 ) | uint8_t( in[i+2] );
        out += chars[ ( n >> 18 ) & 63 ];
        out += chars[ ( n >> 12 ) & 63 ];
        out += chars[ ( n >> 6 ) & 63 ];
        out += chars[ n & 63 ];
    }
    if ( i + 1 == in.size() )
    {
        uint32_t n = uint32_t( uint8_t( in[i] ) ) << 16;
        out += chars[ ( n >> 18 ) & 63 ];
        out += chars[ ( n >> 12 ) & 63 ];
    }
    else if ( i + 2 == in.size() )
    {
        uint32_t n = ( uint32_t( uint8_t( in[i] ) ) << 16 ) | ( uint32_t( uint8_t( in[i+1] ) ) << 8 );
        out += chars[ ( n >> 18 ) & 63 ];
        out += chars[ ( n >> 12 ) & 63 ];
        out += chars[ ( n >> 6 ) & 63 ];
    }
    return out;
}

// *******************************************************************************
// JSON
// *******************************************************************************

class sp_Json
{
public:
    enum Type { NUL, BOOL, NUMBER, STRING, ARRAY, OBJECT };

    static sp_Json Parse( std::string const & text )
    {
        size_t pos = 0;
        sp_Json ret;
        if ( !ret.ParseValue( text, pos ) )
            return sp_Json();
        return ret;
    }

    sp_Json const & operator[]( char const * key ) const
    {
        static sp_Json const none;
        auto found = object_.find( key );
        return found == object_.end() ? none : found->second;
    }
    sp_Json const & operator[]( size_t index ) const
    {
        static sp_Json const none;
        return index < array_.size() ? array_[ index ] : none;
    }
    size_t Size() const { return array_.size(); }
    bool IsNull() const { return type_ == NUL; }
    std::string const & String() const { return string_; }
    double Number() const { return number_; }
    bool Bool() const { return bool_; }

private:
    Type type_ = NUL;
    bool bool_ = false;
    double number_ = 0;
    std::string string_;
    std::vector< sp_Json > array_;
    std::map< std::string, sp_Json > object_;

    static void SkipSpace( std::string const & t, size_t & p )
    {
        while ( p < t.size() && isspace( static_cast< unsigned char >( t[p] ) ) )
            ++p;
    }

    static void AppendUtf8( std::string & out, uint32_t c )
    {
        if ( c < 0x80 )
            out += char( c );
        else if ( c < 0x800 )
        {
            out += char( 0xC0 | ( c >> 6 ) );
            out += char( 0x80 | ( c & 0x3F ) );
        }
        else if ( c < 0x10000 )
        {
            out += char( 0xE0 | ( c >> 12 ) );
            out += char( 0x80 | ( ( c >> 6 ) & 0x3F ) );
            out += char( 0x80 | ( c & 0x3F ) );
        }
        else
        {
            out += char( 0xF0 | ( c >> 18 ) );
            out += char( 0x80 | ( ( c >> 12 ) & 0x3F ) );
            out += char( 0x80 | ( ( c >> 6 ) & 0x3F ) );
            out += char( 0x80 | ( c & 0x3F ) );
        }
    }

    static bool ParseString( std::string const & t, size_t & p, std::string & out )
    {
        if ( p >= t.size() || t[p] != '"' )
            return false;
        ++p;
        while ( p < t.size() && t[p] != '"' )
        {
            if ( t[p] != '\\' )
            {
                out += t[p++];
                continue;
            }
            if ( ++p >= t.size() )
                return false;
            char e = t[p++];
            switch ( e )
            {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u':
            {
                if ( p + 4 > t.size() )
                    return false;
                uint32_t c = strtoul( t.substr( p, 4 ).c_str(), NULL, 16 );
                p += 4;
                if ( c >= 0xD800 && c < 0xDC00 && p + 6 <= t.size() && t[p] == '\\' && t[p+1] == 'u' )
                {
                    uint32_t low = strtoul( t.substr( p + 2, 4 ).c_str(), NULL, 16 );
                    if ( low >= 0xDC00 && low < 0xE000 )
                    {
                        c = 0x10000 + ( ( c - 0xD800 ) << 10 ) + ( low - 0xDC00 );
                        p += 6;
                    }
                }
                AppendUtf8( out, c );
                break;
            }
            default: out += e; break;
            }
        }
        if ( p >= t.size() )
            return false;
        ++p;
        return true;
    }

    bool ParseValue( std::string const & t, size_t & p )
    {
        SkipSpace( t, p );
        if ( p >= t.size() )
            return false;
        char c = t[p];
        if ( c == '{' )
        {
            type_ = OBJECT;
            ++p;
            SkipSpace( t, p );
            if ( p < t.size() && t[p] == '}' )
            {
                ++p;
                return true;
            }
            while ( true )
            {
                SkipSpace( t, p );
                std::string key;
                if ( !ParseString( t, p, key ) )
                    return false;
                SkipSpace( t, p );
                if ( p >= t.size() || t[p++] != ':' )
                    return false;
                if ( !object_[ key ].ParseValue( t, p ) )
                    return false;
                SkipSpace( t, p );
                if ( p < t.size() && t[p] == ',' )
                {
                    ++p;
                    continue;
                }
                if ( p < t.size() && t[p] == '}' )
                {
                    ++p;
                    return true;
                }
                return false;
            }
        }
        if ( c == '[' )
        {
            type_ = ARRAY;
            ++p;
            SkipSpace( t, p );
            if ( p < t.size() && t[p] == ']' )
            {
                ++p;
                return true;
            }
            while ( true )
            {
                array_.emplace_back();
                if ( !array_.back().ParseValue( t, p ) )
                    return false;
                SkipSpace( t, p );
                if ( p < t.size() && t[p] == ',' )
                {
                    ++p;
                    continue;
                }
                if ( p < t.size() && t[p] == ']' )
                {
                    ++p;
                    return true;
                }
                return false;
            }
        }
        if ( c == '"' )
        {
            type_ = STRING;
            return ParseString( t, p, string_ );
        }
        if ( t.compare( p, 4, "true" ) == 0 )
        {
            type_ = BOOL;
            bool_ = true;
            p += 4;
            return true;
        }
        if ( t.compare( p, 5, "false" ) == 0 )
        {
            type_ = BOOL;
            p += 5;
            return true;
        }
        if ( t.compare( p, 4, "null" ) == 0 )
        {
            p += 4;
            return true;
        }
        char * end = NULL;
        number_ = strtod( t.c_str() + p, &end );
        if ( end == t.c_str() + p )
            return false;
        type_ = NUMBER;
        p = end - t.c_str();
        return true;
    }
};

// *******************************************************************************
// HTTP
// *******************************************************************************

struct sp_Response
{
    long status = 0;
    std::string body;
    int retryAfter = 0;
};

#ifdef WIN32

static std::wstring sp_Wide( std::string const & s )
{
    int n = MultiByteToWideChar( CP_UTF8, 0, s.c_str(), -1, NULL, 0 );
    if ( n <= 0 )
        return std::wstring();
    std::wstring w( n, L'\0' );
    MultiByteToWideChar( CP_UTF8, 0, s.c_str(), -1, &w[0], n );
    w.resize( n - 1 );
    return w;
}

static sp_Response sp_Request( char const * method, std::string const & url, std::vector< std::string > const & headers, std::string const & body )
{
    sp_Response ret;
    std::wstring wurl = sp_Wide( url );
    wchar_t host[256], path[4096], extra[4096];
    URL_COMPONENTS parts;
    memset( &parts, 0, sizeof( parts ) );
    parts.dwStructSize = sizeof( parts );
    parts.lpszHostName = host;
    parts.dwHostNameLength = 256;
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = 4096;
    parts.lpszExtraInfo = extra;
    parts.dwExtraInfoLength = 4096;
    if ( !WinHttpCrackUrl( wurl.c_str(), 0, 0, &parts ) )
        return ret;
    std::wstring target = std::wstring( path ) + extra;

    HINTERNET session = WinHttpOpen( sp_Wide( sp_userAgent ).c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 );
    if ( !session )
        return ret;
    WinHttpSetTimeouts( session, 8000, 8000, 10000, 10000 );
    HINTERNET connection = WinHttpConnect( session, host, parts.nPort, 0 );
    HINTERNET request = connection ? WinHttpOpenRequest( connection, sp_Wide( method ).c_str(), target.c_str(), NULL, WINHTTP_NO_REFERER,
                                                         WINHTTP_DEFAULT_ACCEPT_TYPES, parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0 ) : NULL;
    if ( request )
    {
        std::string allHeaders;
        for ( auto const & h: headers )
            allHeaders += h + "\r\n";
        std::wstring wheaders = sp_Wide( allHeaders );
        if ( WinHttpSendRequest( request, wheaders.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : wheaders.c_str(), DWORD( wheaders.size() ),
                                 body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast< char * >( body.data() ), DWORD( body.size() ), DWORD( body.size() ), 0 ) &&
             WinHttpReceiveResponse( request, NULL ) )
        {
            DWORD status = 0, size = sizeof( status );
            WinHttpQueryHeaders( request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX );
            ret.status = status;
            DWORD retry = 0;
            size = sizeof( retry );
            if ( WinHttpQueryHeaders( request, WINHTTP_QUERY_RETRY_AFTER | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &retry, &size, WINHTTP_NO_HEADER_INDEX ) )
                ret.retryAfter = retry;
            DWORD available = 0;
            while ( WinHttpQueryDataAvailable( request, &available ) && available > 0 )
            {
                std::string chunk( available, '\0' );
                DWORD read = 0;
                if ( !WinHttpReadData( request, &chunk[0], available, &read ) || read == 0 )
                    break;
                ret.body.append( chunk.data(), read );
            }
        }
        WinHttpCloseHandle( request );
    }
    if ( connection )
        WinHttpCloseHandle( connection );
    WinHttpCloseHandle( session );
    return ret;
}

#elif defined( LIBCURL_PROTOCOL_HTTP )

static size_t sp_CurlWrite( char * data, size_t size, size_t count, void * target )
{
    static_cast< std::string * >( target )->append( data, size * count );
    return size * count;
}

static size_t sp_CurlHeader( char * data, size_t size, size_t count, void * target )
{
    std::string line( data, size * count );
    if ( line.size() > 12 && strncasecmp( line.c_str(), "retry-after:", 12 ) == 0 )
        *static_cast< int * >( target ) = atoi( line.c_str() + 12 );
    return size * count;
}

static sp_Response sp_Request( char const * method, std::string const & url, std::vector< std::string > const & headers, std::string const & body )
{
    sp_Response ret;
    CURL * curl = curl_easy_init();
    if ( !curl )
        return ret;
    struct curl_slist * list = NULL;
    for ( auto const & h: headers )
        list = curl_slist_append( list, h.c_str() );
    curl_easy_setopt( curl, CURLOPT_URL, url.c_str() );
    curl_easy_setopt( curl, CURLOPT_USERAGENT, sp_userAgent );
    curl_easy_setopt( curl, CURLOPT_NOSIGNAL, 1L );
    curl_easy_setopt( curl, CURLOPT_CONNECTTIMEOUT, 8L );
    curl_easy_setopt( curl, CURLOPT_TIMEOUT, 10L );
    curl_easy_setopt( curl, CURLOPT_FOLLOWLOCATION, 1L );
    curl_easy_setopt( curl, CURLOPT_HTTPHEADER, list );
    curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, sp_CurlWrite );
    curl_easy_setopt( curl, CURLOPT_WRITEDATA, &ret.body );
    curl_easy_setopt( curl, CURLOPT_HEADERFUNCTION, sp_CurlHeader );
    curl_easy_setopt( curl, CURLOPT_HEADERDATA, &ret.retryAfter );
    if ( strcmp( method, "POST" ) == 0 )
        curl_easy_setopt( curl, CURLOPT_COPYPOSTFIELDS, body.c_str() );
    if ( curl_easy_perform( curl ) == CURLE_OK )
        curl_easy_getinfo( curl, CURLINFO_RESPONSE_CODE, &ret.status );
    curl_slist_free_all( list );
    curl_easy_cleanup( curl );
    return ret;
}

#else

static sp_Response sp_Request( char const *, std::string const &, std::vector< std::string > const &, std::string const & )
{
    return sp_Response();
}

#endif

// *******************************************************************************
// shared state between the game and the background thread
// *******************************************************************************

typedef std::chrono::steady_clock sp_Clock;

enum sp_LoginState { SP_LOGGED_OUT, SP_WAITING, SP_LOGGED_IN, SP_FAILED };

struct sp_LyricLine
{
    int ms;
    std::string text;
};

struct sp_Shared
{
    std::mutex mutex;
    std::condition_variable wake;
    std::thread thread;
    bool started = false, quit = false, enabled = false, lyrics = true;

    std::string refreshToken;
    bool refreshTokenChanged = false;
    std::string syncedConfToken;

    sp_LoginState login = SP_LOGGED_OUT;
    std::string loginMessage;
    sp_Socket listener = sp_noSocket;
    std::string verifier, state;
    sp_Clock::time_point loginStarted;

    std::string trackID, title, artist, album;
    int durationMs = 0, progressMs = 0;
    bool playing = false;
    sp_Clock::time_point progressTime;

    std::string lyricsFor;
    std::vector< sp_LyricLine > lyricLines;
    gSpotifyNowPlaying::LyricsState lyricsState = gSpotifyNowPlaying::LYRICS_SEARCHING;
    sp_Clock::time_point lyricsRetry;
    int trackVersion = 0;

    std::string artUrl, artFor, artBytes;
    int artVersion = 0;

    ~sp_Shared()
    {
        {
            std::lock_guard< std::mutex > lock( mutex );
            quit = true;
            wake.notify_all();
        }
        if ( thread.joinable() )
            thread.join();
    }
};

static sp_Shared & sp_State()
{
    static sp_Shared shared;
    return shared;
}

static void sp_ClearTrack( sp_Shared & s )
{
    s.trackID.clear();
    s.title.clear();
    s.artist.clear();
    s.album.clear();
    s.playing = false;
    s.lyricsFor.clear();
    s.lyricLines.clear();
    s.artUrl.clear();
    s.artFor.clear();
    if ( !s.artBytes.empty() )
    {
        s.artBytes.clear();
        ++s.artVersion;
    }
}

static void sp_Sleep( std::unique_lock< std::mutex > & lock, int ms )
{
    sp_Shared & s = sp_State();
    s.wake.wait_for( lock, std::chrono::milliseconds( ms ) );
}

// *******************************************************************************
// background work, called with the mutex unlocked
// *******************************************************************************

static std::vector< sp_LyricLine > sp_ParseLrc( std::string const & lrc )
{
    std::vector< sp_LyricLine > lines;
    std::istringstream in( lrc );
    std::string line;
    while ( std::getline( in, line ) )
    {
        if ( !line.empty() && line.back() == '\r' )
            line.pop_back();
        std::vector< int > times;
        size_t p = 0;
        while ( p < line.size() && line[p] == '[' )
        {
            size_t close = line.find( ']', p );
            if ( close == std::string::npos )
                break;
            int minutes = 0;
            double seconds = 0;
            if ( sscanf( line.c_str() + p + 1, "%d:%lf", &minutes, &seconds ) == 2 )
                times.push_back( int( ( minutes * 60 + seconds ) * 1000 + .5 ) );
            p = close + 1;
        }
        std::string text = line.substr( p );
        while ( !text.empty() && isspace( static_cast< unsigned char >( text[0] ) ) )
            text.erase( 0, 1 );
        for ( int t: times )
            lines.push_back( sp_LyricLine{ t, text } );
    }
    std::stable_sort( lines.begin(), lines.end(), []( sp_LyricLine const & a, sp_LyricLine const & b ){ return a.ms < b.ms; } );
    return lines;
}

//! the title without additions Spotify has and LRCLIB often does not: " - Remastered 2011", " (feat. X)"
static std::string sp_PlainTitle( std::string const & title )
{
    std::string plain = title;
    size_t cut = plain.find( " - " );
    if ( cut != std::string::npos && cut > 0 )
        plain.erase( cut );
    for ( char const * open: { " (", " [" } )
    {
        cut = plain.find( open );
        if ( cut != std::string::npos && cut > 0 )
            plain.erase( cut );
    }
    return plain;
}

static std::vector< sp_LyricLine > sp_FetchLyrics( std::string const & title, std::string const & artist, std::string const & album, int durationMs, gSpotifyNowPlaying::LyricsState & state )
{
    std::vector< std::string > headers;
    bool reached = false, failed = false;
    auto request = [&]( std::string const & url, sp_Json & json )
    {
        sp_Response response = sp_Request( "GET", url, headers, std::string() );
        if ( response.status == 200 || response.status == 404 )
            reached = true;
        else
            failed = true;
        json = response.status == 200 ? sp_Json::Parse( response.body ) : sp_Json();
        return response.status == 200;
    };

    sp_Json json;
    std::ostringstream url;
    url << sp_LyricsBase() << "/api/get?track_name=" << sp_UrlEncode( title ) << "&artist_name=" << sp_UrlEncode( artist )
        << "&album_name=" << sp_UrlEncode( album ) << "&duration=" << ( durationMs + 500 ) / 1000;
    if ( request( url.str(), json ) && !json["syncedLyrics"].String().empty() )
    {
        state = gSpotifyNowPlaying::LYRICS_FOUND;
        return sp_ParseLrc( json["syncedLyrics"].String() );
    }

    // search by title and artist, also with the plain title; prefer a version of about the same length
    std::vector< std::string > titles( 1, title );
    if ( sp_PlainTitle( title ) != title && !sp_PlainTitle( title ).empty() )
        titles.push_back( sp_PlainTitle( title ) );
    for ( std::string const & t: titles )
    {
        std::ostringstream search;
        search << sp_LyricsBase() << "/api/search?track_name=" << sp_UrlEncode( t ) << "&artist_name=" << sp_UrlEncode( artist );
        if ( !request( search.str(), json ) )
            continue;
        int best = -1;
        double bestDiff = 1E+30;
        for ( size_t i = 0; i < json.Size(); ++i )
        {
            if ( json[i]["syncedLyrics"].String().empty() )
                continue;
            double const diff = fabs( json[i]["duration"].Number() - durationMs / 1000.0 );
            if ( best < 0 || diff < bestDiff )
            {
                best = int( i );
                bestDiff = diff;
            }
        }
        if ( best >= 0 )
        {
            state = gSpotifyNowPlaying::LYRICS_FOUND;
            return sp_ParseLrc( json[ size_t( best ) ]["syncedLyrics"].String() );
        }
    }
    state = failed && !reached ? gSpotifyNowPlaying::LYRICS_UNREACHABLE : gSpotifyNowPlaying::LYRICS_NONE;
    return std::vector< sp_LyricLine >();
}

static sp_Response sp_TokenRequest( std::string const & form )
{
    std::vector< std::string > headers;
    headers.push_back( "Content-Type: application/x-www-form-urlencoded" );
    return sp_Request( "POST", sp_AccountsBase() + "/api/token", headers, form );
}

static void sp_SendPage( sp_Socket client, char const * status, char const * message )
{
    std::ostringstream page;
    page << "<!doctype html><html><head><meta charset=\"utf-8\"><title>Orchid Client</title></head>"
         << "<body style=\"background:#120916;color:#f7e2ef;font-family:sans-serif;text-align:center;padding-top:15vh\">"
         << "<h1 style=\"color:#d6409f\">Orchid Client</h1><p>" << message << "</p></body></html>";
    std::string body = page.str();
    std::ostringstream response;
    response << "HTTP/1.1 " << status << "\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " << body.size()
             << "\r\nConnection: close\r\n\r\n" << body;
    std::string out = response.str();
    send( client, out.data(), int( out.size() ), sp_sendFlags );
}

// waits a little for the browser to come back; returns the login code once it does
static bool sp_AcceptLogin( sp_Socket listener, std::string const & state, std::string & code, std::string & error )
{
    fd_set set;
    FD_ZERO( &set );
    FD_SET( listener, &set );
    timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 300000;
    if ( select( int( listener + 1 ), &set, NULL, NULL, &timeout ) <= 0 )
        return false;
    sp_Socket client = accept( listener, NULL, NULL );
    if ( client == sp_noSocket )
        return false;

    std::string request;
    char buffer[2048];
    while ( request.find( "\r\n" ) == std::string::npos && request.size() < 8192 )
    {
        FD_ZERO( &set );
        FD_SET( client, &set );
        timeout.tv_sec = 2;
        timeout.tv_usec = 0;
        if ( select( int( client + 1 ), &set, NULL, NULL, &timeout ) <= 0 )
            break;
        int got = recv( client, buffer, sizeof( buffer ), 0 );
        if ( got <= 0 )
            break;
        request.append( buffer, got );
    }

    bool done = false;
    size_t start = request.find( ' ' ), end = start == std::string::npos ? start : request.find( ' ', start + 1 );
    std::string target = start == std::string::npos || end == std::string::npos ? std::string() : request.substr( start + 1, end - start - 1 );
    if ( target.compare( 0, 9, "/callback" ) == 0 )
    {
        std::map< std::string, std::string > query;
        size_t q = target.find( '?' );
        std::string params = q == std::string::npos ? std::string() : target.substr( q + 1 );
        std::istringstream in( params );
        std::string pair;
        while ( std::getline( in, pair, '&' ) )
        {
            size_t eq = pair.find( '=' );
            if ( eq != std::string::npos )
                query[ pair.substr( 0, eq ) ] = sp_UrlDecode( pair.substr( eq + 1 ) );
        }
        if ( query["state"] != state )
            sp_SendPage( client, "400 Bad Request", "This login link is out of date. Start the login again from the game." );
        else if ( !query["error"].empty() )
        {
            error = query["error"] == "access_denied" ? "Login was cancelled" : "Spotify refused the login";
            sp_SendPage( client, "200 OK", "The login was cancelled. You can close this tab." );
            done = true;
        }
        else if ( !query["code"].empty() )
        {
            code = query["code"];
            sp_SendPage( client, "200 OK", "Orchid Client is now connected to Spotify. You can close this tab and go back to the game." );
            done = true;
        }
        else
            sp_SendPage( client, "400 Bad Request", "Something went wrong. Start the login again from the game." );
    }
    else
        sp_SendPage( client, "404 Not Found", "Nothing here." );
    sp_CloseSocket( client );
    return done;
}

static void sp_Worker()
{
    sp_Shared & s = sp_State();
    std::string accessToken;
    sp_Clock::time_point accessExpires;
    sp_Clock::time_point nextPoll;

    std::unique_lock< std::mutex > lock( s.mutex );
    while ( !s.quit )
    {
        if ( s.listener != sp_noSocket )
        {
            sp_Socket listener = s.listener;
            std::string state = s.state, verifier = s.verifier;
            bool expired = sp_Clock::now() - s.loginStarted > std::chrono::minutes( 5 );
            lock.unlock();
            std::string code, error;
            bool done = !expired && sp_AcceptLogin( listener, state, code, error );
            sp_Response response;
            if ( done && error.empty() )
                response = sp_TokenRequest( "grant_type=authorization_code&code=" + sp_UrlEncode( code ) + "&redirect_uri=" + sp_UrlEncode( sp_redirect ) +
                                            "&client_id=" + sp_clientID + "&code_verifier=" + verifier );
            lock.lock();
            if ( expired || done )
            {
                sp_CloseSocket( listener );
                if ( s.listener == listener )
                    s.listener = sp_noSocket;
                if ( expired )
                {
                    s.login = SP_FAILED;
                    s.loginMessage = "Login timed out";
                }
                else if ( !error.empty() )
                {
                    s.login = SP_FAILED;
                    s.loginMessage = error;
                }
                else
                {
                    sp_Json json = sp_Json::Parse( response.body );
                    if ( response.status == 200 && !json["access_token"].String().empty() )
                    {
                        accessToken = json["access_token"].String();
                        accessExpires = sp_Clock::now() + std::chrono::seconds( int( json["expires_in"].Number() ) - 60 );
                        s.refreshToken = json["refresh_token"].String();
                        s.refreshTokenChanged = true;
                        s.login = SP_LOGGED_IN;
                        s.loginMessage.clear();
                        nextPoll = sp_Clock::now();
                    }
                    else
                    {
                        s.login = SP_FAILED;
                        s.loginMessage = response.status == 0 ? "Could not reach Spotify" : "Spotify refused the login";
                    }
                }
            }
            continue;
        }

        if ( !s.enabled || s.refreshToken.empty() )
        {
            accessToken.clear();
            sp_ClearTrack( s );
            sp_Sleep( lock, 1000 );
            continue;
        }

        if ( sp_Clock::now() < nextPoll )
        {
            sp_Sleep( lock, int( std::chrono::duration_cast< std::chrono::milliseconds >( nextPoll - sp_Clock::now() ).count() ) + 1 );
            continue;
        }
        nextPoll = sp_Clock::now() + std::chrono::seconds( 3 );

        if ( accessToken.empty() || sp_Clock::now() > accessExpires )
        {
            std::string refresh = s.refreshToken;
            lock.unlock();
            sp_Response response = sp_TokenRequest( "grant_type=refresh_token&refresh_token=" + sp_UrlEncode( refresh ) + "&client_id=" + sp_clientID );
            lock.lock();
            if ( s.refreshToken != refresh )
                continue;
            sp_Json json = sp_Json::Parse( response.body );
            if ( response.status == 200 && !json["access_token"].String().empty() )
            {
                accessToken = json["access_token"].String();
                accessExpires = sp_Clock::now() + std::chrono::seconds( int( json["expires_in"].Number() ) - 60 );
                if ( !json["refresh_token"].String().empty() && json["refresh_token"].String() != s.refreshToken )
                {
                    s.refreshToken = json["refresh_token"].String();
                    s.refreshTokenChanged = true;
                }
                s.login = SP_LOGGED_IN;
            }
            else if ( response.status == 400 )
            {
                // the login was revoked or has expired
                s.refreshToken.clear();
                s.refreshTokenChanged = true;
                s.login = SP_FAILED;
                s.loginMessage = "Spotify login expired";
                sp_ClearTrack( s );
                continue;
            }
            else
            {
                nextPoll = sp_Clock::now() + std::chrono::seconds( 15 );
                continue;
            }
        }

        std::vector< std::string > headers;
        headers.push_back( "Authorization: Bearer " + accessToken );
        lock.unlock();
        sp_Response response = sp_Request( "GET", sp_ApiBase() + "/v1/me/player/currently-playing?additional_types=track", headers, std::string() );
        lock.lock();

        if ( response.status == 401 )
        {
            accessToken.clear();
            nextPoll = sp_Clock::now();
            continue;
        }
        if ( response.status == 429 )
        {
            nextPoll = sp_Clock::now() + std::chrono::seconds( std::max( 5, response.retryAfter ) );
            continue;
        }
        if ( response.status == 204 )
        {
            sp_ClearTrack( s );
            continue;
        }
        if ( response.status != 200 )
            continue;

        sp_Json json = sp_Json::Parse( response.body );
        sp_Json const & item = json["item"];
        if ( item.IsNull() )
        {
            sp_ClearTrack( s );
            continue;
        }
        std::string artists;
        for ( size_t i = 0; i < item["artists"].Size(); ++i )
        {
            if ( i > 0 )
                artists += ", ";
            artists += item["artists"][i]["name"].String();
        }
        std::string id = item["id"].String().empty() ? item["name"].String() + artists : item["id"].String();
        if ( id != s.trackID )
        {
            s.lyricsFor.clear();
            s.lyricLines.clear();
            ++s.trackVersion;
        }
        s.trackID = id;
        s.title = item["name"].String();
        s.artist = artists;
        s.album = item["album"]["name"].String();
        s.durationMs = int( item["duration_ms"].Number() );
        s.progressMs = int( json["progress_ms"].Number() );
        s.playing = json["is_playing"].Bool();
        s.progressTime = sp_Clock::now();

        // the smallest cover that is still sharp at HUD size
        sp_Json const & images = item["album"]["images"];
        std::string artUrl;
        int artWidth = 0;
        for ( size_t i = 0; i < images.Size(); ++i )
        {
            int width = int( images[i]["width"].Number() );
            if ( artUrl.empty() || ( width >= 200 && ( artWidth < 200 || width < artWidth ) ) )
            {
                artUrl = images[i]["url"].String();
                artWidth = width;
            }
        }
        s.artUrl = artUrl;

        // the lyrics service could not be reached: try again after a while
        if ( s.lyricsFor == id && s.lyricsState == gSpotifyNowPlaying::LYRICS_UNREACHABLE && sp_Clock::now() > s.lyricsRetry )
            s.lyricsFor.clear();

        if ( s.lyrics && s.lyricsFor != id )
        {
            std::string title = s.title, artist = item["artists"][size_t( 0 )]["name"].String(), album = s.album;
            int duration = s.durationMs;
            s.lyricsFor = id;
            s.lyricsState = gSpotifyNowPlaying::LYRICS_SEARCHING;
            lock.unlock();
            gSpotifyNowPlaying::LyricsState state = gSpotifyNowPlaying::LYRICS_NONE;
            std::vector< sp_LyricLine > lines = sp_FetchLyrics( title, artist, album, duration, state );
            lock.lock();
            if ( s.lyricsFor == id )
            {
                s.lyricLines.swap( lines );
                s.lyricsState = state;
                s.lyricsRetry = sp_Clock::now() + std::chrono::seconds( 20 );
            }
        }

        if ( s.artFor != id && !s.artUrl.empty() )
        {
            std::string url = s.artUrl;
            s.artFor = id;
            lock.unlock();
            sp_Response image = sp_Request( "GET", url, std::vector< std::string >(), std::string() );
            lock.lock();
            if ( s.artFor == id && image.status == 200 )
            {
                s.artBytes.swap( image.body );
                ++s.artVersion;
            }
        }
    }
}

// *******************************************************************************
// game side
// *******************************************************************************

// runs on the game thread: starts the worker and keeps the saved login in sync
static void sp_Update( sp_Shared & s )
{
    if ( s.refreshTokenChanged )
    {
        sg_spotifyRefreshToken = s.refreshToken;
        s.syncedConfToken = s.refreshToken;
        s.refreshTokenChanged = false;
    }
    else if ( s.syncedConfToken != sg_spotifyRefreshToken )
    {
        s.refreshToken = s.syncedConfToken = sg_spotifyRefreshToken;
        if ( !s.refreshToken.empty() && s.login == SP_LOGGED_OUT )
            s.login = SP_LOGGED_IN;
    }
    if ( s.enabled != sg_spotifyEnabled || s.lyrics != sg_spotifyLyrics )
    {
        s.enabled = sg_spotifyEnabled;
        s.lyrics = sg_spotifyLyrics;
        s.wake.notify_all();
    }
    if ( !s.started && ( s.enabled || s.listener != sp_noSocket ) )
    {
        s.started = true;
#ifdef WIN32
        WSADATA wsa;
        WSAStartup( MAKEWORD( 2, 2 ), &wsa );
#elif defined( LIBCURL_PROTOCOL_HTTP )
        curl_global_init( CURL_GLOBAL_DEFAULT );
#endif
        s.thread = std::thread( sp_Worker );
    }
}

void gSpotify::Login()
{
    sp_Shared & s = sp_State();
    std::string url;
    {
        std::lock_guard< std::mutex > lock( s.mutex );
#ifdef WIN32
        WSADATA wsa;
        WSAStartup( MAKEWORD( 2, 2 ), &wsa );
#endif
        if ( s.listener == sp_noSocket )
        {
            sp_Socket listener = socket( AF_INET, SOCK_STREAM, 0 );
            int yes = 1;
            setsockopt( listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast< char const * >( &yes ), sizeof( yes ) );
            sockaddr_in address;
            memset( &address, 0, sizeof( address ) );
            address.sin_family = AF_INET;
            address.sin_port = htons( sp_port );
            address.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
            if ( listener == sp_noSocket ||
                 bind( listener, reinterpret_cast< sockaddr * >( &address ), sizeof( address ) ) != 0 ||
                 listen( listener, 4 ) != 0 )
            {
                if ( listener != sp_noSocket )
                    sp_CloseSocket( listener );
                s.login = SP_FAILED;
                s.loginMessage = "Port 43821 is in use by another program";
                return;
            }
            s.listener = listener;
            s.verifier = sp_RandomString( 64 );
            s.state = sp_RandomString( 16 );
        }
        s.login = SP_WAITING;
        s.loginStarted = sp_Clock::now();
        url = sp_AccountsBase() + "/authorize?response_type=code&client_id=" + sp_clientID +
              "&scope=" + sp_UrlEncode( sp_scopes ) + "&redirect_uri=" + sp_UrlEncode( sp_redirect ) +
              "&code_challenge_method=S256&code_challenge=" + sp_Base64Url( sp_Sha256( s.verifier ) ) + "&state=" + s.state;
        sp_Update( s );
        s.wake.notify_all();
    }
    sg_OpenURI( url.c_str() );
}

void gSpotify::Logout()
{
    sp_Shared & s = sp_State();
    std::lock_guard< std::mutex > lock( s.mutex );
    sg_spotifyRefreshToken = "";
    s.refreshToken.clear();
    s.syncedConfToken.clear();
    s.refreshTokenChanged = false;
    s.login = SP_LOGGED_OUT;
    s.loginMessage.clear();
    sp_ClearTrack( s );
    s.wake.notify_all();
}

bool gSpotify::LoggedIn()
{
    sp_Shared & s = sp_State();
    std::lock_guard< std::mutex > lock( s.mutex );
    sp_Update( s );
    return !s.refreshToken.empty();
}

tString gSpotify::Status()
{
    sp_Shared & s = sp_State();
    std::lock_guard< std::mutex > lock( s.mutex );
    sp_Update( s );
    switch ( s.login )
    {
    case SP_WAITING:
        return tString( "Waiting for the browser login..." );
    case SP_FAILED:
        return tString( s.loginMessage );
    default:
        return tString( s.refreshToken.empty() ? "Not logged in" : "Logged in" );
    }
}

static tString sp_Clip( std::string const & text, size_t maxChars )
{
    tString clean = tColoredString::RemoveColors( text.c_str() );
    size_t chars = 0, i = 0;
    for ( ; i < clean.size(); ++i )
    {
        if ( ( static_cast< unsigned char >( clean[i] ) & 0xC0 ) != 0x80 && ++chars > maxChars )
            break;
    }
    if ( i < clean.size() )
    {
        clean = tString( clean.substr( 0, i ) );
        clean << "...";
    }
    return clean;
}

tString gSpotify::Track()
{
    sp_Shared & s = sp_State();
    std::lock_guard< std::mutex > lock( s.mutex );
    sp_Update( s );
    tString ret;
    if ( !s.enabled || s.title.empty() )
        return ret;
    ret << ( s.playing ? "0xffffff" : "0x8c8c96" ) << sp_Clip( s.title, 40 ) << "0x8c8c96  " << sp_Clip( s.artist, 30 );
    return ret;
}

tString gSpotify::Lyric()
{
    sp_Shared & s = sp_State();
    std::lock_guard< std::mutex > lock( s.mutex );
    sp_Update( s );
    tString ret;
    if ( !s.enabled || !s.lyrics || s.title.empty() || !s.playing || s.lyricLines.empty() )
        return ret;
    int position = s.progressMs + int( std::chrono::duration_cast< std::chrono::milliseconds >( sp_Clock::now() - s.progressTime ).count() );
    std::string const * line = NULL;
    for ( auto const & l: s.lyricLines )
    {
        if ( l.ms > position )
            break;
        line = &l.text;
    }
    if ( line && !line->empty() )
        ret << "0xd6409f" << sp_Clip( *line, 70 );
    return ret;
}

bool gSpotify::NowPlaying( gSpotifyNowPlaying & info, bool withLines )
{
    sp_Shared & s = sp_State();
    std::lock_guard< std::mutex > lock( s.mutex );
    sp_Update( s );
    if ( !s.enabled || s.title.empty() )
        return false;
    info.title = tColoredString::RemoveColors( s.title.c_str() );
    info.artist = tColoredString::RemoveColors( s.artist.c_str() );
    info.durationMs = s.durationMs;
    info.playing = s.playing;
    info.progressMs = s.progressMs;
    if ( s.playing )
        info.progressMs += int( std::chrono::duration_cast< std::chrono::milliseconds >( sp_Clock::now() - s.progressTime ).count() );
    if ( info.durationMs > 0 )
        info.progressMs = std::min( info.progressMs, info.durationMs );
    info.showLyrics = s.lyrics;
    info.trackVersion = s.trackVersion;
    info.lyricsState = s.lyricsFor.empty() ? gSpotifyNowPlaying::LYRICS_SEARCHING : s.lyricsState;
    if ( withLines )
    {
        info.lines.clear();
        info.lines.reserve( s.lyricLines.size() );
        for ( auto const & l: s.lyricLines )
            info.lines.push_back( gSpotifyNowPlaying::Line{ l.ms, tColoredString::RemoveColors( l.text.c_str() ) } );
    }
    return true;
}

bool gSpotify::AlbumArt( int & version, std::string & bytes )
{
    sp_Shared & s = sp_State();
    std::lock_guard< std::mutex > lock( s.mutex );
    if ( version == s.artVersion )
        return false;
    version = s.artVersion;
    bytes = s.artBytes;
    return true;
}

void gSpotify::Shutdown()
{
    sp_Shared & s = sp_State();
    {
        std::lock_guard< std::mutex > lock( s.mutex );
        s.quit = true;
        s.wake.notify_all();
    }
    if ( s.thread.joinable() )
        s.thread.join();
    std::lock_guard< std::mutex > lock( s.mutex );
    if ( s.listener != sp_noSocket )
    {
        sp_CloseSocket( s.listener );
        s.listener = sp_noSocket;
    }
}

#endif // DEDICATED
