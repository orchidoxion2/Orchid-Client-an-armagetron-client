/*

*************************************************************************

Orchid Client -- path finder for Armagetron Advanced.

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

#include "gPathFinder.h"

#include "aa_config.h"
#include "gServerBrowser.h"
#include "tConfiguration.h"

static bool sg_pathFinder = false;
static tConfItem< bool > sg_pathFinderConf( "PATHFINDER", sg_pathFinder );

bool & gPathFinder::Enabled(){ return sg_pathFinder; }

bool gPathFinder::Active()
{
    return sg_pathFinder && !gServerBrowser::OnRankedServer();
}

#ifdef DEDICATED

void gPathFinder::Render( gCycle const * ){}

#else

#include "eAdvWall.h"
#include "ePlayer.h"
#include "gCycle.h"
#include "nNetwork.h"
#include "rGL.h"
#include "rRender.h"
#include "tSysTime.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <queue>
#include <utility>
#include <vector>

namespace
{

// the area around the cycle that is searched: a square of cells aligned with its heading
int const pf_half = 55;
int const pf_size = 2 * pf_half + 1;
REAL const pf_cell = 1.6;
int const pf_unreached = 1 << 30;
int const pf_maxSteps = 140;

// moves in the cycle's frame: forward, left, back, right
int const pf_di[4] = { 1, 0, -1, 0 };
int const pf_dj[4] = { 0, 1, 0, -1 };

inline int pf_Index( int i, int j )
{
    return j * pf_size + i;
}

struct pf_Frame
{
    eCoord origin, ex, ey;

    void Local( eCoord const & p, REAL & u, REAL & v ) const
    {
        eCoord const d = p - origin;
        u = d.x * ex.x + d.y * ex.y;
        v = d.x * ey.x + d.y * ey.y;
    }

    eCoord World( int i, int j ) const
    {
        return origin + ex * ( ( i - pf_half ) * pf_cell ) + ey * ( ( j - pf_half ) * pf_cell );
    }

    bool Cell( eCoord const & p, int & i, int & j ) const
    {
        REAL u, v;
        Local( p, u, v );
        i = int( floor( u / pf_cell + .5 ) ) + pf_half;
        j = int( floor( v / pf_cell + .5 ) ) + pf_half;
        return i >= 0 && j >= 0 && i < pf_size && j < pf_size;
    }
};

void pf_Mark( std::vector< unsigned char > & blocked, int i, int j )
{
    if ( i >= 0 && j >= 0 && i < pf_size && j < pf_size )
        blocked[ pf_Index( i, j ) ] = 1;
}

//! marks the cells a wall passes through, without diagonal gaps a path could slip through
void pf_MarkSegment( pf_Frame const & frame, eCoord const & a, eCoord const & b, std::vector< unsigned char > & blocked )
{
    REAL ua, va, ub, vb;
    frame.Local( a, ua, va );
    frame.Local( b, ub, vb );
    REAL const limit = ( pf_half + 1 ) * pf_cell;
    if ( ( ua < -limit && ub < -limit ) || ( ua > limit && ub > limit ) ||
         ( va < -limit && vb < -limit ) || ( va > limit && vb > limit ) )
        return;

    REAL const length = sqrt( ( ub - ua ) * ( ub - ua ) + ( vb - va ) * ( vb - va ) );
    int const steps = 1 + int( length / ( pf_cell * .3 ) );
    bool first = true;
    int lastI = 0, lastJ = 0;
    for ( int s = 0; s <= steps; ++s )
    {
        REAL const t = REAL( s ) / steps;
        int const i = int( floor( ( ua + ( ub - ua ) * t ) / pf_cell + .5 ) ) + pf_half;
        int const j = int( floor( ( va + ( vb - va ) * t ) / pf_cell + .5 ) ) + pf_half;
        if ( !first && i != lastI && j != lastJ )
            pf_Mark( blocked, lastI, j );
        pf_Mark( blocked, i, j );
        first = false;
        lastI = i;
        lastJ = j;
    }
}

//! steps from a start cell to every free cell
void pf_Flood( std::vector< unsigned char > const & blocked, int start, int maxSteps, std::vector< int > & steps )
{
    steps.assign( pf_size * pf_size, pf_unreached );
    static std::vector< int > queue;
    queue.clear();
    steps[ start ] = 0;
    queue.push_back( start );
    for ( size_t q = 0; q < queue.size(); ++q )
    {
        int const index = queue[q];
        if ( steps[ index ] >= maxSteps )
            continue;
        int const i = index % pf_size, j = index / pf_size;
        for ( int d = 0; d < 4; ++d )
        {
            int const ni = i + pf_di[d], nj = j + pf_dj[d];
            if ( ni < 0 || nj < 0 || ni >= pf_size || nj >= pf_size )
                continue;
            int const next = pf_Index( ni, nj );
            if ( blocked[ next ] || steps[ next ] != pf_unreached )
                continue;
            steps[ next ] = steps[ index ] + 1;
            queue.push_back( next );
        }
    }
}

struct pf_Result
{
    double time = -1;
    std::vector< eCoord > points;
    REAL r = 0, g = 0, b = 0;
};

//! finds the route into the biggest area the cycle can claim before anyone else
void pf_Compute( gCycle const * cycle, pf_Result & result )
{
    result.points.clear();

    pf_Frame frame;
    frame.origin = cycle->PredictPosition();
    frame.ex = cycle->Direction();
    if ( frame.ex.NormSquared() <= 0 )
        return;
    frame.ex.Normalize();
    frame.ey = frame.ex.Turn( 0, 1 );

    // walls: everything outside the arena, the rim and all trails
    // reused between runs, the search runs ten times a second
    static std::vector< unsigned char > blocked;
    blocked.assign( pf_size * pf_size, 0 );
    for ( int j = 0; j < pf_size; ++j )
        for ( int i = 0; i < pf_size; ++i )
            if ( !eWallRim::IsBound( frame.World( i, j ) ) )
                blocked[ pf_Index( i, j ) ] = 1;
    for ( int w = se_rimWalls.Len() - 1; w >= 0; --w )
        pf_MarkSegment( frame, se_rimWalls[w]->EndPoint(0), se_rimWalls[w]->EndPoint(1), blocked );

    std::vector< std::pair< eCoord, eCoord > > segments;
    std::vector< gCycle const * > others;
    for ( int p = se_PlayerNetIDs.Len() - 1; p >= 0; --p )
    {
        gCycle const * other = dynamic_cast< gCycle const * >( se_PlayerNetIDs(p)->Object() );
        if ( !other )
            continue;
        segments.clear();
        other->BlockingSegments( segments );
        for ( auto const & s: segments )
            pf_MarkSegment( frame, s.first, s.second, blocked );
        if ( other != cycle && other->Alive() )
            others.push_back( other );
    }

    int const start = pf_Index( pf_half, pf_half );
    blocked[ start ] = 0;

    // when the other cycles could get to each cell
    static std::vector< REAL > enemyTime;
    enemyTime.assign( pf_size * pf_size, 1E+30 );
    static std::vector< int > steps;
    for ( gCycle const * other: others )
    {
        int oi, oj;
        if ( !frame.Cell( other->PredictPosition(), oi, oj ) )
            continue;
        int const otherStart = pf_Index( oi, oj );
        unsigned char const wasBlocked = blocked[ otherStart ];
        blocked[ otherStart ] = 0;
        pf_Flood( blocked, otherStart, pf_maxSteps, steps );
        blocked[ otherStart ] = wasBlocked;
        REAL const speed = std::max( other->Speed(), REAL( 1 ) );
        for ( size_t c = 0; c < steps.size(); ++c )
            if ( steps[c] != pf_unreached )
                enemyTime[c] = std::min( enemyTime[c], steps[c] * pf_cell / speed );
    }

    // distance of every cell to the nearest wall
    static std::vector< int > clear;
    clear.assign( pf_size * pf_size, pf_unreached );
    {
        static std::vector< int > queue;
        queue.clear();
        for ( int c = 0; c < pf_size * pf_size; ++c )
            if ( blocked[c] )
            {
                clear[c] = 0;
                queue.push_back( c );
            }
        for ( size_t q = 0; q < queue.size(); ++q )
        {
            int const index = queue[q];
            if ( clear[ index ] >= 12 )
                continue;
            int const i = index % pf_size, j = index / pf_size;
            for ( int d = 0; d < 4; ++d )
            {
                int const ni = i + pf_di[d], nj = j + pf_dj[d];
                if ( ni < 0 || nj < 0 || ni >= pf_size || nj >= pf_size )
                    continue;
                int const next = pf_Index( ni, nj );
                if ( clear[ next ] != pf_unreached )
                    continue;
                clear[ next ] = clear[ index ] + 1;
                queue.push_back( next );
            }
        }
    }

    // cheapest routes for the cycle itself: it cannot reverse, turns and wall hugging cost extra,
    // and cells someone else reaches first are risky
    REAL const speed = std::max( cycle->Speed(), REAL( 1 ) );
    int const states = pf_size * pf_size * 4;
    static std::vector< int > cost, stateSteps, parent;
    static std::vector< signed char > firstMove;
    cost.assign( states, pf_unreached );
    stateSteps.assign( states, 0 );
    parent.assign( states, -1 );
    firstMove.assign( states, -1 );

    // the step costs are small whole numbers, so a ring of buckets replaces a priority queue
    int const ring = 256;
    static std::vector< std::vector< int > > buckets( ring );
    for ( auto & bucket: buckets )
        bucket.clear();
    cost[ start * 4 ] = 0;
    buckets[0].push_back( start * 4 );
    int pending = 1;
    for ( int current = 0; pending > 0; )
    {
        std::vector< int > & bucket = buckets[ current % ring ];
        if ( bucket.empty() )
        {
            ++current;
            continue;
        }
        int const state = bucket.back();
        bucket.pop_back();
        --pending;
        if ( cost[ state ] != current || stateSteps[ state ] >= pf_maxSteps )
            continue;
        int const index = state / 4, dir = state % 4;
        int const i = index % pf_size, j = index / pf_size;
        int const turns[3] = { dir, ( dir + 1 ) % 4, ( dir + 3 ) % 4 };
        for ( int t = 0; t < 3; ++t )
        {
            int const nd = turns[t];
            int const ni = i + pf_di[nd], nj = j + pf_dj[nd];
            if ( ni < 0 || nj < 0 || ni >= pf_size || nj >= pf_size )
                continue;
            int const next = pf_Index( ni, nj );
            if ( blocked[ next ] )
                continue;
            int const nsteps = stateSteps[ state ] + 1;
            bool const safe = nsteps * pf_cell / speed < enemyTime[ next ] - .15;
            int const wallGap = clear[ next ];
            int const add = 10 + ( t ? 25 : 0 ) + ( wallGap <= 1 ? 40 : wallGap == 2 ? 15 : wallGap == 3 ? 5 : 0 ) + ( safe ? 0 : 60 );
            int const nextState = next * 4 + nd;
            if ( cost[ state ] + add < cost[ nextState ] )
            {
                cost[ nextState ] = cost[ state ] + add;
                stateSteps[ nextState ] = nsteps;
                parent[ nextState ] = state;
                firstMove[ nextState ] = state == start * 4 ? t : firstMove[ state ];
                buckets[ cost[ nextState ] % ring ].push_back( nextState );
                ++pending;
            }
        }
    }

    // the cheapest way into each cell, and how much safe space each first move leads to
    static std::vector< int > best;
    best.assign( pf_size * pf_size, -1 );
    int territory[3] = { 0, 0, 0 }, reachable[3] = { 0, 0, 0 };
    for ( int c = 0; c < pf_size * pf_size; ++c )
    {
        for ( int d = 0; d < 4; ++d )
            if ( cost[ c * 4 + d ] != pf_unreached && ( best[c] < 0 || cost[ c * 4 + d ] < cost[ best[c] ] ) )
                best[c] = c * 4 + d;
        if ( best[c] < 0 || c == start )
            continue;
        int const move = firstMove[ best[c] ];
        ++reachable[ move ];
        if ( stateSteps[ best[c] ] * pf_cell / speed < enemyTime[c] - .15 )
            ++territory[ move ];
    }
    int move = 0;
    for ( int m = 1; m < 3; ++m )
        if ( territory[m] > territory[move] || ( territory[m] == territory[move] && reachable[m] > reachable[move] ) )
            move = m;
    if ( reachable[ move ] == 0 )
        return;
    bool const useSafe = territory[ move ] > 0;

    // aim for open space a little way ahead
    int target = -1, targetScore = 0;
    for ( int c = 0; c < pf_size * pf_size; ++c )
    {
        if ( best[c] < 0 || c == start || firstMove[ best[c] ] != move )
            continue;
        int const s = stateSteps[ best[c] ];
        if ( useSafe && !( s * pf_cell / speed < enemyTime[c] - .15 ) )
            continue;
        int const score = std::min( clear[c], 10 ) * 20 + std::min( s, 30 ) * 4 - std::max( 0, s - 30 ) * 3;
        if ( target < 0 || score > targetScore )
        {
            target = c;
            targetScore = score;
        }
    }
    if ( target < 0 )
        return;

    // walk back to the cycle, keeping only the corners
    std::vector< int > cells;
    for ( int state = best[ target ]; state >= 0; state = parent[ state ] )
        cells.push_back( state / 4 );
    std::reverse( cells.begin(), cells.end() );
    for ( size_t k = 0; k < cells.size(); ++k )
    {
        bool const corner = k == 0 || k + 1 == cells.size() ||
                            ( cells[k] - cells[k - 1] ) != ( cells[k + 1] - cells[k] );
        if ( corner )
            result.points.push_back( k == 0 ? frame.origin : frame.World( cells[k] % pf_size, cells[k] / pf_size ) );
    }

    // green with plenty of safe space, through yellow to red when it gets tight
    REAL const room = REAL( useSafe ? territory[ move ] : 0 );
    REAL const danger = room >= 600 ? 0 : ( room <= 80 ? 1 : 1 - ( room - 80 ) / 520 );
    if ( danger < .5 )
    {
        REAL const f = danger * 2;
        result.r = .3 + .7 * f; result.g = 1 - .1 * f; result.b = .45 - .3 * f;
    }
    else
    {
        REAL const f = danger * 2 - 1;
        result.r = 1; result.g = .9 - .65 * f; result.b = .15;
    }
}

}

void gPathFinder::Render( gCycle const * cycle )
{
    if ( !Active() || !cycle || !cycle->Alive() )
        return;
    ePlayerNetID const * player = cycle->Player();
    if ( !player || player->Owner() != sn_myNetID || player->pID < 0 )
        return;

    static std::map< gCycle const *, pf_Result > results;
    if ( results.size() > 16 && results.find( cycle ) == results.end() )
        results.clear();
    pf_Result & result = results[ cycle ];
    double const now = tSysTimeFloat();
    if ( result.time < 0 || now - result.time > .1 || now < result.time )
    {
        pf_Compute( cycle, result );
        result.time = now;
    }
    if ( result.points.size() < 2 )
        return;

    // start the line at the cycle as long as it still drives along the first stretch
    eCoord const position = cycle->PredictPosition();
    eCoord const along = result.points[1] - result.points[0];
    eCoord const done = position - result.points[0];
    REAL const forward = along.x * done.x + along.y * done.y;
    REAL const sideways = along.x * done.y - along.y * done.x;
    if ( forward > 0 && done.NormSquared() < along.NormSquared() && fabs( sideways ) < .3 * sqrt( along.NormSquared() ) )
        result.points[0] = position;

    RenderEnd();
    glDisable( GL_TEXTURE_2D );
    glLineWidth( 4 );
    Color( result.r, result.g, result.b, .9 );
    BeginLineStrip();
    for ( eCoord const & p: result.points )
        Vertex( p.x, p.y, .08 );
    RenderEnd();
    glLineWidth( 1 );
}

#endif
