/* fscale
  Floating Point Autoscale Function V0.1
  Paul Badger 2007
  Modified from code by Greg Shakar

  This function will scale one set of floating point numbers (range) to another set of floating point numbers (range)
  It has a "curve" parameter so that it can be made to favor either the end of the output. (Logarithmic mapping)

  It takes 6 parameters

  originalMin - the minimum value of the original range - this MUST be less than origninalMax
  originalMax - the maximum value of the original range - this MUST be greater than orginalMin

  newBegin - the end of the new range which maps to orginalMin - it can be smaller, or larger, than newEnd, to facilitate inverting the ranges
  newEnd - the end of the new range which maps to originalMax  - it can be larger, or smaller, than newBegin, to facilitate inverting the ranges

  inputValue - the variable for input that will mapped to the given ranges, this variable is constrained to originaMin <= inputValue <= originalMax
  curve - curve is the curve which can be made to favor either end of the output scale in the mapping. Parameters are from -10 to 10 with 0 being
          a linear mapping (which basically takes curve out of the equation)

  To understand the curve parameter do something like this:

  void loop(){
  for ( j=0; j < 200; j++){
    scaledResult = fscale( 0, 200, 0, 200, j, -1.5);

    Serial.print(j, DEC);
    Serial.print("    ");
    Serial.println(scaledResult, DEC);
  }
  }

  And try some different values for the curve function - remember 0 is a neutral, linear mapping

  To understand the inverting ranges, do something like this:

  void loop(){
  for ( j=0; j < 200; j++){
    scaledResult = fscale( 0, 200, 200, 0, j, 0);

    //  Serial.print lines as above

  }
  }

*/

#include <math.h>

float fscale( float originalMin, float originalMax, float newBegin, float
              newEnd, float inputValue, float curve) {

  float OriginalRange = 0;
  float NewRange = 0;
  float zeroRefCurVal = 0;
  float normalizedCurVal = 0;
  float rangedValue = 0;
  boolean invFlag = 0;


  // condition curve parameter
  // limit range

  if (curve > 10) curve = 10;
  if (curve < -10) curve = -10;

  curve = (curve * -.1) ; // - invert and scale - this seems more intuitive - postive numbers give more weight to high end on output
  curve = pow(10, curve); // convert linear scale into lograthimic exponent for other pow function

  /*
    Serial.println(curve * 100, DEC);   // multply by 100 to preserve resolution
    Serial.println();
  */

  // Check for out of range inputValues
  if (inputValue < originalMin) {
    inputValue = originalMin;
  }
  if (inputValue > originalMax) {
    inputValue = originalMax;
  }

  // Zero Refference the values
  OriginalRange = originalMax - originalMin;

  if (newEnd > newBegin) {
    NewRange = newEnd - newBegin;
  }
  else
  {
    NewRange = newBegin - newEnd;
    invFlag = 1;
  }

  zeroRefCurVal = inputValue - originalMin;
  normalizedCurVal  =  zeroRefCurVal / OriginalRange;   // normalize to 0 - 1 float

  /*
    Serial.print(OriginalRange, DEC);
    Serial.print("   ");
    Serial.print(NewRange, DEC);
    Serial.print("   ");
    Serial.println(zeroRefCurVal, DEC);
    Serial.println();
  */

  // Check for originalMin > originalMax  - the math for all other cases i.e. negative numbers seems to work out fine
  if (originalMin > originalMax ) {
    return 0;
  }

  if (invFlag == 0) {
    rangedValue =  (pow(normalizedCurVal, curve) * NewRange) + newBegin;

  }
  else     // invert the ranges
  {
    rangedValue =  newBegin - (pow(normalizedCurVal, curve) * NewRange);
  }

  return rangedValue;
}
