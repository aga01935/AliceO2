/// \file Point.cxx
/// \brief Implementation of the Point class

#include "Point.h"

#include <iostream>

using std::cout;
using std::endl;
using namespace AliceO2::ITS;

Point::Point() : FairMCPoint()
{
}

Point::Point(Int_t trackID, Int_t detID, TVector3 startPos, TVector3 pos, TVector3 mom,
             Double_t startTime, Double_t time, Double_t length, Double_t eLoss, Int_t shunt)
  : FairMCPoint(trackID, detID, pos, mom, time, length, eLoss)
{
}

Point::~Point()
{
}

void Point::Print(const Option_t* opt) const
{
  cout << *this;
}


ClassImp(AliceO2::ITS::Point)
