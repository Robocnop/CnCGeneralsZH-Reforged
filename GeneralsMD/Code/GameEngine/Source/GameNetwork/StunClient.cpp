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

// FILE: StunClient.cpp ///////////////////////////////////////////////////////////////////////////
// Desc:   The address the internet sees us at, asked of a public STUN server.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameNetwork/StunClient.h"

#include <chrono>
#include <random>
#include <string.h>
#include <thread>

#include "Lib/Clock.h"
#include "GameNetwork/NetworkUtil.h"
#include "GameNetwork/udp.h"

// Google's and Cloudflare's, both free and both answering RFC 5389 on UDP
const char *const TheStunServers[] =
{
	"stun.l.google.com:19302",
	"stun.cloudflare.com:3478",
	NULL
};

static const UnsignedShort STUN_BINDING_REQUEST = 0x0001;
static const UnsignedShort STUN_BINDING_SUCCESS = 0x0101;
static const UnsignedInt STUN_MAGIC_COOKIE = 0x2112A442;
static const UnsignedShort STUN_ATTR_MAPPED_ADDRESS = 0x0001;
static const UnsignedShort STUN_ATTR_XOR_MAPPED_ADDRESS = 0x0020;
static const UnsignedByte STUN_FAMILY_IPV4 = 0x01;
static const Int STUN_IPV4_ADDRESS_BYTES = 8;	// a reserved byte, the family, the port, the address

static UnsignedShort readShort( const UnsignedByte *at ) { return (UnsignedShort)( ( at[ 0 ] << 8 ) | at[ 1 ] ); }
static UnsignedInt readInt( const UnsignedByte *at )
{
	return ( (UnsignedInt)at[ 0 ] << 24 ) | ( (UnsignedInt)at[ 1 ] << 16 ) | ( (UnsignedInt)at[ 2 ] << 8 ) | at[ 3 ];
}

Int stunBuildBindingRequest( UnsignedByte out[ STUN_HEADER_BYTES ], const UnsignedByte transactionID[ STUN_TRANSACTION_ID_BYTES ] )
{
	out[ 0 ] = (UnsignedByte)( STUN_BINDING_REQUEST >> 8 );
	out[ 1 ] = (UnsignedByte)( STUN_BINDING_REQUEST & 0xff );
	out[ 2 ] = 0;	// no attributes
	out[ 3 ] = 0;
	out[ 4 ] = (UnsignedByte)( STUN_MAGIC_COOKIE >> 24 );
	out[ 5 ] = (UnsignedByte)( ( STUN_MAGIC_COOKIE >> 16 ) & 0xff );
	out[ 6 ] = (UnsignedByte)( ( STUN_MAGIC_COOKIE >> 8 ) & 0xff );
	out[ 7 ] = (UnsignedByte)( STUN_MAGIC_COOKIE & 0xff );
	memcpy( out + 8, transactionID, STUN_TRANSACTION_ID_BYTES );
	return STUN_HEADER_BYTES;
}

Bool stunParseBindingResponse( const UnsignedByte *data, Int length,
	const UnsignedByte transactionID[ STUN_TRANSACTION_ID_BYTES ], UnsignedInt *ip, UnsignedShort *port )
{
	if( data == NULL || length < STUN_HEADER_BYTES )
		return FALSE;
	if( readShort( data ) != STUN_BINDING_SUCCESS || readInt( data + 4 ) != STUN_MAGIC_COOKIE )
		return FALSE;
	if( memcmp( data + 8, transactionID, STUN_TRANSACTION_ID_BYTES ) != 0 )
		return FALSE;

	// the attributes the header says there are, never past what arrived
	const Int bodyLength = readShort( data + 2 );
	const Int end = STUN_HEADER_BYTES + bodyLength < length ? STUN_HEADER_BYTES + bodyLength : length;

	Bool found = FALSE;
	UnsignedInt foundIP = 0;
	UnsignedShort foundPort = 0;
	for( Int at = STUN_HEADER_BYTES; at + 4 <= end; )
	{
		const UnsignedShort type = readShort( data + at );
		const Int valueLength = readShort( data + at + 2 );
		const UnsignedByte *value = data + at + 4;
		if( at + 4 + valueLength > end )
			break;

		const Bool xorMapped = type == STUN_ATTR_XOR_MAPPED_ADDRESS;
		if( ( xorMapped || type == STUN_ATTR_MAPPED_ADDRESS ) && valueLength >= STUN_IPV4_ADDRESS_BYTES
			&& value[ 1 ] == STUN_FAMILY_IPV4 )
		{
			UnsignedShort mappedPort = readShort( value + 2 );
			UnsignedInt mappedIP = readInt( value + 4 );
			if( xorMapped )
			{
				mappedPort ^= (UnsignedShort)( STUN_MAGIC_COOKIE >> 16 );
				mappedIP ^= STUN_MAGIC_COOKIE;
			}
			// XOR-MAPPED-ADDRESS is the one to trust: some routers rewrite a bare address they spot in a
			// packet, which is why RFC 5389 hides it.  A MAPPED-ADDRESS only counts when there is no other.
			if( xorMapped || !found )
			{
				foundIP = mappedIP;
				foundPort = mappedPort;
				found = TRUE;
			}
			if( xorMapped )
				break;
		}
		at += 4 + ( ( valueLength + 3 ) & ~3 );	// attributes are padded to four bytes
	}

	if( !found || foundIP == 0 )
		return FALSE;
	*ip = foundIP;
	*port = foundPort;
	return TRUE;
}

/** One server, one request, an answer or a timeout. */
static Bool queryOneServer( UDP &socket, const char *server, Int timeoutMS, UnsignedInt *publicIP )
{
	const char *colon = strrchr( server, ':' );
	if( colon == NULL )
		return FALSE;
	char hostName[ 256 ];
	const size_t hostLength = (size_t)( colon - server );
	if( hostLength == 0 || hostLength >= sizeof( hostName ) )
		return FALSE;
	memcpy( hostName, server, hostLength );
	hostName[ hostLength ] = 0;
	const AsciiString host( hostName );
	const UnsignedShort serverPort = (UnsignedShort)atoi( colon + 1 );
	const UnsignedInt serverIP = ResolveIP( host );
	if( serverIP == 0 || serverPort == 0 )
	{
		DEBUG_LOG(( "stunQueryPublicIP - could not resolve %s\n", server ));
		return FALSE;
	}

	// the transaction ID only has to tell our answer from a stray packet, so it need not be secret,
	// and it must not come from the game's random streams, which every player keeps in step
	std::random_device entropy;
	UnsignedByte transactionID[ STUN_TRANSACTION_ID_BYTES ];
	for( Int i = 0; i < STUN_TRANSACTION_ID_BYTES; ++i )
		transactionID[ i ] = (UnsignedByte)( entropy() & 0xff );

	UnsignedByte request[ STUN_HEADER_BYTES ];
	const Int requestLength = stunBuildBindingRequest( request, transactionID );

	// UDP may lose the request or the answer, so it goes out again every quarter of the wait
	const UnsignedInt start = Clock_Milliseconds();
	UnsignedInt lastSent = 0;
	Bool sent = FALSE;
	while( (Int)( Clock_Milliseconds() - start ) < timeoutMS )
	{
		if( !sent || (Int)( Clock_Milliseconds() - lastSent ) >= timeoutMS / 4 )
		{
			socket.Write( request, requestLength, serverIP, serverPort );
			lastSent = Clock_Milliseconds();
			sent = TRUE;
		}

		UnsignedByte reply[ 512 ];
		sockaddr_in from;
		const Int got = socket.Read( reply, sizeof( reply ), &from );
		if( got > 0 )
		{
			UnsignedShort mappedPort = 0;
			if( ntohl( from.sin_addr.s_addr ) == serverIP
				&& stunParseBindingResponse( reply, got, transactionID, publicIP, &mappedPort ) )
			{
				DEBUG_LOG(( "stunQueryPublicIP - %s sees us at %d.%d.%d.%d:%d\n", server,
					*publicIP >> 24, ( *publicIP >> 16 ) & 0xff, ( *publicIP >> 8 ) & 0xff, *publicIP & 0xff, mappedPort ));
				return TRUE;
			}
			continue;	// something else arrived; look again before waiting
		}
		std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
	}
	DEBUG_LOG(( "stunQueryPublicIP - no answer from %s in %d ms\n", server, timeoutMS ));
	return FALSE;
}

Bool stunQueryPublicIP( UnsignedInt bindIP, UnsignedInt *publicIP, Int timeoutMS )
{
	// a socket of its own on any free port: the lobby's and the game's ports stay the game's, and a
	// router that keeps a port as it is shows the same address for all three
	UDP socket;
	if( socket.Bind( bindIP, 0 ) != UDP::OK )
	{
		DEBUG_LOG(( "stunQueryPublicIP - could not open a socket on %X\n", bindIP ));
		return FALSE;
	}
	for( Int i = 0; TheStunServers[ i ] != NULL; ++i )
	{
		if( queryOneServer( socket, TheStunServers[ i ], timeoutMS, publicIP ) )
			return TRUE;
	}
	return FALSE;
}
