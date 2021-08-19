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

#include "inet/applications/clock/SimpleClockSynchronizer.h"

#include "inet/common/ModuleAccess.h"

namespace inet {

Define_Module(SimpleClockSynchronizer);

void SimpleClockSynchronizer::initialize(int stage)
{
    ApplicationBase::initialize(stage);
    if (stage == INITSTAGE_LOCAL) {
        synhronizationTimer = new cMessage("SynchronizationTimer");
        masterClock.reference(this, "masterClockModule", true);
        slaveClock.reference(this, "slaveClockModule", true);
        synchronizationIntervalParameter = &par("synchronizationInterval");
        synchronizationAccuracyParameter = &par("synchronizationAccuracy");
    }
}

void SimpleClockSynchronizer::handleMessageWhenUp(cMessage *msg)
{
    if (msg == synhronizationTimer) {
        synchronizeSlaveClock();
        scheduleSynchronizationTimer();
    }
    else
        throw cRuntimeError("Unknown message");
}

void SimpleClockSynchronizer::handleStartOperation(LifecycleOperation *operation)
{
    scheduleSynchronizationTimer();
}

void SimpleClockSynchronizer::synchronizeSlaveClock()
{
    slaveClock->setClockTime(masterClock->getClockTime() + synchronizationAccuracyParameter->doubleValue(), true);
}

void SimpleClockSynchronizer::scheduleSynchronizationTimer()
{
    scheduleAfter(synchronizationIntervalParameter->doubleValue(), synhronizationTimer);
}

} // namespace

