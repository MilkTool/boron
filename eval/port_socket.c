/*
  Copyright 2010-2015 Karl Robillard

  This file is part of the Boron programming language.

  Boron is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Boron is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with Boron.  If not, see <http://www.gnu.org/licenses/>.
*/


#ifdef _WIN32

#include <time.h>
#include <stdio.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#define ssize_t     int
#define SOCKET_ERR  WSAGetLastErrorMessage()
#define INVALID(s)  (s == INVALID_SOCKET)

static char* WSAGetLastErrorMessage()
{
    static char buf[40];
    sprintf( buf, "%d", WSAGetLastError() );
    return buf;
}

#else

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>

#define SOCKET      int
#define SOCKET_ERR  strerror(errno)
#define INVALID(s)  (s < 0)
#define closesocket close

#endif

#include "boron.h"
#include "os.h"


#define FD      used
#define TCP     elemSize


typedef struct
{
    const UPortDevice* dev;
    struct sockaddr addr;
    socklen_t addrlen;
#ifdef _WIN32
    HANDLE event;
#endif
}
SocketExt;


typedef struct
{
    const char* node;
    const char* service;
    int socktype;
}
NodeServ;


UPortDevice port_listenSocket;
UPortDevice port_socket;


/*
   Initialize NodeServ from strings like "myhost.net",
   "udp://myhost.net:180", "tcp://:4600", etc.
   Pointers are to the Boron temporary thread binary buffer.
*/
static void stringToNodeServ( UThread* ut, const UCell* strC, NodeServ* ns )
{
    char* start;
    char* cp;

    ns->node = ns->service = 0;
    ns->socktype = SOCK_STREAM;

    start = boron_cstr( ut, strC, 0 );
    if( start )
    {
        ns->node = start;
        for( cp = start; *cp; ++cp )
        {
            if( *cp == ':' )
            {
                if( cp[1] == '/' && cp[2] == '/' )
                {
                    if( start[0] == 'u' && start[1] == 'd' && start[2] == 'p' )
                        ns->socktype = SOCK_DGRAM;
                    if( cp[3] == ':' )
                        ns->node = 0;
                    else
                        ns->node = cp + 3;
                    cp += 2;
                }
                else
                {
                    *cp++ = '\0';
                    ns->service = cp;
                    break;
                }
            }
        }
    }
    //printf( "KR node: {%s} service: {%s}\n", ns->node, ns->service );
}


static int makeSockAddr( UThread* ut, SocketExt* ext, const NodeServ* ns )
{
    struct addrinfo hints;
    struct addrinfo* res;
    int err;

    memset( &hints, 0, sizeof(hints) );
    hints.ai_family   = AF_INET;  // AF_UNSPEC
    hints.ai_socktype = ns->socktype;
    if( ! ns->node )
        hints.ai_flags = AI_PASSIVE;

    err = getaddrinfo( ns->node, ns->service, &hints, &res );
    if( err )
    {
        return ur_error( ut, UR_ERR_SCRIPT, "getaddrinfo (%s)",
                         gai_strerror( err ) );
    }

    memcpy( &ext->addr, res->ai_addr, res->ai_addrlen );
    ext->addrlen = res->ai_addrlen;

    freeaddrinfo( res );
    return UR_OK;
}


static SocketExt* _socketPort( UThread* ut, UCell* cell, const char* funcName )
{
    SocketExt* ext;
    UBuffer* pbuf = ur_buffer( cell->series.buf );
    if( (pbuf->form == UR_PORT_EXT) && pbuf->ptr.v )
    {
        ext = ur_ptr(SocketExt, pbuf);
        if( ext->dev == &port_socket )
            return ext;
    }
    ur_error( ut, UR_ERR_SCRIPT, "%s expected socket port", funcName );
    return 0;
}


/*-cf-
    set-addr
        socket      port!
        hostname    string!
    return: unset!
    group: os

    Set Internet address of socket.
*/
CFUNC_PUB( cfunc_set_addr )
{
    const UCell* addr = a1+1;

    ur_setId(res, UT_UNSET);

    if( ur_is(a1, UT_PORT) )
    {
        SocketExt* ext = _socketPort( ut, a1, "set-addr" );
        if( ! ext )
            return UR_THROW;

        if( ur_is(addr, UT_STRING) )
        {
            NodeServ ns;
            stringToNodeServ( ut, addr, &ns );
            return makeSockAddr( ut, ext, &ns );
        }
    }
    return ur_error( ut, UR_ERR_TYPE, "set-addr expected port! string!" );
}


/*-cf-
    hostname
        socket      none!/port!
    return: string!
    group: os

    Get name of local host (if socket is none) or peer (if socket is a port).
*/
CFUNC_PUB( cfunc_hostname )
{
#define HLEN    80
#define SLEN    10
    char host[ HLEN ];
    char serv[ SLEN ];
    UBuffer* str;
    int len;
    int sl;

    if( ur_is(a1, UT_PORT) )
    {
        int err;
        SocketExt* ext = _socketPort( ut, a1, "hostname" );
        if( ! ext )
            return UR_THROW;

        err = getnameinfo( &ext->addr, ext->addrlen, host, HLEN, serv, SLEN,
                           /*NI_NUMERICHOST |*/ NI_NUMERICSERV );
        if( ! err )
        {
            sl = strLen( serv );
            goto makeStr;
        }
        return ur_error( ut, UR_ERR_ACCESS, "getnameinfo %s",
                         gai_strerror( err ) );
    }
    else if( ur_is(a1, UT_NONE) )
    {
        if( gethostname( host, HLEN ) == 0 )
        {
            sl = 0;
            goto makeStr;
        }
        return ur_error( ut, UR_ERR_ACCESS, "gethostname %s", SOCKET_ERR );
    }
    return ur_error( ut, UR_ERR_TYPE, "hostname expected none!/port!" );

makeStr:

    host[ HLEN - 1 ] = '\0';
    len = strLen( host );

    str = ur_makeStringCell( ut, UR_ENC_LATIN1, len + sl + 1, res );
    memCpy( str->ptr.c, host, len );
    if( sl )
    {
        str->ptr.c[ len++ ] = ':';
        memCpy( str->ptr.c + len, serv, sl );
        len += sl;
    }
    str->used = len;
    return UR_OK;
}


/*
   \param ext  If non-zero, then bind socket to ext->addr.
*/
static int _openUdpSocket( UThread* ut, SocketExt* ext, int nowait )
{
    SOCKET fd;

    fd = socket( AF_INET, SOCK_DGRAM, 0 );
    if( INVALID(fd) )
    {
        ur_error( ut, UR_ERR_ACCESS, "socket %s", SOCKET_ERR );
        return -1;
    }

    if( nowait )
    {
#ifdef _WIN32
        u_long flags = 1;
        ioctlsocket( fd, FIONBIO, &flags );
#else
        fcntl( fd, F_SETFL, fcntl( fd, F_GETFL, 0 ) | O_NONBLOCK );
#endif
    }

    if( ext )
    {
        if( bind( fd, &ext->addr, ext->addrlen ) < 0 )
        {
            closesocket( fd );
            ur_error( ut, UR_ERR_ACCESS, "bind %s", SOCKET_ERR );
            return -1;
        }
    }

    return fd;
}


static int _openTcpClient( UThread* ut, struct sockaddr* addr,
                           socklen_t addrlen )
{
    SOCKET fd;
#ifdef __APPLE__
    int yes = 1;
#endif

    fd = socket( AF_INET, SOCK_STREAM, 0 );
    if( INVALID(fd) )
    {
        ur_error( ut, UR_ERR_ACCESS, "socket %s", SOCKET_ERR );
        return -1;
    }

#ifdef __APPLE__
    if( setsockopt( fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(int) ) != 0 )
    {
        closesocket( fd );
        ur_error( ut, UR_ERR_ACCESS, "setsockopt %s", SOCKET_ERR );
        return -1;
    }
#endif

    if( connect( fd, addr, addrlen ) < 0 )
    {
        closesocket( fd );
        ur_error( ut, UR_ERR_ACCESS, "connect %s", SOCKET_ERR );
        return -1;
    }

    return fd;
}


static int _openTcpServer( UThread* ut, struct sockaddr* addr,
                           socklen_t addrlen, int backlog )
{
    SOCKET fd;
    int yes = 1;

    fd = socket( AF_INET, SOCK_STREAM, 0 );
    if( INVALID(fd) )
    {
        ur_error( ut, UR_ERR_ACCESS, "socket %s", SOCKET_ERR );
        return -1;
    }

    if( setsockopt( fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int) ) != 0 )
    {
        closesocket( fd );
        ur_error( ut, UR_ERR_ACCESS, "setsockopt %s", SOCKET_ERR );
        return -1;
    }

    if( bind( fd, addr, addrlen ) != 0 )
    {
        closesocket( fd );
        ur_error( ut, UR_ERR_ACCESS, "bind %s", SOCKET_ERR );
        return -1;
    }

    if( listen( fd, backlog ) != 0 )
    {
        closesocket( fd );
        ur_error( ut, UR_ERR_ACCESS, "listen %s", SOCKET_ERR );
        return -1;
    }

    return fd;
}


/*
  "tcp://host:port"
  "tcp://:port"
  "udp://host:port"
  "udp://:port"
*/
static int socket_open( UThread* ut, const UPortDevice* pdev,
                        const UCell* from, int opt, UCell* res )
{
    NodeServ ns;
    SocketExt* ext;
    int socket;
    int nowait = opt & UR_PORT_NOWAIT;
    //int port = 0;
    //int hostPort = 0;
    //UCell* initAddr = 0;


    if( ! ur_is(from, UT_STRING) )
        return ur_error( ut, UR_ERR_TYPE, "socket open expected string" );

    ext = (SocketExt*) memAlloc( sizeof(SocketExt) );
    ext->addrlen = 0;
#ifdef _WIN32
    ext->event = WSA_INVALID_EVENT;
#endif

    //if( ur_is(from, UT_STRING) )
    {
        stringToNodeServ( ut, from, &ns );
        if( ! makeSockAddr( ut, ext, &ns ) )
            goto fail;
    }
#if 0
    else if( ur_is(from, UT_BLOCK) )
    {
        /*
        ['udp local-port]
        ['udp local-port "host" host-port 'nowait]
        ['tcp "host" host-port]
        */
        UBlockIt bi;
        UAtom atoms[3];

        ur_internAtoms( ut, "tcp udp nowait", atoms );

        ur_blockIt( ut, &bi, from );
        ur_foreach( bi )
        {
            if( ur_is(bi.it, UT_INT) )
            {
                if( initAddr )
                    hostPort = ur_int(bi.it);
                else
                    port = ur_int(bi.it);
            }
            else if( ur_is(bi.it, UT_STRING) )
            {
                stringToNodeServ( ut, bi.it, &ns );
            }
            else if( ur_is(bi.it, UT_WORD) )
            {
                UAtom atom = ur_atom(bi.it);
                if( atom == atoms[0] )
                    ns.socktype = SOCK_STREAM;
                else if( atom == atoms[1] )
                    ns.socktype = SOCK_DGRAM;
                else if( atom == atoms[2] )
                    nowait = 1;
            }
        }

        if( initAddr && hostPort )
        {
            if( ! makeSockAddr( ut, ext, &ns ) )
                goto fail;
        }
    }
#endif

    if( ! ns.service )
    {
        ur_error( ut, UR_ERR_SCRIPT,
                  "Socket port requires hostname and/or port" );
        goto fail;
    }

    if( ! boron_requestAccess( ut, "Open socket %s", ns.service ) )
        goto fail;

    if( ns.socktype == SOCK_DGRAM )
    {
        socket = _openUdpSocket( ut, ns.node ? 0 : ext, nowait );
    }
    else
    {
        if( ns.node && ! (opt & UR_PORT_READ) )
        {
            socket = _openTcpClient( ut, &ext->addr, ext->addrlen );
        }
        else
        {
            socket = _openTcpServer( ut, &ext->addr, ext->addrlen, 10 );
            pdev = &port_listenSocket;
        }
    }

    if( socket > -1 )
    {
        UBuffer* pbuf;

        pbuf = boron_makePort( ut, pdev, ext, res );
        pbuf->TCP = (ns.socktype == SOCK_STREAM) ? 1 : 0;
        pbuf->FD  = socket;
        //printf( "KR socket_open %d %d\n", pbuf->FD, pbuf->TCP );
        return UR_OK;
    }

fail:

    memFree( ext );
    return UR_THROW;
}


static void socket_close( UBuffer* pbuf )
{
    //printf( "KR socket_close %d\n", pbuf->FD );

#ifdef _WIN32
    SocketExt* ext = ur_ptr(SocketExt, pbuf);
    if( ext->event != WSA_INVALID_EVENT )
    {
        WSACloseEvent( ext->event );
        ext->event = WSA_INVALID_EVENT;
    }
#endif

    if( pbuf->FD > -1 )
    {
        closesocket( pbuf->FD );
        pbuf->FD = -1;
    }

    memFree( pbuf->ptr.v );
    //pbuf->ptr.v = 0;      // Done by port_destroy().
}


/*
  http://linac.fnal.gov/LINAC/software/locsys/syscode/ipsoftware/IPFragmentation.html

  Maximum datagram size is 64K bytes.
  Ethernet frame size limit is 1500 bytes.
  Limit game packets to 1448 bytes?
*/

static int socket_read( UThread* ut, UBuffer* port, UCell* dest, int len )
{
    SocketExt* ext;
    ssize_t n;
    SOCKET fd = port->FD;
    UBuffer* buf = ur_buffer( dest->series.buf );

    //printf( "KR socket_read fd:%d tcp:%d len:%d\n", fd, port->TCP, len );

    if( port->TCP )
    {
        n = recv( fd, buf->ptr.c + buf->used, len, 0 );     // TCP
        if( ! n )
        {
            // If blocking, zero means the peer has closed the connection.
            closesocket( fd );
            port->FD = -1;
        }
    }
    else
    {
        ext = ur_ptr(SocketExt, port);
        n = recvfrom( fd, buf->ptr.c + buf->used, len, 0,
                      &ext->addr, &ext->addrlen );          // UDP
    }

    if( n > 0 )
    {
        buf->used += n;
    }
    else if( n == -1 )
    {
#ifdef _WIN32
        int err = WSAGetLastError();
        if( (err == WSAEWOULDBLOCK) || (err == WSAEINTR) )
#else
        if( (errno == EAGAIN) || (errno == EINTR) )
#endif
        {
            ur_setId(dest, UT_NONE);
        }
        else
        {
            return ur_error( ut, UR_ERR_ACCESS,
                             "recvfrom %s", SOCKET_ERR );
        }
    }
    else
    {
        ur_setId(dest, UT_NONE);
    }
    return UR_OK;
}


#ifdef _WIN32
static HANDLE _createSocketEvent( int socketFD, int accept )
{
    HANDLE event = WSACreateEvent();
    if( event != WSA_INVALID_EVENT )
    {
        long mask = accept ? FD_ACCEPT
                           : FD_CLOSE | FD_READ;

        // NOTE: WSAEventSelect put the socket into non-blocking mode.
        if( SOCKET_ERROR == WSAEventSelect( socketFD, event, mask ) )
        {
            WSACloseEvent( event );
            event = WSA_INVALID_EVENT;
        }
    }
    return event;
}
#endif


static int socket_accept( UThread* ut, UBuffer* port, UCell* dest, int part )
{
    SOCKET fd;
    SocketExt* ext;
    UBuffer* buf;
    (void) part;


    ext = (SocketExt*) memAlloc( sizeof(SocketExt) );
    ext->addrlen = sizeof(ext->addr);

    fd = accept( port->FD, &ext->addr, &ext->addrlen );
    if( INVALID(fd) )
    {
        memFree( ext );
        return ur_error( ut, UR_ERR_INTERNAL, "accept %s", SOCKET_ERR );
    }

#ifdef _WIN32
    // Windows gives the accept fd the same WSAEventSelect association as
    // the listener, so we must override it.
#if 0
    ext->event = _createSocketEvent( fd, 0 );
#else
    {
    u_long flags = 0;
    WSAEventSelect( fd, NULL, 0 );
    ioctlsocket( fd, FIONBIO, &flags );
    ext->event = WSA_INVALID_EVENT;
    }
#endif
#endif

    buf = boron_makePort( ut, &port_socket, ext, dest );
    buf->TCP = 1;
    buf->FD  = fd;
    return UR_OK;
}


extern int boron_sliceMem( UThread* ut, const UCell* cell, const void** ptr );


static int socket_write( UThread* ut, UBuffer* port, const UCell* data )
{
    const void* buf;
    int n;
    ssize_t len = boron_sliceMem( ut, data, &buf );

#ifndef __linux__
#define MSG_NOSIGNAL    0
#endif

    if( port->FD > -1 && len )
    {
        if( port->TCP )
        {
            n = send( port->FD, buf, len, MSG_NOSIGNAL );
        }
        else
        {
            SocketExt* ext = ur_ptr(SocketExt, port);
            n = sendto( port->FD, buf, len, 0, &ext->addr, ext->addrlen );
        }

        if( n == -1 )
        {
            ur_error( ut, UR_ERR_ACCESS, "send %s", SOCKET_ERR );

            // An error occured; the socket must not be used again.
            closesocket( port->FD );
            port->FD = -1;
            return UR_THROW;
        }
        else if( n != len )
        {
            return ur_error( ut, UR_ERR_ACCESS,
                             "send only sent %d of %d bytes", n, len );
        }
    }
    return UR_OK;
}


static int socket_seek( UThread* ut, UBuffer* port, UCell* pos, int where )
{
    (void) port;
    (void) pos;
    (void) where;
    return ur_error( ut, UR_ERR_SCRIPT, "Cannot seek on socket port" );
}


#ifdef _WIN32
static int socket_waitFD( UBuffer* port, void** handle )
{
    int fd = port->FD;
    if( fd > -1 )
    {
        SocketExt* ext = ur_ptr(SocketExt, port);
        if( ext->event == WSA_INVALID_EVENT )
        {
            ext->event = _createSocketEvent(fd, ext->dev == &port_listenSocket);
        }
        *handle = ext->event;
    }
    return fd;
}
#else
static int socket_waitFD( UBuffer* port )
{
    return port->FD;
}
#endif


UPortDevice port_socket =
{
    socket_open, socket_close, socket_read, socket_write, socket_seek,
    socket_waitFD, 2048
};


UPortDevice port_listenSocket =
{
    socket_open, socket_close, socket_accept, socket_write, socket_seek,
    socket_waitFD, 0
};


#ifdef CONFIG_SSL
#include "port_ssl.c"
#endif


/*EOF*/
