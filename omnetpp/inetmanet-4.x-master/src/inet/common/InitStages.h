//
// Copyright (C) 2013 OpenSim Ltd.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//

#ifndef __INET_INITSTAGES_H
#define __INET_INITSTAGES_H

#include "inet/common/InitStageRegistry.h"

namespace inet {

/**
 * Initialization of local state that don't use or affect other modules includes:
 *  - initializing member variables
 *  - initializing statistic collection
 *  - reading module parameters
 *  - reading configuration files
 *  - adding watches
 *  - looking up other modules without actually using them
 *  - subscribing to module signals
 */
extern INET_API InitStage INITSTAGE_LOCAL;

/**
 * Initialization of clocks.
 */
extern INET_API InitStage INITSTAGE_CLOCK;

/**
 * Initialization of the physical environment.
 */
extern INET_API InitStage INITSTAGE_PHYSICAL_ENVIRONMENT;

/**
 * Initialization of the cache of physical objects present in the physical environment.
 */
extern INET_API InitStage INITSTAGE_PHYSICAL_OBJECT_CACHE;

/**
 * Initialization of group mobility modules: calculating the initial position and orientation.
 */
extern INET_API InitStage INITSTAGE_GROUP_MOBILITY;

/**
 * Initialization of single mobility modules: calculating the initial position and orientation.
 */
extern INET_API InitStage INITSTAGE_SINGLE_MOBILITY;

/**
 * Initialization of the power model: energy storage, energy consumer, energy generator, and energy management modules.
 */
extern INET_API InitStage INITSTAGE_POWER;

/**
 * Initialization of physical layer protocols includes:
 *  - registering radios in the RadioMedium
 *  - initializing radio mode, transmission and reception states
 */
extern INET_API InitStage INITSTAGE_PHYSICAL_LAYER;

/**
 * Initialization of physical layer neighbor cache.
 */
extern INET_API InitStage INITSTAGE_PHYSICAL_LAYER_NEIGHBOR_CACHE;

/**
 * Initialization of network interfaces includes:
 *  - assigning MAC addresses
 *  - registering network interfaces in the InterfaceTable
 */
extern INET_API InitStage INITSTAGE_NETWORK_INTERFACE_CONFIGURATION;

/**
 * Initialization of queueing modules.
 */
extern INET_API InitStage INITSTAGE_QUEUEING;

/**
 * Initialization of link-layer protocols.
 */
extern INET_API InitStage INITSTAGE_LINK_LAYER;

/**
 * Initialization of network configuration (e.g. Ipv4NetworkConfigurator) includes:
 *  - determining IP addresses and static routes
 *  - adding protocol-specific data (e.g. Ipv4InterfaceData) to NetworkInterface
 */
extern INET_API InitStage INITSTAGE_NETWORK_CONFIGURATION;

/**
 * Initialization of network addresses.
 */
extern INET_API InitStage INITSTAGE_NETWORK_ADDRESS_ASSIGNMENT;

/**
 * Initialization of network addresses.
 */
extern INET_API InitStage INITSTAGE_ROUTER_ID_ASSIGNMENT;

/**
 * Initialization of static routing.
 */
extern INET_API InitStage INITSTAGE_STATIC_ROUTING;

/**
 * Initialization of network layer protocols. (IPv4, IPv6, ...)
 */
extern INET_API InitStage INITSTAGE_NETWORK_LAYER;

/**
 * Initialization of network layer protocols over IP. (ICMP, IGMP, ...)
 */
extern INET_API InitStage INITSTAGE_NETWORK_LAYER_PROTOCOLS;

/**
 * Initialization of transport-layer protocols.
 */
extern INET_API InitStage INITSTAGE_TRANSPORT_LAYER;

/**
 * Initialization of routing protocols.
 */
extern INET_API InitStage INITSTAGE_ROUTING_PROTOCOLS;

/**
 * Initialization of applications.
 */
extern INET_API InitStage INITSTAGE_APPLICATION_LAYER;

/**
 * Operations that no other initializations can depend on, e.g. display string updates.
 */
extern INET_API InitStage INITSTAGE_LAST;

} // namespace inet

#endif

