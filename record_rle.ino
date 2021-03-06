/* Teensy Logic Analyzer
 * Copyright (c) 2018 LAtimes2
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

//
// This function records data using Run Length Encoding (RLE).
// This is useful when the data does not change very often. If the
// value does not change from the previous value, it just increments
// a count. When the value changes again, it stores an extra sample
// with the count, along with the highest channel set to 1 (e.g.
// channel 7 for 8 channel mode, channel 3 for 4 channel mode). This
// means that the highest channel cannot be used for data.
//
void recordRLEData (sumpSetupVariableStruct &sv,
                    sumpDynamicVariableStruct &dynamic)
{
  bool bufferHasWrapped = false;
  int elementsToRecord = sv.samplesToRecord / sv.samplesPerElement;
  register uint32_t *inputPtr = (uint32_t *)sv.startOfBuffer;
  uint32_t *endOfBuffer = (uint32_t *)sv.endOfBuffer;
  uint32_t *startOfBuffer = (uint32_t *)sv.startOfBuffer;
  uint32_t *startPtr = (uint32_t *)sv.startOfBuffer;
  byte samplesPerElement = sv.samplesPerElement;
  byte samplesPerElementMinusOne = samplesPerElement - 1;
  
  // shift right 1 to mask off upper channel, which is used for RLE
  uint32_t sampleMask = sv.sampleMask >> 1;
  uint32_t anyDataMask = sv.anyDataMask;;

  // this is to set the highest channel for an RLE count
  uint32_t rleCountIndicator = sv.rleCountIndicator;

  uint32_t sampleShift = sv.sampleShift;
  int sampleValue = -1;
  int previousSampleValue = -1;
  uint32_t previousFirstValue = 0;
  bool rleInProgress = false;
  int triggerCount = samplesPerElementMinusOne;
  register int workingCount = samplesPerElementMinusOne + 1;
  register uint32_t workingValue = 0;

  int currentTriggerLevel = 0;
  uint32_t triggerMask = sv.triggerMask[0];
  uint32_t triggerValue = sv.triggerValue[0];
  uint32_t triggerDelay = sv.triggerDelay[0];

  // state is not used except to make a switch context for ptr
  stateType state;
  // ptr points to a label inside the switch statement to speed it up,
  // since it doesn't have to calculate the jump table each time through.
  // Label names are the case names with '_Label' added
  register void *switch_ptr;

  // if using a trigger
  if (sv.triggerMask[0])
  {
    //state = Buffering;
    switch_ptr = &&Buffering_Label;

    // position to arm the trigger
    startPtr = inputPtr + sv.delaySizeInElements;
  }
  else
  {
    //state = Triggered_Second_Pass;
    switch_ptr = &&Triggered_Second_Pass_Label;

    startPtr = endOfBuffer;
  }

  // 100% causes a problem with circular buffer - never stops
  if (startPtr >= endOfBuffer)
  {
    startPtr = endOfBuffer - 1;
  }

  maskInterrupts ();

  // read enough samples prior to arming to meet the pre-trigger request
  // (for speed, use while (1) and break instead of while (inputPtr != startPtr))
  while (1)
  {
    waitForTimeout ();

    // read a sample
    sampleValue = PORT_DATA_INPUT_REGISTER & sampleMask;

    if (sampleValue != previousSampleValue)
    {
      // if previous rle count has not been written
      if (workingCount == 0)
      {
        #ifdef TIMING_DISCRETES_2
          digitalWriteFast (TIMING_PIN_3, HIGH);
        #endif

        *(inputPtr) = workingValue;
        ++inputPtr;

        // adjust for circular buffer wraparound at the end
        if (inputPtr >= endOfBuffer)
        {
          #ifdef TIMING_DISCRETES_2
            digitalWriteFast (TIMING_PIN_5, HIGH);
          #endif

          inputPtr = startOfBuffer;

          bufferHasWrapped = true;

          // if any data is received from PC, then stop (assume it is a reset)
          if (usbInterruptPending ())
          {
            DEBUG_SERIAL(print(" Halt due to USB interrupt"));
            set_led_off ();
            SUMPreset();
            break;
          }

          #ifdef TIMING_DISCRETES_2
            digitalWriteFast (TIMING_PIN_5, LOW);
          #endif
        }

        // save new value (no need to shift since just saved)
        workingValue = sampleValue;
        workingCount = samplesPerElementMinusOne;

        #ifdef TIMING_DISCRETES_2
          digitalWriteFast (TIMING_PIN_3, LOW);
        #endif
      }
      else
      {
        // save new value
        workingValue = (workingValue << sampleShift) + sampleValue;
        --workingCount;
      }

      previousSampleValue = sampleValue;
      rleInProgress = false;
    }
    else  // same
    {
      #ifdef TIMING_DISCRETES_2
        digitalWriteFast (TIMING_PIN_2, HIGH);
      #endif

      if (rleInProgress == false)
      {
        // save count for previous value
        workingValue = (workingValue << sampleShift) + rleCountIndicator;
        --workingCount;

        rleInProgress = true;
      }

      // number of RLE instances is stored in the working Value
      workingValue++;

      // if RLE count is at the maximum value
      if ((workingValue & sampleMask) == sampleMask)
      {
        #ifdef TIMING_DISCRETES_2
          digitalWriteFast (TIMING_PIN_4, HIGH);
        #endif

        // force current count to be written and new count started
        rleInProgress = false;

        // not enough time to check if also going to write working value
        if (workingCount != 0)
        {
          // if any data is received from PC, then stop (assume it is a reset)
          if (usbInterruptPending ())
          {
            DEBUG_SERIAL(print(" Halt due to USB interrupt"));
            set_led_off ();
            SUMPreset();
            break;
          }
        }

        #ifdef TIMING_DISCRETES_2
          digitalWriteFast (TIMING_PIN_4, LOW);
        #endif
      }

      #ifdef TIMING_DISCRETES_2
        digitalWriteFast (TIMING_PIN_2, LOW);
      #endif
    }

    // save the working value when it is full
    if (workingCount == 0 && rleInProgress == false)
    {
      #ifdef TIMING_DISCRETES_2
        digitalWriteFast (TIMING_PIN_3, HIGH);
      #endif

      *(inputPtr) = workingValue;
      ++inputPtr;

      // adjust for circular buffer wraparound at the end
      if (inputPtr >= endOfBuffer)
      {
        #ifdef TIMING_DISCRETES_2
          digitalWriteFast (TIMING_PIN_5, HIGH);
        #endif

        inputPtr = startOfBuffer;

        bufferHasWrapped = true;

        // if any data is received from PC, then stop (assume it is a reset)
        if (usbInterruptPending ())
        {
          DEBUG_SERIAL(print(" Halt due to USB interrupt"));
          set_led_off ();
          SUMPreset();
          break;
        }

        #ifdef TIMING_DISCRETES_2
          digitalWriteFast (TIMING_PIN_5, LOW);
        #endif
      }

      workingCount = samplesPerElement;
      workingValue = 0;

      #ifdef TIMING_DISCRETES_2
        digitalWriteFast (TIMING_PIN_3, LOW);
      #endif
      
    }  // if workingCount == 0
    else if (workingCount == 1)
    {
      // just before overwriting old data, check if it has data in it.
      // This is to prevent having just RLE counts at the start of the
      // buffer because it wrapped arn overwrote the original first value.

      // if any data (i.e. not just RLE counts) in this value, save it
      if ((*(inputPtr) & anyDataMask) != anyDataMask)
      {
        previousFirstValue = *(inputPtr);
      }
    }

    //
    // For speed, perform the switch statement using a pointer to
    // the current state and a goto. The switch statement has to
    // be in the code so the compiler sets it up properly, but
    // the goto uses the duplicate labels for each case.
    //
    goto *switch_ptr;

    switch (state) {
      case LookingForTrigger :
      LookingForTrigger_Label:
        // if trigger has occurred
        if ((sampleValue & triggerMask) == triggerValue)
        {
          if (triggerDelay > 0) {
            //state = TriggerDelay;
            switch_ptr = &&TriggerDelay_Label;
          } else {
            // if last trigger level
            if (currentTriggerLevel >= sv.lastTriggerLevel)
            {
              triggerCount = workingCount;

              // last location to save
              startPtr = inputPtr - sv.delaySizeInElements;

              // move to triggered state
              //state = Triggered_First_Pass;
              switch_ptr = &&Triggered_First_Pass_Label;
              #ifdef TIMING_DISCRETES
                digitalWriteFast (TIMING_PIN_1, LOW);
              #endif

            } else {

              #ifdef TIMING_DISCRETES
                digitalWriteFast (TIMING_PIN_1, LOW);
              #endif

              // advance to next trigger level
              ++currentTriggerLevel;
              triggerMask = sv.triggerMask[currentTriggerLevel];
              triggerValue = sv.triggerValue[currentTriggerLevel];

              #ifdef TIMING_DISCRETES
                digitalWriteFast (TIMING_PIN_1, HIGH);
              #endif
            }
          }
        }
        break;

        case TriggerDelay :
        TriggerDelay_Label:
          --triggerDelay;
          if (triggerDelay == 0) {
            // if last trigger level
            if (currentTriggerLevel >= sv.lastTriggerLevel) {
              triggerCount = workingCount;
      
              // last location to save
              startPtr = inputPtr - sv.delaySizeInElements;

              // move to triggered state
              //state = Triggered_First_Pass;
              switch_ptr = &&Triggered_First_Pass_Label;

            } else {
              ++currentTriggerLevel;
              triggerMask = sv.triggerMask[currentTriggerLevel];
              triggerValue = sv.triggerValue[currentTriggerLevel];
              triggerDelay = sv.triggerDelay[currentTriggerLevel];
              //state = LookingForTrigger;
              switch_ptr = &&LookingForTrigger_Label;
            }
          }
          break;

        case Triggered:
        Triggered_Label:
          if (inputPtr == startPtr) {
            // done recording. Use a goto for speed so that
            // no 'if' needed to check for done in the main loop
            goto DoneRecording;
          }
          break;

        case Buffering:
        Buffering_Label:
          // if enough data is buffered
          if (inputPtr >= startPtr)
          {
            // move to armed state
            //state = LookingForTrigger;
            switch_ptr = &&LookingForTrigger_Label;
            set_led_on ();

            #ifdef TIMING_DISCRETES
              digitalWriteFast (TIMING_PIN_1, HIGH);
            #endif
          }
          break;

        case Triggered_Second_Pass:
        Triggered_Second_Pass_Label:
          // adjust for circular buffer wraparound at the end.
          if (startPtr < startOfBuffer)
          {
            startPtr = startPtr + elementsToRecord;
          }

          // move to triggered state
          //state = Triggered;
          switch_ptr = &&Triggered_Label;

          #ifdef TIMING_DISCRETES
            digitalWriteFast (TIMING_PIN_1, LOW);
          #endif
          break;

        case Triggered_First_Pass:
        Triggered_First_Pass_Label:
          // go as fast as possible to try to catch up from Triggered state
          //state = Triggered_Second_Pass;
          switch_ptr = &&Triggered_Second_Pass_Label;
          set_led_off (); // TRIGGERED, turn off LED
          break;
    }  // switch
 
  } // while (1)

  DoneRecording:

  // cleanup
  unmaskInterrupts ();

  #ifdef TIMING_DISCRETES
    digitalWriteFast (TIMING_PIN_0, LOW);
  #endif

  // save the first value in case it was overwritten due to buffer overflow
  // (i.e. first sample is an RLE count, but what was the value it is counting?)
  sv.firstRLEValue = previousFirstValue;

  // adjust trigger count
  dynamic.triggerSampleIndex = (startPtr + sv.delaySizeInElements - startOfBuffer) * samplesPerElement + samplesPerElementMinusOne - triggerCount;

  dynamic.bufferHasWrapped = bufferHasWrapped;

  // adjust for circular buffer wraparound at the end.
  if (dynamic.triggerSampleIndex >= (uint32_t)sv.samplesToRecord)
  {
    dynamic.triggerSampleIndex = dynamic.triggerSampleIndex - sv.samplesToRecord;
  }

  if (inputPtr != startPtr)
  {
    int deltaElements = inputPtr - startOfBuffer;

    if (deltaElements < 0)
    {
      deltaElements += elementsToRecord;
    }

    dynamic.interruptedIndex = deltaElements * samplesPerElement;
  }
}





