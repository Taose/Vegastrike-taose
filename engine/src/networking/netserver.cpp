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
 *  NetServer - Network Server Interface - written by Stephane Vaxelaire <svax@free.fr>
 */

#include <time.h>
#include <math.h>
#if !defined (_WIN32) || defined (__CYGWIN__)
#include <unistd.h>
#else
#include <io.h>
#endif

#include "cmd/unit_generic.h"
#include "cmd/unit_util.h"
#include "cmd/weapon_xml.h"
#include "cmd/bolt.h"
#include "gfx/cockpit_generic.h"
#include "universe_util.h"
#include "cmd/unit_factory.h"
#include "load_mission.h"
#include "save_util.h"
#include "networking/client.h"
#include "networking/lowlevel/packet.h"
#include "lin_time.h"
#include "python/init.h"
#include "networking/netserver.h"
#include "networking/zonemgr.h"
#include "networking/lowlevel/vsnet_serversocket.h"
#include "networking/lowlevel/vsnet_sockethttp.h"
#include "networking/lowlevel/vsnet_debug.h"
#include "networking/savenet_util.h"
#include "vsfilesystem.h"
#include "options.h"
#include "networking/lowlevel/netbuffer.h"
#include "networking/lowlevel/vsnet_dloadmgr.h"
#include "cmd/ai/script.h"
#include "cmd/ai/order.h"
#include "cmd/ai/fire.h"
#include "cmd/ai/fireall.h"
#include "cmd/ai/flybywire.h"
#include "cmd/ai/communication.h"
#include "cmd/pilot.h"
#include "cmd/role_bitmask.h"
#include "gfxlib_struct.h"
#include "posh.h"
#include "fileutil.h"
#include "faction_generic.h"
#include "cmd/unit_const_cache.h"

#include "python/init.h"
#include <Python.h>

#include "netversion.h"
ObjSerial SERVER_NETVERSION = NETWORK_VERSION;

extern class vs_options game_options;

double clienttimeout;
double logintimeout;
int    acct_con;
double DAMAGE_ATOM;
double PLANET_ATOM;
double SAVE_ATOM;

static const char*const MISSION_SCRIPTS_LABEL = "mission_scripts";
static const char*const MISSION_NAMES_LABEL   = "mission_names";
static const char*const MISSION_DESC_LABEL    = "mission_descriptions";

#define MAXINPUT 1024
char   input_buffer[MAXINPUT];
int    nbchars;

string universe_file;
string universe_path;

using namespace VSFileSystem;

//What header are these *supposed* to be defined in ???
extern const Unit * getUnitFromUpgradeName( const string &upgradeName, int myUnitFaction = 0 );
extern int GetModeFromName( const char* );  //1=add, 2=mult, 0=neither.
static const string LOAD_FAILED = "LOAD_FAILED";
//Takes in a category of an upgrade or cargo and returns true if it is any type of mountable weapon.
extern bool isWeapon( std::string name );
extern Cargo * GetMasterPartList( const char *input_buffer );
extern void ExecuteDirector();

void getZoneInfoBuffer( unsigned short zoneid, NetBuffer &netbuf )
{
    VSServer->zonemgr->getZoneBuffer( zoneid, netbuf );
}

/*
 *************************************************************
 **** Constructor / Destructor                             ***
 *************************************************************
 */

NetServer::NetServer()
{
    udpNetwork        = new SOCKETALT();
    this->nbclients   = 0;
    this->nbaccts     = 0;
    this->keeprun     = 1;
    this->acctserver  = 0;
    this->srvtimeout.tv_sec = 0;
    this->srvtimeout.tv_usec = 0;
    this->snapchanged = 0;
    /***** number of zones should be determined as server loads zones files *****/
    zonemgr = new ZoneMgr();
    UpdateTime();
    srand( (unsigned int) getNewTime() );
    //Here 500 could be something else between 1 and 0xFFFF
    serial_seed = (ObjSerial) ( rand()*( 500./( ( (double) (RAND_MAX) )+1 ) ) );
    globalsave  = new SaveGame( "" );

    _downloadManagerServer.reset( new VsnetDownload::Server::Manager( _sock_set ) );
    _sock_set.addDownloadManager( _downloadManagerServer );
#ifdef CRYPTO
    FileUtil::use_crypto = true;
#endif
}

NetServer::~NetServer()
{
    delete zonemgr;
    delete globalsave;
    delete udpNetwork;
}

/*
 *************************************************************
 **** Display info on the server at startup                ***
 *************************************************************
 */

void NetServer::startMsg()
{
    cout<<endl<<"Vegastrike Server version "<<GAMESERVER_VERSION<<endl;
    cout<<"Written by Stephane Vaxelaire"<<endl<<endl<<endl;
    cout<<POSH_GetArchString()<<endl;
}

/*
 *************************************************************
 **** Start the server loop                                ***
 *************************************************************
 */

extern void InitUnitTables(); //universe_generic.cpp

void NetServer::start( int argc, char **argv )
{
    const char *serverport = NULL;
    int i;
    for (i = 0; i < argc; ++i)
    {
        char match = 1;
        int  j;
        if (strncmp( argv[i], "-p", 2 ) == 0)
            serverport = argv[i]+2;
        else
            match = 0;
        if (match)
        {
            for (j = i+1; j < argc; ++j)
                argv[j-1] = argv[j];
            argc--;
            i--;
        }
    }
    string strperiod, strtimeout, strlogintimeout, strnetatom;
    int    periodrecon;
    keeprun = 1;
    double savetime       = 0;
    double reconnect_time = 0;
    double curtime        = 0;
    double snaptime       = 0;
    double planettime     = 0;
    acct_con = 1;
    nbchars  = 0;
    memset( input_buffer, 0, MAXINPUT );
    Packet p2;

    _sock_set.start();

    startMsg();
    if (argc == 2)
    {
        CONFIGFILE = argv[1];
    }
    else
    {
        CONFIGFILE = new char[42];
        strcpy( CONFIGFILE, "vegastrike.config" );
    }
    cout<<"Loading server config...";
    VSFileSystem::InitPaths( CONFIGFILE );

    game_options.init();

    InitUnitTables();     //universe_generic.cpp
    //Here we say we want to only handle activity in all starsystems
    run_only_player_starsystem = false;
    //vs_config = new VegaConfig( SERVERCONFIGFILE);
    cout<<" config loaded"<<endl;
    //Save period in seconds
    strperiod       = vs_config->getVariable( "server", "saveperiod", "7200" );
    SAVE_ATOM       = atoi( strperiod.c_str() );
    string strperiodrecon = vs_config->getVariable( "server", "reconnectperiod", "60" );
    periodrecon     = atoi( strperiodrecon.c_str() );
    strtimeout      = vs_config->getVariable( "server", "clienttimeout", "180" );
    clienttimeout   = atoi( strtimeout.c_str() );
    strlogintimeout = vs_config->getVariable( "server", "logintimeout", "60" );
    logintimeout    = atoi( strlogintimeout.c_str() );

    this->server_password = vs_config->getVariable( "server", "server_password", "" );

    strnetatom   = vs_config->getVariable( "network", "network_atom", "0.2" );
    NETWORK_ATOM = (double) atof( strnetatom.c_str() );
    strnetatom   = vs_config->getVariable( "network", "damage_atom", "1" );
    DAMAGE_ATOM  = (double) atof( strnetatom.c_str() );
    strnetatom   = vs_config->getVariable( "network", "planet_atom", "10" );
    PLANET_ATOM  = (double) atof( strnetatom.c_str() );

    strnetatom   = vs_config->getVariable( "server", "difficulty", "1" );
    g_game.difficulty = atof( strnetatom.c_str() );
    InitTime();
    UpdateTime();
    savetime = getNewTime();
    reconnect_time = getNewTime()+periodrecon;
    std::string configport = vs_config->getVariable( "network", "server_port", "6777" );
    if ( configport.empty() )
        configport = vs_config->getVariable( "network", "serverport", "6777" );
    if (serverport == NULL)
        serverport = configport.c_str();
    string tmp;
    acctserver = XMLSupport::parse_bool( vs_config->getVariable( "server", "useaccountserver",
                                         vs_config->getVariable( "network", "use_account_server",
                                                 "false" ) ) );

    //Create and bind sockets
    COUT<<"Initializing TCP server ..."<<endl;
    tcpNetwork = NetUITCP::createServerSocket( atoi( serverport ), _sock_set );
    if (tcpNetwork == NULL)
    {
        COUT<<"Couldn't create TCP server - quitting"<<endl;
        exit( -100 );
    }
    COUT<<"Initializing UDP server ..."<<endl;
    *udpNetwork = NetUIUDP::createServerSocket( atoi( serverport ), _sock_set );
    if (*udpNetwork == NULL)
    {
        COUT<<"Couldn't create UDP server - quitting"<<endl;
        exit( -100 );
    }
    COUT<<"done."<<endl;
    std::string acctsrv = vs_config->getVariable( "network", "account_server_url", "" );
    if ( acctsrv.empty() )
        acctsrv = vs_config->getVariable( "network", "accountsrv", "" );
    if (!acctserver)
    {
        cout<<"Not connecting to account server."<<endl;
    }
    else
    {
        cout<<"Initializing connection to account server..."<<endl;
        if ( acctsrv.empty() )
        {
            cout<<"Account server IP not specified, exiting"<<endl;
            VSExit( 1 );
        }
        if (acctsrv.find( '/' ) == std::string::npos)
        {
            int acctport = atoi( vs_config->getVariable( "network", "accountsrvport", "" ).c_str() );
            if (!acctport)
                acctport = ACCT_PORT;
        }
        else
        {
            acct_sock = new VsnetHTTPSocket( acctsrv, _sock_set );
        }
        if (acct_sock == NULL)
            cerr<<"Invalid Accountserver URL... "<<endl;
        else
            COUT<<"accountserver on socket "<<acct_sock<<" done."<<endl;
    }
    //Create the _Universe telling it we are on server side
    universe_path = "";
    universe_file = vs_config->getVariable( "server", "galaxy", "milky_way.xml" );
    cout<<"LOADING Universe file : "<<universe_file<<endl;
    _Universe     = new Universe( argc, argv, universe_file.c_str(), true );
    cout<<"Universe LOADED"<<endl;
    string strmission = vs_config->getVariable( "server", "missionfile", "networking.mission" );
    Python::init();
    Python::test();
    active_missions.push_back( mission = new Mission( strmission.c_str() ) );
    mission->initMission( true );

    //Loads dynamic universe
    string  dynpath = "dynaverse.dat";
    VSFile  f;
    VSError err     = f.OpenReadOnly( dynpath, ::VSFileSystem::UnknownFile );
    if (err > Ok)
    {
        cerr<<"!!! ERROR : opening dynamic universe file "<<dynpath.c_str()<<" !!!"<<endl;
    }
    else
    {
        string dynaverse = f.ReadFull();
        char  *dynchar   = strdup( dynaverse.c_str() );
        globalsave->ReadSavedPackets( dynchar, true );
        f.Close();
    }
    std::vector< std::vector< char > >temp = ROLES::getAllRolePriorities();
    {
        char     hostName[128];
        hostName[0] = '\0';
        gethostname( hostName, 128 );
        hostent *local = NULL;
        cout<<endl<<endl<<" ======== SERVER IS NOW RUNNING ========"<<endl;
        const AddressIP &adr = this->tcpNetwork->get_adr();
        cout<<"    Server Port: "<<ntohs( adr.sin_port )<<endl;
        cout<<"    Server IP Addresses: "<<endl;
        int num = 0;
        if (hostName[0])
            local = gethostbyname( hostName );
        if (local)
        {
            in_addr **localaddr = (in_addr**) local->h_addr_list;
            for (int i = 0; i < 5 && localaddr[i]; i++)
            {
                string ipaddr = inet_ntoa( *(localaddr[i]) );
                if (ipaddr.substr( 0, 4 ) == "127.")
                {
                    continue;
                }
                else
                {
                    cout<<"        "<<ipaddr;
                    num++;
                    if (ipaddr.substr( 0, 8 ) == "169.254.")
                        cout<<" (Ethernet connection)";
                    else if (ipaddr.substr( 0, 8 ) == "192.168."
                             || ipaddr.substr( 0, 3 ) == "10.")
                        cout<<" (Local Area Network)";
                    else
                        cout<<" (Internet Connection)";
                }
                cout<<endl;
            }
        }
        if (!num)
        {
            cout<<"        No network interfaces found associated to your hostname."<<endl;
#ifdef _WIN32
            cout<<"        (Consult Start-> Run-> 'cmd /k ipconfig' for your IP.)"<<endl;
#else
            cout<<"        (Consult the '/sbin/ifconfig' command-line tool for your IP.)"<<endl;
#endif
        }
        cout<<"        You can also connect locally using 'localhost'"<<endl;
        if (acctserver)
        {
            cout<<"    Public Server: "<<endl<<"    ";
            if (acctsrv.length() > 75)
                cout<<acctsrv.substr( 0, 50 )<<"..."<<acctsrv.substr( acctsrv.length()-20, 20 )<<endl;
            else
                cout<<acctsrv<<endl;
        }
        else
        {
            if ( this->server_password.empty() )
                cout<<"    Private Server"<<endl;
            else
                cout<<"    Private Server, Password Protected: <"<<this->server_password<<">"<<endl;
        }
        cout<<" --------------------------------------- "<<endl;
        cout<<"To stop this server, hit Ctrl-C, Ctrl-\\, Ctrl-Break, or close this window."<<endl;
        cout<<endl<<"Have fun!"<<endl<<endl;
    }
    //Server loop
    while (keeprun)
    {

        UpdateTime();
        if (_Universe->numPlayers() > 0)
            ExecuteDirector();

        //Check received communications
        checkMsg( _sock_set );
        if (acctserver && acct_con)
            //Listen for account server answers
            checkAcctMsg( _sock_set );
        //And send to it the login request we received
        //Then send clients confirmations or errors
        curtime = getNewTime();
        if (acctserver && !acct_con && (curtime-reconnect_time) > periodrecon)
        {
            std::string netbuf;
            reconnect_time = curtime+periodrecon;
            //We previously lost connection to account server
            //We try to reconnect
            if (acct_sock)
                delete acct_sock;
            acct_sock = new VsnetHTTPSocket( acctsrv, _sock_set );
            if ( acct_sock->valid() )
            {
                LI  i;
                int j = 0;
                COUT<<">>> Reconnected accountserver on socket "<<*acct_sock<<" done."<<endl;
                //Send a list of ingame clients
                //Build a buffer with number of clients and client serials
                //Put first the number of clients
                addSimpleChar( netbuf, ACCT_RESYNC );
                for (j = 0, i = allClients.begin(); i != allClients.end(); i++, j++)
                    //Add the current client's serial to the buffer
                    addSimpleString( netbuf, (*i)->callsign );
                //Passing NULL to AddressIP arg because between servers -> only TCP
                //Use the serial packet's field to send the number of clients
                if ( !acct_sock->sendstr( netbuf ) )
                    COUT<<"Failure to resync, SOCKET was : "<<*acct_sock<<endl;
            }
            else
            {
                cerr<<">>> Reconnection to account server failed."<<endl;
            }
        }
        //See if we have some timed out clients and disconnect them
        this->checkTimedoutClients_udp();

        //Remove all clients to be disconnected
        LI j;
        for (j = discList.begin(); j != discList.end(); j++)
            disconnect( (*j), __FILE__, PSEUDO__LINE__( 328 ) );
        discList.clear();
        //Remove all clients that logged out
        for (j = logoutList.begin(); j != logoutList.end(); j++)
            this->logout( (*j) );
        logoutList.clear();

        /****************************** VS STUFF TO DO ************************************/
        //UPDATE STAR SYSTEM -> TO INTEGRATE WITH NETWORKING
        //PROCESS JUMPS -> MAKE UNITS CHANGE THEIR STAR SYSTEM

//NETFIXME: Why was StarSystem->Update() commented out?

        unsigned int i;
        for (i = 0; i < _Universe->star_system.size(); i++)
//NETFIXME: No Director for you!
            _Universe->star_system[i]->Update( 1, true /*need to run python serverside*/ );
        StarSystem::ProcessPendingJumps();
        /****************************** VS STUFF TO DO ************************************/
        if (snapchanged && (curtime-snaptime) > NETWORK_ATOM)
        {
            //If planet time we send planet and nebula info
            if ( (curtime-planettime) > PLANET_ATOM )
            {
                zonemgr->broadcastSnapshots( true );
                planettime = curtime;
            }
            //Otherwise we just send ships/bases... info
            else
            {
                zonemgr->broadcastSnapshots( false );
            }
            snapchanged = 0;
            snaptime    = curtime;
        }
        sendNewUnitQueue();
        //Check for automatic server status save time (in seconds)
        if ( (curtime-savetime) > SAVE_ATOM )
        {
            //Not implemented
            cout<<">>> Saving server status... Time="<<curtime<<endl;
            this->save();
            savetime = curtime;
            cout<<"<<< Finished saving."<<endl;
        }
        _sock_set.waste_time( 0, 10000 );
    }
    delete CONFIGFILE;
    delete vs_config;
    vs_config = NULL;
    this->closeAllSockets();
}

/*
 *************************************************************
 **** Check keyboard interaction                           ***
 *************************************************************
 */

void NetServer::checkKey( SocketSet &sets )
{
#if 0
    int  memory_use = 0;
    char c;
    if ( sets.select( 0, 0 ) )
    {
        if (read( 0, &c, 1 ) == -1)
            cerr<<"Error reading char on std input "<<endl;
        if (c != 0x0a)
        {
            input_buffer[nbchars] = c;
            nbchars++;
        }
        else
        {
            if ( !strncmp( input_buffer, "quit", 4 ) || !strncmp( input_buffer, "QUIT", 4 ) )
            {
                VSExit( 0 );
            }
            else if ( !strncmp( input_buffer, "stats", 4 ) || !strncmp( input_buffer, "STATS", 4 ) )
            {
                //Display server stats
                cout<<endl;
                cout<<"-----------------------------------------------"<<endl;
                cout<<"| Server stats                                |"<<endl;
                cout<<"-----------------------------------------------"<<endl<<endl;
                cout<<"\tNumber of loaded and active star systems :\t"<<_Universe->star_system.size()<<endl;
                cout<<"\tNumber of players in all star systems :\t\t"<<allClients.size()<<endl;
                cout<<"\t\tClients : "<<allClients.size()<<endl;
                cout<<"\tNumber of clients waiting for authorization :\t"<<waitList.size()<<endl<<endl;
                zonemgr->displayStats();
                cout<<"-----------------------------------------------"<<endl;
                cout<<"| End stats                                   |"<<endl;
                cout<<"-----------------------------------------------"<<endl<<endl;
            }
            else if (!strncmp( input_buffer, "mem",
                               3 ) || !strncmp( input_buffer, "MEM", 3 ) || input_buffer[0] == 'm' && nbchars == 1)
            {
                //Display memory usage
                cout<<endl;
                cout<<"-----------------------------------------------"<<endl;
                cout<<"| Server memory usage                         |"<<endl;
                cout<<"-----------------------------------------------"<<endl<<endl;
                memory_use  = sizeof (ServerSocket)*2+sizeof (class Packet)*2+sizeof (class SaveGame)+sizeof (class ZoneMgr);
                memory_use += sizeof (int)*5+sizeof (SOCKETALT)+sizeof (struct timeval);
                //List of clients
                memory_use += sizeof (Client*)*allClients.size()+discList.size()*sizeof (Client*)+waitList.size()
                              *sizeof (struct WaitListEntry);
                cout<<"\tSize of NetServer variables :\t"<<(memory_use/1024)<<" KB ("<<memory_use<<" bytes)"<<endl;
                memory_use += zonemgr->displayMemory();
                cout<<"\t========== TOTAL MEMORY USAGE = "
                    <<(memory_use/1024)<<" KB ("<<memory_use<<" bytes) ==========="<<endl<<endl;
            }
            nbchars = 0;
            memset( input_buffer, 0, MAXINPUT );
        }
    }
#endif
}

/*
 *************************************************************
 **** Check which clients are sending data to the server   ***
 *************************************************************
 */

//NETFIXME: Completely separate code logic in debug and non-debug. put #ifdef's only around print statements.

void NetServer::checkMsg( SocketSet &sets )
#ifdef VSNET_DEBUG
{
    ostringstream ostr;
    bool printit = false;
    ostr<<"Checking activity on sockets, TCP=";
    for (LI i = allClients.begin(); i != allClients.end(); i++)
    {
        ClientPtr cl = *i;
        if ( cl->sock.isActive() )
        {
            ostr<<cl->sock.get_fd()<<"+ ";
            printit = true;
            this->recvMsg_tcp( cl );
        }
    }
    ostr<<" ";
    if ( udpNetwork->isActive() )
    {
        ostr<<"UDP="<<udpNetwork->get_fd()<<"+"<<ends;
        recvMsg_udp();
        printit = true;
    }
    if ( tcpNetwork->isActive() )
        newConnection_tcp();
    ostr<<ends;
    if (printit) COUT<<ostr.str()<<endl;
}
#else
{
    for (LI i = allClients.begin(); i != allClients.end(); i++)
    {
        ClientPtr cl = *i;
        if ( cl->tcp_sock.isActive() )
            this->recvMsg_tcp( cl );
    }
    if ( udpNetwork->isActive() )
        recvMsg_udp();
    if ( tcpNetwork->isActive() )
        newConnection_tcp();
}
#endif

//Return true if ok, false if we received a late packet
bool NetServer::updateTimestamps( ClientPtr cltp, Packet &p )
{
    assert( cltp );
    Client *clt = cltp.get();

    bool    ret = true;
    //A packet's timestamp is in ms whereas getNewTime is in seconds
    unsigned int int_ts = p.getTimestamp();

    double curtime = getNewTime();
    //Check for late packet : compare received timestamp to the latest we have
    if ( int_ts < clt->getLatestTimestamp() )
    {
        //This is not really a reliable test -> we may still have late packet in that range of timestamps
        //Only check for late packets when sent non reliable because we need others
        ret = !(p.getCommand() == CMD_SNAPSHOT || p.getCommand() == CMD_POSUPDATE || p.getCommand() == CMD_PING);            //only invalidates updates if its a snapshot or posupdate--same reason it updates the timestamps to begin with
    }
    //If packet is late we don't update time vars but we process it if we have to
    else
    {
        //Update the timeout vals anytime we receive a packet
        //Set old_timeout to the old_latest one and latest_timeout to current time in seconds
        clt->old_timeout    = clt->latest_timeout;
        clt->latest_timeout = curtime;
        //Packet is not late so we update timestamps only when receving a CMD_SNAPSHOT
        //because we predict and interpolate based on the elapsed time between 2 SNAPSHOTS or PING
        if (p.getCommand() == CMD_SNAPSHOT || p.getCommand() == CMD_POSUPDATE || p.getCommand() == CMD_PING)
            //Set old_timestamp to the old latest_timestamp and the latest_timestamp to the received one
            clt->setLatestTimestamp( int_ts );
    }
    return ret;
}

/*
 *************************************************************
 **** Add a client in the game                             ***
 *************************************************************
 */

void NetServer::processPacket( ClientPtr clt, unsigned char cmd, const AddressIP &ipadr, Packet &p )
{
    packet = p;

    Packet         p2;
    NetBuffer      netbuf( packet.getData(), packet.getDataLength() );
    if (clt)
        clt->versionBuf( netbuf );
    unsigned int   mount_num;
    unsigned short zone;
    char      mis;
    //Find the unit
    Unit     *un    = NULL;
    Unit     *unclt = NULL;
    ObjSerial target_serial;
    ObjSerial packet_serial = p.getSerial();
    switch (cmd)
    {
    case CMD_CONNECT:
    {
        if (!clt) break;
        clt->netversion = packet_serial;
        if (clt->netversion > SERVER_NETVERSION)
            clt->netversion = SERVER_NETVERSION;
        Packet    psend;
        NetBuffer netnewbuf;
        netnewbuf.addSerial( SERVER_NETVERSION );
        netnewbuf.addString( clt->cltadr.ipadr() );
        psend.send( CMD_CONNECT, 0, netnewbuf.getData(), netnewbuf.getDataLength(), SENDRELIABLE,
                    &ipadr, clt->tcp_sock, __FILE__, PSEUDO__LINE__( 656 ) );
        break;
    }
    case CMD_LOGIN:
        if (!clt) break;
        COUT<<">>> LOGIN REQUEST --------------------------------------"<<endl;
        //Authenticate client
        //Need to give the IP address of incoming message in UDP mode to store it
        //in the Client struct
        if (!acctserver)
        {
            this->localLogin( clt, packet );                    //NETFIXME--right now assume acctserver
        }
        else if (!acct_con)
        {
            this->sendLoginUnavailable( clt );
        }
        else
        {
            SOCKETALT        tmpsock;
            WaitListEntry    entry;
            NetBuffer        netbuf( packet.getData(), packet.getDataLength() );
            std::string      user   = netbuf.getString();
            std::string      passwd = netbuf.getString();
            //This must be a TCP client
            entry.tcp  = true;
            entry.type = WaitListEntry::CONNECTING;
            entry.t    = clt;
            if ( user.empty() )
            {
                sendLoginError( clt );
                break;
            }
            if (clt->loginstate != Client::CONNECTED)
                break;
            if ( waitList.find( user ) != waitList.end() )
            {
                sendLoginAlready( clt );
                break;
            }
            tmpsock = clt->tcp_sock;

            //Redirect the login request packet to account server
            COUT<<"Redirecting login request to account server on socket "<<*acct_sock<<endl
                <<"*** Packet to copy length : "<<packet.getDataLength()<<endl;
            char redirectcommand[2] = {ACCT_LOGIN, '\0'};
            std::string redirect( redirectcommand );
            for (unsigned int i = 0; i < _Universe->numPlayers(); i++)
            {
                Cockpit *cp = _Universe->AccessCockpit( i );
                if (cp->savegame && cp->savegame->GetCallsign() == user)
                {
                    COUT<<"Cannot login player "<<user<<": already exists on this server!";
                    sendLoginAlready( clt );
                    user = "";
                }
            }
            if ( !user.empty() )
            {
                addSimpleString( redirect, user );
                addSimpleString( redirect, passwd );
                if ( !acct_sock->sendstr( redirect ) )
                {
                    //NETFIXME is this in http format or binary format
                    perror( "FATAL ERROR sending redirected login request to ACCOUNT SERVER : " );
                    COUT<<"SOCKET was : "<<acct_sock<<endl;
                    this->sendLoginUnavailable( clt );
                    break;
                }
                this->waitList[user] = (entry);
                clt->loginstate = Client::WAITLISTED;

                getSimpleChar( redirect );
                clt->callsign   = getSimpleString( redirect );
                clt->passwd     = getSimpleString( redirect );
            }
        }
        COUT<<"<<< LOGIN REQUEST --------------------------------------"<<endl;
        break;
    case CMD_CHOOSESHIP:
        if (!acctserver)
            this->chooseShip( clt, packet );
        //No logic since accountserver only supports one ship per player.
        else
            this->sendLoginError( clt );                   //Client is in a confused state if it sends this here.
        //chooseShip is currently intended to be a temporary, one-time selection.
        //In the future it can be expanded to pick a player ship from an account if there is more than one.
        break;
    case CMD_ADDCLIENT:
        //Add the client to the game
        COUT<<">>> ADD REQUEST =( serial #"<<packet.getSerial()<<" )= --------------------------------------"<<endl;
        //COUT<<"Received ADDCLIENT request"<<endl;
        this->addClient( clt );
        COUT<<"<<< ADD REQUEST --------------------------------------------------------------"<<endl;
        break;
    case CMD_POSUPDATE:
        //Received a position update from a client
        this->posUpdate( clt );
        break;
    case CMD_PING:
        //Nothing to do here, just receiving the packet is enough
        break;
    case CMD_SERVERTIME:

        serverTimeInitUDP( clt, netbuf );
        {}
        break;
    case CMD_TXTMESSAGE:
    {
        if (!clt) break;
        un = clt->game_unit.GetUnit();
        string message = netbuf.getString();
        netbuf.Reset();
        if ( message.empty() ) break;
        if (message[0] == '/')
        {
            string cmd, args;
            bool   local = (clt->cltadr.inaddr() == 0x0100007f);
            if (!acctserver)
                //NETFIXME: Trusted always true in deathmatch!
                local = true;
            int cp = _Universe->whichPlayerStarship( un );
            if (cp < 0)
            {
                if (local)
                    cp = 0;
                else
                    break;
            }
            std::replace( message.begin(), message.end(), '\n', ' ' );
            std::replace( message.begin(), message.end(), '\r', ' ' );
            string::size_type first_space = message.find( ' ' );
            if (first_space == string::npos)
            {
                cmd = message.substr( 1 );
            }
            else
            {
                cmd  = message.substr( 1, first_space-1 );
                args = message.substr( first_space+1 );
            }
            UniverseUtil::receivedCustom( cp, local, cmd, args, string() );
            break;
        }
        if (!un) break;
        message = message.substr( 0, 160 );
        std::replace( message.begin(), message.end(), '#', '$' );
        std::replace( message.begin(), message.end(), '\n', ' ' );
        std::replace( message.begin(), message.end(), '\r', ' ' );
        netbuf.addString( clt->callsign );
        netbuf.addString( message );
        p2.bc_create( CMD_TXTMESSAGE, un->GetSerial(),
                      netbuf.getData(), netbuf.getDataLength(), SENDRELIABLE,
                      __FILE__, PSEUDO__LINE__( 1293 ) );
        //Send to concerned clients
        zonemgr->broadcast( un->getStarSystem()->GetZone(), un->GetSerial(), &p2, true );
        COUT<<"Received text message from client "<<clt->callsign<<endl;
        break;
    }
    case CMD_LOGOUT:
        if (clt->loginstate >= Client::LOGGEDIN)
        {
            COUT<<">>> LOGOUT REQUEST =( serial #"<<packet.getSerial()<<" )= --------------------------------------"<<endl;
            //Client wants to quit the game
            logoutList.push_back( clt );
            COUT<<"<<< LOGOUT REQUEST -----------------------------------------------------------------"<<endl;
        }
        break;
    case CMD_CUSTOM:
    {
        if (!clt) break;
        un = clt->game_unit.GetUnit();
        int  cp = _Universe->whichPlayerStarship( un );
        //NETFIXME: CMD_CUSTOM should work with a dead unit.
        bool trusted = (clt->cltadr.inaddr() == 0x0100007f);
        if (!acctserver)
            //NETFIXME: Trusted always true in deathmatch!
            trusted = true;
        if (cp < 0)
        {
            if (trusted)
                cp = 0;
            else
                break;                     //You died or something... too bad.
        }
        string cmd  = netbuf.getString();
        string args = netbuf.getString();
        string id   = netbuf.getString();
        UniverseUtil::receivedCustom( cp, trusted, cmd, args, id );
        break;
    }
    //SHOULD NOT BE USED ANYMORE
    case CMD_ASKFILE:
        break;
    case CMD_SAVEACCOUNTS:
        COUT<<"Received a save request for "
            <<clt->callsign<<" ("<<packet_serial<<")..."<<endl;
        un = clt->game_unit.GetUnit();
        if (un)
        {
            int cpnum = _Universe->whichPlayerStarship( un );
            if (cpnum != -1)
                saveAccount( cpnum );
        }
        break;
    case CMD_KILL:
        un = clt->game_unit.GetUnit();
        if (un)
        {
            un->hull = 0;
            un->Destroy();
        }
        break;
    case CMD_RESPAWN:
        COUT<<"Received a respawning request for "
            <<clt->callsign<<"..."<<endl;
        {
            //Remove the client from its current starsystem
            Unit *oldun = clt->game_unit.GetUnit();
            if (oldun == NULL || oldun->GetHull() <= 0)
            {
                zonemgr->removeClient( clt );
                if (oldun) oldun->Kill( true, true );
                Cockpit *cp = loadCockpit( clt ); //Should find existing cp.
                loadFromSavegame( clt, cp );
                //actually cp not used
                this->addClient( clt );
            }
            else
            {
                COUT<<clt->callsign<<"'s not quite dead yet laddie. Disallowing respawn\n";
            }
        }
        break;
    case CMD_SHIPDEALER:
    {
        std::string cargoName = netbuf.getString();
        int      type   = netbuf.getChar();

        Unit    *docked = NULL;
        Unit    *player = clt->game_unit.GetUnit();
        if (!player) break;
        int      cpnum  = _Universe->whichPlayerStarship( player );
        if (cpnum == -1) break;
        Cockpit *cp     = _Universe->AccessCockpit( cpnum );
        {
            const Unit *un;
            for (un_kiter ui = player->getStarSystem()->getUnitList().constIterator(); (un = *ui); ++ui)
                if ( un->isDocked( player ) )
                {
                    docked = const_cast< Unit* > (un);                      //Stupid STL.
                    break;
                }
        }
        if (!docked) break;
        if (type == Subcmd::BuyShip)
        {
            unsigned int cargIndex = UINT_MAX;
            Cargo *cargptr = docked->GetCargo( cargoName, cargIndex );
            if (cargIndex == UINT_MAX || !cargptr) break;
            if (cargptr->price > cp->credits) break;
            saveAccount( cpnum );
            player->hull = 0;
            player->Destroy();
        }
        break;
    }
    case CMD_DOWNLOAD:
        COUT<<">>> CMD DOWNLOAD =( serial #"<<packet.getSerial()<<" )= --------------------------------------"<<endl;
        if (_downloadManagerServer)
            _downloadManagerServer->addCmdDownload( clt->tcp_sock, netbuf );
        COUT<<"<<< CMD DOWNLOAD --------------------------------------------------------------"<<endl;
        break;
    case CMD_FIREREQUEST:
        //Here should put a flag on the concerned mount of the concerned Unit to say we want to fire
        //target_serial is in fact the serial of the firing unit (client itself or turret)
        target_serial = netbuf.getSerial();
        un = clt->game_unit.GetUnit();
        if (!un)
        {
            COUT<<"ERROR --> Received a fire order for dead UNIT"<<endl;
            break;                     //Don't fire from a dead unit...
        }
        zone = un->getStarSystem()->GetZone();
        mis  = netbuf.getChar();
        mount_num = (unsigned int)netbuf.getInt32();
        //Find the unit
        //Set the concerned mount as ACTIVE and others as INACTIVE
        un = zonemgr->getUnit( target_serial, zone );
        if (un == NULL)
        {
            COUT<<"ERROR --> Received a fire order for non-existing UNIT"<<endl;
        }
        else
        {
            if ( mount_num > un->mounts.size() )
            {
                COUT<<"ERROR recvd information about "<<mount_num<<" mounts, only "<<un->mounts.size()<<" on ship"<<std::endl;
                mount_num = un->mounts.size();
            }
            printf( "[x " );

            vector< Mount >
            ::iterator i = un->mounts.begin();                            //note to self: if vector<Mount *> is ever changed to vector<Mount> remove the const_ from the const_iterator
            for (; i != un->mounts.end(); ++i)
            {
                printf( "%.1f, ", (*i).time_to_lock );
                (*i).status = Mount::INACTIVE;
            }
            for (unsigned int j = 0; j < mount_num; ++j)
            {
                unsigned int mnt = (unsigned int)netbuf.getInt32();
                if (mnt < un->mounts.size() && mnt >= 0)
                    un->mounts[mnt].status = Mount::ACTIVE;
                else
                    COUT<<"ERROR --> Received a fire order on an invalid MOUNT: "<<mount_num<<" > "<<( un->mounts.size() )
                        <<endl;
            }
            //Ask for fire
            if (mis != 0)
                un->Fire( ROLES::FIRE_MISSILES|ROLES::EVERYTHING_ELSE, false );
            else
                un->Fire( ROLES::EVERYTHING_ELSE|ROLES::FIRE_GUNS, false );
            printf( "]\n" );
        }
        break;
    case CMD_UNFIREREQUEST:
        target_serial = netbuf.getSerial();
        mount_num     = (unsigned int)netbuf.getInt32();
        un = clt->game_unit.GetUnit();
        if (!un)
        {
            COUT<<"ERROR --> Received an unfire order for dead UNIT"<<endl;
            break;                     //Don't fire from a dead unit...
        }
        zone = un->getStarSystem()->GetZone();
        //Find the unit
        //Set the concerned mount as ACTIVE and others as INACTIVE
        un   = zonemgr->getUnit( target_serial, zone );
        if (un == NULL)
        {
            COUT<<"ERROR --> Received an unfire order for non-existing UNIT"<<endl;
        }
        else
        {
            vector< Mount >
            ::iterator i = un->mounts.begin();                            //note to self: if vector<Mount *> is ever changed to vector<Mount> remove the const_ from the const_iterator
            if ( mount_num > un->mounts.size() )
            {
                COUT<<"ERROR recvd information about "<<mount_num<<" mounts, only "<<un->mounts.size()<<" on ship"<<std::endl;
                mount_num = un->mounts.size();
            }
            for (; i != un->mounts.end(); ++i)
                (*i).status = Mount::INACTIVE;
            for (unsigned int j = 0; j < mount_num; j++)
            {
                unsigned int mnt = (unsigned int)netbuf.getInt32();
                if (mnt < un->mounts.size() && mnt >= 0)
                    un->mounts[mnt].status = Mount::ACTIVE;
                else
                    COUT<<"ERROR --> Received an unfire order on an invalid MOUNT: "<<mount_num<<" > "<<( un->mounts.size() )
                        <<endl;
            }
            //Ask for fire
            un->UnFire();
        }
        break;
    case CMD_JUMP:
    {
        if (clt && clt->loginstate > Client::LOGGEDIN)
        {
            un = clt->game_unit.GetUnit();
            if (un)
                //Do Magic.
                un->ActivateJumpDrive();
        }
        break;
        //Everything handled by Magic.  We don't need this any more.
        string    newsystem        = netbuf.getString();
#ifdef CRYPTO
        unsigned char *server_hash = new unsigned char[FileUtil::Hash.DigestSize()];
        unsigned char *client_hash = netbuf.getBuffer( FileUtil::Hash.DigestSize() );
#endif
        cerr<<"ATTEMPTING TO JUMP, BUT JUMP UNIMPLEMENTED"<<endl;
        bool found = false;
        NetBuffer   netbuf2;
        Cockpit    *cp;
        un = clt->game_unit.GetUnit();
        if (un == NULL)
        {
            COUT<<"ERROR --> Received a jump request from non-existing UNIT"<<endl;
        }
        else
        {
            cp = _Universe->isPlayerStarship( un );
            //Verify if there really is a jump point to the new starsystem
            const vector< string > &adjacent = _Universe->getAdjacentStarSystems( cp->savegame->GetStarSystem()+".system" );
            for (unsigned int i = 0; !found && i < adjacent.size(); i++)
                if (adjacent[i] == newsystem)
                    found = true;
            if (found)
            {
                //Then activate jump drive to say we want to jump
                un->ActivateJumpDrive();
                //The jump reply is sent in Unit::jumpReactToCollision()
                //In the meantime we create the star system if it isn't loaded yet
                //The starsystem maybe loaded for nothing if the client has not enough warp energy to jump
                //but that's no big deal since they all will be loaded finally
            }
        }
#ifdef CRYPTO
        delete[] server_hash;
#endif
        break;
    }
    case CMD_MISSION:
    {
        Unit  *sender       = clt->game_unit.GetUnit();
        if (!sender) break;
        int    playernum    = _Universe->whichPlayerStarship( sender );
        if (playernum < 0) break;
        unsigned short type = netbuf.getShort();
        string qualname     = netbuf.getString();
        int    pos = netbuf.getInt32();
        if (type == Subcmd::TerminateMission)
        {
            //Abstracting number of actual missions running on server.
            //(added 1 for player's main mission).
            Mission *mis = Mission::getNthPlayerMission( playernum, (pos+1) );
            if (mis)
                //Found it!
                mis->terminateMission();
        }
        else if (type == Subcmd::AcceptMission)
        {
            string finalScript;
            unsigned int stringCount = getSaveStringLength( playernum, MISSION_NAMES_LABEL );
            unsigned int temp = getSaveStringLength( playernum, MISSION_DESC_LABEL );
            if (temp < stringCount) stringCount = temp;
            temp = getSaveStringLength( playernum, MISSION_SCRIPTS_LABEL );
            if (temp < stringCount) stringCount = temp;
            for (unsigned int i = 0; i < stringCount; i++)
            {
                if (getSaveString( playernum, MISSION_NAMES_LABEL, i ) == qualname)
                {
                    finalScript = getSaveString( playernum, MISSION_SCRIPTS_LABEL, i );
                    eraseSaveString( playernum, MISSION_SCRIPTS_LABEL, i );
                    eraseSaveString( playernum, MISSION_NAMES_LABEL, i );
                    eraseSaveString( playernum, MISSION_DESC_LABEL, i );
                    break;
                }
            }
            if ( finalScript.empty() ) break;
            unsigned int oldcp = _Universe->CurrentCockpit();
            _Universe->SetActiveCockpit( playernum );
            _Universe->pushActiveStarSystem( _Universe->AccessCockpit()->activeStarSystem );
            string nission     = string( "#" )+qualname;
            LoadMission( nission.c_str(), finalScript, false );
            _Universe->popActiveStarSystem();
            _Universe->SetActiveCockpit( oldcp );
        }
        break;
    }
    case CMD_COMM:
    {
        ObjSerial send_to = netbuf.getSerial();
        Unit     *targ    = UniverseUtil::GetUnitFromSerial( send_to );
        if (!targ)
            break;
        char  newEdge     = netbuf.getChar();
        int   node   = netbuf.getInt32();
        Unit *parent = clt->game_unit.GetUnit();
        if (!parent)
            break;
        FSM  *fsm    = FactionUtil::GetConversation( parent->faction, targ->faction );
        int   oldNode;
        int   newNode;
        if (newEdge < 0)
        {
            oldNode = fsm->getDefaultState( parent->getRelation( targ ) );
            if ( node < 0 || (unsigned int) node >= fsm->nodes.size() )
                break;
            newNode = node; //fixme make sure it's a default node, or go to a special one.
        }
        else
        {
            oldNode = node; //fixme validate
            if ( oldNode < 0 || (unsigned int) node >= fsm->nodes.size() )
                break;
            if ( (unsigned int) newEdge >= fsm->nodes[oldNode].edges.size() )
                break;
            newNode = fsm->nodes[oldNode].edges[newEdge];
        }
        unsigned char sex = 0;
        if (parent->pilot)
            sex = parent->pilot->getGender();
        CommunicationMessage c( parent, targ, oldNode, newNode, NULL, sex );
        Order *oo = targ->getAIState();
        if (oo)
            oo->Communicate( c );
        break;
    }
    case CMD_CARGOUPGRADE:
    {
        ObjSerial   buyer_ser  = netbuf.getSerial();
        ObjSerial   seller_ser = netbuf.getSerial();
        int quantity = netbuf.getInt32();
        std::string cargoName  = netbuf.getString();
        int      mountOffset   = ( (int) netbuf.getInt32() );
        int      subunitOffset = ( (int) netbuf.getInt32() );
        Unit    *sender     = clt->game_unit.GetUnit();
        Cockpit *sender_cpt = _Universe->isPlayerStarship( sender );
        if (!sender || !sender->getStarSystem() || !sender_cpt) break;
        zone = sender->getStarSystem()->GetZone();
        unsigned int cargIndex = UINT_MAX;
        Unit *seller = zonemgr->getUnit( seller_ser, zone );
        Unit *buyer  = zonemgr->getUnit( buyer_ser, zone );
        Unit *docked = NULL;
        {
            const Unit *un;
            for (un_kiter ui = sender->getStarSystem()->getUnitList().constIterator(); (un = *ui); ++ui)
            {
                if ( un->isDocked( sender ) )
                {
                    docked = const_cast< Unit* > (un); //Stupid STL.
                    break;
                }
            }
        }
        if (docked)
        {
            if (seller == sender)
            {
                buyer = docked;
            }
            else
            {
                seller = docked;
                buyer  = sender;
            }
        }
        else
        {
            if (seller == sender)
                buyer = NULL;
            else
                seller = NULL;
        }
        Cockpit *buyer_cpt   = _Universe->isPlayerStarship( buyer );
        Cockpit *seller_cpt  = _Universe->isPlayerStarship( seller );

        bool     sellerEmpty = false;
        Cargo   *cargptr     = NULL;
        if (seller)
            cargptr = seller->GetCargo( cargoName, cargIndex );
        if (!cargptr)
        {
            cargIndex   = UINT_MAX;
            cargptr     = GetMasterPartList( cargoName.c_str() );
            sellerEmpty = true;
            if (!cargptr)
            {
                fprintf( stderr, "Player id %d attempted transaction with NULL cargo %s, %d->%d\n",
                         sender ? sender->GetSerial() : -1, cargoName.c_str(),
                         buyer ? buyer->GetSerial() : -1, seller ? seller->GetSerial() : -1 );
                //Return the credits.
                sendCredits( sender->GetSerial(), sender_cpt->credits );
                break;
            }
        }
        Cargo carg       = *cargptr;
        bool  upgrade    = false;
        bool  weapon     = false;
        bool  didMoney   = false;
        bool  didUpgrade = false;
        bool  repair     = false;
        if (carg.GetCategory().find( "upgrades" ) == 0)
        {
            upgrade = true;
            if ( isWeapon( carg.GetCategory() ) )
                weapon = true;
            else if (!quantity && buyer == sender)
                repair = true;
        }
        if (weapon && quantity)
        {
            //Return the credits.
            sendCredits( sender->GetSerial(), sender_cpt->credits );
            break;
        }
        if (!weapon && sellerEmpty)
        {
            //Cargo does not exist... allowed only for mounted cargo.
            sendCredits( sender->GetSerial(), sender_cpt->credits );
            break;
        }
        if (!weapon && !quantity && !repair)
        {
            sendCredits( sender->GetSerial(), sender_cpt->credits );
            break;
        }
        if (seller == NULL)
        {
            sendCredits( sender->GetSerial(), sender_cpt->credits );
            break;
        }
        //Guaranteed: seller, sender, sender_cpt are not NULL.
        if (buyer == NULL)
        {
            if (cargIndex != UINT_MAX)
                seller->EjectCargo( cargIndex );
            quantity = 0;               //So that the cargo won't be bought/sold again.
        }
        if (quantity && cargIndex != UINT_MAX)
        {
            //Guaranteed: buyer, sender, seller, and one cockpit are not null.
            //Guaranteed: Not a weapon: (weapon && quantity) is disallowed.
            _Universe->netLock( true );
            if (buyer == sender && buyer_cpt)
            {
                float  creds_before = buyer_cpt->credits;
                float &creds = buyer_cpt->credits;
                if ( buyer->BuyCargo( cargIndex, quantity, seller, creds ) )
                {
                    didMoney = true;
                    if (seller_cpt)
                        seller_cpt->credits += (creds_before-creds);
                }
            }
            else if (seller == sender && seller_cpt)
            {
                float  creds_before = seller_cpt->credits;
                float &creds   = seller_cpt->credits;
                bool   success = false;
                if ( !carg.GetMissionFlag() )
                    success = seller->SellCargo( cargIndex, quantity, creds, carg, buyer );
                else
                    success = seller->RemoveCargo( cargIndex, quantity, true );
                didMoney = success;
                if (success)
                    if (buyer_cpt)
                        buyer_cpt->credits += (creds_before-creds);
            }
            _Universe->netLock( false );
        }
        if ( (didMoney || weapon) && upgrade && (seller == sender || buyer == sender) )
        {
            double percent;                 //not used.
            const Unit *unitCarg = getUnitFromUpgradeName( carg.GetContent(), seller->faction );
            if (!unitCarg)
            {
                //Return the credits.
                sendCredits( sender->GetSerial(), sender_cpt->credits );
                break;                     //not an upgrade, and already did cargo transactions.
            }
            int multAddMode = GetModeFromName( carg.GetContent().c_str() );

            //Now we're sure it's an authentic upgrade...
            //Wow! So much code just to perform an upgrade!

            string templateName;
            int    faction; //FIXME If neither the seller nor the buyer is the sender, faction is uninitialized!!!
            faction = 0; //FIXME This line temporarily added by chuck_starchaser
            const string unitDir = GetUnitDir( sender->name.get().c_str() );
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
            //Get the "limiter" for the upgrade.  Stats can't increase more than this.
            const Unit *templateUnit = UnitConstCache::getCachedConst( StringIntKey( templateName, faction ) ); //FIXME faction uninitialized!!!
            if (!templateUnit)
                templateUnit = UnitConstCache::setCachedConst( StringIntKey( templateName, faction ), //FIXME faction uninitialized!!!
                               UnitFactory::createUnit( templateName.c_str(), true, faction ) );
            if (templateUnit->name == LOAD_FAILED)
                templateUnit = NULL;
            if (unitCarg->name == LOAD_FAILED)
            {
                //Return money.
                sendCredits( sender->GetSerial(), sender_cpt->credits );
                break;
            }
            if (seller == sender)
            {
                //Selling it... Downgrade time!
                if ( seller->canDowngrade( unitCarg, mountOffset, subunitOffset, percent, templateUnit ) )
                {
                    if (weapon)
                    {
                        if (seller_cpt)
                        {
                            didMoney = true;
                            seller_cpt->credits += carg.GetPrice();
                        }
                        if (buyer && didMoney)
                            buyer->AddCargo( carg, true );
                    }
                    if (didMoney)
                    {
                        _Universe->netLock( true );
                        seller->Downgrade( unitCarg, mountOffset, subunitOffset, percent, templateUnit );
                        _Universe->netLock( false );
                        didUpgrade = true;
                    }
                }
            }
            else if (buyer == sender)
            {
                //Buying it... Upgrade time!
                if ( buyer->canUpgrade( unitCarg, mountOffset, subunitOffset, multAddMode, true, percent,
                                        templateUnit ) )
                {
                    if (weapon)
                    {
                        if ( buyer_cpt && buyer_cpt->credits > carg.GetPrice() )
                        {
                            buyer_cpt->credits -= carg.GetPrice();
                            didMoney = true;
                        }
                        if (seller && didMoney && cargIndex != UINT_MAX)
                            seller->RemoveCargo( cargIndex, 1, true );
                    }
                    if (didMoney)
                    {
                        _Universe->netLock( true );
                        buyer->Upgrade( unitCarg, mountOffset, subunitOffset, multAddMode, true, percent, templateUnit );
                        _Universe->netLock( false );
                        didUpgrade = true;
                    }
                }
            }
        }
        if (repair && !didMoney)
            didMoney = sender->RepairUpgradeCargo( &carg, seller, sender_cpt ? &sender_cpt->credits : NULL );
        if (sender_cpt)
            //The client always needs to get credits back, no matter what.
            sendCredits( sender->GetSerial(), sender_cpt->credits );
        //Otherwise, it will get stuck with 0 credits.
        if (didMoney)
        {
            ObjSerial buyer_ser = buyer ? buyer->GetSerial() : 0;
            if (!upgrade)
            {
                BroadcastCargoUpgrade( sender->GetSerial(), buyer_ser, seller->GetSerial(), cargoName,
                                       carg.GetPrice(), carg.GetMass(), carg.GetVolume(), carg.GetMissionFlag(),
                                       quantity, 0, 0, zone );
            }
            else if (didUpgrade)
            {
                BroadcastCargoUpgrade( sender->GetSerial(), buyer_ser, seller->GetSerial(), cargoName,
                                       carg.GetPrice(), carg.GetMass(), carg.GetVolume(), false,
                                       weapon || repair ? 0 : 1, mountOffset, subunitOffset, zone );
            }
        }
        //Completed transaction.
        //Send player new amount of credits.
        //Broadcast out cargo request.
        break;
    }
    case CMD_TARGET:
        //Received a computer targetting request
        target_serial = netbuf.getSerial();
        unclt = clt->game_unit.GetUnit();
        if (!unclt)
            break;
        {
            StarSystem *ss = unclt->getStarSystem();
            if (!ss)
            {
                COUT<<"StarSystem for client "<<clt->callsign
                    <<", "<<unclt->GetSerial()<<" not found!"<<endl;
                break;
            }
            _Universe->pushActiveStarSystem( ss );
            zone = ss->GetZone();
        }
        //NETFIXME: Make sure that serials have 0 allocated for NULL
        un = zonemgr->getUnit( target_serial, zone );
        if (unclt)
            //It's fine if un is null...
            unclt->Target( un );
        _Universe->popActiveStarSystem();
        break;
    case CMD_CLOAK:
    {
        //Received a computer targetting request
        char engage = netbuf.getChar();
        unclt = clt->game_unit.GetUnit();
        if (!unclt)
            break;
        unclt->Cloak( engage );
        break;
    }
    case CMD_SCAN:
        //Received a target scan request
        //NETFIXME: WE SHOULD FIND A WAY TO CHECK THAT THE CLIENT HAS THE RIGHT SCAN SYSTEM FOR THAT
        target_serial = netbuf.getSerial();
        unclt = clt->game_unit.GetUnit();
        zone  = unclt->activeStarSystem->GetZone();                //netbuf.getShort();
        un    = zonemgr->getUnit( target_serial, zone );
        //Get the un Unit data and send it in a packet
        //Here we should get what a scanner could get on the target ship
        //Get the unit that asked for target info
        netbuf.Reset();
        unclt = zonemgr->getUnit( packet.getSerial(), zone );
        //Add armor data

        //Add shield data


        //Add hull

        //Add distance

        break;
    case CMD_STARTNETCOMM:
    {
        un = clt->game_unit.GetUnit();
        if (!un) break;
        float freq = netbuf.getFloat();
        clt->comm_freq = freq;
        clt->secured   = netbuf.getChar();
        clt->webcam    = netbuf.getChar();
        clt->portaudio = netbuf.getChar();
        //Broadcast players with same frequency that there is a new one listening to it
        p2.bc_create( packet.getCommand(), packet_serial,
                      packet.getData(), packet.getDataLength(), SENDRELIABLE,
                      __FILE__, PSEUDO__LINE__( 1293 ) );
        //Send to concerned clients
        zonemgr->broadcast( un->getStarSystem()->GetZone(), packet_serial, &p2, true );
        break;
    }
    case CMD_STOPNETCOMM:
        un = clt->game_unit.GetUnit();
        if (!un) break;
        clt->comm_freq = -1;
        //Broadcast players with same frequency that this client is leaving the comm session
        p2.bc_create( packet.getCommand(), packet_serial,
                      packet.getData(), packet.getDataLength(), SENDRELIABLE,
                      __FILE__, PSEUDO__LINE__( 1302 ) );
        //Send to concerned clients
        zonemgr->broadcast( un->getStarSystem()->GetZone(), packet_serial, &p2, true );
        break;
    case CMD_SOUNDSAMPLE:
        un = clt->game_unit.GetUnit();
        if (!un) break;
        //Broadcast sound sample to the clients in the same zone and the have PortAudio support
        p2.bc_create( packet.getCommand(), packet_serial,
                      packet.getData(), packet.getDataLength(), SENDRELIABLE,
                      __FILE__, PSEUDO__LINE__( 1341 ) );
        zonemgr->broadcastSample( un->getStarSystem()->GetZone(), packet_serial, &p2, clt->comm_freq );
#if 0
    //NETFIXME maybe this code just works fine
    case CMD_TXTMESSAGE:
        un = clt->game_unit.GetUnit();
        if (!un) break;
        //Broadcast sound sample to the clients in the same zone and the have PortAudio support
        p2.bc_create( packet.getCommand(), packet_serial, ,
                      SENDRELIABLE,
                      __FILE__, PSEUDO__LINE__( 1341 ) );
        zonemgr->broadcastText( un->getStarSystem()->GetZone(), packet_serial, &p2, clt->comm_freq );
#endif
    case CMD_DOCK:
    {
        Unit     *docking_unit;
        un = clt->game_unit.GetUnit();
        if (!un) break;
        ObjSerial utdwserial   = netbuf.getSerial();
        unsigned short zonenum = un->getStarSystem()->GetZone();
        cerr<<"RECEIVED a DockRequest from unit "<<un->GetSerial()<<" to unit "<<utdwserial<<" in zone "<<zonenum<<endl;
        docking_unit = zonemgr->getUnit( utdwserial, zonenum );
        if (docking_unit)
        {
            docking_unit->RequestClearance( un );
            int dockport = un->Dock( docking_unit )-1;                  //For some reason Unit::Dock adds 1.
            if (dockport >= 0)
            {
                this->sendDockAuthorize( un->GetSerial(), utdwserial, dockport, zonenum );
                int cpt = UnitUtil::isPlayerStarship( un );
                if (cpt >= 0)
                {
                    vector< string >vec;
                    vec.push_back( docking_unit->name );
                    saveStringList( cpt, mission_key, vec );
                }
            }
            else
            {
                this->sendDockDeny( un->GetSerial(), zonenum );
            }
        }
        else
        {
            cerr<<"!!! ERROR : cannot dock with unit serial="<<utdwserial<<endl;
        }
        break;
    }
    case CMD_UNDOCK:
    {
        Unit     *docking_unit;
        un = clt->game_unit.GetUnit();
        if (!un) break;
        ObjSerial utdwserial   = netbuf.getSerial();
        unsigned short zonenum = un->getStarSystem()->GetZone();
        cerr<<"RECEIVED an UnDockRequest from unit "<<un->GetSerial()<<" to unit "<<utdwserial<<" in zone "<<zonenum<<endl;
        docking_unit = zonemgr->getUnit( utdwserial, zonenum );
        if (docking_unit)
        {
            bool undocked = un->UnDock( docking_unit );
            if (undocked)
            {
                int cpt = UnitUtil::isPlayerStarship( un );
                if (un && cpt >= 0)
                {
                    vector< string >vec;
                    vec.push_back( string() );
                    saveStringList( cpt, mission_key, vec );
                }
                this->sendUnDock( un->GetSerial(), utdwserial, zonenum );
            }
        }
        else
        {
            cerr<<"!!! ERROR : cannot dock with unit serial="<<utdwserial<<endl;
        }
        break;
    }
    default:
        un = clt->game_unit.GetUnit();
        COUT<<"Unknown command "<<Cmd( cmd )<<" ! "<<"from client ";
        if (un)
            COUT<<un->GetSerial();
        else
            COUT<<"(Dead)";
        COUT<<" ("<<clt->callsign<<")";
        COUT<<endl;
    }
}

/*
 *************************************************************
 **** Broadcast a netbuffer to a given zone                ***
 *************************************************************
 */

void NetServer::broadcast( NetBuffer &netbuf, ObjSerial serial, unsigned short zone, Cmd command, bool isTcp )
{
    Packet p;
    p.bc_create( command, serial,
                 netbuf.getData(), netbuf.getDataLength(), SENDRELIABLE,
                 __FILE__, PSEUDO__LINE__( 902 ) );
    zonemgr->broadcast( zone, 0, &p, isTcp );
}

/*
 *************************************************************
 **** Close all sockets for shutdown                       ***
 *************************************************************
 */

void NetServer::closeAllSockets()
{
    tcpNetwork->disconnect( "Closing sockettcp" );
    udpNetwork->disconnect( "Closing socketudp" );
    for (LI i = allClients.begin(); i != allClients.end(); i++)
    {
        ClientPtr cl = *i;
        cl->tcp_sock.disconnect( cl->callsign.c_str() );
    }
}

void NetServer::addSystem( string &sysname, string &system )
{
    zonemgr->addSystem( sysname, system );
}

