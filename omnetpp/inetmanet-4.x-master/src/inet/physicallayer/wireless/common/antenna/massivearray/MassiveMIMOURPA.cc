
#include "inet/physicallayer/wireless/common/antenna/massivearray/MassiveMIMOURPA.h"
#include "inet/physicallayer/wireless/common/base/packetlevel/AntennaBase.h"

#include "inet/common/ModuleAccess.h"

#include <iostream>
#include <sstream>
#include <fstream>
#include <istream>
#include <vector>
#include <complex>
#include <cmath>
#include <iomanip>
#include <algorithm>
#include <math.h>
#include <stdlib.h>
#include <mutex>
namespace inet {

namespace physicallayer {
using std::cout;

Define_Module(MassiveMIMOURPA);

MassiveMIMOURPA::MassiveMIMOURPA() : MassiveArray()
{
}

void MassiveMIMOURPA::initialize(int stage) {
    MassiveArray::initialize(stage);
    if (stage == INITSTAGE_LOCAL) {
        auto length = m(par("length"));
        auto freq = (par("freq").doubleValue());
        auto distance = (par("distance").doubleValue());
        auto phiz = (par("phiz").doubleValue());

        if (std::isnan(M))
            M = (par("M").intValue());
        if (std::isnan(N))
            N = (par("N").intValue());
        // cout << "Posizione: " << getMobility()->getCurrentPosition() << endl;

        auto it = risValuesUrpa.find(std::make_tuple(M,N));
        if (it == risValuesUrpa.end()) {
            risInt =  computeIntegral();
            risValuesUrpa[std::make_tuple(M,N)] = risInt;
        }
        else
            risInt = it->second;
        cModule *radioModule = getParentModule();
        IRadio * radio = check_and_cast<IRadio *>(radioModule);
        gain = makeShared<AntennaGain>(length, M, N, phiz, freq, distance, risInt, this, radio);
/*
        const char *energySourceModule = par("energySourceModule");

        energySource = dynamic_cast<IEnergySource *>(getModuleByPath(energySourceModule));
        if (energySource)
            energyConsumerId = energySource->addEnergyConsumer(this);
*/
    }
}

static double getFunc (double x, double y, int M, int N)
{
    if (x == 0)
        return 0;
    if (y == 0)
        return 0;
    if (y == M_PI/2 || y == M_PI || y == 3/2 * M_PI || y == 2 * M_PI)
        return 0;
    int emme = M;
    int enne = N;
    double first = sin(0.5*(emme)*M_PI*sin(x)*cos(y))/sin(0.5*M_PI*sin(x)*cos(y));
    double second = sin(0.5*(enne)*M_PI*sin(x)*sin(y))/sin(0.5*M_PI*sin(x)*sin(y));
    double third = sin(x);
    double afmod= (first*second)*(first*second);
    return afmod*third;
}

double MassiveMIMOURPA::AntennaGain::getMaxGain() const {
       int numel = ourpa->getNumAntennas();
       double numer = 4 * M_PI * numel*numel;
       double maxG = 20 * log10 (numer/risInt);
       return maxG;
}


double  MassiveMIMOURPA::AntennaGain::computeGain(const Quaternion &direction) const {
       double gain;
       double maxGain = getMaxGain();
       if (phiz == -1) {
           // omni
           return 1;
       }
       int numel = ourpa->getNumAntennas(); //without energy control
       cout<<"ACTIVE ARRAY ELEMENTS:"<<numel<<endl;
       double c = 300000 * 10 * 10 * 10;
       double lambda = c / freq;
       double d = distance * lambda;
       double k = (2 * M_PI) / lambda;
       double phizero = phiz * (M_PI / 180);
       auto heading = direction.toEulerAngles().alpha;

       double currentangle = heading.get() * 180.0/M_PI;
       if (currentangle == phiz )
           gain = maxGain;
       cout<<"PHI:"<<phiz<<endl;
       cout<<"CurrentAngle(degree):"<<currentangle<<endl;
       double psi1 = (k * d * sin(heading) * cos(heading)) + (- k * d * sin(phizero) * cos(phizero));
       double psi2 = (k * d * sin(heading) * sin(heading)) + (- k * d * sin(phizero) * sin(phizero));
       double first = sin(0.5*M*psi1)/sin(0.5*psi1);
       double second = sin(0.5*N*psi2)/sin(0.5*psi2);
       double afmodu= (first*second)*(first*second);
       double nume = 4 * M_PI * afmodu;
       gain= 20 * log10 (nume/risInt);
       //if (phiz==0)gain=1;
       if (gain<0)gain=1;
       if (gain > maxGain) gain=maxGain;
       Ieee80211ScalarReceiver * rec = dynamic_cast <Ieee80211ScalarReceiver *>(const_cast<IReceiver *>(radio->getReceiver()));
       if (rec != nullptr ) {
           // is of type Ieee80211ScalarReceiver
           if (phiz >=0 )
               gain = computeRecGain(direction.toEulerAngles().alpha - rad(M_PI));
           else
               gain = computeRecGain(direction.toEulerAngles().alpha + rad(M_PI));
       }
       cout<<"Gain (dB) at angle (degree): "<<currentangle<< " is: "<<gain<<endl;
       return gain;
}

double MassiveMIMOURPA::AntennaGain::computeRecGain(const rad &direction) const
{
    double gain;
    double maxGain = getMaxGain();
    if (phiz == -1) {
        // omni
        return 1;
    }
    int numel = ourpa->getNumAntennas(); //without energy control
    cout<<"ACTIVE ARRAY ELEMENTS:"<<numel<<endl;
    double c = 300000 * 10 * 10 * 10;
    double lambda = c / freq;
    double d = distance * lambda;
    double k = (2 * M_PI) / lambda;
    double phizero = phiz * (M_PI / 180);
    double heading = direction.get();

   // auto elevation = direction.toEulerAngles().beta;
    double currentangle = heading * 180.0/M_PI;

    if (currentangle == phiz )
        gain = maxGain;
    double psi1 = (k * d * sin(heading) * std::cos(heading)) + (- k * d * std::sin(phizero) * cos(phizero));
    double psi2 = (k * d * sin(heading) * std::sin(heading)) + (- k * d * std::sin(phizero) * sin(phizero));
    double first = sin(0.5*M*psi1)/sin(0.5*psi1);
    double second = sin(0.5*N*psi2)/sin(0.5*psi2);
    double afmodu= (first*second)*(first*second);
    double nume = 4 * M_PI * afmodu;
    gain= 20 * log10 (nume/risInt);
    //if (phiz==0)gain=1;
    if (gain<0)gain=1;
    if (gain > maxGain) gain=maxGain;
    return gain;
}

double MassiveMIMOURPA::AntennaGain::getAngolo(Coord p1, Coord p2) const
{
    double angolo;
    double x1, y1, x2, y2;
    double dx, dy;

    x1 = p1.x;
    y1 = p1.y;
    x2 = p2.x;
    y2 = p2.y;
    dx = x2 - x1;
    dy = y2 - y1;
    double cangl = dy / dx;
    angolo = std::atan(cangl) * (180 / 3.14);
    return angolo;
}


std::ostream& MassiveMIMOURPA::printToStream(std::ostream& stream, int level, int evFlags) const {
    stream << "MassiveMIMOURPA";
    if (level >= PRINT_LEVEL_DETAIL) {

        stream << ", length = " << gain->getLength();

        cout << getFullPath().substr(13, 8) << " Posizione: "
                << getMobility()->getCurrentPosition() << endl;

        //  std::vector<Coord>positions = this->getMempos()->getListaPos();
        //  cout<<getAngolo(Coord(2,0,0),Coord(1,2,0))<<endl;
        //  cout<<getAngolo(positions[0],positions[1])<<endl;
    }
    return AntennaBase::printToStream(stream, level, evFlags);
}

void MassiveMIMOURPA::receiveSignal(cComponent *source, simsignal_t signalID, double val, cObject *details)
{
    if (signalID == MassiveArrayConfigureChange)
    {
        auto radio =  check_and_cast<IRadio *>(getParentModule());
        if (radio->getRadioMode() == IRadio::RADIO_MODE_TRANSMITTER &&
                radio->getTransmissionState() == IRadio::TRANSMISSION_STATE_TRANSMITTING)
        {
            // pending
            pendingConfiguration = true;
            nextValue = val;
            return;
        }
        pendingConfiguration = false;
        nextValue = NaN;
        if (val >= -180 && val <= 180)
            gain->setPhizero(val);
        else if (val == 360)
            gain->setPhizero(360);
    }
    else {
        if (pendingConfiguration) {
            auto radio =  check_and_cast<IRadio *>(getParentModule());

            if (radio->getRadioMode() != IRadio::RADIO_MODE_TRANSMITTER
                    || (radio->getRadioMode() == IRadio::RADIO_MODE_TRANSMITTER
                            && radio->getTransmissionState()
                            != IRadio::TRANSMISSION_STATE_TRANSMITTING)) {
                if (std::isnan(nextValue))
                    throw cRuntimeError("next value is nan");
                if (nextValue >= -180 && nextValue <= 180)
                    gain->setPhizero(nextValue);
                else if (nextValue == 360)
                    gain->setPhizero(360);
                nextValue = NaN;
                pendingConfiguration = false;
            }
        }
    }
}

void MassiveMIMOURPA::receiveSignal(cComponent *source, simsignal_t signalID, intval_t val, cObject *details) {
    if (signalID != MassiveArrayConfigureChange) {
        // Radio signals
        if (pendingConfiguration) {
            auto radio = check_and_cast<IRadio*>(getParentModule());
            if (radio->getRadioMode() != IRadio::RADIO_MODE_TRANSMITTER
                    || (radio->getRadioMode() == IRadio::RADIO_MODE_TRANSMITTER
                            && radio->getTransmissionState()
                                    != IRadio::TRANSMISSION_STATE_TRANSMITTING)) {
                if (std::isnan(nextValue))
                    throw cRuntimeError("next value is nan");
                if (nextValue >= -180 && nextValue <= 180)
                    gain->setPhizero(nextValue);
                else if (nextValue == 360)
                    gain->setPhizero(360);
                nextValue = NaN;
                pendingConfiguration = false;
            }
        }
    }
}

void MassiveMIMOURPA::setDirection(const double &val)
{
    auto radio =  check_and_cast<IRadio *>(getParentModule());
    if (radio->getRadioMode() == IRadio::RADIO_MODE_TRANSMITTER
            && radio->getTransmissionState()
                    == IRadio::TRANSMISSION_STATE_TRANSMITTING) {
        // pending
        pendingConfiguration = true;
        nextValue = val;
        return;
    }

    nextValue = NaN;
    if (val >= -180 && val <= 180)
        gain->setPhizero(val);
    else if (val == 360)
        gain->setPhizero(360);
    pendingConfiguration = false;
}


double MassiveMIMOURPA::computeIntegral() {

    Simpson2D::Mat matrix;
    Simpson2D::limits limitX, limitY;
    limitX.setLower(0);
    limitX.setUpper(M_PI);
    limitY.setLower(0);
    limitY.setUpper(2 * M_PI);
    return Simpson2D::Integral(getFunc, limitX, limitY, 1999, M, N);
}


} // namespace physicallayer

} // namespace inet

