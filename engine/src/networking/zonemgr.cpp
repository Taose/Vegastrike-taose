#include "networking/lowlevel/netbuffer.h"
#include "networking/lowlevel/vsnet_debug.h"
#include "universe_generic.h"
#include "universe_util.h"
#include "star_system_generic.h"
#include "cmd/unit_generic.h"
#include "cmd/unit_factory.h"
#include "gfx/cockpit_generic.h"
#include "networking/lowlevel/packet.h"
#include "networking/savenet_util.h"
#include "networking/zonemgr.h"
#include "vs_globals.h"
#include "endianness.h"
#include <assert.h>
#include "networking/netserver.h"
#include "vs_random.h"

extern StarSystem * GetLoadedStarSystem( const char *system );

unsigned short ZoneInfo::next_zonenum = 0;

ZoneMgr::ZoneMgr()
{}

/*
 ***********************************************************************************************
 **** addZone                                                                               ****
 ***********************************************************************************************
 */

void ZoneMgr::addSystem( string &sysname, string &system )
{
    Systems.insert( sysname, system );
}

string ZoneMgr::getSystem( string &name )
{
    return Systems.get( name );
}
/*
 ***********************************************************************************************
 **** getZoneBuffer : returns a buffer containing zone info                                ****
 ***********************************************************************************************
 */

void displayUnitInfo( Unit *un, const string callsign, const char *type )
{
    cout<<type;
    if (!un)
    {
        cout<<"\'"<<callsign<<"\' (dead)"<<endl;
    }
    else
    {
        {
            char tmp[10];
            sprintf( tmp, "% 5d ", un->GetSerial() );
            cout<<tmp;
        }
        cout<<UnitUtil::getFactionName( un )<<" "<<un->getFullname()<<" ("<<un->name<<")";
        Flightgroup *fg = un->getFlightgroup();
        if (fg) cout<<", \'"<<fg->name<<"\' "<<un->getFgSubnumber();
        if ( !callsign.empty() && (!fg || callsign != fg->name) )
            cout<<", callsign \'"<<callsign<<"\'";
        cout<<endl;
    }
}

//Send one by one a CMD_ADDLCIENT to the client for every ship in the star system we enter
void ZoneMgr::getZoneBuffer( unsigned short zoneid, NetBuffer &netbuf )
{
    LI     k;
    int    nbclients = 0;
    Packet packet2;
    string savestr, xmlstr;

    //Loop through client in the same zone to send their current_state and save and xml to "clt"
    std::set< ObjSerial >activeObjects;
    ZoneInfo *zi = GetZoneInfo( zoneid );
    if (zi == NULL)
    {
        COUT<<"\t>>> WARNING: Did not send info about "<<nbclients<<" other ships because of empty (inconsistent?) zone"<<endl;
    }
    else
    {
        ClientList *lst = &zi->zone_list;
        for (k = lst->begin(); k != lst->end(); k++)
        {
            ClientPtr kp( *k );
            //Test if *k is the same as clt in which case we don't need to send info
            if (true)
            {
                Unit *un = kp->game_unit.GetUnit();
                if (un->hull < 0 || !un)
                    continue;
                assert( un->GetSerial() != 0 );
                SaveNetUtil::GetSaveStrings( kp, savestr, xmlstr, false );
                //Add the ClientState at the beginning of the buffer -> NO THIS IS IN THE SAVE !!

                //Add the callsign and save and xml strings
                netbuf.addChar( ZoneMgr::AddClient );
                netbuf.addSerial( un->GetSerial() );
                netbuf.addString( kp->callsign );
                netbuf.addString( savestr );
                netbuf.addString( xmlstr );
                netbuf.addTransformation( kp->game_unit.GetUnit()->curr_physical_state );
                activeObjects.insert( un->GetSerial() );
                nbclients++;
            }
        }
        {
            Unit *un;
            for (un_iter ui = zi->star_system->getUnitList().createIterator();
                    (un = *ui) != NULL;
                    ++ui)
            {
                //NETFIXME Asteroids are disabled for now!
                if (un->hull < 0 || un->isUnit() == ASTEROIDPTR)                  //no point sending a zombie.
                    continue;
                ObjSerial ser = un->GetSerial();
                assert( ser != 0 );
                if ( activeObjects.find( ser ) == activeObjects.end() )
                {
                    UnitFactory::addBuffer( netbuf, un, false );
                    activeObjects.insert( un->GetSerial() );
                    nbclients++;
                }
            }
            netbuf.addChar( ZoneMgr::End );
        }
        COUT<<"\t>>> GOT INFO ABOUT "<<nbclients<<" OTHER SHIPS"<<endl;
    }
}

StarSystem* ZoneMgr::addZone( string starsys )
{
    StarSystem *sts = NULL;
    string sysfile  = starsys+".system";
    if ( ( sts = GetLoadedStarSystem( sysfile.c_str() ) ) )
    {
        cerr<<"--== STAR SYSTEM "<<starsys<<" ALREADY EXISTS ==--"<<endl;
        return sts;
    }
    //Add a network zone (StarSystem equivalent) and create the new StarSystem
    //StarSystem is not loaded so we generate it
    COUT<<">>> ADDING A NEW ZONE = "<<starsys<<" - ZONE # = "<<_Universe->star_system.size()<<endl;
    COUT<<"--== STAR SYSTEM NOT FOUND - GENERATING ==--"<<endl;
    //Generate the StarSystem
    _Universe->netLock( true );
    string starsysfile = starsys+".system";
    //ComputeSystemSerials now done in Universe::Generate1

    sts = _Universe->GenerateStarSystem( starsysfile.c_str(), "", Vector( 0, 0, 0 ) );
    bool newSystem = true;
    unsigned int i;
    for (i = 0; i < _Universe->star_system.size(); i++)
        if (_Universe->star_system[i] == sts)
        {
            newSystem = false;
            break;
        }
    if (newSystem)
        printf( "newSystem is somehow true!!!" );

    unsigned short zone = ZoneInfo::next_zonenum;
    zones.insert( ZoneMap::value_type( zone, ZoneInfo( sts ) ) );
    sts->SetZone( zone );
    _Universe->netLock( false );
    return sts;
}

/*
 ***********************************************************************************************
 **** GetZone                                                                               ****
 ***********************************************************************************************
 */

//Return the client list that are in the zone # serial
ZoneInfo* ZoneMgr::GetZoneInfo( int serial )
{
    ZoneMap::iterator iter = zones.find( serial );
    if ( iter == zones.end() )
        return NULL;
    else
        return &( (*iter).second );
}

//Return the client list that are in the zone # serial
ClientList* ZoneMgr::GetZone( int serial )
{
    ZoneInfo *zi = GetZoneInfo( serial );
    if (zi)
        return &zi->zone_list;
    else
        return NULL;
}

/*
 ***********************************************************************************************
 **** addUnit                                                                               ****
 ***********************************************************************************************
 */


/*
 ***********************************************************************************************
 **** removeUnit                                                                            ****
 ***********************************************************************************************
 */

//Returns NULL if no corresponding Unit was found
Unit* ZoneMgr::getUnit( ObjSerial unserial, unsigned short zone )
{
    ZoneInfo *zi = GetZoneInfo( zone );
    if (!zi) return NULL;
    Unit     *un = NULL;
    //Clients not ingame are removed from the drawList so it is ok not to test that
    for (un_iter iter = ( zi->getSystem()->getUnitList() ).createIterator(); (un = *iter) != NULL; ++iter)
        if (un->GetSerial() == unserial)
            break;
    return un;
}

/*
 ***********************************************************************************************
 **** addClient                                                                             ****
 ***********************************************************************************************
 */

StarSystem* ZoneMgr::addClient( ClientPtr cltw, string starsys, unsigned short &num_zone )
{
    //Remove the client from old starsystem if needed and add it in the new one
    StarSystem *sts = NULL;

    sts = this->addZone( starsys );

    //Get the index of the existing star_system as it represents the zone number
    num_zone = sts->GetZone();

    COUT<<">> ADDING CLIENT IN ZONE # "<<num_zone<<endl;
    //Adds the client in the zone

    ZoneInfo   *zi  = GetZoneInfo( num_zone );
    ClientList *lst = &zi->zone_list;

    lst->push_back( cltw );
    ClientPtr   clt( cltw );
    zi->zone_clients++;
    cerr<<zi->zone_clients<<" clients now in zone "<<num_zone<<endl;

    //QVector safevec;
    Unit *addun = clt->game_unit.GetUnit();
    if (addun)
    {
        _Universe->netLock( true );
        sts->AddUnit( addun );
        displayUnitInfo( addun, clt->callsign, "ENTERING NEW CLIENT: " );
        _Universe->netLock( false );
    }
    else
    {
        cerr<<"dead client attempted to be added to system: refusing\n";
    }
    return sts;
}

/*
 ***********************************************************************************************
 **** removeClient                                                                          ****
 ***********************************************************************************************
 */

//Remove a client from its zone
void ZoneMgr::removeClient( ClientPtr clt )
{
    StarSystem *sts;
    Unit *un = clt->game_unit.GetUnit();
    int   zonenum = -1;

    for (ZoneMap::iterator iter = zones.begin(); iter != zones.end(); ++iter)
    {
        bool found = false;
        ClientList *lst = &( (*iter).second.zone_list );
        for (ClientList::iterator q = lst->begin(); q != lst->end();)
        {
            ClientPtr cwp = *q;
            ClientPtr ocwp( clt );
            if ( ( !(cwp < ocwp) ) && !(ocwp < cwp) )
            {
                q       = lst->erase( q );
                zonenum = (*iter).first;
                found   = true;
                break;
            }
            else
            {
                ++q;
            }
        }
        if (found)
            break;
    }
    displayUnitInfo( un, clt->callsign, " *** REMOVING client: " );
    if (zonenum < 0)
    {
        cerr<<"Client "<<clt->callsign<<" not found in any zone when attempting to remove it"<<endl;
        //NETFIXME CRASH

        return;
    }
    ZoneInfo *zi = GetZoneInfo( zonenum );
    zi->zone_clients--;
    cerr<<zi->zone_clients<<" clients left in zone "<<zonenum<<endl;
    if (!un)
        return;
    sts = zi->star_system;
    if (un->GetHull() < 0)
        un->Kill( true, true );
    else
        sts->RemoveUnit( un );
    clt->ingame = false;
    if (clt->loginstate > Client::LOGGEDIN)
        clt->loginstate = Client::LOGGEDIN;
    //SHIP MAY NOT HAVE BEEN KILLED BUT JUST CHANGED TO ANOTHER STAR SYSTEM -> NO KILL

}

/*
 ***********************************************************************************************
 **** broadcast : broadcast a packet in a zone                                              ****
 ***********************************************************************************************
 */

//Broadcast a packet to a client's zone clients
void ZoneMgr::broadcast( ClientPtr fromcltw, Packet *pckt, bool isTcp, unsigned short minver, unsigned short maxver )
{
    ClientPtr fromclt( fromcltw );
    Unit     *un = fromclt->game_unit.GetUnit();
    if ( !un || !un->getStarSystem() )
    {
        cerr<<"Trying to broadcast information with dead client unit"<<pckt->getCommand()<<endl;
        return;
    }
    unsigned short zonenum = un->getStarSystem()->GetZone();
    ClientList    *lst     = GetZone( zonenum );
    if (lst == NULL)
    {
        cerr<<"Trying to send update to nonexistant zone "<<zonenum<<pckt->getCommand()<<endl;
        return;
    }
    for (LI i = lst->begin(); i != lst->end(); i++)
    {
        ClientPtr clt( *i );
        Unit     *un2 = clt->game_unit.GetUnit();
        //Broadcast to other clients
        if ( (isTcp || clt->ingame) && clt->netversion >= minver && clt->netversion <= maxver
                && ( (un2 == NULL) || ( un->GetSerial() != un2->GetSerial() ) ) )
        {
            COUT<<"BROADCASTING "<<pckt->getCommand()
                <<" to client #";
            if (un2)
                cout<<un2->GetSerial();
            else
                cout<<"dead";
            cout<<endl;
            if (isTcp)
                pckt->bc_send( clt->cltadr, clt->tcp_sock );
            else
                pckt->bc_send( clt->cltudpadr, *clt->lossy_socket );
        }
    }
}

/*
 ***********************************************************************************************
 **** broadcast : broadcast a packet in a zone                                              ****
 ***********************************************************************************************
 */

//Broadcast a packet to a zone clients with its serial as argument
void ZoneMgr::broadcast( int zone, ObjSerial serial, Packet *pckt, bool isTcp, unsigned short minver, unsigned short maxver )
{
    ClientList *lst = GetZone( zone );
    if (lst == NULL) return;
    for (LI i = lst->begin(); i != lst->end(); i++)
    {
        ClientPtr clt( *i );
        //Broadcast to all clients including the one who did a request
        //Allow packets to non-ingame clients to get lost if requested UDP.
        if ( (isTcp || clt->ingame) && clt->netversion >= minver && clt->netversion <= maxver)
        {

            if (isTcp)
                pckt->bc_send( clt->cltadr, clt->tcp_sock );
            else
                pckt->bc_send( clt->cltudpadr, *clt->lossy_socket );
        }
    }
}

//Broadcast a packet to a zone clients with its serial as argument but not to the originating client
void ZoneMgr::broadcastNoSelf( int zone, ObjSerial serial, Packet *pckt, bool isTcp )
{
    ClientList *lst = GetZone( zone );
    if (lst == NULL) return;
    for (LI i = lst->begin(); i != lst->end(); i++)
    {
        ClientPtr clt( *i );
        Unit     *broadcastTo = clt->game_unit.GetUnit();
        //Broadcast to all clients including the one who did a request
        if ( (isTcp || clt->ingame) && ( (!broadcastTo) || broadcastTo->GetSerial() != serial ) )
        {
            if (isTcp)
                pckt->bc_send( clt->cltadr, clt->tcp_sock );
            else
                pckt->bc_send( clt->cltudpadr, *clt->lossy_socket );
        }
    }
}

/*
 ***********************************************************************************************
 **** broadcastSample : broadcast sound sample to players in the zone and on same frequency ****
 ***********************************************************************************************
 */

//Broadcast a packet to a zone clients with its serial as argument
//NETFIXME: Should this be always TCP?
void ZoneMgr::broadcastSample( int zone, ObjSerial serial, Packet *pckt, float frequency )
{
    ClientList *lst = GetZone( zone );
    Unit *un;
    if (lst == NULL) return;
    for (LI i = lst->begin(); i != lst->end(); i++)
    {
        ClientPtr clt( *i );
        un = clt->game_unit.GetUnit();
        //Broadcast to all clients excluding the one who did a request and
        //excluding those who are listening on a different frequency, those who aren't communicating
        //and those who don't have PortAudio support
        if ( clt->ingame && clt->comm_freq != -1 && clt->portaudio && clt->comm_freq == frequency
                && ( (!un) || (un->GetSerial() != serial) ) )
            pckt->bc_send( clt->cltadr, clt->tcp_sock );
    }
}

/*
 ***********************************************************************************************
 **** broadcastText : broadcast a text message to players in the zone and on same frequency ****
 ***********************************************************************************************
 */

//Broadcast a packet to a zone clients with its serial as argument
//Always TCP.
void ZoneMgr::broadcastText( int zone, ObjSerial serial, Packet *pckt, float frequency )
{
    ClientList *lst = GetZone( zone );
    Unit *un;
    if (lst == NULL) return;
    for (LI i = lst->begin(); i != lst->end(); i++)
    {
        ClientPtr clt( *i );
        un = clt->game_unit.GetUnit();
        //Broadcast to all clients excluding the one who did a request and
        //excluding those who are listening on a different frequency, those who aren't communicating
        //and those who don't have PortAudio support
        if ( clt->ingame && clt->comm_freq != -1 && ( (!un) || (un->GetSerial() != serial) ) )
            pckt->bc_send( clt->cltadr, clt->tcp_sock );
    }
}

/*
 ***********************************************************************************************
 **** broadcastSnapshots                                                                    ****
 ***********************************************************************************************
 */
#include "lin_time.h"
//Broadcast all positions
//This function sends interpolated and predicted positions based on the "semi-ping" between the sender clients and the server
//the receiver client will only have to interpolate and predict on his own "semi-ping" value
//Always UDP.
//NETFIXME:  May be too big for UDP if there are too many ships.  We may want to split these up into reasonable sizes.
void ZoneMgr::broadcastSnapshots( bool update_planets )
{
    LI k;
    //Loop for all systems/zones
    for (ZoneMap::iterator iter = zones.begin(); iter != zones.end(); ++iter)
    {
        int totalunits = 0;
        //Check if system contains player(s)
        if (true)
        {
            ZoneInfo *zi = &(*iter).second;
            //Loop for all the zone's clients
            for (k = zi->zone_list.begin(); k != zi->zone_list.end(); ++k)
            {
                totalunits = 0;
                ClientPtr cltk( *k );
                //If that client is ingame we send to it position info
                un_iter   iter = ( zi->star_system->getUnitList() ).createIterator();
                while (*iter)
                {
                    int       nbunits = 0;
                    Packet    pckt;
                    NetBuffer netbuf;
                    cltk->versionBuf( netbuf );

                    //This also means clients will receive info about themselves
                    //which they should ignore or take into account sometimes
                    //(according to their ping value)
                    Unit *unit;

                    //Add the client we send snapshot to its own deltatime (semi-ping)
                    netbuf.addFloat( cltk->getDeltatime() );
                    //Clients not ingame are removed from the drawList so it is ok not to test that
                    for (; (unit = *iter) != NULL; ++iter)
                    {
                        if ( netbuf.getOffset() > (504-108) && (&cltk->tcp_sock != cltk->lossy_socket) )
                            //Don't want to go over MTU. 512 is UDP maximum and you lose 8 for header.
                            break;
                        ++totalunits;
                        if ( unit->GetSerial() != 0 && unit != cltk->game_unit.GetUnit() )
                        {
                            //Only send unit that ate UNITPTR and PLANETPTR+NEBULAPTR if update_planets
                            if ( (unit->isUnit() == UNITPTR || unit->isUnit() == ASTEROIDPTR || unit->isUnit()
                                    == MISSILEPTR)
                                    || ( (unit->isUnit() == PLANETPTR || unit->isUnit() == NEBULAPTR) && update_planets ) )
                            {
                                ClientState cs( unit );

                                bool added = addPosition( cltk, netbuf, unit, cs );
                                if (added)
                                    ++nbunits;
                            }
                        }
                    }
                    //Send snapshot to client k
                    if (nbunits > 0)
                    {
                        pckt.send( CMD_SNAPSHOT, nbunits,
                                   netbuf.getData(), netbuf.getDataLength(),
                                   SENDANDFORGET, &(cltk->cltudpadr), *cltk->lossy_socket,
                                   __FILE__, PSEUDO__LINE__( 337 ) );
                    }
                }
                {
                    Unit *unit;
                    if ( ( unit = cltk->game_unit.GetUnit() ) != NULL )
                    {
                        Unit *targ = unit->Target();
                        if (targ)
                        {
                            double range = unit->GetComputerData().radar.maxrange;
                            unit->GetComputerData().radar.maxrange *= 1.5;                             //generous
                            if ( !unit->InRange( targ, false ) )
                                unit->Target( NULL );
                            unit->GetComputerData().radar.maxrange = range;
                        }
                    }
                }
            }
            Unit *unit;
            //Clients not ingame are removed from the drawList so it is ok not to test that
            for (un_iter iter = ( zi->star_system->getUnitList() ).createIterator(); (unit = *iter) != NULL; ++iter)
            {
                clsptr typ = unit->isUnit();
                if ( (typ == UNITPTR || typ == MISSILEPTR) && vsrandom.genrand_int31()%(totalunits*10+1) == 1 )
                    unit->damages = 0xffff;
                if (unit->damages)
                {
                    for (k = zi->zone_list.begin(); k != zi->zone_list.end(); k++)
                    {
                        //Each damage sent in a separate UDP packet.
                        NetBuffer netbuf;
                        ClientPtr cltk( *k );
                        cltk->versionBuf( netbuf );
                        netbuf.addFloat( cltk->getDeltatime() );
                        netbuf.addChar( ZoneMgr::DamageUpdate );
                        netbuf.addShort( unit->GetSerial() );
                        addDamage( netbuf, unit );
                        Packet pckt;
                        pckt.send( CMD_SNAPSHOT, 1,
                                   netbuf.getData(), netbuf.getDataLength(),
                                   SENDANDFORGET, &(cltk->cltudpadr), *cltk->lossy_socket,
                                   __FILE__, PSEUDO__LINE__( 522 ) );
                    }
                }
                unit->damages = Unit::NO_DAMAGE;
            }
        }
    }
}
bool Nearby( ClientPtr clt, Unit *un )
{
    Unit *parent = clt->game_unit.GetUnit();
    if (parent)
    {
        if (un == parent) return true;
        double mm;
        if ( !parent->InRange( un, mm, false, false, true ) )
        {
            static double always_send_range =
                XMLSupport::parse_float( vs_config->getVariable( "server", "visible_send_range", "1000" ) );
            if (parent->computer.radar.maxrange >= always_send_range)
                return false;
            {
                QVector dist = ( un->Position()-parent->Position() );
                if (dist.Dot( dist ) >= always_send_range*always_send_range)
                    return false;
            }
        }
        static double maxrange = XMLSupport::parse_float( vs_config->getVariable( "server", "max_send_range", "1e21" ) );
        if (mm > maxrange)
            return false;
    }
    else
    {
        return false;
    }
    return true;
}
bool ZoneMgr::addPosition( ClientPtr client, NetBuffer &netbuf, Unit *un, ClientState &un_cs )
{
    bool dodamage = false;     //Now done in separate packets in broadcastSnapshots.
    //This test may be wrong for server side units -> may cause more traffic than needed
    if (1)
    {
        //Unit 'un' can see Unit 'iter'
        //For now only check if the 'iter' client is in front of Unit 'un')
        if (1)
        {
            //Test if client 'l' is far away from client 'k' = test radius/distance<=X
            //So we can send only position
            //Here distance should never be 0
            //ratio = radius/distance;
            if (un->damages || Nearby( client, un ) /* ratio > XX client not too far */)
            {
                unsigned char type = ZoneMgr::FullUpdate;
                if (dodamage && un->damages)
                    type |= ZoneMgr::DamageUpdate;
                //Always send spec info for now...
                if (client->netversion > 4960)
                    if (un_cs.getSpecMult() >= 1.0)
                        type |= ZoneMgr::SPECUpdate;

                //Mark as position+orientation+velocity update
                netbuf.addChar( type );
                netbuf.addShort( un->GetSerial() );
                //Put the current client state in
                netbuf.addClientState( un_cs );
                //Throw in some other cheap but useful info.
                netbuf.addFloat( un->energy );
                //Increment the number of clients we send full info about
                if (type&ZoneMgr::SPECUpdate)
                {
                    netbuf.addFloat( un_cs.getSpecRamp() );
                    netbuf.addFloat( un_cs.getSpecMult() );
                }
                if (dodamage && un->damages)
                    addDamage( netbuf, un );
            }
            //Here find a condition for which sending only position would be enough
            else if (0)
            {
                //Mark as position update only
                netbuf.addChar( ZoneMgr::PosUpdate );
                //Add the client serial
                netbuf.addShort( un->GetSerial() );
                netbuf.addQVector( un_cs.getPosition() );
                //Increment the number of clients we send limited info about
            }
            else
            {
                static int i = 0;
                if ( (i++)%8192 == 0 )
                    COUT<<"PosUpdate not sent (too far away) "<<un->name<<" #"<<un->GetSerial()<<endl;
                return false;
            }
        }
        else
        {
            COUT<<"Client counted but not sent because of distance!"<<endl;
            return false;
        }
    }
    else
    {
        COUT<<"Client counted but not sent because of position/orientation test!"<<endl;
        return false;
    }
    return true;
}

/*
 ***********************************************************************************************
 **** broadcastDamage                                                                       ****
 ***********************************************************************************************
 */

//Broadcast all damages
//NETFIXME:  May be too big for UDP.
void ZoneMgr::broadcastDamage()
{
    LI k;
    NetBuffer netbuf;
    //Loop for all systems/zones
    for (ZoneMap::iterator iter = zones.begin(); iter != zones.end(); ++iter)
    {
        int totalunits = 0;
        //Check if system is non-empty
        if (true)
        {
            ZoneInfo *zi = &(*iter).second;
            /************* Second method : send independently to each client a buffer of its zone  ***************/
            //It allows to check (for a given client) if other clients are far away (so we can only
            //send position, not orientation and stuff) and if other clients are visible to the given
            //client.
            Packet pckt;
            //Loop for all the zone's clients
            for (k = zi->zone_list.begin(); k != zi->zone_list.end(); k++)
            {
                int nbunits = 0;
                totalunits = 0;
                ClientPtr cp( *k );
                cp->versionBuf( netbuf );
                if (cp->ingame)
                {
                    Unit *unit;
                    //Clients not ingame are removed from the drawList so it is ok not to test that
                    for (un_iter iter = ( zi->star_system->getUnitList() ).createIterator(); (unit = *iter) != NULL; ++iter)
                        if (unit->GetSerial() != 0)
                        {
                            if (unit->damages)
                            {
                                //Add the client serial
                                netbuf.addSerial( unit->GetSerial() );
                                addDamage( netbuf, unit );
                                ++nbunits;
                            }
                            ++totalunits;
                        }
                    //NETFIXME: Should damage updates be UDP or TCP?
                    //Send snapshot to client k
                    if (netbuf.getDataLength() > 0)
                    {
                        pckt.send( CMD_SNAPDAMAGE, /*nbclients+*/ nbunits,
                                   netbuf.getData(), netbuf.getDataLength(),
                                   SENDANDFORGET, &(cp->cltudpadr), *cp->lossy_socket,
                                   __FILE__, PSEUDO__LINE__( 442 ) );
                    }
                }
            }
            {
                Unit *unit;
                //Clients not ingame are removed from the drawList so it is ok not to test that
                for (un_iter iter = ( zi->star_system->getUnitList() ).createIterator(); (unit = *iter) != NULL; ++iter)
                {
                    unit->damages = Unit::NO_DAMAGE;
                    clsptr typ = unit->isUnit();
                    if ( (typ == UNITPTR || typ == MISSILEPTR) && vsrandom.genrand_int31()%(totalunits*10+1) == 1 )
                        unit->damages = 0xffff&(~Unit::COMPUTER_DAMAGED);
                }
            }
        }
    }
}

void ZoneMgr::addDamage( NetBuffer &netbuf, Unit *un )
{
    unsigned int   it = 0;

    //Add the damage flag
    unsigned short damages = un->damages;
    if ( (damages&Unit::COMPUTER_DAMAGED) && (damages&Unit::LIMITS_DAMAGED) )
        damages &= (~Unit::MOUNT_DAMAGED);
    if ( netbuf.version() < 4500 && (damages&Unit::LIMITS_DAMAGED) )
        //Makes it too big with a bunch of 32-bit floats.
        damages &= (~Unit::COMPUTER_DAMAGED);
    netbuf.addShort( damages );
    //Put the altered stucts after the damage enum flag
    if (damages&Unit::SHIELD_DAMAGED)
        netbuf.addShield( un->shield );
    if (damages&Unit::ARMOR_DAMAGED)
    {
        netbuf.addArmor( un->armor );
        netbuf.addFloat( un->hull );
    }
    if (damages&Unit::COMPUTER_DAMAGED)
    {
        netbuf.addChar( un->computer.itts );
        netbuf.addChar( un->computer.radar.UseFriendFoe() ? 1 : 0 );
        netbuf.addFloat( un->limits.retro );
        netbuf.addFloat( un->computer.radar.maxcone );
        netbuf.addFloat( un->computer.radar.lockcone );
        netbuf.addFloat( un->computer.radar.trackingcone );
        netbuf.addFloat( un->computer.radar.maxrange );
        unsigned char c = 1+UnitImages< void >::NUMGAUGES+MAXVDUS;
        netbuf.addChar( c );
        for (it = 0; it < c; it++)
            netbuf.addFloat8( un->pImage->cockpit_damage[it] );
    }
    if (damages&Unit::MOUNT_DAMAGED)
    {
        netbuf.addShort( un->pImage->ecm );
        netbuf.addShort( un->mounts.size() );
        for (it = 0; it < un->mounts.size(); it++)
        {
            netbuf.addChar( (char) un->mounts[it].status );

            netbuf.addInt32( un->mounts[it].ammo );
            netbuf.addFloat( un->mounts[it].time_to_lock );
            netbuf.addShort( un->mounts[it].size );
        }
    }
    if (damages&Unit::CARGOFUEL_DAMAGED)
    {
        netbuf.addFloat( un->FuelData() );
        netbuf.addFloat( un->AfterburnData() );
        netbuf.addFloat( un->pImage->CargoVolume );
        netbuf.addFloat( un->pImage->UpgradeVolume );
    }
    if (damages&Unit::JUMP_DAMAGED)
    {
        netbuf.addChar( un->shield.leak );
        netbuf.addFloat( un->shield.recharge );
        netbuf.addFloat( un->EnergyRechargeData() );
        netbuf.addFloat( un->MaxEnergyData() );
        netbuf.addFloat( un->jump.energy );                //NETFIXME: Add insys energy too?
        netbuf.addChar( un->jump.damage );
        netbuf.addChar( un->pImage->repair_droid );
    }
    if (damages&Unit::CLOAK_DAMAGED)
    {
        netbuf.addInt32( un->cloaking );
        netbuf.addFloat( un->pImage->cloakenergy );
        netbuf.addInt32( un->cloakmin );
        netbuf.addInt32( un->pImage->cloakrate );
    }
    if (damages&Unit::LIMITS_DAMAGED)
    {
        netbuf.addFloat( un->computer.max_pitch_down );
        netbuf.addFloat( un->computer.max_pitch_up );
        netbuf.addFloat( un->computer.max_yaw_left );
        netbuf.addFloat( un->computer.max_yaw_right );
        netbuf.addFloat( un->computer.max_roll_left );
        netbuf.addFloat( un->computer.max_roll_right );
        netbuf.addFloat( un->limits.roll );
        netbuf.addFloat( un->limits.yaw );
        netbuf.addFloat( un->limits.pitch );
        netbuf.addFloat( un->limits.lateral );
    }
}

/*
 ***********************************************************************************************
 ****  isVisible                                                                            ****
 ***********************************************************************************************
 */

double ZoneMgr::isVisible( Quaternion orient, QVector src_pos, QVector tar_pos )
{
    double  dotp = 0;
    Matrix  m;

    orient.to_matrix( m );
    QVector src_tar( m.getR() );

    src_tar = tar_pos-src_pos;
    dotp    = DotProduct( src_tar, (QVector) orient.v );

    return dotp;
}

void ZoneInfo::display()
{
    cout<<star_system->getFileName()<<"(zone "<<zonenum<<")"<<endl;
    for (ClientList::iterator ci = zone_list.begin(); ci != zone_list.end(); ++ci)
    {
        Client *clt = (*ci).get();
        if (!clt) continue;
        Unit   *un  = clt->game_unit.GetUnit();
        if (un && _Universe->isPlayerStarship( un ) == NULL)
            displayUnitInfo( un, clt->callsign, " CltNPC " );
    }
    un_iter iter = ( star_system->getUnitList() ).createIterator();
    while (Unit*un = *iter)
    {
        char   name[15] = "    NPC ";
        string callsign;
        int    cp;
        if ( ( cp = _Universe->whichPlayerStarship( un ) ) != -1 )
        {
            sprintf( name, "*Plr% 3d ", cp );
            callsign = _Universe->AccessCockpit( cp )->savegame->GetCallsign();
        }
        displayUnitInfo( un, callsign, name );
        ++iter;
    }
}
void ZoneMgr::displayNPCs()
{
    cout<<"----- Active Systems:"<<endl;
    for (ZoneMap::iterator iter = zones.begin(); iter != zones.end(); ++iter)
    {
        ZoneInfo   *zi = &( (*iter).second );
        StarSystem *ss = zi->star_system;
        for (unsigned int i = 0; i < _Universe->numPlayers(); ++i)
        {
            Cockpit *cp = _Universe->AccessCockpit( i );
            if (cp->activeStarSystem != ss)
                continue;
            char     name[15];
            if ( !cp->GetParent() )
            {
                sprintf( name, "*Plr% 3d ", i );
                displayUnitInfo( NULL, cp->savegame->GetCallsign(), name );
            }
        }
        zi->display();
    }
}
/*
 ***********************************************************************************************
 ****  displayStats                                                                         ****
 ***********************************************************************************************
 */

void ZoneMgr::displayStats()
{
    cout<<"\tStar system stats"<<endl;
    cout<<"\t-----------------"<<endl;
    for (ZoneMap::const_iterator iter = zones.begin(); iter != zones.end(); ++iter)
    {
        const ZoneInfo *zi = &( (*iter).second );
        cout<<"\t\tStar system "<<(zi->zonenum)<<" = "<<zi->star_system->getName()<<endl;
        cout<<"\t\t\tNumber of clients :\t"<<zi->zone_clients<<endl;
    }
}

/*
 ***********************************************************************************************
 ****  displayMemory                                                                        ****
 ***********************************************************************************************
 */

int ZoneMgr::displayMemory()
{
   return -1;
}

std::string ZoneMgr::Systems::insert( std::string sysname, std::string system )
{
    if (sysname != "" && system != "")
        _map.insert( SystemPair( sysname, system ) );
    return sysname;
}

std::string ZoneMgr::Systems::get( std::string sysname )
{
    SystemIt it = _map.find( sysname );
    if ( it == _map.end() ) return string( "" );
    return it->second;
}

bool ZoneMgr::Systems::remove( std::string sysname )
{
    size_t s = _map.erase( sysname );
    if (s == 0) return false;
    return true;
}

