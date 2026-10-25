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

// FILE: GameCode.cpp /////////////////////////////////////////////////////////////////////////////
// Desc:   A host's internet address as a short code a friend can type.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameNetwork/GameCode.h"

static const char CROCKFORD[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

// CRC-8, polynomial 0x07: every error of up to eight bits in a row changes it, which is one or
// two mistyped characters
static UnsignedByte crc8( UnsignedInt ip )
{
	UnsignedByte crc = 0;
	for (Int i = 3; i >= 0; --i)
	{
		crc ^= (UnsignedByte)(ip >> (i * 8));
		for (Int bit = 0; bit < 8; ++bit)
			crc = (UnsignedByte)((crc & 0x80) ? ((crc << 1) ^ 0x07) : (crc << 1));
	}
	return crc;
}

static Int crockfordValue( char c )
{
	if (c >= 'a' && c <= 'z')
		c = (char)(c - 'a' + 'A');
	if (c == 'O')
		c = '0';
	if (c == 'I' || c == 'L')
		c = '1';
	for (Int i = 0; i < 32; ++i)
	{
		if (CROCKFORD[i] == c)
			return i;
	}
	return -1;
}

void gameCodeFromIP( UnsignedInt ip, char out[ GAME_CODE_CHARS + 1 ] )
{
	// 40 bits, the address then its check, five to a character from the top
	const UnsignedInt64 value = ((UnsignedInt64)ip << 8) | crc8(ip);
	Int pos = 0;
	for (Int i = 7; i >= 0; --i)
	{
		out[pos++] = CROCKFORD[(value >> (i * 5)) & 31];
		if (i == 4)
			out[pos++] = '-';
	}
	out[pos] = '\0';
}

Bool gameCodeToIP( const char *text, UnsignedInt *ip )
{
	if (text == NULL)
		return FALSE;

	UnsignedInt64 value = 0;
	Int digits = 0;
	for (const char *c = text; *c != '\0'; ++c)
	{
		if (*c == '-' || *c == ' ' || *c == '\t')
			continue;
		const Int v = crockfordValue(*c);
		if (v < 0 || digits == 8)
			return FALSE;
		value = (value << 5) | (UnsignedInt64)v;
		++digits;
	}
	if (digits != 8)
		return FALSE;

	const UnsignedInt address = (UnsignedInt)(value >> 8);
	if ((UnsignedByte)(value & 0xff) != crc8(address))
		return FALSE;
	*ip = address;
	return TRUE;
}
