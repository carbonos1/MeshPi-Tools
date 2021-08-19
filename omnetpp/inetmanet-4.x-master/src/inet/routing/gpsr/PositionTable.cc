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

#include "inet/routing/gpsr/PositionTable.h"

namespace inet {

std::vector<L3Address> PositionTable::getAddresses() const
{
    std::vector<L3Address> addresses;
    for (const auto& elem : addressToPositionMap)
        addresses.push_back(elem.first);
    return addresses;
}

bool PositionTable::hasPosition(const L3Address& address) const
{
    AddressToPositionMap::const_iterator it = addressToPositionMap.find(address);
    return it != addressToPositionMap.end();
}

Coord PositionTable::getPosition(const L3Address& address) const
{
    AddressToPositionMap::const_iterator it = addressToPositionMap.find(address);
    if (it == addressToPositionMap.end())
        return Coord(NaN, NaN, NaN);
    else
        return it->second.second;
}

void PositionTable::setPosition(const L3Address& address, const Coord& coord)
{
    ASSERT(!address.isUnspecified());
    addressToPositionMap[address] = AddressToPositionMapValue(simTime(), coord);
}

void PositionTable::removePosition(const L3Address& address)
{
    auto it = addressToPositionMap.find(address);
    addressToPositionMap.erase(it);
}

void PositionTable::removeOldPositions(simtime_t timestamp)
{
    for (auto it = addressToPositionMap.begin(); it != addressToPositionMap.end();)
        if (it->second.first <= timestamp)
            addressToPositionMap.erase(it++);
        else
            it++;

}

void PositionTable::clear()
{
    addressToPositionMap.clear();
}

simtime_t PositionTable::getOldestPosition() const
{
    simtime_t oldestPosition = SimTime::getMaxTime();
    for (const auto& elem : addressToPositionMap) {
        const simtime_t& time = elem.second.first;
        if (time < oldestPosition)
            oldestPosition = time;
    }
    return oldestPosition;
}

std::ostream& operator<<(std::ostream& o, const PositionTable& t)
{
    o << "{ ";
    for (auto elem : t.addressToPositionMap) {
        o << elem.first << ":(" << elem.second.first << ";" << elem.second.second << ") ";
    }
    o << "}";
    return o;
}

} // namespace inet
