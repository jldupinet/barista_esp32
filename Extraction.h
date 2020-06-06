#ifndef Extraction_h
#define Extraction_h

#include "Arduino.h"

class Extraction
{
  public:
    Extraction();
    Extraction(float preInfusionTime, float targetWeight);
    float preInfusionTime;
    float targetWeight;
    float elapsedTime;
    float currentWeight;
    bool shouldExtract = false;

};

#endif
