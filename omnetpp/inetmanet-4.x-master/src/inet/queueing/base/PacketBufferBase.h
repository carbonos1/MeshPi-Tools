//
// Copyright (C) 2020 OpenSim Ltd.
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

#ifndef __INET_PACKETBUFFERBASE_H
#define __INET_PACKETBUFFERBASE_H

#include "inet/queueing/base/PacketProcessorBase.h"
#include "inet/queueing/contract/IPacketQueue.h"

namespace inet {
namespace queueing {

class INET_API PacketBufferBase : public PacketProcessorBase, public virtual IPacketCollection
{
  protected:
    const char *displayStringTextFormat = nullptr;
    int numAddedPackets = -1;
    int numRemovedPackets = -1;
    int numDroppedPackets = -1;

  protected:
    virtual void initialize(int stage) override;
    virtual void emit(simsignal_t signal, cObject *object, cObject *details = nullptr) override;
    virtual const char *resolveDirective(char directive) const override;
};

} // namespace queueing
} // namespace inet

#endif

