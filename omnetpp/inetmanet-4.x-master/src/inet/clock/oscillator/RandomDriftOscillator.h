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

#ifndef __INET_RANDOMDRIFTOSCILLATOR_H
#define __INET_RANDOMDRIFTOSCILLATOR_H

#include "inet/clock/base/DriftingOscillatorBase.h"

namespace inet {

class INET_API RandomDriftOscillator : public DriftingOscillatorBase
{
  protected:
    cMessage *changeTimer = nullptr;
    cPar *driftRateParameter = nullptr;
    cPar *driftRateChangeParameter = nullptr;
    cPar *changeIntervalParameter = nullptr;
    double driftRateChangeTotal = 0;
    double driftRateChangeLowerLimit = NaN;
    double driftRateChangeUpperLimit = NaN;

  protected:
    virtual ~RandomDriftOscillator() { cancelAndDelete(changeTimer); }

    virtual void initialize(int stage) override;
    virtual void handleMessage(cMessage *message) override;
};

} // namespace inet

#endif

