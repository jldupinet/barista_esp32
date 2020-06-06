#include "Arduino.h"
#include "Extraction.h"


Extraction::Extraction() {
  this->shouldExtract = false;
}

Extraction::Extraction(float preInfusionTime, float targetWeight) {
  this->preInfusionTime = preInfusionTime;
  this->targetWeight = targetWeight;
  this->shouldExtract = true;
}
