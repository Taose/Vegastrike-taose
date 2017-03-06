#include "networking/lowlevel/vsnet_debug.h"
#include "cmd/unit_generic.h"
#include "vs_globals.h"
#include "vsfilesystem.h"
#include "networking/netclient.h"
#include "savegame.h"
#include "main_loop.h"
#include "networking/lowlevel/netbuffer.h"
#include "networking/lowlevel/packet.h"
#include "lin_time.h"
#include "networking/lowlevel/vsnet_notify.h"
#include "networking/lowlevel/vsnet_sockethttp.h"
#include "networking/lowlevel/vsnet_dloadmgr.h"
#include "networking/lowlevel/netui.h"
#include "networking/client.h"
#include "networking/fileutil.h"

std::string global_username;
std::string global_password;
/*
 ************************************************************
 **** Authenticate the client                             ***
 ************************************************************
 */

int NetClient::authenticate()
{
    COUT<<" enter "<<__PRETTY_FUNCTION__<<endl;

    Packet    packet2;
    string    str_callsign, str_passwd;
    NetBuffer netbuf;

    /*Get the name and password from vegastrike.config
     *Maybe someday use a default Guest account if no callsign or password is provided thus allowing
     *Player to wander but not interact with the universe
     */
    this->callsign = str_callsign = vs_config->getVariable( "player", "callsign", "" );
    this->password = str_passwd = vs_config->getVariable( "player", "password", "" );
    if ( global_username.length() )
        this->callsign = global_username;
    if ( global_password.length() )
        this->password = global_password;
    if ( str_callsign.length() && str_passwd.length() )
    {
        netbuf.addString( str_callsign );
        netbuf.addString( str_passwd );

        packet2.send( CMD_LOGIN, 0,
                      netbuf.getData(), netbuf.getDataLength(),
                      SENDRELIABLE, NULL, *this->clt_tcp_sock,
                      __FILE__, PSEUDO__LINE__( 165 ) );
        COUT<<"Send login for player <"<<str_callsign<<">:<*******"
            "> - buffer length : "<<packet2.getDataLength()
            <<" (+"<<packet2.getHeaderLength()<<" header len"<<endl;
    }
    else
    {
        cerr<<"Callsign and/or password not specified in vegastrike.config, please check this."<<endl<<endl;
        return -1;
    }
    return 0;
}

/*
 ************************************************************
 **** Login loop : waiting for game server to respond     ***
 ************************************************************
 */

int NetClient::loginAuth( string str_callsign, string str_passwd, string &error )
{
    COUT<<"enter "<<"NetClient::loginLoop"<<endl;
    lastsave.clear();
    ship_select_list.clear();

    Packet    packet2;
    NetBuffer netbuf;

    netbuf.addString( str_callsign );
    netbuf.addString( str_passwd );

    packet2.send( CMD_LOGIN, 0,
                  netbuf.getData(), netbuf.getDataLength(),
                  SENDRELIABLE, NULL, *this->clt_tcp_sock,
                  __FILE__, PSEUDO__LINE__( 316 ) );
    COUT<<"Sent login for player <"<<str_callsign<<">:<*******"
        <<">"<<endl
        <<"   - buffer length : "<<packet2.getDataLength()<<endl;
    this->callsign = str_callsign;
    this->password = str_passwd;
    //Now the loop
    return loginLoop( error );
}

int NetClient::loginLoop( string &error )
{
    int     timeout = 0, recv = 0;
    Packet  packet;

    string  login_tostr = vs_config->getVariable( "network", "logintimeout", "100" );
    timeval tv = {atoi( login_tostr.c_str() ), 0};
    while (!timeout)
    {
        recv = this->recvMsg( &packet, &tv );
        if (recv == 0)
        {
            error   = "NETWORK ERROR : Login procedeure timed out.";
            timeout = 1;
        }
        else if (recv < 0)
        {
            char str[127];
            sprintf( str, "NETWORK ERROR in recieving login data (error number %d)!!!",
#ifdef _WIN32
                     WSAGetLastError()
#else
                     errno
#endif
                   );
            error   = (str);
            timeout = 1;
        }
        else
        {
            break;
        }
    }
    COUT<<"End of login loop"<<endl;
    if (lastsave.empty() || lastsave[0] == "")
    {
        if ( ship_select_list.empty() )
            error = "Login failure!";
        return ship_select_list.size();
    }
    return 1;
}

/*
 ************************************************************
 **** Login loop : waiting for account server to respond  ***
 ************************************************************
 */

vector< string >& NetClient::loginAcctLoop( string str_callsign, string str_passwd )
{
    COUT<<"enter "<<"NetClient::loginAcctLoop"<<endl;

    this->error_message = string();

    std::string netbuf;
    addSimpleChar( netbuf, ACCT_LOGIN_DATA );
    addSimpleString( netbuf, str_callsign );
    addSimpleString( netbuf, str_passwd );

    COUT<<"Buffering to send with LOGIN_DATA for "<<str_callsign<<endl;
    acct_sock->sendstr( netbuf );

    //Now the loop
    int    timeout = 0, recv = 0;
    Packet packet;
    double initial     = queryTime();
    double newtime     = 0;
    double elapsed     = 0;
    string login_tostr = vs_config->getVariable( "network", "logintimeout", "20" );
    int    login_to    = atoi( login_tostr.c_str() );

    while (!timeout && !recv)
    {
        //If we have no response in "login_to" seconds -> fails
        newtime = queryTime();
        elapsed = newtime-initial;
        if (elapsed > login_to)
        {
            COUT<<"!!! NETWORK ERROR : Connection to account server timed out !!!"<<endl;
            timeout = 1;
            break;
        }
        recv = checkAcctMsg();
        _sock_set.waste_time( 0, 40000 );
    }
    COUT<<"End of loginAcct loop"<<endl;
    //globalsaves should be empty otherwise we filled it with an empty string followed by the error message
    if (lastsave.empty() || lastsave[0] != "")
    {
        COUT<<"Trying to connect to game server..."<<endl
            <<"\tIP="<<_serverip<<":"<<_serverport<<endl;
    }
    return lastsave;
}

void NetClient::loginChooseShip( Packet &p1 )
{
    NetBuffer netbuf( p1.getData(), p1.getDataLength() );
    ship_select_list.clear();
    unsigned short numShips = netbuf.getShort();
    ship_select_list.reserve( numShips );
    for (int i = 0; i < numShips; i++)
        ship_select_list.push_back( netbuf.getString() );
}

void NetClient::loginAccept( Packet &p1 )
{
    using namespace VSFileSystem;
    NetBuffer netbuf( p1.getData(), p1.getDataLength() );

    Packet    pckt;
    this->serial = p1.getSerial();
    COUT<<">>> LOGIN ACCEPTED =( serial #"<<serial<<" )= --------------------------------------"<<endl;
    {
        char msg[100];
        sprintf( msg, "#cc66ffNETWORK: Login Accepted.  Serial number is %d.  Downloading system file...", serial );
        bootstrap_draw( msg, NULL );
    }
    //Should receive player's data (savegame) from server if there is a save
    localSerials.push_back( serial );

    string datestr = netbuf.getString();
    _Universe->current_stardate.InitTrek( datestr );
    cerr<<"Stardate initialized"<<endl;
    cerr<<"WE ARE ON STARDATE "<<datestr<<" - converted : "
        <<_Universe->current_stardate.GetFullTrekDate()<<endl;
    lastsave.push_back( netbuf.getString() );
    lastsave.push_back( netbuf.getString() );

    //Get the initial system file...
    string sysfile = netbuf.getString();
    bool   downloadsystem = true;
    bool   autogen;
    string fullsys = VSFileSystem::GetCorrectStarSysPath( sysfile, autogen );
    if ( fullsys.empty() ) fullsys = sysfile;

    COUT<<"Initial system = "<<fullsys;

    //Set the zone number
    this->zone = netbuf.getShort();
    //Did the hash compare fail?
    if (downloadsystem)
    {
        cerr<<": Downloading system from server..."<<endl;
        VsnetDownload::Client::NoteFile f( fullsys, *this->clt_tcp_sock, sysfile, VSFileSystem::SystemFile );
        _downloadManagerClient->addItem( &f );
        timeval timeout = {10, 0};
        while ( !f.done() )
            if (recvMsg( NULL, &timeout ) <= 0)
                //NETFIXME: what to do if timeout elapses...
                break;
    }
    cout<<endl;
}

void NetClient::respawnRequest()
{
    Packet    packet2;
    NetBuffer netbuf;
    //No data.
    packet2.send( CMD_RESPAWN, 0,
                  netbuf.getData(), netbuf.getDataLength(),
                  SENDRELIABLE, NULL, *this->clt_tcp_sock,
                  __FILE__, PSEUDO__LINE__( 165 ) );
}
void NetClient::textMessage( const std::string &data )
{
    Packet    packet2;
    NetBuffer netbuf;
    netbuf.addString( data );
    //No data.
    packet2.send( CMD_TXTMESSAGE, 0,
                  netbuf.getData(), netbuf.getDataLength(),
                  SENDRELIABLE, NULL, *this->clt_tcp_sock,
                  __FILE__, PSEUDO__LINE__( 165 ) );
}

void NetClient::GetCurrentServerAddress( string &addr, unsigned short &port )
{
    addr = this->_serverip;
    port = this->_serverport;
}

void NetClient::SetCurrentServerAddress( string addr, unsigned short port )
{
    this->_serverip   = addr;
    this->_serverport = port;
}

void NetClient::SetConfigServerAddress( string &addr, unsigned short &port )
{
    bool use_acctserver = XMLSupport::parse_bool( vs_config->getVariable( "network", "use_account_server", "false" ) );
    if (use_acctserver)
    {
        this->_serverport = port = 0;
        this->_serverip   = addr = vs_config->getVariable( "network", "account_server_url",
                                   "http://localhost/cgi-bin/accountserver.py" );
        cout<<endl<<"Account Server URL : "<<addr<<endl<<endl;
        return;
    }
    int    port_tmp;
    string srvport = vs_config->getVariable( "network", "server_port", "6777" );
    port_tmp = atoi( srvport.c_str() );
    if (port_tmp > 65535 || port_tmp < 0)
        port_tmp = 0;
    port     = (unsigned short) port_tmp;

    addr     = vs_config->getVariable( "network", "server_ip", "" );
    this->_serverip   = addr;
    this->_serverport = port;
    cout<<endl<<"Server IP : "<<addr<<" - port : "<<srvport<<endl<<endl;
}

/*
 ************************************************************
 **** Initialize the client network to account server     ***
 ************************************************************
 */
VsnetHTTPSocket* NetClient::init_acct( const std::string &addr )
{
    COUT<<" enter "<<__PRETTY_FUNCTION__
        <<" with "<<addr<<endl;

    _sock_set.start();

    cout<<"Initializing connection to account server..."<<endl;
    acct_sock = new VsnetHTTPSocket( addr, _sock_set );
    COUT<<"accountserver on socket "<<acct_sock<<" done."<<endl;

    return acct_sock;
}

/*
 ************************************************************
 **** Initialize the client network                       ***
 ************************************************************
 */

SOCKETALT NetClient::init( const char *addr, unsigned short port, std::string &error )
{
    if ( clt_tcp_sock && clt_tcp_sock->valid() ) clt_tcp_sock->disconnect( "NC_init_tcp" );
    if ( clt_udp_sock && clt_udp_sock->valid() ) NetUIUDP::disconnectSaveUDP( *clt_udp_sock );
    lastsave.clear();
    netversion = 0;
    if (addr == NULL)
    {
        addr = _serverip.c_str();
        port = _serverport;
    }
    COUT<<" enter "<<__PRETTY_FUNCTION__
        <<" with "<<addr<<":"<<port<<endl;

    _sock_set.start();

    string strnetatom;
    strnetatom = vs_config->getVariable( "network", "network_atom", "" );
    if (strnetatom == "")
        NETWORK_ATOM = 0.2;
    else
        NETWORK_ATOM = (double) atof( strnetatom.c_str() );
    *this->clt_tcp_sock = NetUITCP::createSocket( addr, port, _sock_set );
    this->lossy_socket  = this->clt_tcp_sock;
    if ( !clt_tcp_sock->valid() )
        return *this->clt_tcp_sock;
    COUT<<"created TCP socket ("<<addr<<","<<port<<") -> "<<this->clt_tcp_sock<<endl;

    Packet join;
    join.send( CMD_CONNECT, CLIENT_NETVERSION, "", 0, SENDRELIABLE, NULL,
               *this->clt_tcp_sock, __FILE__, PSEUDO__LINE__( 407 ) );
    this->enabled = 1;

    string  login_tostr = vs_config->getVariable( "network", "connecttimeout", "10" );
    timeval tv = {atoi( login_tostr.c_str() ), 0};
    int     timeout     = 0;
    Packet  packet;
    while (!timeout)
    {
        int recvd = this->recvMsg( &packet, &tv );
        if (recvd == 0)
        {
            error   = "Connection to game server timed out!";
            timeout = 1;
        }
        else if (recvd < 0)
        {
            char str[127];
            sprintf( str, "NETWORK ERROR in recieving socket (error number %d)!!!",
#ifdef _WIN32
                     WSAGetLastError()
#else
                     errno
#endif
                   );
            error   = str;
            timeout = 1;
        }
        else if (this->netversion)
        {
            break;
        }
    }
    if (!this->netversion)
    {
        if ( error.empty() )
            error = "Unable to receive a valid version from this server.";
        timeout = 1;
    }
    if (timeout)
        clt_tcp_sock->disconnect( "NCinit_timedout" );
    return *this->clt_tcp_sock;
}

/*
 ************************************************************
 **** Synchronize server time and client time             ***
 ************************************************************
 **** This function creates the UDP socket and determines ***
 **** whether to use the UDP or the TCP socket for lossy  ***
 **** packet data.                                        ***
 ************************************************************
 */

//NETFIXME: Correctly obtain ping time.
#include "vs_random.h" //For random ping time.

void NetClient::synchronizeTime( SOCKETALT *udpsock )
{
    int            i  = 0;
    int            timeout = 0;
    int            recv;
    timeval        tv = {1, 0};   //Timeout after 1 second, request send again.
    double         ping; //use deltaTime?
    double         pingavg           = 0.;
    double         timeavg           = 0.;
    std::map< double, double >times;     //sorted container.
    double         initialTime       = queryTime();
    static int     NUM_TIMES         = XMLSupport::parse_int( vs_config->getVariable( "network", "servertime_calibration", "10" ) );
    static int     UDP_TIMEOUT       = XMLSupport::parse_int( vs_config->getVariable( "network", "udp_timeout", "1" ) );
    static int     clt_port_read     = XMLSupport::parse_int( vs_config->getVariable( "network", "udp_listen_port", "6778" ) );
    if (clt_port_read > 65535 || clt_port_read <= 0)
        clt_port_read = 0;
    static int     clt_port_read_max = XMLSupport::parse_int( vs_config->getVariable( "network", "udp_listen_port_max", "6778" ) );
    if (clt_port_read_max > 65535 || clt_port_read_max <= 0)
        clt_port_read_max = clt_port_read;
    unsigned short clt_port          = (unsigned short) clt_port_read;
    unsigned short clt_port_max      = (unsigned short) clt_port_read_max;
    if (clt_port_max < clt_port)
        clt_port_max = clt_port;
    static string  nettransport      = vs_config->getVariable( "network", "transport", "udp" );

    unsigned short port = this->_serverport;
    if ( !( udpsock != NULL && udpsock->setRemoteAddress( NetUIBase::lookupHost( this->_serverip.c_str(), port ) ) ) )
    {
        do
            *this->clt_udp_sock = NetUIUDP::createSocket( this->_serverip.c_str(), port, clt_port, _sock_set );
        while ( ( !this->clt_udp_sock->valid() ) && (clt_port++) );
    }
    else
    {
        this->clt_udp_sock = udpsock;
    }
    COUT<<"created UDP socket ("<<this->_serverip<<","<<port<<", listen on "<<clt_port<<") -> "<<this->clt_udp_sock<<endl;
    if (nettransport == "udp")
    {
        //NETFIXME:  Keep trying ports until a connection is established.
        COUT<<"Default lossy transport configured to UDP."<<endl;
        this->lossy_socket = clt_udp_sock;
    }
    else
    {
        COUT<<"Default lossy transport configured to TCP (behind firewall)."<<endl;
        this->lossy_socket = clt_tcp_sock;
        clt_port = 0;
    }
    this->clt_tcp_sock->set_block();
    this->clt_udp_sock->set_block();
    //Wait for NUM_TIMES (10) successful tries, or 10 consecutive 1-second timeouts
    //(we use UDP on the response (SENDANDFORGET) to improve timing accuracy).
    while (i < NUM_TIMES && timeout < UDP_TIMEOUT)
    {
        Packet    packet;
        NetBuffer outData;
        outData.addShort( clt_port );
        packet.send( CMD_SERVERTIME, 0,
                     outData.getData(), outData.getDataLength(),                      //No data.
                     SENDRELIABLE, NULL, *this->clt_tcp_sock,
                     __FILE__, PSEUDO__LINE__( 343 ) );
        recv = this->recvMsg( &packet, &tv );
        //If we have no response.
        if (recv <= 0)
        {
            COUT<<"synchronizeTime() Timed out"<<endl;
            ++timeout;
            if (timeout >= UDP_TIMEOUT)
            {
                if (this->lossy_socket->isTcp() == false)
                {
                    if (clt_port < clt_port_max && !udpsock)
                    {
                        NetUIUDP::disconnectSaveUDP( *this->clt_udp_sock );
                        *this->clt_udp_sock = NetUIUDP::createSocket( this->_serverip.c_str(), port, clt_port, _sock_set );
                        clt_port++;
                        COUT<<"Trying UDP port "<<clt_port<<"."<<endl;
                    }
                    else
                    {
                        //no UDP requests made it, fallback to TCP.
                        this->lossy_socket = this->clt_tcp_sock;
                        clt_port = 0;
                        COUT<<"Setting default lossy transport to TCP (UDP timeout)."<<endl;
                    }
                    timeout = 0;
                }
            }
        }
        else if (packet.getCommand() == CMD_SERVERTIME)
        {
            //NETFIXME: obtain actual ping time
            ping = exp( vsrandom.uniformInc( -10, 0 ) );
            if (ping > 0 && ping < 1.)
            {
                ++i;
                NetBuffer data( packet.getData(), packet.getDataLength() );
                double    serverTime  = data.getDouble();
                double    currentTime = queryTime();
                serverTime += initialTime-currentTime;
                times.insert( std::multimap< double, double >::value_type( ping, serverTime-ping ) );
                timeout     = 0;
            }
            else
            {
                ++timeout;
            }
        }
    }
    this->clt_tcp_sock->set_nonblock();
    this->clt_udp_sock->set_nonblock();
    if (i >= NUM_TIMES)
    {
        int    mid      = i/2;
        double median   = 0.;
        double tot      = 0.;
        int    location = 0;
        std::map< double, double >::const_iterator iter;
        for (iter = times.begin(); iter != times.end(); ++iter)
        {
            if (location == mid)
            {
                median = iter->first;
                if (i%2 == 1)
                {
                    ++iter;
                    median += iter->first;
                }
                break;
            }
            ++location;
        }
        if (i%2 == 1)
            median /= 2;
        for (iter = times.begin(); iter != times.end(); ++iter)
        {
            double wdiff = exp( -10*(median-iter->first)*(median-iter->first) );
            pingavg += wdiff*iter->first;
            timeavg += wdiff*iter->second;
            tot     += wdiff;
        }
        pingavg /= tot;
        timeavg /= tot;
    }
    else
    {
        COUT<<"Error in time synchronization: connection ended or timed out.";
    }
    this->deltatime = pingavg;
    double newTime = timeavg+queryTime()-initialTime;
    COUT<<"Setting time to: New time: "<<newTime<<endl;
    setNewTime( newTime );
    for (unsigned int cpnum = 0; cpnum < _Universe->numPlayers(); cpnum++)
        //Seems like a bad idea... shouldn't this rely on SIMULATION_ATOM?
        _Universe->AccessCockpit( cpnum )->TimeOfLastCollision = -200;
    cur_time = newTime;
}

/*
 ************************************************************
 **** Receive that start locations                        ***
 ************************************************************
 */

//Receives possible start locations (first a short representing number of locations)
//Then for each number, a desc

void NetClient::receiveLocations( const Packet* )
{
#ifdef __DEBUG__
    COUT<<"Nb start locations : "<<nblocs<<endl;
#endif
    //Choose starting location here
}

/*
 ************************************************************
 **** Create a new character                              ***
 ************************************************************
 */
bool NetClient::selectShip( unsigned int ship )
{
    if (lastsave.empty() || lastsave[0] == "")
    {
        NetBuffer netbuf;
        string    shipname;
        netbuf.addShort( (unsigned short) ship );
        if ( ship < ship_select_list.size() )
            shipname = ship_select_list[ship];
        netbuf.addString( shipname );
        Packet p;
        p.send( CMD_CHOOSESHIP, 0, netbuf.getData(), netbuf.getDataLength(), SENDRELIABLE,
                NULL, *clt_tcp_sock, __FILE__, PSEUDO__LINE__( 628 ) );
        string err;
        int    ret = loginLoop( err );
        if (ret != 1 || lastsave.size() < 2 || lastsave[0] == "")
        {
            cout<<"Error in CHOOSEHIP: "<<err
                <<"choice="<<ship<<"("<<shipname<<"), max="<<ret<<endl;
            return false;
        }
    }
    return true;
}

void NetClient::createChar()
{}

int NetClient::connectLoad( string username, string passwd, string &error )
{
    localSerials.resize( 0 );
    bootstrap_draw( "#cc66ffNETWORK: Initializing...", NULL );
    cout<<"NETWORK: Initializing..."<<endl;
    string srvipadr;
    unsigned short port;
    bool   ret = false;
    //Are we using the directly account server to identify us ?
    GetCurrentServerAddress( srvipadr, port );
    if (!port)
    {
        //using account server.
        string srvipadr = vs_config->getVariable( "network", "account_server_url", "http://localhost/cgi-bin/accountserver.py" );
        bootstrap_draw( "#cc66ffNETWORK: Connecting to account server.", NULL );
        cout<<"NETWORK: Connecting to account server."<<endl;
        init_acct( srvipadr );
        vector< string > &savetmp = loginAcctLoop( username, passwd );
        //We don't expect a saved game...
        if ( savetmp.size() >= 2 && savetmp[0].empty() )
        {
            //But this is the way the acctserver code indicates an error.
            error = savetmp[1];
            return 0;
        }
        bootstrap_draw( "#cc66ffNETWORK: Connecting to VegaServer.", NULL );
        cout<<"NETWORK: Connecting to VegaServer."<<endl;
        ret = init( NULL, 0, error ).valid();
    }
    else
    {
        //Or are we going through a game server to do so ?
        bootstrap_draw( "#cc66ffNETWORK: Connecting to VegaServer.", NULL );
        cout<<"NETWORK: Connecting to VegaServer."<<endl;
        ret = init( srvipadr.c_str(), port, error ).valid();
    }
    if (ret == false)
    {
        //If network initialization fails, exit
        if ( error.empty() ) error = "Network connection error";
        if ( !this->error_message.empty() )
            error += "\n"+this->error_message;
        cout<<"Error: "<<error<<endl;
        return 0;
    }
    cout<<"Successfully connected!";
    cout<<"Waiting for player "<<username<<": login response..."<<endl;
    bootstrap_draw( "#cc66ffNETWORK: Successful connection! Waiting to log in.", NULL );
    int loggedin = loginAuth( username, passwd, error );
    if ( !this->error_message.empty() )
    {
        cout<<"Warning: "<<this->error_message<<endl;
        if ( !error.empty() )
            error += "\n";
        error += this->error_message;
    }
    return loggedin;
}
vector< string >* NetClient::loginSavedGame( int ship )
{
    if ( !selectShip( ship ) )
        return NULL;
    /************* NETWORK PART ***************/

    return &lastsave;
}

void NetClient::startGame()
{
    vector< string >savedships;
    QVector pos;
    //useless.
    string  mysystem;
    string  savefiles;
    bool    setplayerXloc = false;
    float   credits = 0.0;
    vector< StarSystem* >ss;
    vector< QVector >    playerNloc;
    vector< string >playersaveunit;
    vector< vector< string > >vecstr;

    bootstrap_draw( "#cc66ffNETWORK: Checking for UDP connection.", NULL );
    cout<<"NETWORK: Checking for UDP connection."<<endl;
    synchronizeTime( NULL );
    cout<<" logged in !"<<endl;

    bootstrap_draw( "#cc66ffNETWORK: Loading player ship.", NULL );
    cout<<"NETWORK: Loading player ship."<<endl;
    if (_Universe->numPlayers() == 0)
        _Universe->createCockpit( callsign );
    _Universe->clearAllSystems();
    _Universe->AccessCockpit( 0 )->savegame->SetStarSystem( string() );
    _Universe->AccessCockpit( 0 )->savegame->ParseSaveGame( "",
            mysystem,
            "",
            pos,
            setplayerXloc,
            credits,
            savedships,
            0,
            lastsave[0],
            false );

    ss.push_back( _Universe->Init( mysystem, Vector( 0, 0, 0 ), string() ) );

    CopySavedShips( callsign, 0, savedships, true );
    playersaveunit.push_back( savedships[0] );
    if (setplayerXloc)
        playerNloc.push_back( pos );
    else
        playerNloc.push_back( QVector( FLT_MAX, FLT_MAX, FLT_MAX ) );
    vecstr.push_back( lastsave );
    createObjects( playersaveunit, ss, playerNloc, vecstr );
    bootstrap_draw( "#cc66ffNETWORK: Loading system.", NULL );
    cout<<"NETWORK: Loading system."<<endl;
    inGame();
}

