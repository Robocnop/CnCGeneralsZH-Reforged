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

// FILE: RelayEnvelope.cpp ////////////////////////////////////////////////////////////////////////
// Desc:   Datagrams one player sends another through the packet router.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameNetwork/RelayEnvelope.h"

#include <string.h>

static const UnsignedByte TAG_FORWARD[4] = { 'Z', 'H', 'R', 'F' };
static const UnsignedByte TAG_DELIVER[4] = { 'Z', 'H', 'R', 'D' };
static const UnsignedByte TAG_PROBE[4] = { 'Z', 'H', 'R', 'P' };

Int relayWrap( RelayKind kind, UnsignedInt ip, UnsignedShort port, const UnsignedByte *datagram, Int length,
	UnsignedByte *out, Int outSize )
{
	if (kind == RELAY_NONE || datagram == NULL || length <= 0 || out == NULL || outSize < RELAY_ENVELOPE_BYTES + length)
		return 0;

	memcpy( out, (kind == RELAY_FORWARD) ? TAG_FORWARD : TAG_DELIVER, 4 );
	out[4] = (UnsignedByte)(ip >> 24);
	out[5] = (UnsignedByte)(ip >> 16);
	out[6] = (UnsignedByte)(ip >> 8);
	out[7] = (UnsignedByte)ip;
	out[8] = (UnsignedByte)(port >> 8);
	out[9] = (UnsignedByte)port;
	memcpy( out + RELAY_ENVELOPE_BYTES, datagram, length );
	return RELAY_ENVELOPE_BYTES + length;
}

RelayKind relayUnwrap( const UnsignedByte *data, Int length, UnsignedInt *ip, UnsignedShort *port,
	const UnsignedByte **datagram, Int *datagramLength )
{
	if (data == NULL || length <= RELAY_ENVELOPE_BYTES)
		return RELAY_NONE;

	RelayKind kind = RELAY_NONE;
	if (memcmp( data, TAG_FORWARD, 4 ) == 0)
		kind = RELAY_FORWARD;
	else if (memcmp( data, TAG_DELIVER, 4 ) == 0)
		kind = RELAY_DELIVER;
	else
		return RELAY_NONE;

	*ip = ((UnsignedInt)data[4] << 24) | ((UnsignedInt)data[5] << 16) | ((UnsignedInt)data[6] << 8) | data[7];
	*port = (UnsignedShort)((data[8] << 8) | data[9]);
	*datagram = data + RELAY_ENVELOPE_BYTES;
	*datagramLength = length - RELAY_ENVELOPE_BYTES;
	return kind;
}

Int relayBuildProbe( Bool heardYou, UnsignedByte out[ RELAY_PROBE_BYTES ] )
{
	memcpy( out, TAG_PROBE, 4 );
	out[4] = heardYou ? 1 : 0;
	out[5] = 0;
	out[6] = 0;
	out[7] = 0;
	return RELAY_PROBE_BYTES;
}

Bool relayParseProbe( const UnsignedByte *data, Int length, Bool *heardYou )
{
	if (data == NULL || length != RELAY_PROBE_BYTES || memcmp( data, TAG_PROBE, 4 ) != 0)
		return FALSE;
	*heardYou = (data[4] != 0);
	return TRUE;
}

Bool relayNeedsRouter( UnsignedInt now, UnsignedInt since, UnsignedInt confirmed )
{
	if (confirmed != 0 && now - confirmed < (UnsignedInt)RELAY_SILENCE_MS)
		return FALSE;
	return now - since >= (UnsignedInt)RELAY_SILENCE_MS;
}
