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

// FILE: UPnPPortMapper.h /////////////////////////////////////////////////////////////////////////
// Desc:   Asks the home router to let the game's UDP ports in, the way UPnP IGD routers allow.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _UPNP_PORT_MAPPER_H_
#define _UPNP_PORT_MAPPER_H_

#include "Lib/BaseType.h"

#include <string>
#include <vector>

/* A player hosting over the internet has to be reachable: his router must pass UDP 8086 (the lobby)
	 and 8088 (the game) to his machine.  Most home routers - Livebox, Freebox, SFR and Bouygues boxes,
	 and the rest of them - take that request from the machine itself over UPnP's Internet Gateway
	 Device protocol, so nobody has to open their router's pages:

	   1. SSDP: one UDP datagram to 239.255.255.250:1900 asking for an Internet Gateway Device; the
	      router answers with the URL of its description.
	   2. That description, an XML file over HTTP, names the WANIPConnection (or, on a PPP link,
	      WANPPPConnection) service and the URL that controls it.
	   3. SOAP over HTTP to that URL: AddPortMapping for each port, DeletePortMapping when the game
	      leaves.

	 Everything that reads or writes those messages is a plain function, apart from the sockets, so a
	 test drives it with a router's answers written out by hand. */

/** The SSDP M-SEARCH datagram for an Internet Gateway Device. */
std::string upnpBuildSearchRequest( void );

/** The LOCATION header of an SSDP answer, the description URL.  False when there is none. */
Bool upnpParseSearchLocation( const std::string &response, std::string *location );

/** Splits http://host[:port]/path.  The port is 80 when the URL gives none, and the path "/" when it
		gives none.  False for anything but an http URL. */
Bool upnpParseHttpUrl( const std::string &url, std::string *host, UnsignedShort *port, std::string *path );

/** Finds the WAN connection service in a device description: its service type, and its control URL
		made absolute against the description's URLBase or, without one, the URL the description came
		from.  WANIPConnection:2, then :1, then WANPPPConnection:1.  False when there is none. */
Bool upnpFindWanService( const std::string &descriptionXml, const std::string &descriptionUrl,
	std::string *serviceType, std::string *controlUrl );

/** The SOAP body of AddPortMapping: UDP externalPort to internalClient:externalPort, for leaseSeconds
		(0 = until the router restarts). */
std::string upnpBuildAddPortMapping( const std::string &serviceType, UnsignedShort port,
	const std::string &internalClient, const std::string &description, UnsignedInt leaseSeconds );

/** The SOAP body of DeletePortMapping for UDP port. */
std::string upnpBuildDeletePortMapping( const std::string &serviceType, UnsignedShort port );

/** The status code of an HTTP response, 0 when it does not start like one. */
Int upnpHttpStatus( const std::string &response );

/** The router's port mappings for this machine, made by mapPorts and taken back by unmapAll (or the
		destructor), so a mapping does not outlive the game that asked for it. */
class UPnPPortMapper
{
public:
	UPnPPortMapper();
	~UPnPPortMapper();

	/** Finds the router and asks it to pass each UDP port in ports to bindIP.  True when every port
			is mapped.  Blocks for up to a few seconds while the router is looked for. */
	Bool mapPorts( UnsignedInt bindIP, const UnsignedShort *ports, Int count );
	void unmapAll( void );

private:
	Bool discover( UnsignedInt bindIP );
	Bool soap( const std::string &action, const std::string &body, std::string *response );

	std::string m_serviceType;
	std::string m_controlHost;
	UnsignedShort m_controlPort;
	std::string m_controlPath;
	std::vector<UnsignedShort> m_mapped;
};

#endif // _UPNP_PORT_MAPPER_H_
