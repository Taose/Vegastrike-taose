/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

/*
 *  NetClient - Network Client Interface
 * - written by Stephane Vaxelaire <svax@free.fr>
 */
#include "config.h"
#include "networking/netclient.h"
#include "networking/lowlevel/netui.h"

#include <iostream>
#include <stdio.h>
#if !defined (WIN32)
#include <unistd.h>
#endif

#include "gfx/background.h" //Background::BackgroundCache

#include "vs_globals.h"
#include "endianness.h"
#include "cmd/unit_generic.h"
#include "cmd/unit_util.h"
#include "cmd/unit_const_cache.h"
#include "configxml.h"
#include "networking/client.h"
#include "networking/const.h"
#include "networking/lowlevel/packet.h"
#include "universe_util.h"
#include "cmd/unit_factory.h"
#include "gfx/matrix.h"
#include "load_mission.h"
#include "lin_time.h"
#include "vsfilesystem.h"
#include "cmd/role_bitmask.h"
#include "cmd/base_util.h"
#include "gfx/cockpit_generic.h"
#include "savenet_util.h"
#include "save_util.h"

#include "cmd/pilot.h"
#include "cmd/ai/communication.h"
#include "cmd/ai/order.h"

#include "networking/lowlevel/vsnet_clientstate.h"
#include "networking/lowlevel/vsnet_debug.h"
#include "networking/lowlevel/vsnet_dloadmgr.h"
#include "networking/lowlevel/vsnet_notify.h"
#include "vegastrike.h"
#include "networking/client.h"
#include "networking/lowlevel/netbuffer.h"
#include "networking/networkcomm.h"
#include "posh.h"
#include "networking/prediction.h"
#include "fileutil.h"
#include "faction_generic.h"

#include "netversion.h"
ObjSerial CLIENT_NETVERSION = NETWORK_VERSION;

using std::cout;
using std::endl;
using std::cin;

double NETWORK_ATOM;

extern Hashtable< std::string, StarSystem, 127 >star_system_table;
typedef vector< Client* >::iterator   VC;
typedef vector< ObjSerial >::iterator ObjI;

extern const Unit * getUnitFromUpgradeName( const string &upgradeName, int myUnitFaction = 0 );
extern int GetModeFromName( const char* );  //1=add, 2=mult, 0=neither.
static const string LOAD_FAILED = "LOAD_FAILED";
extern Cargo * GetMasterPartList( const char *input_buffer );
extern bool isWeapon( std::string name );

/*
 ************************************************************
 **** Tool functions                                      ***
 ************************************************************
 */

vector< ObjSerial >localSerials;
bool isLocalSerial( ObjSerial sernum )
{
    bool ret = false;
    for (ObjI i = localSerials.begin(); !ret && i != localSerials.end(); i++)
        if ( sernum == (*i) )
            ret = true;

    return ret;
}

Unit * getNetworkUnit( ObjSerial cserial )
{
    for (unsigned int i = 0; i < _Universe->numPlayers(); i++)
        if (Network[i].getUnit()->GetSerial() == cserial)
            return Network[i].getUnit();
    return NULL;
}
void NetClient::Reinitialize()
{
    deltatime = 0;
    game_unit = NULL;
    latest_timestamp = 0;
    keeprun   = 1;
    cur_time  = getNewTime();
    enabled   = 0;
    nbclients = 0;
    jumpok    = false;
    ingame    = false;
#ifdef CRYPTO
    FileUtil::use_crypto = true;
#endif

    NetComm = NULL;
    lastsave.resize( 0 );
    _serverip     = "";
    _serverport   = 0;
    callsign      = password = "";
    this->Clients = ClientsMap();

    delete clt_tcp_sock;
    clt_tcp_sock  = new SOCKETALT;
    //leave UDP well-enough alone
}
NetClient::NetClient()
{
    keeprun          = 1;
    clt_tcp_sock     = new SOCKETALT;
    clt_udp_sock     = new SOCKETALT;
    deltatime        = 0;
    game_unit        = NULL;
    latest_timestamp = 0;
    cur_time         = getNewTime();
    enabled          = 0;
    nbclients        = 0;
    jumpok = false;
    ingame = false;
#ifdef CRYPTO
    FileUtil::use_crypto = true;
#endif

    NetComm = NULL;

    _downloadManagerClient.reset( new VsnetDownload::Client::Manager( _sock_set ) );
    _sock_set.addDownloadManager( _downloadManagerClient );

#ifdef CRYPTO
    cout<<endl<<endl<<POSH_GetArchString()<<endl;
#endif
}

NetClient::~NetClient()
{
    if (NetComm != NULL)
    {
        delete NetComm;
        NetComm = NULL;
    }
    if (clt_tcp_sock) delete clt_tcp_sock;       //UDP sockets don't seem to like being deleted.
}

/*
 ************************************************************
 **** Login loop                                          ***
 ************************************************************
 */

int NetClient::checkAcctMsg()
{
    std::string packeta;
    AddressIP   ipadr;
    int ret = 0;
    //Watch account server socket
    //Get the number of active clients
    if ( acct_sock->isActive() )
    {
        //Receive packet and process according to command
        if ( acct_sock->recvstr( packeta ) != false && packeta.length() )
        {
            ret = 1;
            std::string netbuf = packeta;
            std::string warning;
            switch ( getSimpleChar( netbuf ) )
            {
            case ACCT_LOGIN_DATA:
            {
                COUT<<">>> LOGIN DATA --------------------------------------"<<endl;
                //We received game server info (the one we should connect to)
                getSimpleString( netbuf );                  //uname
                string warning      = getSimpleString( netbuf );             //message
                this->error_message = warning;
                _serverip = getSimpleString( netbuf );
                string srvportstr   = getSimpleString( netbuf );
                const char *srvport = srvportstr.c_str();
                int    porttemp     = atoi( srvport );
                if (porttemp > 65535) porttemp = 0;
                if (porttemp < 0) porttemp = 0;
                this->_serverport = (unsigned short) porttemp;
                COUT<<"<<< LOGIN DATA --------------------------------------"<<endl;
                break;
            }
            case ACCT_LOGIN_ERROR:
                COUT<<">>> LOGIN ERROR =( DENIED )= --------------------------------------"<<endl;
                lastsave.resize( 0 );
                getSimpleString( netbuf );                      //uname
                warning = getSimpleString( netbuf );                      //message
                lastsave.push_back( "" );
                if ( !warning.empty() )
                    lastsave.push_back( warning );
                else
                    lastsave.push_back( "Failed to login with this password!" );
                break;
            case ACCT_LOGIN_ALREADY:
                COUT<<">>> LOGIN ERROR =( ALREADY LOGGED IN )= --------------------------------------"<<endl;
                lastsave.resize( 0 );
                getSimpleString( netbuf );                      //uname
                warning = getSimpleString( netbuf );                      //message
                lastsave.push_back( "" );
                if ( !warning.empty() )
                    lastsave.push_back( warning );
                else
                    lastsave.push_back( "The account is already logged in to a server!" );
                break;
            default:
                COUT<<">>> UNKNOWN COMMAND =( "<<std::hex<<packeta<<std::dec<<" )= --------------------------------------"
                    <<std::endl;
                lastsave.resize( 0 );
                lastsave.push_back( "" );
                lastsave.push_back( "!!! PROTOCOL ERROR : Unexpected command received !!!" );
            }
        }
        else
        {
            char str[127];
            sprintf( str, "!!! NETWORK ERROR : Connection to account server lost (error number %d)!!!",
#ifdef _WIN32
                     WSAGetLastError()
#else
                     errno
#endif
                   );
            cerr<<str;
        }
    }
    return ret;
}

/*
 ************************************************************
 **** Launch the client                                   ***
 ************************************************************
 */



/*
 *************************************************************
 **** Check if its is time to get network messages         ***
 *************************************************************
 */

//This function is made to decide whether it is time to check
//network messages or not... depending on how often we want to
//do so.
//For now, it is always time to receive network messages

void NetClient::versionBuf( NetBuffer &buf ) const
{
    buf.setVersion( this->netversion );
}

int NetClient::isTime()
{
    int ret = 0;

    if ( (getNewTime()-cur_time) > NETWORK_ATOM )
    {
        cur_time = getNewTime();
        ret = 1;
    }
    return ret;
}

/*
 *************************************************************
 **** Send packets to server                               ***
 *************************************************************
 */

/*
 *************************************************************
 **** Check if server has sent something                   ***
 *************************************************************
 */

int NetClient::checkMsg( Packet *outpacket )
{
    int     ret = 0;
    string  jpeg_str( "" );
    timeval tv;
    tv.tv_sec  = 0;
    tv.tv_usec = 0;
    if ( clt_tcp_sock->isActive() || clt_udp_sock->isActive() )
        ret = recvMsg( outpacket, &tv );
    if (ret == -1)
    {
        NetClient::CleanUp();         //Kill networking!!!
        UniverseUtil::startMenuInterface( false, "Connection to VegaServer closed." );
        return -1;
    }
    //If we have network communications enabled and webcam support enabled we grab an image
    if ( NetComm != NULL && NetComm->IsActive() )
        //Here also send samples
        NetComm->SendSound( *this->clt_tcp_sock, this->serial );
    return ret;
}

#include "lowlevel/vsnet_err.h"
extern void SwitchUnits2( Unit *un );
void NetClient::Respawn( ObjSerial newserial )
{
    unsigned int whichcp;
    for (whichcp = 0; whichcp < _Universe->numPlayers(); ++whichcp)
        if (_Universe->AccessCockpit( whichcp )->GetParent() == NULL)
            break;
    if ( whichcp == _Universe->numPlayers() )
    {
        whichcp = 0;
        cerr<<"Error could not find blank cockpit to respawn into\n";
    }
    QVector      pos( 0, 0, 0 );
    bool         setplayerXloc;
    string       mysystem;
    Cockpit     *cp = _Universe->AccessCockpit( whichcp );
    vector< string > packedInfo;

    static float initialzoom = XMLSupport::parse_float( vs_config->getVariable( "graphics", "inital_zoom_factor", "2.25" ) );
    cp->zoomfactor = initialzoom;
    cp->savegame->SetStarSystem( mysystem );
    cp->savegame->ParseSaveGame( "",
                                 mysystem,
                                 "",
                                 pos,
                                 setplayerXloc,
                                 cp->credits,
                                 packedInfo,
                                 whichcp,
                                 lastsave[0],
                                 false );
    string fullsysname = mysystem+".system";
    StarSystem *ss;

    cp->UnpackUnitInfo(packedInfo);

    {
        Background::BackgroundClone savedtextures =
        {
            {NULL, NULL, NULL, NULL, NULL, NULL, NULL}
        };
        Background *tmp = NULL;
        if ( _Universe->activeStarSystem() )
        {
            tmp = _Universe->activeStarSystem()->getBackground();
            savedtextures = tmp->Cache();
        }
        _Universe->clearAllSystems();
        ss = _Universe->GenerateStarSystem( fullsysname.c_str(), "", Vector( 0, 0, 0 ) );
        if (tmp)
            savedtextures.FreeClone();
    }
    _Universe->pushActiveStarSystem( ss );
    unsigned int oldcp     = _Universe->CurrentCockpit();
    _Universe->SetActiveCockpit( cp );
    std::string  unkeyname = cp->GetUnitFileName();
    int fgsnumber = 0;
    if (cp->fg)
    {
        fgsnumber = cp->fg->flightgroup_nr++;
        cp->fg->nr_ships++;
        cp->fg->nr_ships_left++;
    }
    Unit *un = UnitFactory::createUnit( unkeyname.c_str(), false, FactionUtil::GetFactionIndex(
                                            cp->savegame->GetPlayerFaction() ), std::string(
                                            "" ), cp->fg, fgsnumber, &lastsave[1], newserial );
    un->SetSerial( newserial );
    cp->SetParent( un, unkeyname.c_str(), "", pos );
    un->SetPosAndCumPos( pos );
    this->game_unit.SetUnit( un );
    localSerials.push_back( newserial );
    ss->AddUnit( un );
    AddClientObject( un, newserial );
    SwitchUnits2( un );
    cp->SetView( CP_FRONT );
    cp->activeStarSystem = _Universe->activeStarSystem();
    _Universe->SetActiveCockpit( oldcp );
    _Universe->popActiveStarSystem();
    UniverseUtil::hideSplashScreen();
}
/*
 *************************************************************
 **** Receive a message from the server                    ***
 *************************************************************
 */
extern bool preEmptiveClientFire( const weapon_info* );
int NetClient::recvMsg( Packet *outpacket, timeval *timeout )
{
    using namespace VSFileSystem;
    ObjSerial packet_serial = 0;

    static vector< Mount::STATUS >backupMountStatus;

    //Receive data
    Unit        *un  = NULL;
    unsigned int mount_num;
    ObjSerial    mis = 0;
    ObjSerial    local_serial = 0;
    if (this->game_unit.GetUnit() != NULL)
        local_serial = this->game_unit.GetUnit()->GetSerial();
    Cockpit     *cp;

    Packet       p1;
    AddressIP    ipadr;

    static bool udpgetspriority = true;
    bool wasudp    = udpgetspriority;

    //First check if there is data in the client's recv queue.
    int  recvbytes = (udpgetspriority ? clt_udp_sock : clt_tcp_sock)->recvbuf( &p1, &ipadr );
    if (recvbytes <= 0)
    {
        recvbytes = (udpgetspriority == false ? clt_udp_sock : clt_tcp_sock)->recvbuf( &p1, &ipadr );
        wasudp    = !udpgetspriority;
    }
    if (recvbytes <= 0)
    {
        //Now, select and wait for data to come in the queue.
        clt_tcp_sock->addToSet( _sock_set );
        clt_udp_sock->addToSet( _sock_set );
        int socketstat = _sock_set.wait( timeout );
        if ( !clt_tcp_sock->valid() )
        {
            perror( "Error socket closed " );
            clt_tcp_sock->disconnect( "socket error closed" );
            //NETFIXME: Error handling on socket error?  Exit?
            return -1;
        }
        if (socketstat < 0)
        {
            perror( "Error select -1 " );
            clt_tcp_sock->disconnect( "socket error recv err" );
            return -1;
        }
        if (socketstat == 0)
            return -1;
        //timeout expired.

        //NETFIXME: check for file descriptors in _sock_set.fd_set...
        //Check the queues again.
        recvbytes = (udpgetspriority ? clt_udp_sock : clt_tcp_sock)->recvbuf( &p1, &ipadr );
        wasudp    = udpgetspriority;
        if (recvbytes <= 0)
        {
            recvbytes = (udpgetspriority == false ? clt_udp_sock : clt_tcp_sock)->recvbuf( &p1, &ipadr );
            wasudp    = !udpgetspriority;
        }
        udpgetspriority = !udpgetspriority;
        if (recvbytes <= 0)
        {
            //If nothing has come in either queue, and the select did not return 0, then this must be from a socket error.
            perror( "Error recv -1 " );
            clt_tcp_sock->disconnect( "socket error recv" );
            return -1;
        }
    }
    if (true)
    {

        bool nostarsystem = _Universe->activeStarSystem() == NULL ? true : false;

        _Universe->netLock( true );         //Don't bounce any commands back to the server again!

        NetBuffer netbuf( p1.getData(), p1.getDataLength() );
        versionBuf( netbuf );
        if (outpacket)
            *outpacket = p1;
        packet_serial = p1.getSerial();
        Cmd cmd = p1.getCommand();
        if (cmd != CMD_SNAPSHOT)
            COUT<<"Rcvd "<<(wasudp ? "UDP" : "TCP")<<": "<<cmd<<" from serial "<<packet_serial<<endl;
        switch (cmd)
        {
        //Login accept
        case CMD_CONNECT:
        {
            ObjSerial server_netversion = netbuf.getSerial();
            this->netversion = server_netversion;
            string    ipaddress = netbuf.getString();
            cout<<"Connection by "<<CLIENT_NETVERSION<<" to "
                <<"VegaServer "<<server_netversion<<" from address "<<ipaddress<<endl;
            if (server_netversion > CLIENT_NETVERSION)
            {
                cout<<"Using old client... setting version to "<<CLIENT_NETVERSION<<endl;
                this->netversion = CLIENT_NETVERSION;
            }
            else if (server_netversion < CLIENT_NETVERSION)
            {
                cout<<"Connected to an old server.  Setting version to "<<server_netversion<<endl;
            }
            break;
        }
        case CMD_CHOOSESHIP:
            this->loginChooseShip( p1 );
            break;
        case LOGIN_ACCEPT:
            this->loginAccept( p1 );
            break;
        case CMD_ASKFILE:
        {
            //NETFIXME: Broken code... shouldn't write to client's stuff
            //Also, shouldn't open files Read-only and then write to them.
            //Also it shouldn't exit(1)...
            string filename;
            string file;
            filename = netbuf.getString();
            file     = netbuf.getString();
            //If packet serial == 0 then it means we have an up to date file
            if (packet_serial == local_serial)
            {
                //Receive the file and write it (trunc if exists)
                cerr<<"RECEIVING file : "<<filename<<endl;
                VSFile  f;
                VSError err = f.OpenReadOnly( filename, ::VSFileSystem::UnknownFile );
                if (err > Ok)
                {
                    cerr<<"!!! ERROR : opening received file !!!"<<endl;
                    VSExit( 1 );
                }
                if ( f.Write( file ) != file.length() )
                {
                    cerr<<"!!! ERROR : writing received file !!!"<<endl;
                    VSExit( 1 );
                }
                f.Close();
            }
            else
            {
                //Something is wrong
                //displayError( packet_serial);
            }
            break;
        }
        case CMD_DOWNLOAD:
            COUT<<endl;
            if (_downloadManagerClient)
                _downloadManagerClient->processCmdDownload( *clt_tcp_sock, netbuf );
            break;
        //Login failed
        case LOGIN_ERROR:
            COUT<<">>> LOGIN ERROR =( DENIED )= ------------------------------------------------"<<endl;
            this->disconnect();
            lastsave.push_back( "" );
            lastsave.push_back( "!!! ACCESS DENIED : Account does not exist with this password !!!" );
            return -1;

            break;
        case LOGIN_UNAVAIL:
            COUT<<">>> ACCOUNT SERVER UNAVAILABLE ------------------------------------------------"<<endl;
            lastsave.push_back( "" );
            lastsave.push_back( "!!! ACCESS DENIED : Account server unavailable !!!" );
            this->disconnect();
            return -1;

            break;
        //Create a character
        case CMD_CREATECHAR:
            COUT<<endl;
            //Should begin character/ship creation process
            //this->createChar();
            break;
        //Receive start locations
        case CMD_LOCATIONS:
            COUT<<endl;
            //Should receive possible starting locations list
            this->receiveLocations( &p1 );
            break;
        case CMD_TXTMESSAGE:
        {
            string sender  = netbuf.getString();
            string message = netbuf.getString();
            UniverseUtil::IOmessage( 0, sender, "all", message );
            break;
        }
        case CMD_CUSTOM:
        {
            un = this->game_unit.GetUnit();
            int    cp   = _Universe->whichPlayerStarship( un );
            string cmd  = netbuf.getString();
            string args = netbuf.getString();
            string id   = netbuf.getString();
            UniverseUtil::receivedCustom( cp, true, cmd, args, id );
            break;
        }
        case CMD_SNAPSHOT:
        {
            if (nostarsystem) break;
            //Should update another client's position
            //Zone hack:
            //When receiving a snapshot, packet serial is considered as the
            //number of client updates.
            unsigned int numUnits  = p1.getSerial();
            unsigned int timestamp = p1.getTimestamp();
            double deltatime = netbuf.getFloat();

            this->receivePositions( numUnits, timestamp, netbuf, deltatime );
            break;
        }
        case CMD_ENTERCLIENT:
            if (nostarsystem) break;
            //Saving 4 bytes for every 50kB saved game isn't worth the bugs that come with it.
            this->AddObjects( netbuf );
            break;
        case CMD_EXITCLIENT:
            if (nostarsystem) break;
            COUT<<">>> "<<local_serial<<" >>> EXITING CLIENT =( serial #"
                <<packet_serial<<" )= --------------------------------------"<<endl;
            this->removeClient( &p1 );
            break;
        case CMD_ADDEDYOU:
            UniverseUtil::hideSplashScreen();
            if (nostarsystem) break;
            COUT<<">>> "<<local_serial<<" >>> ADDED IN GAME =( serial #"
                <<packet_serial<<" )= --------------------------------------"<<endl;
            //now we have to make the unit if it is null (this would be a respawn)
            if (this->game_unit.GetUnit() == NULL)
                this->Respawn( packet_serial );
            this->game_unit.GetUnit()->curr_physical_state = netbuf.getTransformation();
            break;
        case CMD_DISCONNECT:
            /*** TO REDO IN A CLEAN WAY ***/
            COUT<<">>> "<<local_serial<<" >>> DISCONNECTED -> Client killed =( serial #"
                <<packet_serial<<" )= --------------------------------------"<<endl;

            break;
        case CMD_FIREREQUEST:
        {
            if (nostarsystem) break;
            //WE RECEIVED A FIRE NOTIFICATION SO FIRE THE WEAPON
            float energy = netbuf.getFloat();
            mis = netbuf.getSerial();
            mount_num = netbuf.getInt32();
            //Find the unit
            if (p1.getSerial() == local_serial)               //WE have fired and receive the broadcast
                un = this->game_unit.GetUnit();
            else
                un = UniverseUtil::GetUnitFromSerial( p1.getSerial() );
            if (un != NULL)
            {
                //Set the concerned mount as ACTIVE and others as INACTIVE
                vector< Mount >
                ::iterator   i = un->mounts.begin();                          //note to self: if vector<Mount *> is ever changed to vector<Mount> remove the const_ from the const_iterator
                if ( mount_num > un->mounts.size() )
                    mount_num = un->mounts.size();
                unsigned int j;

                un->energy = energy;                     //It's important to set energy before firing.
                for (j = backupMountStatus.size(); j < un->mounts.size(); ++j)
                    backupMountStatus.push_back( Mount::UNCHOSEN );
                for (j = 0; i != un->mounts.end(); ++i, ++j)
                {
                    backupMountStatus[j] = (*i).status;
                    if ( (*i).status == Mount::ACTIVE )
                        (*i).status = Mount::INACTIVE;
                }
                Cockpit *ps = _Universe->isPlayerStarship( un );
                for (j = 0; j < mount_num; ++j)
                {
                    unsigned int mnt = (unsigned int)netbuf.getInt32();
                    if (mnt < un->mounts.size() && mnt >= 0)
                    {
                        if ( ps == NULL || !preEmptiveClientFire( un->mounts[mnt].type ) )
                        {
                            un->mounts[mnt].processed = Mount::ACCEPTED;
                            un->mounts[mnt].status    = Mount::ACTIVE;
                            //Store the missile id in the mount that should fire a missile
                            un->mounts[mnt].serial    = mis;
                        }
                    }
                }
                //Ask for fire
                if (mis != 0)
                    un->Fire( ROLES::FIRE_MISSILES|ROLES::EVERYTHING_ELSE, false );
                else
                    un->Fire( ROLES::EVERYTHING_ELSE|ROLES::FIRE_GUNS, false );
                i = un->mounts.begin();
                for (j = 0; i != un->mounts.end(); ++i, ++j)
                    (*i).status = backupMountStatus[j];
            }
            else
            {
                COUT<<"!!! Problem -> CANNOT FIRE UNIT NOT FOUND !!!"<<endl;
            }
            break;
        }
        case CMD_UNFIREREQUEST:
            if (nostarsystem) break;
            //WE RECEIVED AN UNFIRE NOTIFICATION SO DEACTIVATE THE WEAPON
            mount_num = netbuf.getInt32();
            //Find the unit
            un = UniverseUtil::GetUnitFromSerial( p1.getSerial() );
            if (un != NULL)
            {
                //Set the concerned mount as ACTIVE and others as INACTIVE
                vector< Mount >
                ::iterator   i = un->mounts.begin();                              //note to self: if vector<Mount *> is ever changed to vector<Mount> remove the const_ from the const_iterator
                unsigned int j;
                if ( mount_num > un->mounts.size() )
                    mount_num = un->mounts.size();
                for (j = backupMountStatus.size(); j < un->mounts.size(); ++j)
                    backupMountStatus.push_back( Mount::UNCHOSEN );
                for (j = 0; i != un->mounts.end(); ++i, ++j)
                {
                    backupMountStatus[j] = (*i).status;
                    if ( (*i).status == Mount::ACTIVE )
                        (*i).status = Mount::INACTIVE;
                }
                for (j = 0; j < mount_num; ++j)
                {
                    unsigned int mnt = (unsigned int)netbuf.getInt32();
                    if (mnt < un->mounts.size() && mnt >= 0)
                    {
                        un->mounts[mnt].processed = Mount::UNFIRED;
                        un->mounts[mnt].status    = Mount::ACTIVE;
                        //Store the missile id in the mount that should fire a missile
                        un->mounts[mnt].serial    = 0;                           //mis;
                    }
                }
                //Ask for fire
                un->UnFire();

                i = un->mounts.begin();
                for (j = 0; i != un->mounts.end(); ++i, ++j)
                    (*i).status = backupMountStatus[j];
            }
            else
            {
                COUT<<"!!! Problem -> CANNOT UNFIRE UNIT NOT FOUND !!!"<<endl;
            }
            break;
        case CMD_TARGET:
            if (nostarsystem) break;
            un = UniverseUtil::GetUnitFromSerial( packet_serial );
            if (un)
            {
                unsigned short targserial = netbuf.getSerial();
                Unit *target_un = UniverseUtil::GetUnitFromSerial( targserial );
                if (target_un)
                    COUT<<"Confirmed targeting unit "<<target_un->name<<" ("<<targserial<<")."<<endl;
                Unit *oldtarg   = un->Target();
                if ( oldtarg && oldtarg->GetSerial() == 0 && (target_un == NULL || target_un->GetSerial() == 0) )
                    COUT<<"Setting target from "<<oldtarg->name<<" to NULL."<<endl;
                //don't do anything
                else
                    un->computer.target.SetUnit( target_un );
            }
            break;
        case CMD_SCAN:
            if (nostarsystem) break;
            //We received the target info with the target serial in the packet as an answer to a scanRequest

            //Update info with received buffer

            //And tell all VDUs we received the target info
            cp = _Universe->isPlayerStarship( this->game_unit.GetUnit() );
            cp->ReceivedTargetInfo();
            break;
        case CMD_SNAPDAMAGE:
        {
            if (nostarsystem) break;
            //In case we use damage snapshots : we do not call ApplyNetDamage
            //In fact we trusted the client only for visual FX : Check where they are done !
            //but the server computes the damage itself

            //SHOULD READ THE DAMAGE SNAPSHOT HERE !
            unsigned int nbupdates = packet_serial;
            ObjSerial    serial;
            int offset = netbuf.getOffset();
            for (unsigned int i = 0; i < nbupdates; i++)
            {
                serial = netbuf.getSerial();
                int noffset = netbuf.getOffset();
                if (noffset == offset)
                {
                    COUT<<"ERROR Premature end of Snapshot buffer "<<std::hex<<std::string(
                            netbuf.getData(), netbuf.getSize() )<<std::dec<<std::endl;
                    break;
                }
                offset = noffset;
                Unit *un = UniverseUtil::GetUnitFromSerial( serial );
                receiveUnitDamage( netbuf, un );
            }
            break;
        }
#if 1
        case CMD_DAMAGE:
        {
            if (nostarsystem) break;
            float    amt    = netbuf.getFloat();
            float    ppercentage = netbuf.getFloat();
            float    spercentage = netbuf.getFloat();
            Vector   pnt    = netbuf.getVector();
            Vector   normal = netbuf.getVector();
            GFXColor col    = netbuf.getColor();
            un = UniverseUtil::GetUnitFromSerial( p1.getSerial() );
            float    hul    = netbuf.getFloat();
            Shield   sh     = netbuf.getShield();
            Armor    ar     = netbuf.getArmor();
            if (un)
            {
                if (un->hull >= 0)
                {
                    //Apply the damage
                    un->ApplyNetDamage( pnt, normal, amt, ppercentage, spercentage, col );
                    un->shield = sh;
                    un->armor  = ar;
                    un->hull   = hul;
                }
                if (un->hull < 0)
                    un->Destroy();
            }
            else
            {
                COUT<<"!!! Problem -> CANNOT APPLY DAMAGE UNIT NOT FOUND !!!"<<endl;
            }
            break;
        }
#endif
#if 0
        case CMD_DAMAGE1:
            break;
#endif
        case CMD_KILL:
        {
            if (nostarsystem) break;
            ClientPtr clt = Clients.get( p1.getSerial() );
            //If it is not a player
            if (!clt)
            {
                un = UniverseUtil::GetUnitFromSerial( p1.getSerial() );
                if (un)
                {
                    un->Destroy();
                    un->Kill( true, true );
                }
                else
                {
                    COUT<<"!!! Problem -> CANNOT KILL UNIT NOT FOUND !!!"<<endl;
                }
            }
            else
            {
                un = clt->game_unit.GetUnit();
                //Remove the player unit
                nbclients--;
                Clients.remove( p1.getSerial() );
                if (un)
                {
                    un->Destroy();
                }
                else
                {
                    un = UniverseUtil::GetUnitFromSerial( p1.getSerial() );
                    if (un)
                    {
                        un->Destroy();
                        un->Kill( true, true );
                    }
                }
                COUT<<"Client #"<<p1.getSerial()<<" killed - now "<<nbclients<<" clients in system"<<endl;
                if ( !clt->callsign.empty() )
                {
                    string msg = clt->callsign+" has died.";
                    UniverseUtil::IOmessage( 0, "game", "all", "#FFFF66"+msg+"#000000" );
                }
            }
            break;
        }
        case CMD_SAVEACCOUNTS:
        {
            Unit *un = this->game_unit.GetUnit();
            if (un)
            {
                int cpnum = _Universe->whichPlayerStarship( un );
                if (cpnum >= 0 && this->lastsave.size() >= 2)
                    SaveNetUtil::GetSaveStrings( cpnum, lastsave[0], lastsave[1], true );
            }
            break;
        }
        case CMD_JUMP:
            if (nostarsystem) break;
            if (1)
            {
                std::string    srvipadr( netbuf.getString() );
                unsigned short port( netbuf.getShort() );
                //SetConfigServerAddress(srvipadr,port);
                Reconnect( srvipadr, port );
            }
            else
            {
                //this is the old way of doing it
                StarSystem    *sts;
                string newsystem = netbuf.getString();
                ObjSerial      unserial   = netbuf.getSerial();
                ObjSerial      jumpserial = netbuf.getSerial();
                unsigned short zoneid     = netbuf.getShort();
                un = this->game_unit.GetUnit();
                if (!un)
                    break;
                //Get the pointer to the new star system sent by server
                if ( !( sts = star_system_table.Get( newsystem ) ) )
                {
                    //The system should have been loaded just before we asked for the jump so this is just a safety check
                    cerr<<"!!! FATAL ERROR : Couldn't find destination Star system !!!"<<endl;
                    sts = _Universe->GenerateStarSystem( newsystem.c_str(), "", Vector( 0, 0, 0 ) );
                }
                //If unserial == un->GetSerial() -> then we are jumping otherwise it is another unit/player
                if ( unserial == un->GetSerial() )
                {
                    this->zone = zoneid;
                    //If we received a CMD_JUMP with serial==player serial jump is granted
                    if ( packet_serial == un->GetSerial() )
                    {
                        this->jumpok = true;
                        this->ingame = false;
                    }
                    //The jump has been allowed but we don't have the good system file
                    else
                    {
                        //Here really do the jump function
                        Unit  *jumpun = UniverseUtil::GetUnitFromSerial( jumpserial );
                        sts->JumpTo( un, jumpun, newsystem, true );
                        string sysfile( newsystem+".system" );
                        VsnetDownload::Client::NoteFile f( *this->clt_tcp_sock, sysfile, SystemFile );
                        _downloadManagerClient->addItem( &f );

                        timeval timeout = {10, 0};
                        while ( !f.done() )
                            if (recvMsg( NULL, &timeout ) <= 0)
                            {
//NETFIXME: What to do if the download times out?
                                COUT<<"recvMsg <=0: "<<(vsnetEWouldBlock() ? "wouldblock" : "")<<endl;
                                break;
                            }
                        this->jumpok = true;
                    }
                }
                else
                {
                    //If another player / unit is jumping force it
                    Unit *jumpun = UniverseUtil::GetUnitFromSerial( jumpserial );
                    sts->JumpTo( un, jumpun, newsystem, true );
                }
            }
            break;
        case CMD_SNAPCARGO:
        {
            if (nostarsystem) break;
            ObjSerial ser;
            Unit     *mpl = UnitFactory::getMasterPartList();
            while ( ( ser = netbuf.getSerial() ) != 0 )
            {
                Unit *un = UniverseUtil::GetUnitFromSerial( ser );
                unsigned int i;
                //Clear cargo... back to front to make it more efficient.
                if (un)
                {
                    i = un->numCargo();
                    while (i > 0)
                    {
                        i--;
                        un->RemoveCargo( i, un->GetCargo( i ).GetQuantity(), true );
                    }
                }
                float mass    = netbuf.getFloat();
                float cargvol = netbuf.getFloat();
                float upgvol  = netbuf.getFloat();
                if (un)
                {
                    un->Mass = mass;
                    un->pImage->CargoVolume = cargvol;
                    un->pImage->UpgradeVolume = upgvol;
                }
                unsigned int numcargo = (unsigned int)netbuf.getInt32();
                if (numcargo < 0)
                {
                    numcargo = -numcargo;
                }
                Cargo carg;
                for (i = 0; i < numcargo; i++)
                {
                    unsigned int mplind;
                    unsigned int quantity = (unsigned int)netbuf.getInt32();
                    string str = netbuf.getString();
                    if (un)
                    {
                        Cargo *foundcarg = mpl->GetCargo( str.c_str(), mplind );
                        if (foundcarg)
                        {
                            carg = *foundcarg;
                        }
                        else
                        {
                            carg = Cargo();
                            carg.SetContent( str );
                        }
                    }
                    carg.SetQuantity( quantity );
                    carg.SetPrice( netbuf.getFloat() );
                    carg.SetMass( netbuf.getFloat() );
                    carg.SetVolume( netbuf.getFloat() );
                    if (un)
                        un->AddCargo( carg, false );
                }
            }
            break;
        }
        case CMD_MISSION:
        {
            un = this->game_unit.GetUnit();
            int    cp = _Universe->whichPlayerStarship( un );
            if (cp == -1) break;
            unsigned short type = netbuf.getShort();
            string qualname     = netbuf.getString();
            int    pos = netbuf.getInt32();
            if (type == Subcmd::TerminateMission)
            {
                Mission *activeMis = Mission::getNthPlayerMission( cp, pos+1 );
                if (activeMis)
                    activeMis->terminateMission();
                else
                    fprintf( stderr, "Failed to find and terminate mission %d for player %d\n", pos, cp );
            }
            else if (type == Subcmd::AcceptMission)
            {
                //lame duck mission
                unsigned int oldcp = _Universe->CurrentCockpit();
                _Universe->SetActiveCockpit( cp );
                _Universe->pushActiveStarSystem( _Universe->AccessCockpit()->activeStarSystem );
                while ( !Mission::getNthPlayerMission( cp, pos+1 ) )
                    LoadMission( "", "import Director; temp=Director.Mission()", false );
                string::size_type tpos = qualname.find( '/' );
                string cat = qualname.substr( 0, tpos );
                active_missions.back()->mission_name = cat;
                _Universe->popActiveStarSystem();
                _Universe->SetActiveCockpit( oldcp );
            }
            BaseUtil::refreshBaseComputerUI( NULL );
            break;
        }
        case CMD_COMM:
        {
            if (nostarsystem) break;
            Unit *from = UniverseUtil::GetUnitFromSerial( packet_serial );
            Unit *to   = game_unit.GetUnit();
            unsigned int curstate = netbuf.getInt32();
            if (!from)
            {
                COUT<<"Received invalid comm message "<<curstate<<" from "<<packet_serial<<endl;
                break;
            }
            if (!to)
            {
                COUT<<"Received comm message while dead."<<endl;
                break;
            }
            FSM *fsm = FactionUtil::GetConversation( to->faction, from->faction );
            if ( curstate >= 0 && curstate < fsm->nodes.size() )
            {
                unsigned char sex = 0;
                if (from->pilot)
                    sex = from->pilot->getGender();
                CommunicationMessage c( from, game_unit.GetUnit(), NULL, sex );
                c.SetCurrentState( curstate, NULL, sex );
                Order *oo = to->getAIState();
                if (oo)
                    oo->Communicate( c );
            }
            //if not a valid new node (-1)

            factions[from->faction]->faction[to->faction].relationship = netbuf.getFloat();
            factions[to->faction]->faction[from->faction].relationship = netbuf.getFloat();
            float relfrompilot = netbuf.getFloat();
            float reltopilot   = netbuf.getFloat();
            if (from->pilot)
            {
                Pilot::relationmap::iterator i = from->pilot->effective_relationship.find( to );
                if ( i != from->pilot->effective_relationship.end() )
                    (*i).second = relfrompilot;
            }
            if (to->pilot)
            {
                Pilot::relationmap::iterator i = to->pilot->effective_relationship.find( from );
                if ( i != to->pilot->effective_relationship.end() )
                    (*i).second = reltopilot;
            }
            break;
        }
        case CMD_CARGOUPGRADE:
        {
            if (nostarsystem) break;
            ObjSerial   buyer_ser  = netbuf.getSerial();
            ObjSerial   seller_ser = netbuf.getSerial();
            int quantity = netbuf.getInt32();
            std::string cargoName  = netbuf.getString();
            float  price  = netbuf.getFloat();
            float  mass   = netbuf.getFloat();
            float  volume = netbuf.getFloat();
            int    mountOffset = ( (int) netbuf.getInt32() );
            int    subunitOffset = ( (int) netbuf.getInt32() );
            Unit  *sender = UniverseUtil::GetUnitFromSerial( packet_serial );
            Unit  *buyer  = UniverseUtil::GetUnitFromSerial( buyer_ser );
            Unit  *seller = UniverseUtil::GetUnitFromSerial( seller_ser );
            bool   missioncarg     = false;

            unsigned int cargIndex = 0;
            Cargo *cargptr = NULL;
            if (!sender)
                break;
            if (seller)
                cargptr = seller->GetCargo( cargoName, cargIndex );
            if (!cargptr)
            {
                cargptr = GetMasterPartList( cargoName.c_str() );
                if (!cargptr)
                    break;
            }
            Cargo carg    = *cargptr;
            bool  upgrade = false;
            bool  repair  = false;
            if (carg.GetCategory().find( "upgrades" ) == 0)
            {
                upgrade = true;
                if ( isWeapon( carg.GetCategory() ) )
                {
                }
                else if (!quantity && buyer == sender)
                    repair = true;
            }
            if (!upgrade)
                missioncarg = (mountOffset == 1 && subunitOffset == 1);
            carg.mass    = mass;
            carg.price   = price;
            carg.volume  = volume;
            carg.mission = missioncarg;
            if (quantity)
            {
                if (buyer)
                {
                    carg.SetQuantity( quantity );
                    buyer->AddCargo( carg, true );
                }
                if (seller)
                    seller->RemoveCargo( cargIndex, quantity, true );
            }
            if ( upgrade && !repair && (seller == sender || buyer == sender) )
            {
                double percent;                     //not used.
                int    multAddMode = GetModeFromName( carg.GetContent().c_str() );

                //Now we're sure it's an authentic upgrade...
                //Wow! So much code just to perform an upgrade!
                const string unitDir = GetUnitDir( sender->name.get().c_str() );
                string templateName;
                int    faction = 0;
                if (seller == sender)
                {
                    templateName = unitDir+".blank";
                    faction = seller->faction;
                }
                else if (buyer == sender)
                {
                    faction = buyer->faction;
                    templateName = unitDir+".template";
                }
                const Unit *unitCarg     = getUnitFromUpgradeName( carg.GetContent(), faction );
                if (!unitCarg) break;                     //not an upgrade, and already did cargo transactions.
                //Get the "limiter" for the upgrade.  Stats can't increase more than this.
                const Unit *templateUnit = UnitConstCache::getCachedConst( StringIntKey( templateName, faction ) );
                if (!templateUnit)
                {
                    templateUnit = UnitConstCache::setCachedConst( StringIntKey( templateName, faction ),
                                   UnitFactory::createUnit( templateName.c_str(), true,
                                                            faction ) );
                }
                if (templateUnit->name == LOAD_FAILED)
                    templateUnit = NULL;
                if (unitCarg->name == LOAD_FAILED)
                    break;
                if (seller == sender)
                {
                    //Selling it... Downgrade time!
                    if ( seller->canDowngrade( unitCarg, mountOffset, subunitOffset, percent, templateUnit ) )
                        seller->Downgrade( unitCarg, mountOffset, subunitOffset, percent, templateUnit );
                }
                else if (buyer == sender)
                {
                    //Buying it... Upgrade time!
                    if ( buyer->canUpgrade( unitCarg, mountOffset, subunitOffset, multAddMode, true, percent, templateUnit ) )
                        buyer->Upgrade( unitCarg, mountOffset, subunitOffset, multAddMode, true, percent, templateUnit );
                }
            }
            if (repair)
                sender->RepairUpgradeCargo( &carg, seller, NULL );
            Unit *player = game_unit.GetUnit();
            if ( player
                    && ( ( buyer
                           && buyer->isDocked( player ) )
                         || ( seller && seller->isDocked( player ) ) || player == buyer || player == seller ) )
                BaseUtil::refreshBaseComputerUI( &carg );
            break;
        }
        case CMD_CREDITS:
            cp = _Universe->isPlayerStarship( this->game_unit.GetUnit() );
            if (cp)
                cp->credits = netbuf.getFloat();
            break;
        case CMD_SAVEDATA:
        {
            un = this->game_unit.GetUnit();
            int      cp = _Universe->whichPlayerStarship( un );
            if (cp == -1) break;
            unsigned short type = netbuf.getShort();
            string   key;
            string   strValue;
            float    floatValue = 0;
            int      pos = 0;
            Mission *activeMis  = NULL;
            Mission *origMis    = mission;
            if ( (type&Subcmd::StringValue) || (type&Subcmd::FloatValue) )
                key = netbuf.getString();
            pos = netbuf.getInt32();
            if (type&Subcmd::Objective)
            {
                int missionnum = netbuf.getInt32();
                activeMis = Mission::getNthPlayerMission( cp, missionnum+1 );
            }
            if (activeMis == NULL) activeMis = mission;
            if (type&Subcmd::SetValue)
            {
                if (pos < 0) break;                   //-1 is valid for erasing.
                if ( (type&Subcmd::StringValue) || (type&Subcmd::Objective) )
                    strValue = netbuf.getString();
                if ( (type&Subcmd::FloatValue) || (type&Subcmd::Objective) )
                    floatValue = netbuf.getFloat();
            }
            switch (type)
            {
            case (Subcmd::FloatValue|Subcmd::SetValue):
                while (getSaveDataLength( cp, key ) <= (unsigned int) pos && (unsigned int) pos != UINT_MAX)
                    pushSaveData( cp, key, 0 );
                putSaveData( cp, key, pos, floatValue );
                break;
            case (Subcmd::FloatValue|Subcmd::EraseValue):
                if (pos < 0)
                    for (unsigned int i = getSaveDataLength( cp, key ); i > 0; i--)
                        eraseSaveData( cp, key, (i-1) );
                else
                    eraseSaveData( cp, key, pos );
                break;
            case (Subcmd::StringValue|Subcmd::SetValue):
                while (getSaveStringLength( cp, key ) <= (unsigned int) pos && (unsigned int) pos != UINT_MAX)
                    pushSaveString( cp, key, "" );
                putSaveString( cp, key, pos, strValue );
                break;
            case (Subcmd::StringValue|Subcmd::EraseValue):
                if (pos < 0)
                    for (unsigned int i = getSaveStringLength( cp, key ); i > 0; i--)
                        eraseSaveString( cp, key, (i-1) );
                else
                    eraseSaveString( cp, key, pos );
                break;
            case (Subcmd::Objective|Subcmd::SetValue):
                mission = activeMis;
                while (mission->objectives.size() <= (unsigned int) pos && (unsigned int) pos != UINT_MAX)
                    UniverseUtil::addObjective( "" );
                UniverseUtil::setObjective( pos, strValue );
                UniverseUtil::setCompleteness( pos, floatValue );
                mission = origMis;
                BaseUtil::refreshBaseComputerUI( NULL );                   //objectives
                break;
            case (Subcmd::Objective|Subcmd::EraseValue):
                mission = activeMis;
                if ( (unsigned int) pos < mission->objectives.size() && (unsigned int) pos >= 0 && (unsigned int) pos
                        != UINT_MAX )
                    mission->objectives.erase( activeMis->objectives.begin()+pos );
                else
                    UniverseUtil::clearObjectives();
                mission = origMis;
                BaseUtil::refreshBaseComputerUI( NULL );                   //objetives
                break;
            }
            break;
        }
        case CMD_STARTNETCOMM:
#ifdef NETCOMM
        {
            float freq    = netbuf.getFloat();
            char  secured = netbuf.getChar();
            char  webc    = netbuf.getChar();
            char  pa = netbuf.getChar();
            if (freq == current_freq)
            {
                if ( secured == NetComm->IsSecured() )
                {
                    ClientPtr clt;
                    //Check this is not us
                    if (packet_serial != this->serial)
                    {
                        //Add the client to netcomm list in NetComm ?
                        clt = Clients.get( packet_serial );
                        clt->webcam    = webc;
                        clt->portaudio = pa;
                        NetComm->AddToSession( clt );
                    }
                }
                else
                {
                    cerr<<"WARNING : Received a STARTCOMM from a channel not in the same mode"<<endl;
                }
            }
            else
            {
                cerr<<"WARNING : Received a STARTCOMM from another frequency"<<endl;
            }
        }
#endif
        break;
        case CMD_STOPNETCOMM:
#ifdef NETCOMM
        {
            ClientPtr clt;
            //Check this is not us
            if (packet_serial != this->serial)
            {
                //Remove the client to netcomm list in NetComm
                clt = Clients.get( packet_serial );
                NetComm->RemoveFromSession( clt );
            }
        }
#endif
        break;
        case CMD_SOUNDSAMPLE:
#ifdef NETCOMM
        {
            NetComm->RecvSound( p1.getData(), p1.getDataLength(), false );
        }
#endif
        break;
        case CMD_SECSNDSAMPLE:
#ifdef NETCOMM
        {
            NetComm->RecvSound( p1.getData(), p1.getDataLength(), true );
        }
#endif
        break;
#if 0
        //NETFIXME  this is probably more consistent
        case CMD_TXTMESSAGE:
#ifdef NETCOMM
        {
            string msg( p1.getData() );
            NetComm->RecvMessage( msg, false );
        }
#endif
#endif
        case CMD_SECMESSAGE:
#ifdef NETCOMM
        {
            string msg( p1.getData() );
            NetComm->RecvMessage( msg, true );
        }
#endif
        break;
        case CMD_DOCK:
        {
            if (nostarsystem) break;
            ObjSerial    utdw_serial = netbuf.getSerial();
            un = UniverseUtil::GetUnitFromSerial( utdw_serial );
            unsigned int dockport;
            for (dockport = 0; dockport < un->pImage->dockingports.size(); ++dockport)
                if (!un->pImage->dockingports[dockport].IsOccupied())
                    break;
            if ( dockport > un->pImage->dockingports.size() )
            {
                cerr<<"CMD_DOCK: All docking ports used up! Kicking out port 0!"<<endl;
                dockport = 0;
                un->pImage->dockingports[0].Occupy(false);
            }
            cerr<<"RECEIVED A DOCK AUTHORIZATION for unit "<<p1.getSerial()<<" to unit "<<utdw_serial
                <<" at docking port #"<<dockport<<endl;
            Unit *un2 = UniverseUtil::GetUnitFromSerial( p1.getSerial() );
            un->RequestClearance( un2 );
            un2->ForceDock( un, dockport );
            break;
        }
        case CMD_UNDOCK:
        {
            if (nostarsystem) break;
            ObjSerial utdw_serial = netbuf.getSerial();
            cerr<<"RECEIVED A UNDOCK ORDER for unit "<<p1.getSerial()<<" to unit "<<utdw_serial<<endl;
            un = UniverseUtil::GetUnitFromSerial( utdw_serial );
            Unit     *un2 = UniverseUtil::GetUnitFromSerial( p1.getSerial() );
            un2->UnDock( un );
            break;
        }
        case CMD_POSUPDATE:
        {
            if (nostarsystem) break;
            //If a client receives that it means the server want to force the client position to be updated
            //with server data
            ClientState serverpos = netbuf.getClientState();
            un = this->game_unit.GetUnit();
            if (!un)
                break;
            un->old_state = serverpos;
            serverpos.setUnitState( un );
            un->BackupState();
            break;
        }
        case CMD_SERVERTIME:
            break;
        default:
            COUT<<">>> "<<local_serial<<" >>> UNKNOWN COMMAND =( "<<std::hex<<cmd
                <<" )= --------------------------------------"<<std::endl;
            keeprun = 0;
            this->disconnect();
        }
        _Universe->netLock( false );
    }
    return recvbytes;
}

/*
 ************************************************************
 **** Disconnect from the server                          ***
 ************************************************************
 */

void NetClient::disconnect()
{
    keeprun = 0;
    //Disconnection is handled in the VSExit(1) function for each player
    //Or, if you don't actually want to exit(1), you can logout with:
    logout( true );
}

SOCKETALT* NetClient::logout( bool leaveUDP )
{
    keeprun = 0;
    Packet p;
    if (clt_tcp_sock->valid() && clt_tcp_sock->get_fd() != -1)
    {
        Unit *un = this->game_unit.GetUnit();
        if (un)
        {
            p.send( CMD_LOGOUT, un->GetSerial(),
                    (char*) NULL, 0,
                    SENDRELIABLE, NULL, *this->clt_tcp_sock,
                    __FILE__, PSEUDO__LINE__( 1382 ) );
            timeval tv = {10, 0};
            recvMsg( &p, &tv );
        }
        clt_tcp_sock->disconnect( "Closing connection to server" );
    }
    Mission *mis;
    //Can't figure out how to get cockpit number?
    while ( ( mis = Mission::getNthPlayerMission( (this-Network), 0 ) ) )
        mis->terminateMission();
    if (!leaveUDP)
        NetUIUDP::disconnectSaveUDP( *clt_udp_sock );
    else if (lossy_socket == clt_udp_sock)
        return clt_udp_sock;
    return NULL;
}

void NetClient::CleanUp()
{
    if (Network)
    {
        for (unsigned int i = 0; i < _Universe->numPlayers(); ++i)
            Network[i].logout( false );
        delete[] Network;
        Network = NULL;
    }
}

void NetClient::Reconnect( std::string srvipadr, unsigned short port )
{
    vector< string >    usernames;
    vector< string >    passwords;
    vector< SOCKETALT* >udp;
    unsigned int i;
    if (!Network)
        Network = new NetClient[_Universe->numPlayers()];
    for (i = 0; i < _Universe->numPlayers(); ++i)
    {
        usernames.push_back( Network[i].callsign );
        passwords.push_back( Network[i].password );
        SOCKETALT *udpsocket = Network[i].logout( true );
        if (udpsocket)
            udp.push_back( udpsocket );
        else
            udp.push_back( NULL );
        Network[i].disconnect();
    }
    _Universe->clearAllSystems();
    localSerials.resize( 0 );
    for (i = 0; i < _Universe->numPlayers(); ++i)
        Network[i].Reinitialize();
    UniverseUtil::showSplashScreen( "" );
    //necessary? usually we would ask acctserver for it .. or pass it in NetClient::getConfigServerAddress(srvipadr, port);
    for (unsigned int k = 0; k < _Universe->numPlayers(); ++k)
    {
        string err;
        if ( !srvipadr.empty() )
            Network[k].SetCurrentServerAddress( srvipadr, port );
        else
            Network[k].SetConfigServerAddress( srvipadr, port );
        int response = Network[k].connectLoad( usernames[k], passwords[k], err );
        if (response == 0)
        {
            COUT<<"Network login error: \n"<<err<<endl;
            UniverseUtil::startMenuInterface( false, "Jumping to system, but got a login error: \n\n"+err );
            return;
        }
        vector< string > *loginResp = Network[k].loginSavedGame( 0 );
        if (!loginResp)
        {
            COUT<<"Failed to get a ship";
            UniverseUtil::startMenuInterface( false, "Jumping to system, but failed to get a ship!" );
            return;
        }
        cout<<" logged in !"<<endl;
        Network[k].Respawn( Network[k].serial );
        Network[k].synchronizeTime( udp[k] );
        _Universe->AccessCockpit( k )->TimeOfLastCollision = getNewTime();
        Network[k].inGame();
    }
    UniverseUtil::hideSplashScreen();
}

ClientPtr NetClient::ClientsMap::insert( int x, Client *c )
{
    if (c != NULL)
    {
        ClientPtr cp( c );
        _map.insert( ClientPair( x, cp ) );
        return cp;
    }
    else
    {
        return ClientPtr();
    }
}

ClientPtr NetClient::ClientsMap::get( int x )
{
    ClientIt it = _map.find( x );
    if ( it == _map.end() ) return ClientPtr();
    return it->second;
}

bool NetClient::ClientsMap::remove( int x )
{
    size_t s = _map.erase( x );
    if (s == 0) return false;
    return true;
    //shared_ptr takes care of delete
}

Transformation NetClient::Interpolate( Unit *un, double addtime )
{
    if (!un) return Transformation();
//NETFIXME: Interpolation is kind of borked...?
    ClientPtr clt = Clients.get( un->GetSerial() );
    Transformation trans;
    if (clt)
    {
        clt->elapsed_since_packet += addtime;
        trans = clt->prediction->Interpolate( un, clt->elapsed_since_packet );
    }
    else
    {
        trans = un->curr_physical_state;
    }
    return trans;
}

