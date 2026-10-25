/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// FILE: UPnPPortMapper.cpp ///////////////////////////////////////////////////////////////////////
// Desc:   Asks the home router to let the game's UDP ports in, the way UPnP IGD routers allow.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameNetwork/UPnPPortMapper.h"

#include <chrono>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <thread>

#include "Lib/Clock.h"
#include "GameNetwork/NetworkUtil.h"
#include "GameNetwork/udp.h"

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/select.h>
#include <unistd.h>
#define closesocket close
#endif

static const UnsignedInt SSDP_ADDRESS = ( 239u << 24 ) | ( 255u << 16 ) | ( 255u << 8 ) | 250u;
static const UnsignedShort SSDP_PORT = 1900;
static const Int SSDP_WAIT_MS = 2000;
static const Int HTTP_WAIT_MS = 3000;
static const UnsignedInt LEASE_SECONDS = 4 * 60 * 60;	// a crashed game's mappings lapse on their own

static const char *const TheWanServiceTypes[] =
{
	"urn:schemas-upnp-org:service:WANIPConnection:2",
	"urn:schemas-upnp-org:service:WANIPConnection:1",
	"urn:schemas-upnp-org:service:WANPPPConnection:1",
	NULL
};

static std::string lowered( const std::string &text )
{
	std::string out( text );
	for( size_t i = 0; i < out.size(); ++i )
		out[ i ] = (char)tolower( (unsigned char)out[ i ] );
	return out;
}

static std::string trimmed( const std::string &text )
{
	size_t first = 0;
	size_t last = text.size();
	while( first < last && isspace( (unsigned char)text[ first ] ) )
		++first;
	while( last > first && isspace( (unsigned char)text[ last - 1 ] ) )
		--last;
	return text.substr( first, last - first );
}

/** The text between <tag> and </tag>; empty when the element is not there. */
static std::string elementText( const std::string &xml, const char *tag )
{
	const std::string open = std::string( "<" ) + tag + ">";
	const std::string close = std::string( "</" ) + tag + ">";
	const size_t start = xml.find( open );
	if( start == std::string::npos )
		return std::string();
	const size_t end = xml.find( close, start + open.size() );
	if( end == std::string::npos )
		return std::string();
	return trimmed( xml.substr( start + open.size(), end - start - open.size() ) );
}

std::string upnpBuildSearchRequest( void )
{
	return
		"M-SEARCH * HTTP/1.1\r\n"
		"HOST: 239.255.255.250:1900\r\n"
		"MAN: \"ssdp:discover\"\r\n"
		"MX: 2\r\n"
		"ST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n"
		"\r\n";
}

Bool upnpParseSearchLocation( const std::string &response, std::string *location )
{
	// header names are not case sensitive, and routers write LOCATION, Location and location alike
	const std::string lower = lowered( response );
	size_t at = 0;
	while( ( at = lower.find( "location:", at ) ) != std::string::npos )
	{
		if( at == 0 || lower[ at - 1 ] == '\n' )
		{
			const size_t valueStart = at + strlen( "location:" );
			size_t valueEnd = response.find_first_of( "\r\n", valueStart );
			if( valueEnd == std::string::npos )
				valueEnd = response.size();
			const std::string value = trimmed( response.substr( valueStart, valueEnd - valueStart ) );
			if( value.empty() )
				return FALSE;
			*location = value;
			return TRUE;
		}
		++at;
	}
	return FALSE;
}

Bool upnpParseHttpUrl( const std::string &url, std::string *host, UnsignedShort *port, std::string *path )
{
	const char *const scheme = "http://";
	if( lowered( url.substr( 0, strlen( scheme ) ) ) != scheme )
		return FALSE;
	const size_t hostStart = strlen( scheme );
	size_t hostEnd = url.find( '/', hostStart );
	if( hostEnd == std::string::npos )
		hostEnd = url.size();
	const std::string hostPort = url.substr( hostStart, hostEnd - hostStart );
	if( hostPort.empty() )
		return FALSE;

	const size_t colon = hostPort.find( ':' );
	UnsignedShort parsedPort = 80;
	if( colon != std::string::npos )
	{
		const Int value = atoi( hostPort.c_str() + colon + 1 );
		if( value <= 0 || value > 65535 )
			return FALSE;
		parsedPort = (UnsignedShort)value;
	}
	*host = hostPort.substr( 0, colon );
	*port = parsedPort;
	*path = hostEnd < url.size() ? url.substr( hostEnd ) : std::string( "/" );
	return !host->empty();
}

Bool upnpFindWanService( const std::string &descriptionXml, const std::string &descriptionUrl,
	std::string *serviceType, std::string *controlUrl )
{
	for( Int kind = 0; TheWanServiceTypes[ kind ] != NULL; ++kind )
	{
		// every <service> block, its type and its control URL read inside that block only
		size_t from = 0;
		for( ;; )
		{
			const size_t blockStart = descriptionXml.find( "<service>", from );
			if( blockStart == std::string::npos )
				break;
			const size_t close = descriptionXml.find( "</service>", blockStart );
			if( close == std::string::npos )
				break;
			const size_t blockEnd = close + strlen( "</service>" );
			const std::string block = descriptionXml.substr( blockStart, blockEnd - blockStart );
			from = blockEnd;

			if( elementText( block, "serviceType" ) != TheWanServiceTypes[ kind ] )
				continue;
			std::string control = elementText( block, "controlURL" );
			if( control.empty() )
				continue;

			if( lowered( control.substr( 0, 7 ) ) != "http://" )
			{
				// relative: against URLBase when the description has one, else against where it came from
				std::string base = elementText( descriptionXml, "URLBase" );
				if( base.empty() )
					base = descriptionUrl;
				std::string host, path;
				UnsignedShort port = 0;
				if( !upnpParseHttpUrl( base, &host, &port, &path ) )
					return FALSE;
				char root[ 300 ];
				snprintf( root, sizeof( root ), "http://%s:%d", host.c_str(), port );
				if( control[ 0 ] != '/' )
					control = "/" + control;
				control = root + control;
			}
			*serviceType = TheWanServiceTypes[ kind ];
			*controlUrl = control;
			return TRUE;
		}
	}
	return FALSE;
}

static std::string soapEnvelope( const std::string &serviceType, const char *action, const std::string &arguments )
{
	return
		"<?xml version=\"1.0\"?>\r\n"
		"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
		"s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
		"<s:Body><u:" + std::string( action ) + " xmlns:u=\"" + serviceType + "\">" + arguments +
		"</u:" + std::string( action ) + "></s:Body></s:Envelope>\r\n";
}

std::string upnpBuildAddPortMapping( const std::string &serviceType, UnsignedShort port,
	const std::string &internalClient, const std::string &description, UnsignedInt leaseSeconds )
{
	char arguments[ 600 ];
	snprintf( arguments, sizeof( arguments ),
		"<NewRemoteHost></NewRemoteHost>"
		"<NewExternalPort>%d</NewExternalPort>"
		"<NewProtocol>UDP</NewProtocol>"
		"<NewInternalPort>%d</NewInternalPort>"
		"<NewInternalClient>%s</NewInternalClient>"
		"<NewEnabled>1</NewEnabled>"
		"<NewPortMappingDescription>%s</NewPortMappingDescription>"
		"<NewLeaseDuration>%u</NewLeaseDuration>",
		port, port, internalClient.c_str(), description.c_str(), leaseSeconds );
	return soapEnvelope( serviceType, "AddPortMapping", arguments );
}

std::string upnpBuildDeletePortMapping( const std::string &serviceType, UnsignedShort port )
{
	char arguments[ 200 ];
	snprintf( arguments, sizeof( arguments ),
		"<NewRemoteHost></NewRemoteHost>"
		"<NewExternalPort>%d</NewExternalPort>"
		"<NewProtocol>UDP</NewProtocol>",
		port );
	return soapEnvelope( serviceType, "DeletePortMapping", arguments );
}

Int upnpHttpStatus( const std::string &response )
{
	if( lowered( response.substr( 0, 5 ) ) != "http/" )
		return 0;
	const size_t space = response.find( ' ' );
	if( space == std::string::npos )
		return 0;
	return atoi( response.c_str() + space + 1 );
}

/** One HTTP exchange with the router: connect, send the request, read until the router closes or the
		wait runs out.  The router is on the LAN, so a few seconds is plenty. */
static Bool httpExchange( const std::string &host, UnsignedShort port, const std::string &request, std::string *response )
{
	response->clear();
	const UnsignedInt ip = ResolveIP( AsciiString( host.c_str() ) );
	if( ip == 0 )
		return FALSE;

	const Int sock = (Int)socket( AF_INET, SOCK_STREAM, 0 );
	if( sock < 0 )
		return FALSE;

	// non-blocking, so neither connect nor recv can hold the game past the wait
#if defined(_WIN32)
	u_long nonBlocking = 1;
	ioctlsocket( sock, FIONBIO, &nonBlocking );
#else
	fcntl( sock, F_SETFL, fcntl( sock, F_GETFL, 0 ) | O_NONBLOCK );
#endif

	sockaddr_in address;
	memset( &address, 0, sizeof( address ) );
	address.sin_family = AF_INET;
	address.sin_port = htons( port );
	address.sin_addr.s_addr = htonl( ip );
	connect( sock, (sockaddr *)&address, sizeof( address ) );

	const UnsignedInt start = Clock_Milliseconds();
	size_t sent = 0;
	while( (Int)( Clock_Milliseconds() - start ) < HTTP_WAIT_MS )
	{
		fd_set readable, writable;
		FD_ZERO( &readable );
		FD_ZERO( &writable );
		FD_SET( sock, &readable );
		if( sent < request.size() )
			FD_SET( sock, &writable );
		timeval tick = { 0, 50 * 1000 };
		if( select( sock + 1, &readable, &writable, NULL, &tick ) <= 0 )
			continue;

		if( sent < request.size() && FD_ISSET( sock, &writable ) )
		{
			const Int wrote = (Int)send( sock, request.c_str() + sent, (Int)( request.size() - sent ), 0 );
			if( wrote <= 0 )
				break;
			sent += wrote;
		}
		if( FD_ISSET( sock, &readable ) )
		{
			char buffer[ 2048 ];
			const Int got = (Int)recv( sock, buffer, sizeof( buffer ), 0 );
			if( got <= 0 )
				break;	// the router closed the connection: the answer is complete
			response->append( buffer, got );
		}
	}
	closesocket( sock );
	return !response->empty();
}

static std::string httpGet( const std::string &host, UnsignedShort port, const std::string &path )
{
	char request[ 600 ];
	snprintf( request, sizeof( request ),
		"GET %s HTTP/1.1\r\nHost: %s:%d\r\nConnection: close\r\n\r\n", path.c_str(), host.c_str(), port );
	std::string response;
	httpExchange( host, port, request, &response );
	return response;
}

UPnPPortMapper::UPnPPortMapper() : m_controlPort( 0 )
{
}

UPnPPortMapper::~UPnPPortMapper()
{
	unmapAll();
}

Bool UPnPPortMapper::discover( UnsignedInt bindIP )
{
	UDP socket;
	if( socket.Bind( bindIP, 0 ) != UDP::OK )
		return FALSE;

	// a router that misses the first datagram gets a second halfway through the wait
	const std::string search = upnpBuildSearchRequest();
	socket.Write( (const unsigned char *)search.c_str(), (UnsignedInt)search.size(), SSDP_ADDRESS, SSDP_PORT );
	Bool resent = FALSE;

	const UnsignedInt start = Clock_Milliseconds();
	while( (Int)( Clock_Milliseconds() - start ) < SSDP_WAIT_MS )
	{
		if( !resent && (Int)( Clock_Milliseconds() - start ) > SSDP_WAIT_MS / 2 )
		{
			socket.Write( (const unsigned char *)search.c_str(), (UnsignedInt)search.size(), SSDP_ADDRESS, SSDP_PORT );
			resent = TRUE;
		}

		unsigned char reply[ 2048 ];
		sockaddr_in from;
		const Int got = socket.Read( reply, sizeof( reply ), &from );
		if( got <= 0 )
		{
			std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
			continue;
		}

		std::string location;
		if( !upnpParseSearchLocation( std::string( (const char *)reply, got ), &location ) )
			continue;
		std::string host, path;
		UnsignedShort port = 0;
		if( !upnpParseHttpUrl( location, &host, &port, &path ) )
			continue;

		const std::string description = httpGet( host, port, path );
		std::string serviceType, controlUrl;
		if( upnpHttpStatus( description ) != 200 || !upnpFindWanService( description, location, &serviceType, &controlUrl ) )
		{
			DEBUG_LOG(( "UPnPPortMapper - %s describes no WAN connection service\n", location.c_str() ));
			continue;
		}
		if( !upnpParseHttpUrl( controlUrl, &m_controlHost, &m_controlPort, &m_controlPath ) )
			continue;
		m_serviceType = serviceType;
		DEBUG_LOG(( "UPnPPortMapper - router found, %s at %s\n", serviceType.c_str(), controlUrl.c_str() ));
		return TRUE;
	}
	DEBUG_LOG(( "UPnPPortMapper - no UPnP router answered in %d ms\n", SSDP_WAIT_MS ));
	return FALSE;
}

Bool UPnPPortMapper::soap( const std::string &action, const std::string &body, std::string *response )
{
	char header[ 700 ];
	snprintf( header, sizeof( header ),
		"POST %s HTTP/1.1\r\n"
		"Host: %s:%d\r\n"
		"Content-Type: text/xml; charset=\"utf-8\"\r\n"
		"SOAPAction: \"%s#%s\"\r\n"
		"Content-Length: %d\r\n"
		"Connection: close\r\n"
		"\r\n",
		m_controlPath.c_str(), m_controlHost.c_str(), m_controlPort, m_serviceType.c_str(), action.c_str(), (Int)body.size() );
	return httpExchange( m_controlHost, m_controlPort, std::string( header ) + body, response );
}

Bool UPnPPortMapper::mapPorts( UnsignedInt bindIP, const UnsignedShort *ports, Int count )
{
	if( m_serviceType.empty() && !discover( bindIP ) )
		return FALSE;

	char client[ 32 ];
	snprintf( client, sizeof( client ), "%u.%u.%u.%u", bindIP >> 24, ( bindIP >> 16 ) & 0xff, ( bindIP >> 8 ) & 0xff, bindIP & 0xff );

	Bool all = TRUE;
	for( Int i = 0; i < count; ++i )
	{
		char description[ 64 ];
		snprintf( description, sizeof( description ), "Zero Hour Reforged %d", ports[ i ] );
		std::string response;
		soap( "AddPortMapping", upnpBuildAddPortMapping( m_serviceType, ports[ i ], client, description, LEASE_SECONDS ), &response );
		if( upnpHttpStatus( response ) != 200 )
		{
			// some routers refuse a lease and take only a mapping that lasts until they restart
			soap( "AddPortMapping", upnpBuildAddPortMapping( m_serviceType, ports[ i ], client, description, 0 ), &response );
			if( upnpHttpStatus( response ) != 200 )
			{
				DEBUG_LOG(( "UPnPPortMapper - the router refused UDP %d (HTTP %d)\n", ports[ i ], upnpHttpStatus( response ) ));
				all = FALSE;
				continue;
			}
		}
		DEBUG_LOG(( "UPnPPortMapper - UDP %d passes to %s\n", ports[ i ], client ));
		m_mapped.push_back( ports[ i ] );
	}
	return all;
}

void UPnPPortMapper::unmapAll( void )
{
	for( size_t i = 0; i < m_mapped.size(); ++i )
	{
		std::string response;
		soap( "DeletePortMapping", upnpBuildDeletePortMapping( m_serviceType, m_mapped[ i ] ), &response );
	}
	m_mapped.clear();
}
