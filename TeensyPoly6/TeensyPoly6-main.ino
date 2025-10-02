/*
  Poly 6 virtual Synth and editor

  Includes code by:
    Dave Benn - Handling MUXs, a few other bits and original inspiration  https://www.notesandvolts.com/2019/01/teensy-synth-part-10-hardware.html

  Arduino IDE
  Tools Settings:
  Board: "Teensy3.6"
  USB Type: "Serial + MIDI"
  CPU Speed: "192" OVERCLOCK
  Optimize: "Fastest"

  Additional libraries:
    Agileware CircularBuffer available in Arduino libraries manager
    Replacement files are in the Modified Libraries folder and need to be placed in the teensy Audio folder.
*/

#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>
#include <MIDI.h>
#include "MidiCC.h"
#include "Constants.h"
#include "Parameters.h"
#include "PatchMgr.h"
#include "HWControls.h"
#include "EepromMgr.h"



#define PARAMETER 0      //The main page for displaying the current patch and control (parameter) changes
#define RECALL 1         //Patches list
#define SAVE 2           //Save patch page
#define REINITIALISE 3   // Reinitialise message
#define PATCH 4          // Show current patch bypassing PARAMETER
#define PATCHNAMING 5    // Patch naming page
#define DELETE 6         //Delete patch page
#define DELETEMSG 7      //Delete patch message page
#define SETTINGS 8       //Settings page
#define SETTINGSVALUE 9  //Settings page

unsigned int state = PARAMETER;

#include "ST7735Display.h"

boolean cardStatus = false;

//MIDI 5 Pin DIN
MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI);
unsigned long buttonDebounce = 0;

#include "Settings.h"
//#include "SettingsService.h"

int patchNo = 1;               //Current patch no
int voiceToReturn = -1;        //Initialise
long earliestTime = millis();  //For voice allocation - initialise to now

struct VoiceAndNote {
  int note;
  int velocity;
  long timeOn;
};

struct VoiceAndNote voices[NO_OF_VOICES] = {
  { -1, -1, 0 },
  { -1, -1, 0 },
  { -1, -1, 0 },
  { -1, -1, 0 },
  { -1, -1, 0 },
  { -1, -1, 0 }
};

boolean voiceOn[NO_OF_VOICES] = { false, false, false, false, false, false };
int prevNote = 0;  //Initialised to middle value
bool notes[88] = { 0 }, initial_loop = 1;
int8_t noteOrder[80] = { 0 }, orderIndx = { 0 };
int noteMsg;

void setup() {
  AudioMemory(470);
  SPI.begin();
  setupDisplay();
  setUpSettings();
  setupHardware();

  cardStatus = SD.begin(BUILTIN_SDCARD);
  if (cardStatus) {
    Serial.println("SD card is connected");
    //Get patch numbers and names from SD card
    loadPatches();
    if (patches.size() == 0) {
      //save an initialised patch to SD card
      savePatch("1", INITPATCH);
      loadPatches();
    }
  } else {
    Serial.println("SD card is not connected or unusable");

    reinitialiseToPanel();
    showPatchPage("No SD", "conn'd / usable");
  }


  //USB Client MIDI#
  usbMIDI.begin();
  usbMIDI.setHandleNoteOn(myNoteOn);
  usbMIDI.setHandleNoteOff(myNoteOff);
  usbMIDI.setHandlePitchChange(myPitchBend);
  usbMIDI.setHandleControlChange(myControlChange);
  usbMIDI.setHandleProgramChange(myProgramChange);
  usbMIDI.setHandleAfterTouchChannel(myAfterTouch);
  Serial.println("USB Client MIDI Listening");

  //MIDI 5 Pin DIN
  MIDI.begin();
  MIDI.setHandleNoteOn(myNoteOn);
  MIDI.setHandleNoteOff(myNoteOff);
  MIDI.setHandlePitchBend(myPitchBend);
  MIDI.setHandleControlChange(myControlChange);
  MIDI.setHandleProgramChange(myProgramChange);
  MIDI.setHandleAfterTouchChannel(myAfterTouch);
  MIDI.turnThruOn(midi::Thru::Mode::Off);
  Serial.println("MIDI In DIN Listening");

  encCW = getEncoderDir();
  midiChannel = getMIDIChannel();
  modWheelDepth = getModWheelDepth();
  pitchBendRange = getPitchBendRange();
  afterTouchDepth = getafterTouchDepth();
  NP = getNotePriority();
  unidetune = getUnisonDetune();
  oldunidetune = unidetune;
  switch (unidetune) {
    case 0:
      detune = 1.000;
      break;
    case 1:
      detune = 1.002;
      break;
    case 2:
      detune = 1.004;
      break;
    case 3:
      detune = 1.006;
      break;
    case 4:
      detune = 1.008;
      break;
    case 5:
      detune = 1.010;
      break;
    case 6:
      detune = 1.012;
      break;
    case 7:
      detune = 1.014;
      break;
    case 8:
      detune = 1.016;
      break;
    case 9:
      detune = 1.018;
      break;
    case 10:
      detune = 1.020;
      break;
  }
  olddetune = detune;

  //vco setup
  vcoA1.begin(vcoVol, 150, WAVEFORM_SAWTOOTH);
  vcoB1.begin(vcoVol, 150, WAVEFORM_SQUARE);
  vcoC1.begin(vcoVol * 1.5, 150, WAVEFORM_ARBITRARY);
  sub1.begin(vcoVol * 1.5, 150, WAVEFORM_TRIANGLE);

  vcoA2.begin(vcoVol, 150, WAVEFORM_SAWTOOTH);
  vcoB2.begin(vcoVol, 150, WAVEFORM_SQUARE);
  vcoC2.begin(vcoVol * 1.5, 150, WAVEFORM_ARBITRARY);
  sub2.begin(vcoVol * 1.5, 150, WAVEFORM_TRIANGLE);

  vcoA3.begin(vcoVol, 150, WAVEFORM_SAWTOOTH);
  vcoB3.begin(vcoVol, 150, WAVEFORM_SQUARE);
  vcoC3.begin(vcoVol * 1.5, 150, WAVEFORM_ARBITRARY);
  sub3.begin(vcoVol * 1.5, 150, WAVEFORM_TRIANGLE);

  vcoA4.begin(vcoVol, 150, WAVEFORM_SAWTOOTH);
  vcoB4.begin(vcoVol, 150, WAVEFORM_SQUARE);
  vcoC4.begin(vcoVol * 1.5, 150, WAVEFORM_ARBITRARY);
  sub4.begin(vcoVol * 1.5, 150, WAVEFORM_TRIANGLE);

  vcoA5.begin(vcoVol, 150, WAVEFORM_SAWTOOTH);
  vcoB5.begin(vcoVol, 150, WAVEFORM_SQUARE);
  vcoC5.begin(vcoVol * 1.5, 150, WAVEFORM_ARBITRARY);
  sub5.begin(vcoVol * 1.5, 150, WAVEFORM_TRIANGLE);

  vcoA6.begin(vcoVol, 150, WAVEFORM_SAWTOOTH);
  vcoB6.begin(vcoVol, 150, WAVEFORM_SQUARE);
  vcoC6.begin(vcoVol * 1.5, 150, WAVEFORM_ARBITRARY);
  sub6.begin(vcoVol * 1.5, 150, WAVEFORM_TRIANGLE);


  //filter
  filter1.octaveControl(7);
  filterEnv1.sustain(0);

  filter2.octaveControl(7);
  filterEnv2.sustain(0);

  filter3.octaveControl(7);
  filterEnv3.sustain(0);

  filter4.octaveControl(7);
  filterEnv4.sustain(0);

  filter5.octaveControl(7);
  filterEnv5.sustain(0);

  filter6.octaveControl(7);
  filterEnv6.sustain(0);


  //lfo A
  lfoA1.begin(WAVEFORM_SINE);
  lfoA2.begin(WAVEFORM_SINE);
  lfoA3.begin(WAVEFORM_SINE);
  lfoA4.begin(WAVEFORM_SINE);
  lfoA5.begin(WAVEFORM_SINE);
  lfoA6.begin(WAVEFORM_SINE);

  //lfo B
  lfoB1.begin(0.5, 1, WAVEFORM_TRIANGLE);
  lfoB2.begin(0.5, 1, WAVEFORM_TRIANGLE);
  lfoB3.begin(0.5, 1, WAVEFORM_TRIANGLE);
  lfoB4.begin(0.5, 1, WAVEFORM_TRIANGLE);
  lfoB5.begin(0.5, 1, WAVEFORM_TRIANGLE);
  lfoB6.begin(0.5, 1, WAVEFORM_TRIANGLE);




  //dly
  dlyFiltL.frequency(4000);
  dlyFiltR.frequency(3000);

  dlyMixL.gain(0, 0.75);
  dlyMixL.gain(0, 0.75);


  dlyL.disable(1);
  dlyL.disable(2);
  dlyL.disable(3);
  dlyL.disable(4);
  dlyL.disable(5);
  dlyL.disable(6);
  dlyL.disable(7);

  dlyR.disable(1);
  dlyR.disable(2);
  dlyR.disable(3);
  dlyR.disable(4);
  dlyR.disable(5);
  dlyR.disable(6);
  dlyR.disable(7);


  //reverb
  reverb.roomsize(0.9);
  reverb.damping(0.8);




  //LFO DESTINATION DISCONNECT
  patchCord7.disconnect();   //vcoA
  patchCord8.disconnect();   //vcoB
  patchCord9.disconnect();   //vcoC
  patchCord10.disconnect();  //sub
  patchCord15.disconnect();  //vcoA
  patchCord16.disconnect();  //vcoB
  patchCord17.disconnect();  //vcoC
  patchCord18.disconnect();  //sub
  patchCord20.disconnect();  //vcoA
  patchCord21.disconnect();  //vcoB
  patchCord22.disconnect();  //vcoC
  patchCord23.disconnect();  //sub
  patchCord25.disconnect();  //vcoA
  patchCord26.disconnect();  //vcoB
  patchCord27.disconnect();  //vcoC
  patchCord28.disconnect();  //sub
  patchCord36.disconnect();  //vcoA
  patchCord37.disconnect();  //vcoB
  patchCord38.disconnect();  //vcoC
  patchCord39.disconnect();  //sub
  patchCord41.disconnect();  //vcoA
  patchCord42.disconnect();  //vcoB
  patchCord43.disconnect();  //vcoC
  patchCord44.disconnect();  //sub

  patchCord11.disconnect();  //filter
  patchCord19.disconnect();  //filter
  patchCord24.disconnect();  //filter
  patchCord29.disconnect();  //filter
  patchCord40.disconnect();  //filter
  patchCord45.disconnect();  //filter

  recallPatch(patchNo);  //Load first patch
  updateScreen();
}

void startParameterDisplay() {
  state = PARAMETER;
  updateScreen();

  lastDisplayTriggerTime = millis();
  waitingToUpdate = true;
}

int getVoiceNo(int note) {
  voiceToReturn = -1;       //Initialise to 'null'
  earliestTime = millis();  //Initialise to now
  if (note == -1) {
    //NoteOn() - Get the oldest free voice (recent voices may be still on release stage)
    for (int i = 0; i < NO_OF_VOICES; i++) {
      if (voices[i].note == -1) {
        if (voices[i].timeOn < earliestTime) {
          earliestTime = voices[i].timeOn;
          voiceToReturn = i;
        }
      }
    }
    if (voiceToReturn == -1) {
      //No free voices, need to steal oldest sounding voice
      earliestTime = millis();  //Reinitialise
      for (int i = 0; i < NO_OF_VOICES; i++) {
        if (voices[i].timeOn < earliestTime) {
          earliestTime = voices[i].timeOn;
          voiceToReturn = i;
        }
      }
    }
    return voiceToReturn + 1;
  } else {
    //NoteOff() - Get voice number from note
    for (int i = 0; i < NO_OF_VOICES; i++) {
      if (voices[i].note == note) {
        return i + 1;
      }
    }
  }
  //Shouldn't get here, return voice 1
  return 1;
}

void myNoteOn(byte channel, byte note, byte velocity) {

  if (MONO_POLY_1 < 511 && MONO_POLY_2 < 511) {
    detune = 1.000;  //POLYPHONIC mode
    if (note < 0 || note > 127) return;
    switch (getVoiceNo(-1)) {
      case 1:
        voices[0].note = note;
        note1freq = note;
        env1.noteOn();
        filterEnv1.noteOn();
        lfoAenv1.noteOn();
        env1on = true;
        voiceOn[0] = true;
        break;

      case 2:
        voices[1].note = note;
        note2freq = note;
        env2.noteOn();
        filterEnv2.noteOn();
        lfoAenv2.noteOn();
        env2on = true;
        voiceOn[1] = true;
        break;

      case 3:
        voices[2].note = note;
        note3freq = note;
        env3.noteOn();
        filterEnv3.noteOn();
        lfoAenv3.noteOn();
        env3on = true;
        voiceOn[2] = true;
        break;

      case 4:
        voices[3].note = note;
        note4freq = note;
        env4.noteOn();
        filterEnv4.noteOn();
        lfoAenv4.noteOn();
        env4on = true;
        voiceOn[3] = true;
        break;

      case 5:
        voices[4].note = note;
        note5freq = note;
        env5.noteOn();
        filterEnv5.noteOn();
        lfoAenv5.noteOn();
        env5on = true;
        voiceOn[4] = true;
        break;

      case 6:
        voices[5].note = note;
        note6freq = note;
        env6.noteOn();
        filterEnv6.noteOn();
        lfoAenv6.noteOn();
        env6on = true;
        voiceOn[5] = true;
        break;
    }
  }

  if (MONO_POLY_1 > 511 && MONO_POLY_2 < 511) {  //UNISON mode
    detune = olddetune;
    noteMsg = note;

    if (velocity == 0) {
      notes[noteMsg] = false;
    } else {
      notes[noteMsg] = true;
    }

    switch (NP) {
      case 0:
        commandTopNoteUnison();
        break;

      case 1:
        commandBottomNoteUnison();
        break;

      case 2:
        if (notes[noteMsg]) {  // If note is on and using last note priority, add to ordered list
          orderIndx = (orderIndx + 1) % 40;
          noteOrder[orderIndx] = noteMsg;
        }
        commandLastNoteUnison();
        break;
    }
  }

  if (MONO_POLY_1 < 511 && MONO_POLY_2 > 511) {
    detune = 1.000;
    noteMsg = note;

    if (velocity == 0) {
      notes[noteMsg] = false;
    } else {
      notes[noteMsg] = true;
    }

    switch (NP) {
      case 0:
        commandTopNote();
        break;

      case 1:
        commandBottomNote();
        break;

      case 2:
        if (notes[noteMsg]) {  // If note is on and using last note priority, add to ordered list
          orderIndx = (orderIndx + 1) % 40;
          noteOrder[orderIndx] = noteMsg;
        }
        commandLastNote();
        break;
    }
  }
}

void myNoteOff(byte channel, byte note, byte velocity) {

  if (MONO_POLY_1 < 511 && MONO_POLY_2 < 511) {  //POLYPHONIC mode
    detune = 1.000;
    switch (getVoiceNo(note)) {
      case 1:
        env1.noteOff();
        filterEnv1.noteOff();
        lfoAenv1.noteOff();
        env1on = false;
        voices[0].note = -1;
        voiceOn[0] = false;
        break;

      case 2:
        env2.noteOff();
        filterEnv2.noteOff();
        lfoAenv2.noteOff();
        env2on = false;
        voices[1].note = -1;
        voiceOn[1] = false;
        break;

      case 3:
        env3.noteOff();
        filterEnv3.noteOff();
        lfoAenv3.noteOff();
        env3on = false;
        voices[2].note = -1;
        voiceOn[2] = false;
        break;

      case 4:
        env4.noteOff();
        filterEnv4.noteOff();
        lfoAenv4.noteOff();
        env4on = false;
        voices[3].note = -1;
        voiceOn[3] = false;
        break;

      case 5:
        env5.noteOff();
        filterEnv5.noteOff();
        lfoAenv5.noteOff();
        env5on = false;
        voices[4].note = -1;
        voiceOn[4] = false;
        break;

      case 6:
        env6.noteOff();
        filterEnv6.noteOff();
        lfoAenv6.noteOff();
        env6on = false;
        voices[5].note = -1;
        voiceOn[5] = false;
        break;
    }
  }

  if (MONO_POLY_1 > 511 && MONO_POLY_2 < 511) {  //UNISON
    detune = olddetune;
    noteMsg = note;
    notes[noteMsg] = false;

    switch (NP) {
      case 0:
        commandTopNoteUnison();
        break;

      case 1:
        commandBottomNoteUnison();
        break;

      case 2:
        if (notes[noteMsg]) {  // If note is on and using last note priority, add to ordered list
          orderIndx = (orderIndx + 1) % 40;
          noteOrder[orderIndx] = noteMsg;
        }
        commandLastNoteUnison();
        break;
    }
  }

  if (MONO_POLY_1 < 511 && MONO_POLY_2 > 511) {
    detune = 1.000;
    noteMsg = note;
    notes[noteMsg] = false;

    switch (NP) {
      case 0:
        commandTopNote();
        break;

      case 1:
        commandBottomNote();
        break;

      case 2:
        if (notes[noteMsg]) {  // If note is on and using last note priority, add to ordered list
          orderIndx = (orderIndx + 1) % 40;
          noteOrder[orderIndx] = noteMsg;
        }
        commandLastNote();
        break;
    }
  }
}

int mod(int a, int b) {
  int r = a % b;
  return r < 0 ? r + b : r;
}

void commandTopNote() {
  int topNote = 0;
  bool noteActive = false;

  for (int i = 0; i < 88; i++) {
    if (notes[i]) {
      topNote = i;
      noteActive = true;
    }
  }

  if (noteActive)
    commandNote(topNote);
  else  // All notes are off, turn off gate

    env1.noteOff();
  filterEnv1.noteOff();
  lfoAenv1.noteOff();
  env1on = false;
}

void commandBottomNote() {

  int bottomNote = 0;
  bool noteActive = false;

  for (int i = 87; i >= 0; i--) {
    if (notes[i]) {
      bottomNote = i;
      noteActive = true;
    }
  }

  if (noteActive)
    commandNote(bottomNote);
  else  // All notes are off, turn off gate
    env1.noteOff();
  filterEnv1.noteOff();
  lfoAenv1.noteOff();
  env1on = false;
}

void commandLastNote() {

  int8_t noteIndx;

  for (int i = 0; i < 40; i++) {
    noteIndx = noteOrder[mod(orderIndx - i, 40)];
    if (notes[noteIndx]) {
      commandNote(noteIndx);
      return;
    }
  }
  env1.noteOff();
  filterEnv1.noteOff();
  lfoAenv1.noteOff();
  env1on = false;
}

void commandNote(int note) {

  note1freq = note;
  env1.noteOn();
  filterEnv1.noteOn();
  lfoAenv1.noteOn();
  env1on = true;
}

void commandTopNoteUnison() {
  int topNote = 0;
  bool noteActive = false;

  for (int i = 0; i < 88; i++) {
    if (notes[i]) {
      topNote = i;
      noteActive = true;
    }
  }

  if (noteActive)
    commandNoteUnison(topNote);
  else  // All notes are off, turn off gate

    env1.noteOff();
  filterEnv1.noteOff();
  lfoAenv1.noteOff();
  env1on = false;

  env2.noteOff();
  filterEnv2.noteOff();
  lfoAenv2.noteOff();
  env2on = false;
}

void commandBottomNoteUnison() {

  int bottomNote = 0;
  bool noteActive = false;

  for (int i = 87; i >= 0; i--) {
    if (notes[i]) {
      bottomNote = i;
      noteActive = true;
    }
  }

  if (noteActive)
    commandNoteUnison(bottomNote);
  else  // All notes are off, turn off gate
    env1.noteOff();
  filterEnv1.noteOff();
  lfoAenv1.noteOff();
  env1on = false;

  env2.noteOff();
  filterEnv2.noteOff();
  lfoAenv2.noteOff();
  env2on = false;
}

void commandLastNoteUnison() {

  int8_t noteIndx;

  for (int i = 0; i < 40; i++) {
    noteIndx = noteOrder[mod(orderIndx - i, 40)];
    if (notes[noteIndx]) {
      commandNoteUnison(noteIndx);
      return;
    }
  }
  env1.noteOff();
  filterEnv1.noteOff();
  lfoAenv1.noteOff();
  env1on = false;

  env2.noteOff();
  filterEnv2.noteOff();
  lfoAenv2.noteOff();
  env2on = false;
}

void commandNoteUnison(int note) {

  note1freq = note;
  env1.noteOn();
  filterEnv1.noteOn();
  lfoAenv1.noteOn();
  env1on = true;

  note2freq = note;
  env2.noteOn();
  filterEnv2.noteOn();
  lfoAenv2.noteOn();
  env2on = true;
}

void allNotesOff() {
}

FLASHMEM void updateVolume() {
  mainVol = (float)mux23 / 1024;
  //if (!announce) {
  showCurrentParameterPage("Volume", String(mux23 >> 3));
  startParameterDisplay();
  //}
  midiCCOut(CCvolumeControl, (mux23 >> 3), 1);
}

//main octave
FLASHMEM void updateMainOctave() {
  if (MAIN_OCT_1 > 511) {
    octave = 0.5;
    showCurrentParameterPage("Main Octave", "-1");
    startParameterDisplay();
    midiCCOut(CCoctave_1, 0, 1);
  } else if (MAIN_OCT_1 < 511 && MAIN_OCT_2 < 511) {
    octave = 1;
    showCurrentParameterPage("Main Octave", "0");
    startParameterDisplay();
    midiCCOut(CCoctave_1, 63, 1);
  } else if (MAIN_OCT_2 > 511) {
    octave = 2;
    showCurrentParameterPage("Main Octave", "+1");
    startParameterDisplay();
    midiCCOut(CCoctave_1, 127, 1);
  }
}

///////////////  OCTAVES OCTAVES /////////////////////////////////////////////////////////////7////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////


//octave vco B
FLASHMEM void updateOctaveB() {
  if (B_OCTAVE_1 > 511) {
    octaveB = 0.5;
    showCurrentParameterPage("OscB Octave", "-1");
    startParameterDisplay();
    midiCCOut(CCosc_b_oct_1, 0, 1);
  } else if (B_OCTAVE_1 < 511 && B_OCTAVE_2 < 511) {
    octaveB = 1;
    showCurrentParameterPage("OscB Octave", "0");
    startParameterDisplay();
    midiCCOut(CCosc_b_oct_1, 63, 1);
  } else if (B_OCTAVE_2 > 511) {
    octaveB = 2;
    showCurrentParameterPage("OscB Octave", "+1");
    startParameterDisplay();
    midiCCOut(CCosc_b_oct_1, 127, 1);
  }
}

FLASHMEM void updatemonoPoly() {

  if (MONO_POLY_1 < 511 && MONO_POLY_2 < 511) {
    showCurrentParameterPage("KeyMode", "Polyphonic");
    startParameterDisplay();
  }
  if (MONO_POLY_1 > 511 && MONO_POLY_2 < 511) {
    showCurrentParameterPage("KeyMode", "Unison");
    startParameterDisplay();
  }
  if (MONO_POLY_1 < 511 && MONO_POLY_2 > 511) {
    showCurrentParameterPage("KeyMode", "Mono");
    startParameterDisplay();
  }
}

FLASHMEM void updateOctaveC() {
  if (C_OCTAVE_1 > 511) {
    octaveC = 0.5;
    showCurrentParameterPage("OscC Octave", "-1");
    startParameterDisplay();
    midiCCOut(CCosc_c_oct_1, 0, 1);
  } else if (C_OCTAVE_1 < 511 && C_OCTAVE_2 < 511) {
    octaveC = 1;
    showCurrentParameterPage("OscC Octave", "0");
    startParameterDisplay();
    midiCCOut(CCosc_c_oct_1, 63, 1);
  } else if (C_OCTAVE_2 > 511) {
    octaveC = 2;
    showCurrentParameterPage("OscB Octave", "+1");
    startParameterDisplay();
    midiCCOut(CCosc_c_oct_1, 127, 1);
  }
}

//Shape A
FLASHMEM void updateShapeA() {
  if (A_SHAPE_1 > 511) {
    shapeA = 0;
    showCurrentParameterPage("OscA Wave", "Square");
    startParameterDisplay();
    midiCCOut(CCosc_a_shape_1, 0, 1);
  } else if (A_SHAPE_1 < 511 && A_SHAPE_2 < 511) {
    shapeA = 1;
    showCurrentParameterPage("OscA Wave", "Sawtooth");
    startParameterDisplay();
    midiCCOut(CCosc_a_shape_1, 63, 1);
  } else if (A_SHAPE_2 > 511) {
    shapeA = 2;
    showCurrentParameterPage("OscA Wave", "Triangle");
    startParameterDisplay();
    midiCCOut(CCosc_a_shape_1, 127, 1);
  }
}

//Shape B
FLASHMEM void updateShapeB() {
  if (B_SHAPE_1 > 511) {
    shapeB = 0;
    showCurrentParameterPage("OscB Wave", "Square");
    startParameterDisplay();
    midiCCOut(CCosc_b_shape_1, 0, 1);
  } else if (B_SHAPE_1 < 511 && B_SHAPE_2 < 511) {
    shapeB = 1;
    showCurrentParameterPage("OscB Wave", "Sawtooth");
    startParameterDisplay();
    midiCCOut(CCosc_b_shape_1, 63, 1);
  } else if (B_SHAPE_2 > 511) {
    shapeB = 2;
    showCurrentParameterPage("OscB Wave", "Triangle");
    startParameterDisplay();
    midiCCOut(CCosc_b_shape_1, 127, 1);
  }
}

//Vco C shape
FLASHMEM void updateShapeC() {
  shapeC = mux11;
  midiCCOut(CCosc_C_shape, (mux11 >> 3), 1);
  showCurrentParameterPage("OscC Wave", String(map(mux11, 0, 1023, 1, 28)));
  startParameterDisplay();
}

//tuneB
FLASHMEM void updateTuneB() {
  if (mux13 < 512) {
    tuneB = ((float)mux13 / 1023) + 0.5;
  } else {
    tuneB = ((float)mux13 / 510);
  }
  midiCCOut(CCosc_B_freq, (mux13 >> 3), 1);
}

//tuneC
FLASHMEM void updateTuneC() {
  if (mux12 < 512) {
    tuneC = ((float)mux12 / 1023) + 0.5;
  } else {
    tuneC = ((float)mux12 / 510);
  }
  midiCCOut(CCosc_C_freq, (mux12 >> 3), 1);
}

//Cross mod
FLASHMEM void updateCrossMod() {
  crossMod = (float)mux14 / 512;
  showCurrentParameterPage("Cross Mod", String(mux14 >> 3));
  startParameterDisplay();
  midiCCOut(CCcrossmod, (mux14 >> 3), 1);
}


FLASHMEM void updateVolA() {
  vcoAvol = (float)mux10 / 1023;
  showCurrentParameterPage("OscA Volume", String(mux10 >> 3));
  startParameterDisplay();
  midiCCOut(CCosc_A_vol, (mux10 >> 3), 1);
}

FLASHMEM void updateVolB() {
  vcoBvol = (float)mux9 / 1023;
  showCurrentParameterPage("OscB Volume", String(mux9 >> 3));
  startParameterDisplay();
  midiCCOut(CCosc_B_vol, (mux9 >> 3), 1);
}

FLASHMEM void updateVolC() {
  vcoCvol = (float)mux8 / 1023;
  showCurrentParameterPage("OscC Volume", String(mux8 >> 3));
  startParameterDisplay();
  midiCCOut(CCosc_C_vol, (mux8 >> 3), 1);
}

FLASHMEM void updateSubVol() {
  Subvol = (float)mux17 / 1023;
  showCurrentParameterPage("Sub Volume", String(mux17 >> 3));
  startParameterDisplay();
  midiCCOut(CCosc_Subvol, (mux17 >> 3), 1);
}

//Filter
FLASHMEM void updateCutoff() {
  cut = 15000 * (float)mux25 / 1023 + 15;  /////cut
  showCurrentParameterPage("Filter Cutoff", String(cutoffstr) + " Hz");
  startParameterDisplay();
  midiCCOut(CCvcf_frequency, (mux25 >> 3), 1);
}

FLASHMEM void updateRes() {
  res = 4.5 * (float)mux24 / 1023 + 1.1;
  showCurrentParameterPage("Filter Res", String(mux24 >> 3));
  startParameterDisplay();
  midiCCOut(CCvcf_resonance, (mux24 >> 3), 1);
}

//Filter Env

FLASHMEM void updateFilterAttack() {
  filtAtt = (3000 * (float)mux0 / 1023);
  showCurrentParameterPage("Filter Attack", String(filterAttackstr) + " mS");
  startParameterDisplay();
  midiCCOut(CCvcf_attack, (mux0 >> 3), 1);
}

FLASHMEM void updateFilterDecay() {
  filtDec = (3000 * (float)mux1 / 1023);
  showCurrentParameterPage("Filter Decay", String(filterDecaystr) + " mS");
  startParameterDisplay();
  midiCCOut(CCvcf_decay, (mux1 >> 3), 1);
}

FLASHMEM void updateFilterAmount() {
  filtAmt = (float)mux2 / 512 - 1;
  showCurrentParameterPage("Filter Env", String(mux2 >> 3));
  startParameterDisplay();
  midiCCOut(CCvcf_env_amount, (mux2 >> 3), 1);
}

FLASHMEM void updateFilterMode() {
  if (FILTER_MODE > 511) {
    filterMode = 1;
    //if (!announce) {
    showCurrentParameterPage("Filter Mode", "Low Pass");
    startParameterDisplay();
    //}
    midiCCOut(CCfiltermode, 127, 1);
  } else if (FILTER_MODE < 511) {
    filterMode = 0;
    //if (!announce) {
    showCurrentParameterPage("Filter Mode", "Band Pass");
    startParameterDisplay();
    //}
    midiCCOut(CCfiltermode, 0, 1);
  }
}

FLASHMEM void updateAttack() {
  envAtt = 3000 * (float)mux27 / 1023;
  showCurrentParameterPage("Amp Attack", String(ampAttackstr) + " mS");
  startParameterDisplay();
  midiCCOut(CCvca_attack, (mux27 >> 3), 1);
}

FLASHMEM void updateDecay() {
  envDec = 5000 * (float)mux26 / 1023;
  envRel = 5000 * (float)mux26 / 1023;
  showCurrentParameterPage("Amp Decay", String(ampDecaystr) + " mS");
  startParameterDisplay();
  midiCCOut(CCvca_decay, (mux26 >> 3), 1);
}

FLASHMEM void updateSustain() {
  envSus = (float)mux22 / 100;
  showCurrentParameterPage("Amp Sustain", String(map(mux22, 0, 1023, 0, 100)));
  startParameterDisplay();
  midiCCOut(CCvca_sustain, (mux22 >> 3), 1);
}

FLASHMEM void updateLFOAmount() {
  if (lfoAdest == 0 && lfoAshape != 2) {
    lfoAamp = ((float)mux3) / 1024 / 10;
  } else {
    lfoAamp = ((float)mux3) / 1024 / 3;
  }
  midiCCOut(CCmodulation, (mux3 >> 3), 1);
}

FLASHMEM void updateLFOFreq() {
  lfoAfreq = 20 * (float)mux4 / 1024 + 0.1;
  showCurrentParameterPage("LFO Rate", String(LFOFreqstr) + " Hz");
  startParameterDisplay();
  midiCCOut(CClfo_frequency, (mux4 >> 3), 1);
}

FLASHMEM void updateLFOAttack() {
  lfoAdel = 2000 * (float)mux5 / 1024;
  lfoAatt = 3000 * (float)mux5 / 1024;
  showCurrentParameterPage("LFO Attack", String(lfoAttackstr) + " mS");
  startParameterDisplay();
  midiCCOut(CClfo_attack, (mux5 >> 3), 1);
}

FLASHMEM void updateLFODecay() {
  lfoAdec = 4000 * (float)mux6 / 1024;
  lfoArel = 4000 * (float)mux6 / 1024;
  showCurrentParameterPage("LFO Decay", String(lfoDecaystr) + " mS");
  startParameterDisplay();
  midiCCOut(CClfo_decay, (mux6 >> 3), 1);
}

FLASHMEM void updateLFOSustain() {
  lfoAsus = (float)mux7 / 1024;
  showCurrentParameterPage("LFO Sustain", String(map(mux7, 0, 1023, 0, 100)));
  startParameterDisplay();
  midiCCOut(CClfo_sustain, (mux7 >> 3), 1);
}

FLASHMEM void updateLFODestination() {
  if (LFOA_DEST_1 > 511) {  //lfo - pitch
    lfoAdest = 0;
    showCurrentParameterPage("LFO Dest", "Pitch");
    startParameterDisplay();
    midiCCOut(CClfo_dest_1, 0, 1);
  } else if (LFOA_DEST_1 < 511 && LFOA_DEST_2 < 511) {  //lfo - filter
    lfoAdest = 1;
    showCurrentParameterPage("LFO Dest", "Filter");
    startParameterDisplay();
    midiCCOut(CClfo_dest_1, 63, 1);
  } else if (LFOA_DEST_2 > 511) {  //lfo - amp
    lfoAdest = 2;
    showCurrentParameterPage("LFO Dest", "Amplifier");
    startParameterDisplay();
    midiCCOut(CClfo_dest_1, 127, 1);
  }
}

//lfoA shape
FLASHMEM void updateLFOShape() {
  if (LFOA_SHAPE_1 > 511) {
    lfoAshape = 0;
    showCurrentParameterPage("LFO Wave", "Triangle");
    startParameterDisplay();
    midiCCOut(CClfo_wave_1, 0, 1);
  } else if (LFOA_SHAPE_1 < 511 && LFOA_SHAPE_2 < 511) {
    lfoAshape = 1;
    showCurrentParameterPage("LFO Wave", "Saw");
    startParameterDisplay();
    midiCCOut(CClfo_wave_1, 63, 1);
  } else if (LFOA_SHAPE_2 > 511) {
    lfoAshape = 2;
    showCurrentParameterPage("LFO Wave", "S & H");
    startParameterDisplay();
    midiCCOut(CClfo_wave_1, 127, 1);
  }
}

FLASHMEM void updatePWAmount() {
  lfoBamp = (float)mux15 / 1023;
  showCurrentParameterPage("PWM Depth", String(mux15 >> 3));
  startParameterDisplay();
  midiCCOut(CCoscpwm, (mux15 >> 3), 1);
}

FLASHMEM void updatePWFreq() {
  lfoBfreq = 5 * (float)mux16 / 1023 + 0.1;
  showCurrentParameterPage("PWM Rate", String(PWMFreqstr) + " Hz");
  startParameterDisplay();
  midiCCOut(CCoscpwmrate, (mux16 >> 3), 1);
}

//Delay
FLASHMEM void updateDelayMix() {
  dlyAmt = (float)mux21 / 1100 - 0.1;
  if (dlyAmt < 0) {
    dlyAmt = 0;
  }
  midiCCOut(CCdly_amt, (mux21 >> 3), 1);
}

FLASHMEM void updateDelayTime() {
  dlyTimeL = mux20 / 2.5;
  dlyTimeR = mux20 / 1.25;
  midiCCOut(CCdly_size, (mux20 >> 3), 1);
}

//Reverb
FLASHMEM void updateReverbMix() {
  revMix = ((float)mux18 / 1024 / 1.2);
  midiCCOut(CCrev_amt, (mux18 >> 3), 1);
}

FLASHMEM void updateReverbSize() {
  revSize = ((float)mux19 / 1024 - 0.01);
  midiCCOut(CCrev_size, (mux19 >> 3), 1);
}

void updatePatchname() {
  showPatchPage(String(patchNo), patchName);
}


void myControlChange(byte channel, byte control, byte value) {
  switch (control) {

    case CCmodwheel:
      switch (modWheelDepth) {
        case 0:
          midiMod = 0;
          break;

        case 1:
          midiMod = ((value << 3) / 5);
          break;

        case 2:
          midiMod = ((value << 3) / 4);
          break;

        case 3:
          midiMod = ((value << 3) / 3.5);
          break;

        case 4:
          midiMod = ((value << 3) / 3);
          break;

        case 5:
          midiMod = ((value << 3) / 2.5);
          break;

        case 6:
          midiMod = ((value << 3) / 2);
          break;

        case 7:
          midiMod = ((value << 3) / 1.75);
          break;

        case 8:
          midiMod = ((value << 3) / 1.5);
          break;

        case 9:
          midiMod = ((value << 3) / 1.25);
          break;

        case 10:
          midiMod = (value << 3);
          break;
      }
      oldmux3 = mux3;
      mux3 = mux3 + midiMod;
      if (mux3 > 1023) {
        mux3 = 1023;
      }
      updateLFOAmount();
      mux3 = oldmux3;
      break;

    case CCvolumeControl:
      mux23 = (value << 3);
      updateVolume();
      break;

    case CCvcf_frequency:
      mux25 = (value << 3);
      cutoffstr = FILTERFREQS[value];
      updateCutoff();
      break;

    case CCvcf_resonance:
      mux24 = (value << 3);
      updateRes();
      break;

    case CCvcf_attack:
      mux0 = (value << 3);
      filterAttackstr = ENVTIMES[value];
      updateFilterAttack();
      break;

    case CCvcf_decay:
      mux1 = (value << 3);
      filterDecaystr = ENVTIMES[value];
      updateFilterDecay();
      break;

    case CCvcf_env_amount:
      mux2 = (value << 3);
      updateFilterAmount();
      break;

    case CClfo_frequency:
      mux4 = (value << 3);
      LFOFreqstr = LFOTEMPO[value];
      updateLFOFreq();
      break;

    case CCmodulation:
      mux3 = (value << 3);
      updateLFOAmount();
      break;

    case CClfo_attack:
      mux5 = (value << 3);
      lfoAttackstr = ENVTIMES[value];
      updateLFOAttack();
      break;

    case CClfo_decay:
      mux6 = (value << 3);
      lfoDecaystr = ENVTIMES[value];
      updateLFODecay();
      break;

    case CClfo_sustain:
      mux7 = (value << 3);
      updateLFOSustain();
      break;

    case CCvca_attack:
      mux27 = (value << 3);
      ampAttackstr = ENVTIMES[value];
      updateAttack();
      break;

    case CCvca_decay:
      mux26 = (value << 3);
      ampDecaystr = ENVTIMES[value];
      updateDecay();
      break;

    case CCvca_sustain:
      mux22 = (value << 3);
      updateSustain();
      break;

    case CCoscpwmrate:
      mux16 = (value << 3);
      PWMFreqstr = LFOTEMPO[value];
      updatePWFreq();
      break;

    case CCoscpwm:
      mux15 = (value << 3);
      updatePWAmount();
      break;

    case CCosc_B_freq:
      mux13 = (value << 3);
      updateTuneB();
      break;

    case CCosc_C_freq:
      mux12 = (value << 3);
      updateTuneC();
      break;

    case CCosc_A_vol:
      mux10 = (value << 3);
      updateVolA();
      break;

    case CCosc_B_vol:
      mux9 = (value << 3);
      updateVolB();
      break;

    case CCosc_C_vol:
      mux8 = (value << 3);
      updateVolC();
      break;

    case CCosc_Subvol:
      mux17 = (value << 3);
      updateSubVol();
      break;

    case CCosc_C_shape:
      mux11 = (value << 3);
      updateShapeC();
      break;

    case CCcrossmod:
      mux14 = (value << 3);
      updateCrossMod();
      break;

    case CCrev_size:
      mux19 = (value << 3);
      updateReverbSize();
      break;

    case CCrev_amt:
      mux18 = (value << 3);
      updateReverbMix();
      break;

    case CCdly_size:
      mux21 = (value << 3);
      updateDelayTime();
      break;

    case CCdly_amt:
      mux20 = (value << 3);
      updateDelayMix();
  }
}

void myAfterTouch(byte channel, byte value) {
  switch (afterTouchDepth) {
    case 0:
      midiMod = 0;
      break;

    case 1:
      midiMod = ((value << 3) / 5);
      break;

    case 2:
      midiMod = ((value << 3) / 4);
      break;

    case 3:
      midiMod = ((value << 3) / 3.5);
      break;

    case 4:
      midiMod = ((value << 3) / 3);
      break;

    case 5:
      midiMod = ((value << 3) / 2.5);
      break;

    case 6:
      midiMod = ((value << 3) / 2);
      break;

    case 7:
      midiMod = ((value << 3) / 1.75);
      break;

    case 8:
      midiMod = ((value << 3) / 1.5);
      break;

    case 9:
      midiMod = ((value << 3) / 1.25);
      break;

    case 10:
      midiMod = (value << 3);
      break;
  }
  oldmux3 = mux3;
  mux3 = mux3 + midiMod;
  if (mux3 > 1023) {
    mux3 = 1023;
  }
  updateLFOAmount();
  mux3 = oldmux3;
}

void myPitchBend(byte channel, int pitch) {
  switch (pitchBendRange) {
    case 0:
      break;

    case 1:
      newpitchbend = (pitch + 8192);
      if (newpitchbend > 8192) {
        bend = map(newpitchbend, 8193, 16383, 1, 1.06);
      }
      if (newpitchbend < 8193) {
        bend = (map(newpitchbend, 0, 8192, 1.00, 1.06) - 0.06);
      }
      break;

    case 2:
      newpitchbend = (pitch + 8192);
      if (newpitchbend > 8192) {
        bend = map(newpitchbend, 8193, 16383, 1, 1.12);
      }
      if (newpitchbend < 8193) {
        bend = (map(newpitchbend, 0, 8192, 1.00, 1.11) - 0.11);
      }
      break;

    case 3:
      newpitchbend = (pitch + 8192);
      if (newpitchbend > 8192) {
        bend = map(newpitchbend, 8193, 16383, 1.00, 1.19);
      }
      if (newpitchbend < 8193) {
        bend = (map(newpitchbend, 0, 8192, 1.00, 1.16) - 0.16);
      }
      break;

    case 4:
      newpitchbend = (pitch + 8192);
      if (newpitchbend > 8192) {
        bend = map(newpitchbend, 8193, 16383, 1.00, 1.26);
      }
      if (newpitchbend < 8193) {
        bend = (map(newpitchbend, 0, 8192, 1.00, 1.21) - 0.21);
      }
      break;

    case 5:
      newpitchbend = (pitch + 8192);
      if (newpitchbend > 8192) {
        bend = map(newpitchbend, 8193, 16383, 1.00, 1.33);
      }
      if (newpitchbend < 8193) {
        bend = (map(newpitchbend, 0, 8192, 1.00, 1.25) - 0.25);
      }
      break;

    case 6:
      newpitchbend = (pitch + 8192);
      if (newpitchbend > 8192) {
        bend = map(newpitchbend, 8193, 16383, 1.00, 1.42);
      }
      if (newpitchbend < 8193) {
        bend = (map(newpitchbend, 0, 8192, 1.00, 1.29) - 0.29);
      }
      break;

    case 7:
      newpitchbend = (pitch + 8192);
      if (newpitchbend > 8192) {
        bend = map(newpitchbend, 8193, 16383, 1.00, 1.50);
      }
      if (newpitchbend < 8193) {
        bend = (map(newpitchbend, 0, 8192, 1.00, 1.33) - 0.33);
      }
      break;

    case 8:
      newpitchbend = (pitch + 8192);
      if (newpitchbend > 8192) {
        bend = map(newpitchbend, 8193, 16383, 1.00, 1.58);
      }
      if (newpitchbend < 8193) {
        bend = (map(newpitchbend, 0, 8192, 1.00, 1.37) - 0.37);
      }
      break;

    case 9:
      newpitchbend = (pitch + 8192);
      if (newpitchbend > 8192) {
        bend = map(newpitchbend, 8193, 16383, 1.00, 1.68);
      }
      if (newpitchbend < 8193) {
        bend = (map(newpitchbend, 0, 8192, 1.000, 1.405) - 0.405);
      }

      break;

    case 10:
      newpitchbend = (pitch + 8192);
      if (newpitchbend > 8192) {
        bend = map(newpitchbend, 8193, 16383, 1.00, 1.79);
      }
      if (newpitchbend < 8193) {
        bend = (map(newpitchbend, 0, 8192, 1.00, 1.44) - 0.44);
      }
      break;

    case 11:
      newpitchbend = (pitch + 8192);
      if (newpitchbend > 8192) {
        bend = map(newpitchbend, 8193, 16383, 1.00, 1.885);
      }
      if (newpitchbend < 8193) {
        bend = (map(newpitchbend, 0, 8192, 1.00, 1.47) - 0.47);
      }
      break;

    case 12:
      newpitchbend = (pitch + 8192);
      if (newpitchbend > 8192) {
        bend = map(newpitchbend, 8193, 16383, 1.00, 2.00);
      }
      if (newpitchbend < 8193) {
        bend = (map(newpitchbend, 0, 8192, 1.00, 1.50) - 0.50);
      }
      break;
  }
}

void myProgramChange(byte channel, byte program) {
  state = PATCH;
  patchNo = program + 1;
  recallPatch(patchNo);
  Serial.print("MIDI Pgm Change:");
  Serial.println(patchNo);
  state = PARAMETER;
}

void recallPatch(int patchNo) {
  allNotesOff();
  //announce = true;
  File patchFile = SD.open(String(patchNo).c_str());
  if (!patchFile) {
    Serial.println("File not found");
  } else {
    String data[NO_OF_PARAMS];  //Array of data read in
    recallPatchData(patchFile, data);
    setCurrentPatchData(data);
    patchFile.close();
  }
  //announce = false;
}

FLASHMEM void setCurrentPatchData(String data[]) {
  patchName = data[0];
  octave = data[1].toFloat();
  octaveB = data[2].toFloat();
  octaveC = data[3].toFloat();
  shapeA = data[4].toInt();
  shapeB = data[5].toInt();
  shapeC = data[6].toInt();
  tuneB = data[7].toFloat();
  tuneC = data[8].toFloat();
  crossMod = data[9].toFloat();
  vcoAvol = data[10].toFloat();
  vcoBvol = data[11].toFloat();
  vcoCvol = data[12].toFloat();
  Subvol = data[13].toFloat();
  cut = data[14].toInt();
  res = data[15].toFloat();
  filtAtt = data[16].toInt();
  filtDec = data[17].toInt();
  filtAmt = data[18].toFloat();
  filterMode = data[19].toInt();
  envAtt = data[20].toInt();
  envDec = data[21].toInt();
  envRel = data[22].toInt();
  envSus = data[23].toFloat();
  lfoAamp = data[24].toFloat();
  lfoAfreq = data[25].toFloat();
  lfoAdel = data[26].toInt();
  lfoAatt = data[27].toInt();
  lfoAdec = data[28].toInt();
  lfoArel = data[29].toInt();
  lfoAsus = data[30].toFloat();
  lfoBamp = data[31].toFloat();
  lfoBfreq = data[32].toFloat();
  dlyAmt = data[33].toFloat();
  dlyTimeL = data[34].toFloat();
  dlyTimeR = data[35].toFloat();
  revMix = data[36].toFloat();
  revSize = data[37].toFloat();
  lfoAdest = data[38].toFloat();
  lfoAshape = data[39].toFloat();
  modWheelDepth = data[40].toInt();
  pitchBendRange = data[41].toInt();
  afterTouchDepth = data[42].toInt();
  NP = data[43].toInt();
  unidetune = data[44].toInt();
  MONO_POLY_1 = data[45].toInt();
  MONO_POLY_2 = data[46].toInt();

  //Patchname
  updatePatchname();
  //updateMainOctave();


  Serial.print("Set Patch: ");
  Serial.println(patchName);
}

FLASHMEM String getCurrentPatchData() {
  return patchName + "," + String(octave) + "," + String(octaveB) + "," + String(octaveC) + "," + String(shapeA) + "," + String(shapeB) + "," + String(shapeC) + "," + String(tuneB) + "," + String(tuneC)
   + "," + String(crossMod) + "," + String(vcoAvol) + "," + String(vcoBvol) + "," + String(vcoCvol) + "," + String(Subvol) + "," + String(cut) + "," + String(res) + "," + String(filtAtt) + "," + String(filtDec)
    + "," + String(filtAmt) + "," + String(filterMode) + "," + String(envAtt) + "," + String(envDec) + "," + String(envRel) + "," + String(envSus) + "," + String(lfoAamp) + "," + String(lfoAfreq)
     + "," + String(lfoAdel) + "," + String(lfoAatt) + "," + String(lfoAdec) + "," + String(lfoArel) + "," + String(lfoAsus) + "," + String(lfoBamp) + "," + String(lfoBfreq) + "," + String(dlyAmt)
      + "," + String(dlyTimeL) + "," + String(dlyTimeR) + "," + String(revMix) + "," + String(revSize) + "," + String(lfoAdest) + "," + String(lfoAshape) + "," + String(modWheelDepth) + "," + String(pitchBendRange)
       + "," + String(afterTouchDepth) + "," + String(NP) + "," + String(unidetune) + "," + String(MONO_POLY_1) + "," + String(MONO_POLY_2);
}

FLASHMEM void checkMux() {

  mux1Read = adc->adc0->analogRead(muxPots1);
  mux2Read = adc->adc0->analogRead(muxPots2);
  mux3Read = adc->adc0->analogRead(muxPots3);
  mux4Read = adc->adc1->analogRead(muxPots4);
  mux5Read = adc->adc1->analogRead(muxPots5);
  mux6Read = adc->adc1->analogRead(muxPots6);

  if (mux1Read > (mux1ValuesPrev[muxInput] + QUANTISE_FACTOR) || mux1Read < (mux1ValuesPrev[muxInput] - QUANTISE_FACTOR)) {
    mux1ValuesPrev[muxInput] = mux1Read;

    switch (muxInput) {
      case 0:
        mux0 = mux1Read;
        filterAttackstr = ENVTIMES[mux0 / 8];
        updateFilterAttack();
        break;
      case 1:
        mux1 = mux1Read;
        filterDecaystr = ENVTIMES[mux0 / 8];
        updateFilterDecay();
        break;
      case 2:
        mux2 = mux1Read;
        updateFilterAmount();
        break;
      case 3:
        mux3 = mux1Read;
        updateLFOAmount();
        break;
      case 4:
        mux4 = mux1Read;
        LFOFreqstr = LFOTEMPO[mux4 / 8];
        updateLFOFreq();
        break;
      case 5:
        mux5 = mux1Read;
        lfoAttackstr = ENVTIMES[mux5 / 8];
        updateLFOAttack();
        break;
      case 6:
        mux6 = mux1Read;
        lfoDecaystr = ENVTIMES[mux6 / 8];
        updateLFODecay();
        break;
      case 7:
        mux7 = mux1Read;
        updateLFOSustain();
        break;
    }
  }

  if (mux2Read > (mux2ValuesPrev[muxInput] + QUANTISE_FACTOR) || mux2Read < (mux2ValuesPrev[muxInput] - QUANTISE_FACTOR)) {
    mux2ValuesPrev[muxInput] = mux2Read;

    switch (muxInput) {
      case 0:
        mux8 = mux2Read;
        updateVolC();
        break;
      case 1:
        mux9 = mux2Read;
        updateVolB();
        break;
      case 2:
        mux10 = mux2Read;
        updateVolA();
        break;
      case 3:
        mux11 = mux2Read;
        updateShapeC();
        break;
      case 4:
        mux12 = mux2Read;
        updateTuneC();
        break;
      case 5:
        mux13 = mux2Read;
        updateTuneB();
        break;
      case 6:
        mux14 = mux2Read;
        updateCrossMod();
        break;
      case 7:
        mux15 = mux2Read;
        updatePWAmount();
        break;
    }
  }

  if (mux3Read > (mux3ValuesPrev[muxInput] + QUANTISE_FACTOR) || mux3Read < (mux3ValuesPrev[muxInput] - QUANTISE_FACTOR)) {
    mux3ValuesPrev[muxInput] = mux3Read;

    switch (muxInput) {
      case 0:
        mux16 = mux3Read;
        PWMFreqstr = LFOTEMPO[mux16 / 8];
        updatePWFreq();
        break;
      case 1:
        mux17 = mux3Read;
        updateSubVol();
        break;
      case 2:
        mux18 = mux3Read;
        updateReverbMix();
        break;
      case 3:
        mux19 = mux3Read;
        updateReverbSize();
        break;
      case 4:
        mux20 = mux3Read;
        updateDelayTime();
        break;
      case 5:
        mux21 = mux3Read;
        updateDelayMix();
        break;
      case 6:
        mux22 = mux3Read;
        updateSustain();
        break;
      case 7:
        mux23 = mux3Read;
        updateVolume();
        break;
    }
  }

  if (mux4Read > (mux4ValuesPrev[muxInput] + QUANTISE_FACTOR) || mux4Read < (mux4ValuesPrev[muxInput] - QUANTISE_FACTOR)) {
    mux4ValuesPrev[muxInput] = mux4Read;

    switch (muxInput) {
      case 0:
        mux24 = mux4Read;
        updateRes();
        break;
      case 1:
        mux25 = mux4Read;
        cutoffstr = FILTERFREQS[mux25 / 8];
        updateCutoff();
        break;
      case 2:
        mux26 = mux4Read;
        ampDecaystr = ENVTIMES[mux26 / 8];
        updateDecay();
        break;
      case 3:
        mux27 = mux4Read;
        ampAttackstr = ENVTIMES[mux27 / 8];
        updateAttack();
        break;
      case 4:
        mux28 = mux4Read;
        break;
      case 5:
        mux29 = mux4Read;
        break;
      case 6:
        mux30 = mux4Read;
        break;
      case 7:
        FILTER_MODE = mux4Read;
        updateFilterMode();
        break;
    }
  }

  if (mux5Read > (mux5ValuesPrev[muxInput] + QUANTISE_FACTOR) || mux5Read < (mux5ValuesPrev[muxInput] - QUANTISE_FACTOR)) {
    mux5ValuesPrev[muxInput] = mux5Read;

    switch (muxInput) {
      case 0:
        MONO_POLY_1 = mux5Read;
        updatemonoPoly();
        break;
      case 1:
        A_SHAPE_1 = mux5Read;
        updateShapeA();
        break;
      case 2:
        A_SHAPE_2 = mux5Read;
        updateShapeA();
        break;
      case 3:
        B_SHAPE_1 = mux5Read;
        updateShapeB();
        break;
      case 4:
        B_SHAPE_2 = mux5Read;
        updateShapeB();
        break;
      case 5:
        MAIN_OCT_1 = mux5Read;
        updateMainOctave();
        break;
      case 6:
        MAIN_OCT_2 = mux5Read;
        updateMainOctave();
        break;
      case 7:
        B_OCTAVE_1 = mux5Read;
        updateOctaveB();
        break;
    }
  }

  if (mux6Read > (mux6ValuesPrev[muxInput] + QUANTISE_FACTOR) || mux6Read < (mux6ValuesPrev[muxInput] - QUANTISE_FACTOR)) {
    mux6ValuesPrev[muxInput] = mux6Read;

    switch (muxInput) {
      case 0:
        C_OCTAVE_1 = mux6Read;
        updateOctaveC();
        break;
      case 1:
        C_OCTAVE_2 = mux6Read;
        updateOctaveC();
        break;
      case 2:
        B_OCTAVE_2 = mux6Read;
        updateOctaveB();
        break;
      case 3:
        LFOA_SHAPE_1 = mux6Read;
        updateLFOShape();
        break;
      case 4:
        LFOA_SHAPE_2 = mux6Read;
        updateLFOShape();
        break;
      case 5:
        LFOA_DEST_1 = mux6Read;
        updateLFODestination();
        break;
      case 6:
        LFOA_DEST_2 = mux6Read;
        updateLFODestination();
        break;
      case 7:
        MONO_POLY_2 = mux6Read;
        updatemonoPoly();
        break;
    }
  }

  muxInput++;
  if (muxInput >= MUXCHANNELS)
    muxInput = 0;

  digitalWriteFast(MUX1, muxInput & B0001);
  digitalWriteFast(MUX2, muxInput & B0010);
  digitalWriteFast(MUX3, muxInput & B0100);
}

void midiCCOut(byte cc, byte value, byte ccChannel) {
  MIDI.sendControlChange(cc, value, ccChannel);     //MIDI DIN is set to Out
  usbMIDI.sendControlChange(cc, value, ccChannel);  //MIDI DIN is set to Out
}

void midiProgOut(byte chg, byte channel) {
  if (Program == true) {
    if (chg < 113) {
      MIDI.sendProgramChange(chg - 1, channel);     //MIDI DIN is set to Out
      usbMIDI.sendProgramChange(chg - 1, channel);  //MIDI DIN is set to Out
    }
  }
}

void showSettingsPage() {
  showSettingsPage(settings::current_setting(), settings::current_setting_value(), state);
}

void updateEEPromSettings() {

  if (oldunidetune != unidetune) {
    switch (unidetune) {
      case 0:
        detune = 1.000;
        break;
      case 1:
        detune = 1.002;
        break;
      case 2:
        detune = 1.004;
        break;
      case 3:
        detune = 1.006;
        break;
      case 4:
        detune = 1.008;
        break;
      case 5:
        detune = 1.010;
        break;
      case 6:
        detune = 1.012;
        break;
      case 7:
        detune = 1.014;
        break;
      case 8:
        detune = 1.016;
        break;
      case 9:
        detune = 1.018;
        break;
      case 10:
        detune = 1.020;
        break;
    }
    oldunidetune = unidetune;
    olddetune = detune;
  }
}

void checkSwitches() {

  saveButton.update();
  if (saveButton.held()) {
    switch (state) {
      case PARAMETER:
      case PATCH:
        state = DELETE;
        break;
    }
  } else if (saveButton.numClicks() == 1) {
    switch (state) {
      case PARAMETER:
        if (patches.size() < PATCHES_LIMIT) {
          resetPatchesOrdering();  //Reset order of patches from first patch
          patches.push({ patches.size() + 1, INITPATCHNAME });
          state = SAVE;
        }
        updateScreen();
        break;
      case SAVE:
        //Save as new patch with INITIALPATCH name or overwrite existing keeping name - bypassing patch renaming
        patchName = patches.last().patchName;
        state = PATCH;
        savePatch(String(patches.last().patchNo).c_str(), getCurrentPatchData());
        showPatchPage(patches.last().patchNo, patches.last().patchName);
        patchNo = patches.last().patchNo;
        loadPatches();  //Get rid of pushed patch if it wasn't saved
        setPatchesOrdering(patchNo);
        renamedPatch = "";
        state = PARAMETER;
        updateScreen();
        break;
      case PATCHNAMING:
        if (renamedPatch.length() > 0) patchName = renamedPatch;  //Prevent empty strings
        state = PATCH;
        savePatch(String(patches.last().patchNo).c_str(), getCurrentPatchData());
        showPatchPage(patches.last().patchNo, patchName);
        patchNo = patches.last().patchNo;
        loadPatches();  //Get rid of pushed patch if it wasn't saved
        setPatchesOrdering(patchNo);
        renamedPatch = "";
        state = PARAMETER;
        updateScreen();
        break;
    }
  }

  settingsButton.update();
  if (settingsButton.held()) {
    //If recall held, set current patch to match current hardware state
    //Reinitialise all hardware values to force them to be re-read if different
    state = REINITIALISE;
    reinitialiseToPanel();
    updateScreen();
  } else if (settingsButton.numClicks() == 1) {
    switch (state) {
      case PARAMETER:
        state = SETTINGS;
        showSettingsPage();
        updateScreen();
        break;
      case SETTINGS:
        showSettingsPage();
        updateScreen();
      case SETTINGSVALUE:
        settings::save_current_value();
        state = SETTINGS;
        showSettingsPage();
        updateScreen();
        break;
    }
  }

  backButton.update();
  if (backButton.held()) {
    //If Back button held, Panic - all notes off
    allNotesOff();
    updateScreen();  //Hack
  } else if (backButton.numClicks() == 1) {
    switch (state) {
      case RECALL:
        setPatchesOrdering(patchNo);
        state = PARAMETER;
        updateScreen();
        break;
      case SAVE:
        renamedPatch = "";
        state = PARAMETER;
        loadPatches();  //Remove patch that was to be saved
        setPatchesOrdering(patchNo);
        updateScreen();
        break;
      case PATCHNAMING:
        charIndex = 0;
        renamedPatch = "";
        state = SAVE;
        updateScreen();
        break;
      case DELETE:
        setPatchesOrdering(patchNo);
        state = PARAMETER;
        updateScreen();
        break;
      case SETTINGS:
        state = PARAMETER;
        updateScreen();
        break;
      case SETTINGSVALUE:
        state = SETTINGS;
        showSettingsPage();
        updateScreen();
        break;
    }
  }

  //Encoder switch
  recallButton.update();
  if (recallButton.held()) {
    //If Recall button held, return to current patch setting
    //which clears any changes made
    state = PATCH;
    //Recall the current patch
    patchNo = patches.first().patchNo;
    recallPatch(patchNo);
    state = PARAMETER;
    updateScreen();
  } else if (recallButton.numClicks() == 1) {
    switch (state) {
      case PARAMETER:
        state = RECALL;  //show patch list
        updateScreen();
        break;
      case RECALL:
        state = PATCH;
        //Recall the current patch
        patchNo = patches.first().patchNo;
        recallPatch(patchNo);
        state = PARAMETER;
        updateScreen();
        break;
      case SAVE:
        showRenamingPage(patches.last().patchName);
        patchName = patches.last().patchName;
        state = PATCHNAMING;
        updateScreen();
        break;
      case PATCHNAMING:
        if (renamedPatch.length() < 13) {
          renamedPatch.concat(String(currentCharacter));
          charIndex = 0;
          currentCharacter = CHARACTERS[charIndex];
          showRenamingPage(renamedPatch);
        }
        updateScreen();
        break;
      case DELETE:
        //Don't delete final patch
        if (patches.size() > 1) {
          state = DELETEMSG;
          patchNo = patches.first().patchNo;     //PatchNo to delete from SD card
          patches.shift();                       //Remove patch from circular buffer
          deletePatch(String(patchNo).c_str());  //Delete from SD card
          loadPatches();                         //Repopulate circular buffer to start from lowest Patch No
          renumberPatchesOnSD();
          loadPatches();                      //Repopulate circular buffer again after delete
          patchNo = patches.first().patchNo;  //Go back to 1
          recallPatch(patchNo);               //Load first patch
        }
        state = PARAMETER;
        updateScreen();
        break;
      case SETTINGS:
        state = SETTINGSVALUE;
        showSettingsPage();
        updateScreen();
        break;
      case SETTINGSVALUE:
        settings::save_current_value();
        state = SETTINGS;
        showSettingsPage();
        updateScreen();
        break;
    }
  }
}

void reinitialiseToPanel() {
  //This sets the current patch to be the same as the current hardware panel state - all the pots
  //The four button controls stay the same state
  //This reinialises the previous hardware values to force a re-read
  muxInput = 0;
  for (int i = 0; i < MUXCHANNELS; i++) {
    mux1ValuesPrev[i] = RE_READ;
    mux2ValuesPrev[i] = RE_READ;
    mux3ValuesPrev[i] = RE_READ;
    mux4ValuesPrev[i] = RE_READ;
    mux5ValuesPrev[i] = RE_READ;
    mux6ValuesPrev[i] = RE_READ;
  }
  patchName = INITPATCHNAME;
  showPatchPage("Initial", "Panel Settings");
}

void checkEncoder() {
  //Encoder works with relative inc and dec values
  //Detent encoder goes up in 4 steps, hence +/-3

  long encRead = encoder.read();
  if ((encCW && encRead > encPrevious + 3) || (!encCW && encRead < encPrevious - 3)) {
    switch (state) {
      case PARAMETER:
        state = PATCH;
        patches.push(patches.shift());
        patchNo = patches.first().patchNo;
        recallPatch(patchNo);
        state = PARAMETER;
        updateScreen();
        break;
      case RECALL:
        patches.push(patches.shift());
        updateScreen();
        break;
      case SAVE:
        patches.push(patches.shift());
        updateScreen();
        break;
      case PATCHNAMING:
        if (charIndex == TOTALCHARS) charIndex = 0;  //Wrap around
        currentCharacter = CHARACTERS[charIndex++];
        showRenamingPage(renamedPatch + currentCharacter);
        updateScreen();
        break;
      case DELETE:
        patches.push(patches.shift());
        updateScreen();
        break;
      case SETTINGS:
        settings::increment_setting();
        showSettingsPage();
        updateScreen();
        break;
      case SETTINGSVALUE:
        settings::increment_setting_value();
        showSettingsPage();
        updateScreen();
        break;
    }
    encPrevious = encRead;
  } else if ((encCW && encRead < encPrevious - 3) || (!encCW && encRead > encPrevious + 3)) {
    switch (state) {
      case PARAMETER:
        state = PATCH;
        patches.unshift(patches.pop());
        patchNo = patches.first().patchNo;
        recallPatch(patchNo);
        state = PARAMETER;
        updateScreen();
        break;
      case RECALL:
        patches.unshift(patches.pop());
        updateScreen();
        break;
      case SAVE:
        //if (patchNo < 57 ) patchNo = 57;
        patches.unshift(patches.pop());
        updateScreen();
        break;
      case PATCHNAMING:
        if (charIndex == -1)
          charIndex = TOTALCHARS - 1;
        currentCharacter = CHARACTERS[charIndex--];
        showRenamingPage(renamedPatch + currentCharacter);
        updateScreen();
        break;
      case DELETE:
        patches.unshift(patches.pop());
        updateScreen();
        break;
      case SETTINGS:
        settings::decrement_setting();
        showSettingsPage();
        updateScreen();
        break;
      case SETTINGSVALUE:
        settings::decrement_setting_value();
        showSettingsPage();
        updateScreen();
        break;
    }
    encPrevious = encRead;
  }
}

void loop() {
  checkMux();
  checkSwitches();
  updateEEPromSettings();
  checkEncoder();
  MIDI.read(midiChannel);
  usbMIDI.read(midiChannel);

  if (waitingToUpdate && (millis() - lastDisplayTriggerTime >= displayTimeout)) {
    updateScreen();  // retrigger
    waitingToUpdate = false;
  }

  //cross mod
  modMix1.gain(0, crossMod);
  modMix2.gain(0, crossMod);
  modMix3.gain(0, crossMod);
  modMix4.gain(0, crossMod);
  modMix5.gain(0, crossMod);
  modMix6.gain(0, crossMod);

  //voice 1 frequencies
  vcoA1.frequency(noteFreqs[note1freq] * octave * bend);
  vcoB1.frequency(noteFreqs[note1freq] * octave * octaveB * tuneB * bend);
  vcoC1.frequency(noteFreqs[note1freq] * octave * octaveC * tuneC * bend);
  sub1.frequency(noteFreqs[note1freq] / 2 * octave * bend);

  vcoA2.frequency(noteFreqs[note2freq] * octave * bend * detune);
  vcoB2.frequency(noteFreqs[note2freq] * octave * octaveB * tuneB * bend * detune);
  vcoC2.frequency(noteFreqs[note2freq] * octave * octaveC * tuneC * bend * detune);
  sub2.frequency(noteFreqs[note2freq] / 2 * octave * bend * detune);

  vcoA3.frequency(noteFreqs[note3freq] * octave * bend);
  vcoB3.frequency(noteFreqs[note3freq] * octave * octaveB * tuneB * bend);
  vcoC3.frequency(noteFreqs[note3freq] * octave * octaveC * tuneC * bend);
  sub3.frequency(noteFreqs[note3freq] / 2 * octave * bend);

  vcoA4.frequency(noteFreqs[note4freq] * octave * bend);
  vcoB4.frequency(noteFreqs[note4freq] * octave * octaveB * tuneB * bend);
  vcoC4.frequency(noteFreqs[note4freq] * octave * octaveC * tuneC * bend);
  sub4.frequency(noteFreqs[note4freq] / 2 * octave * bend);

  vcoA5.frequency(noteFreqs[note5freq] * octave * bend);
  vcoB5.frequency(noteFreqs[note5freq] * octave * octaveB * tuneB * bend);
  vcoC5.frequency(noteFreqs[note5freq] * octave * octaveC * tuneC * bend);
  sub5.frequency(noteFreqs[note5freq] / 2 * octave * bend);

  vcoA6.frequency(noteFreqs[note6freq] * octave * bend);
  vcoB6.frequency(noteFreqs[note6freq] * octave * octaveB * tuneB * bend);
  vcoC6.frequency(noteFreqs[note6freq] * octave * octaveC * tuneC * bend);
  sub6.frequency(noteFreqs[note6freq] / 2 * octave * bend);



  //vco Mixer
  voiceMix1.gain(0, vcoAvol * mainVol);
  voiceMix1.gain(1, vcoBvol * mainVol);
  voiceMix1.gain(2, vcoCvol * mainVol);
  voiceMix1.gain(3, Subvol * mainVol);

  voiceMix2.gain(0, vcoAvol * mainVol);
  voiceMix2.gain(1, vcoBvol * mainVol);
  voiceMix2.gain(2, vcoCvol * mainVol);
  voiceMix2.gain(3, Subvol * mainVol);

  voiceMix3.gain(0, vcoAvol * mainVol);
  voiceMix3.gain(1, vcoBvol * mainVol);
  voiceMix3.gain(2, vcoCvol * mainVol);
  voiceMix3.gain(3, Subvol * mainVol);

  voiceMix4.gain(0, vcoAvol * mainVol);
  voiceMix4.gain(1, vcoBvol * mainVol);
  voiceMix4.gain(2, vcoCvol * mainVol);
  voiceMix4.gain(3, Subvol * mainVol);

  voiceMix5.gain(0, vcoAvol * mainVol);
  voiceMix5.gain(1, vcoBvol * mainVol);
  voiceMix5.gain(2, vcoCvol * mainVol);
  voiceMix5.gain(3, Subvol * mainVol);

  voiceMix6.gain(0, vcoAvol * mainVol);
  voiceMix6.gain(1, vcoBvol * mainVol);
  voiceMix6.gain(2, vcoCvol * mainVol);
  voiceMix6.gain(3, Subvol * mainVol);

  //vco A shape
  if (shapeA == 0) {
    vcoA1.begin(WAVEFORM_PULSE);
    vcoA2.begin(WAVEFORM_PULSE);
    vcoA3.begin(WAVEFORM_PULSE);
    vcoA4.begin(WAVEFORM_PULSE);
    vcoA5.begin(WAVEFORM_PULSE);
    vcoA6.begin(WAVEFORM_PULSE);
  } else if (shapeA == 1) {
    vcoA1.begin(WAVEFORM_SAWTOOTH);
    vcoA1.amplitude(vcoVol);
    vcoA2.begin(WAVEFORM_SAWTOOTH);
    vcoA2.amplitude(vcoVol);
    vcoA3.begin(WAVEFORM_SAWTOOTH);
    vcoA3.amplitude(vcoVol);
    vcoA4.begin(WAVEFORM_SAWTOOTH);
    vcoA4.amplitude(vcoVol);
    vcoA5.begin(WAVEFORM_SAWTOOTH);
    vcoA5.amplitude(vcoVol);
    vcoA6.begin(WAVEFORM_SAWTOOTH);
    vcoA6.amplitude(vcoVol);
  } else if (shapeA == 2) {
    vcoA1.begin(WAVEFORM_TRIANGLE_VARIABLE);
    vcoA1.amplitude(vcoVol * 1.5);
    vcoA2.begin(WAVEFORM_TRIANGLE_VARIABLE);
    vcoA2.amplitude(vcoVol * 1.5);
    vcoA3.begin(WAVEFORM_TRIANGLE_VARIABLE);
    vcoA3.amplitude(vcoVol * 1.5);
    vcoA4.begin(WAVEFORM_TRIANGLE_VARIABLE);
    vcoA4.amplitude(vcoVol * 1.5);
    vcoA5.begin(WAVEFORM_TRIANGLE_VARIABLE);
    vcoA5.amplitude(vcoVol * 1.5);
    vcoA6.begin(WAVEFORM_TRIANGLE_VARIABLE);
    vcoA6.amplitude(vcoVol * 1.5);
  }

  //vco B shape
  if (shapeB == 0) {
    vcoB1.begin(WAVEFORM_PULSE);
    vcoB2.begin(WAVEFORM_PULSE);
    vcoB3.begin(WAVEFORM_PULSE);
    vcoB4.begin(WAVEFORM_PULSE);
    vcoB5.begin(WAVEFORM_PULSE);
    vcoB6.begin(WAVEFORM_PULSE);
  } else if (shapeB == 1) {
    vcoB1.begin(WAVEFORM_SAWTOOTH);
    vcoB1.amplitude(vcoVol);
    vcoB2.begin(WAVEFORM_SAWTOOTH);
    vcoB2.amplitude(vcoVol);
    vcoB3.begin(WAVEFORM_SAWTOOTH);
    vcoB3.amplitude(vcoVol);
    vcoB4.begin(WAVEFORM_SAWTOOTH);
    vcoB4.amplitude(vcoVol);
    vcoB5.begin(WAVEFORM_SAWTOOTH);
    vcoB5.amplitude(vcoVol);
    vcoB6.begin(WAVEFORM_SAWTOOTH);
    vcoB6.amplitude(vcoVol);
  } else if (shapeB == 2) {
    vcoB1.begin(WAVEFORM_TRIANGLE_VARIABLE);
    vcoB1.amplitude(vcoVol * 1.5);
    vcoB2.begin(WAVEFORM_TRIANGLE_VARIABLE);
    vcoB2.amplitude(vcoVol * 1.5);
    vcoB3.begin(WAVEFORM_TRIANGLE_VARIABLE);
    vcoB3.amplitude(vcoVol * 1.5);
    vcoB4.begin(WAVEFORM_TRIANGLE_VARIABLE);
    vcoB4.amplitude(vcoVol * 1.5);
    vcoB5.begin(WAVEFORM_TRIANGLE_VARIABLE);
    vcoB5.amplitude(vcoVol * 1.5);
    vcoB6.begin(WAVEFORM_TRIANGLE_VARIABLE);
    vcoB6.amplitude(vcoVol * 1.5);
  }

  //Vco C shapes
  switch (shapeC) {
    case 1 ... 32:
      vcoC1.arbitraryWaveform(wave1, 2000);
      vcoC2.arbitraryWaveform(wave1, 2000);
      vcoC3.arbitraryWaveform(wave1, 2000);
      vcoC4.arbitraryWaveform(wave1, 2000);
      vcoC5.arbitraryWaveform(wave1, 2000);
      vcoC6.arbitraryWaveform(wave1, 2000);
      break;
    case 37 ... 69:
      vcoC1.arbitraryWaveform(wave2, 2000);
      vcoC2.arbitraryWaveform(wave2, 2000);
      vcoC3.arbitraryWaveform(wave2, 2000);
      vcoC4.arbitraryWaveform(wave2, 2000);
      vcoC5.arbitraryWaveform(wave2, 2000);
      vcoC6.arbitraryWaveform(wave2, 2000);
      break;
    case 73 ... 105:
      vcoC1.arbitraryWaveform(wave3, 2000);
      vcoC2.arbitraryWaveform(wave3, 2000);
      vcoC3.arbitraryWaveform(wave3, 2000);
      vcoC4.arbitraryWaveform(wave3, 2000);
      vcoC5.arbitraryWaveform(wave3, 2000);
      vcoC6.arbitraryWaveform(wave3, 2000);
      break;
    case 109 ... 141:
      vcoC1.arbitraryWaveform(wave4, 2000);
      vcoC2.arbitraryWaveform(wave4, 2000);
      vcoC3.arbitraryWaveform(wave4, 2000);
      vcoC4.arbitraryWaveform(wave4, 2000);
      vcoC5.arbitraryWaveform(wave4, 2000);
      vcoC6.arbitraryWaveform(wave4, 2000);
      break;
    case 145 ... 177:
      vcoC1.arbitraryWaveform(wave5, 2000);
      vcoC2.arbitraryWaveform(wave5, 2000);
      vcoC3.arbitraryWaveform(wave5, 2000);
      vcoC4.arbitraryWaveform(wave5, 2000);
      vcoC5.arbitraryWaveform(wave5, 2000);
      vcoC6.arbitraryWaveform(wave5, 2000);
      break;
    case 181 ... 212:
      vcoC1.arbitraryWaveform(wave6, 2000);
      vcoC2.arbitraryWaveform(wave6, 2000);
      vcoC3.arbitraryWaveform(wave6, 2000);
      vcoC4.arbitraryWaveform(wave6, 2000);
      vcoC5.arbitraryWaveform(wave6, 2000);
      vcoC6.arbitraryWaveform(wave6, 2000);
      break;
    case 217 ... 248:
      vcoC1.arbitraryWaveform(wave7, 2000);
      vcoC2.arbitraryWaveform(wave7, 2000);
      vcoC3.arbitraryWaveform(wave7, 2000);
      vcoC4.arbitraryWaveform(wave7, 2000);
      vcoC5.arbitraryWaveform(wave7, 2000);
      vcoC6.arbitraryWaveform(wave7, 2000);
      break;
    case 253 ... 285:
      vcoC1.arbitraryWaveform(wave8, 2000);
      vcoC2.arbitraryWaveform(wave8, 2000);
      vcoC3.arbitraryWaveform(wave8, 2000);
      vcoC4.arbitraryWaveform(wave8, 2000);
      vcoC5.arbitraryWaveform(wave8, 2000);
      vcoC6.arbitraryWaveform(wave8, 2000);
      break;
    case 289 ... 320:
      vcoC1.arbitraryWaveform(wave9, 2000);
      vcoC2.arbitraryWaveform(wave9, 2000);
      vcoC3.arbitraryWaveform(wave9, 2000);
      vcoC4.arbitraryWaveform(wave9, 2000);
      vcoC5.arbitraryWaveform(wave9, 2000);
      vcoC6.arbitraryWaveform(wave9, 2000);
      break;
    case 325 ... 357:
      vcoC1.arbitraryWaveform(wave10, 2000);
      vcoC2.arbitraryWaveform(wave10, 2000);
      vcoC3.arbitraryWaveform(wave10, 2000);
      vcoC4.arbitraryWaveform(wave10, 2000);
      vcoC5.arbitraryWaveform(wave10, 2000);
      vcoC6.arbitraryWaveform(wave10, 2000);
      break;
    case 361 ... 393:
      vcoC1.arbitraryWaveform(wave11, 2000);
      vcoC2.arbitraryWaveform(wave11, 2000);
      vcoC3.arbitraryWaveform(wave11, 2000);
      vcoC4.arbitraryWaveform(wave11, 2000);
      vcoC5.arbitraryWaveform(wave11, 2000);
      vcoC6.arbitraryWaveform(wave11, 2000);
      break;
    case 397 ... 429:
      vcoC1.arbitraryWaveform(wave12, 2000);
      vcoC2.arbitraryWaveform(wave12, 2000);
      vcoC3.arbitraryWaveform(wave12, 2000);
      vcoC4.arbitraryWaveform(wave12, 2000);
      vcoC5.arbitraryWaveform(wave12, 2000);
      vcoC6.arbitraryWaveform(wave12, 2000);
      break;
    case 433 ... 465:
      vcoC1.arbitraryWaveform(wave13, 2000);
      vcoC2.arbitraryWaveform(wave13, 2000);
      vcoC3.arbitraryWaveform(wave13, 2000);
      vcoC4.arbitraryWaveform(wave13, 2000);
      vcoC5.arbitraryWaveform(wave13, 2000);
      vcoC6.arbitraryWaveform(wave13, 2000);
      break;
    case 469 ... 500:
      vcoC1.arbitraryWaveform(wave14, 2000);
      vcoC2.arbitraryWaveform(wave14, 2000);
      vcoC3.arbitraryWaveform(wave14, 2000);
      vcoC4.arbitraryWaveform(wave14, 2000);
      vcoC5.arbitraryWaveform(wave14, 2000);
      vcoC6.arbitraryWaveform(wave14, 2000);
      break;
    case 505 ... 537:
      vcoC1.arbitraryWaveform(wave15, 2000);
      vcoC2.arbitraryWaveform(wave15, 2000);
      vcoC3.arbitraryWaveform(wave15, 2000);
      vcoC4.arbitraryWaveform(wave15, 2000);
      vcoC5.arbitraryWaveform(wave15, 2000);
      vcoC6.arbitraryWaveform(wave15, 2000);
      break;
    case 541 ... 573:
      vcoC1.arbitraryWaveform(wave16, 2000);
      vcoC2.arbitraryWaveform(wave16, 2000);
      vcoC3.arbitraryWaveform(wave16, 2000);
      vcoC4.arbitraryWaveform(wave16, 2000);
      vcoC5.arbitraryWaveform(wave16, 2000);
      vcoC6.arbitraryWaveform(wave16, 2000);
      break;
    case 577 ... 609:
      vcoC1.arbitraryWaveform(wave17, 2000);
      vcoC2.arbitraryWaveform(wave17, 2000);
      vcoC3.arbitraryWaveform(wave17, 2000);
      vcoC4.arbitraryWaveform(wave17, 2000);
      vcoC5.arbitraryWaveform(wave17, 2000);
      vcoC6.arbitraryWaveform(wave17, 2000);
      break;
    case 613 ... 645:
      vcoC1.arbitraryWaveform(wave18, 2000);
      vcoC2.arbitraryWaveform(wave18, 2000);
      vcoC3.arbitraryWaveform(wave18, 2000);
      vcoC4.arbitraryWaveform(wave18, 2000);
      vcoC5.arbitraryWaveform(wave18, 2000);
      vcoC6.arbitraryWaveform(wave18, 2000);
      break;
    case 649 ... 680:
      vcoC1.arbitraryWaveform(wave19, 2000);
      vcoC2.arbitraryWaveform(wave19, 2000);
      vcoC3.arbitraryWaveform(wave19, 2000);
      vcoC4.arbitraryWaveform(wave19, 2000);
      vcoC5.arbitraryWaveform(wave19, 2000);
      vcoC6.arbitraryWaveform(wave19, 2000);
      break;
    case 685 ... 717:
      vcoC1.arbitraryWaveform(wave20, 2000);
      vcoC2.arbitraryWaveform(wave20, 2000);
      vcoC3.arbitraryWaveform(wave20, 2000);
      vcoC4.arbitraryWaveform(wave20, 2000);
      vcoC5.arbitraryWaveform(wave20, 2000);
      vcoC6.arbitraryWaveform(wave20, 2000);
      break;
    case 721 ... 752:
      vcoC1.arbitraryWaveform(wave21, 2000);
      vcoC2.arbitraryWaveform(wave21, 2000);
      vcoC3.arbitraryWaveform(wave21, 2000);
      vcoC4.arbitraryWaveform(wave21, 2000);
      vcoC5.arbitraryWaveform(wave21, 2000);
      vcoC6.arbitraryWaveform(wave21, 2000);
      break;
    case 757 ... 789:
      vcoC1.arbitraryWaveform(wave22, 2000);
      vcoC2.arbitraryWaveform(wave22, 2000);
      vcoC3.arbitraryWaveform(wave22, 2000);
      vcoC4.arbitraryWaveform(wave22, 2000);
      vcoC5.arbitraryWaveform(wave22, 2000);
      vcoC6.arbitraryWaveform(wave22, 2000);
      break;
    case 793 ... 825:
      vcoC1.arbitraryWaveform(wave23, 2000);
      vcoC2.arbitraryWaveform(wave23, 2000);
      vcoC3.arbitraryWaveform(wave23, 2000);
      vcoC4.arbitraryWaveform(wave23, 2000);
      vcoC5.arbitraryWaveform(wave23, 2000);
      vcoC6.arbitraryWaveform(wave23, 2000);
      break;
    case 829 ... 860:
      vcoC1.arbitraryWaveform(wave24, 2000);
      vcoC2.arbitraryWaveform(wave24, 2000);
      vcoC3.arbitraryWaveform(wave24, 2000);
      vcoC4.arbitraryWaveform(wave24, 2000);
      vcoC5.arbitraryWaveform(wave24, 2000);
      vcoC6.arbitraryWaveform(wave24, 2000);
      break;
    case 865 ... 896:
      vcoC1.arbitraryWaveform(wave25, 2000);
      vcoC2.arbitraryWaveform(wave25, 2000);
      vcoC3.arbitraryWaveform(wave25, 2000);
      vcoC4.arbitraryWaveform(wave25, 2000);
      vcoC5.arbitraryWaveform(wave25, 2000);
      vcoC6.arbitraryWaveform(wave25, 2000);
      break;
    case 901 ... 933:
      vcoC1.arbitraryWaveform(wave26, 2000);
      vcoC2.arbitraryWaveform(wave26, 2000);
      vcoC3.arbitraryWaveform(wave26, 2000);
      vcoC4.arbitraryWaveform(wave26, 2000);
      vcoC5.arbitraryWaveform(wave26, 2000);
      vcoC6.arbitraryWaveform(wave26, 2000);
      break;
    case 937 ... 966:
      vcoC1.arbitraryWaveform(wave27, 2000);
      vcoC2.arbitraryWaveform(wave27, 2000);
      vcoC3.arbitraryWaveform(wave27, 2000);
      vcoC4.arbitraryWaveform(wave27, 2000);
      vcoC5.arbitraryWaveform(wave27, 2000);
      vcoC6.arbitraryWaveform(wave27, 2000);
      break;
    case 970 ... 1024:
      vcoC1.arbitraryWaveform(wave28, 2000);
      vcoC2.arbitraryWaveform(wave28, 2000);
      vcoC3.arbitraryWaveform(wave28, 2000);
      vcoC4.arbitraryWaveform(wave28, 2000);
      vcoC5.arbitraryWaveform(wave28, 2000);
      vcoC6.arbitraryWaveform(wave28, 2000);
      break;
  }

  // d_filter_lfos starts here

  //filter
  filter1.frequency(cut);
  filter1.resonance(res);

  filter2.frequency(cut);
  filter2.resonance(res);

  filter3.frequency(cut);
  filter3.resonance(res);

  filter4.frequency(cut);
  filter4.resonance(res);

  filter5.frequency(cut);
  filter5.resonance(res);

  filter6.frequency(cut);
  filter6.resonance(res);


  //filter env
  filterEnv1.attack(filtAtt);
  filterEnv1.decay(filtDec);
  filterEnv1.release(filtDec);
  dc1.amplitude(filtAmt);

  filterEnv2.attack(filtAtt);
  filterEnv2.decay(filtDec);
  filterEnv2.release(filtDec);
  dc2.amplitude(filtAmt);

  filterEnv3.attack(filtAtt);
  filterEnv3.decay(filtDec);
  filterEnv3.release(filtDec);
  dc3.amplitude(filtAmt);

  filterEnv4.attack(filtAtt);
  filterEnv4.decay(filtDec);
  filterEnv4.release(filtDec);
  dc4.amplitude(filtAmt);

  filterEnv5.attack(filtAtt);
  filterEnv5.decay(filtDec);
  filterEnv5.release(filtDec);
  dc5.amplitude(filtAmt);

  filterEnv6.attack(filtAtt);
  filterEnv6.decay(filtDec);
  filterEnv6.release(filtDec);
  dc6.amplitude(filtAmt);


  //filter mode
  if (filterMode == 1) {
    filterMode1.gain(0, 1);
    filterMode1.gain(1, 0);
    filterMode2.gain(0, 1);
    filterMode2.gain(1, 0);
    filterMode3.gain(0, 1);
    filterMode3.gain(1, 0);
    filterMode4.gain(0, 1);
    filterMode4.gain(1, 0);
    filterMode5.gain(0, 1);
    filterMode5.gain(1, 0);
    filterMode6.gain(0, 1);
    filterMode6.gain(1, 0);
  } else if (filterMode == 0) {
    filterMode1.gain(0, 0);
    filterMode1.gain(1, 1);
    filterMode2.gain(0, 0);
    filterMode2.gain(1, 1);
    filterMode3.gain(0, 0);
    filterMode3.gain(1, 1);
    filterMode4.gain(0, 0);
    filterMode4.gain(1, 1);
    filterMode5.gain(0, 0);
    filterMode5.gain(1, 1);
    filterMode6.gain(0, 0);
    filterMode6.gain(1, 1);
  }

  //lfo A params
  lfoA1.amplitude(lfoAamp);
  lfoA1.frequency(lfoAfreq);
  lfoAenv1.delay(lfoAdel);
  lfoAenv1.attack(lfoAatt);
  lfoAenv1.decay(lfoAdec);
  lfoAenv1.release(lfoArel);
  lfoAenv1.sustain(lfoAsus);

  lfoA2.amplitude(lfoAamp);
  lfoA2.frequency(lfoAfreq);
  lfoAenv2.delay(lfoAdel);
  lfoAenv2.attack(lfoAatt);
  lfoAenv2.decay(lfoAdec);
  lfoAenv2.release(lfoArel);
  lfoAenv2.sustain(lfoAsus);

  lfoA3.amplitude(lfoAamp);
  lfoA3.frequency(lfoAfreq);
  lfoAenv3.delay(lfoAdel);
  lfoAenv3.attack(lfoAatt);
  lfoAenv3.decay(lfoAdec);
  lfoAenv3.release(lfoArel);
  lfoAenv3.sustain(lfoAsus);

  lfoA4.amplitude(lfoAamp);
  lfoA4.frequency(lfoAfreq);
  lfoAenv4.delay(lfoAdel);
  lfoAenv4.attack(lfoAatt);
  lfoAenv4.decay(lfoAdec);
  lfoAenv4.release(lfoArel);
  lfoAenv4.sustain(lfoAsus);

  lfoA5.amplitude(lfoAamp);
  lfoA5.frequency(lfoAfreq);
  lfoAenv5.delay(lfoAdel);
  lfoAenv5.attack(lfoAatt);
  lfoAenv5.decay(lfoAdec);
  lfoAenv5.release(lfoArel);
  lfoAenv5.sustain(lfoAsus);

  lfoA6.amplitude(lfoAamp);
  lfoA6.frequency(lfoAfreq);
  lfoAenv6.delay(lfoAdel);
  lfoAenv6.attack(lfoAatt);
  lfoAenv6.decay(lfoAdec);
  lfoAenv6.release(lfoArel);
  lfoAenv6.sustain(lfoAsus);

  //lfo shape switch
  if (lfoAshape == 0) {
    lfoA1.begin(WAVEFORM_SINE);
    lfoA2.begin(WAVEFORM_SINE);
    lfoA3.begin(WAVEFORM_SINE);
    lfoA4.begin(WAVEFORM_SINE);
    lfoA5.begin(WAVEFORM_SINE);
    lfoA6.begin(WAVEFORM_SINE);
  } else if (lfoAshape == 1) {
    lfoA1.begin(WAVEFORM_SAWTOOTH_REVERSE);
    lfoA2.begin(WAVEFORM_SAWTOOTH_REVERSE);
    lfoA3.begin(WAVEFORM_SAWTOOTH_REVERSE);
    lfoA4.begin(WAVEFORM_SAWTOOTH_REVERSE);
    lfoA5.begin(WAVEFORM_SAWTOOTH_REVERSE);
    lfoA6.begin(WAVEFORM_SAWTOOTH_REVERSE);
  } else if (lfoAshape == 2) {
    lfoA1.begin(WAVEFORM_SAMPLE_HOLD);
    lfoA2.begin(WAVEFORM_SAMPLE_HOLD);
    lfoA3.begin(WAVEFORM_SAMPLE_HOLD);
    lfoA4.begin(WAVEFORM_SAMPLE_HOLD);
    lfoA5.begin(WAVEFORM_SAMPLE_HOLD);
    lfoA6.begin(WAVEFORM_SAMPLE_HOLD);
  }


  //lfo A read
  unsigned long currTime = millis();
  if (currTime - prevTime >= readInt) {
    ampMod = lfoAread1.read();
    prevTime = currTime;
  }
  finalMix.gain(0, (1 - (ampMod * 3.2)));
  finalMix.gain(1, (1 - (ampMod * 3.2)));





  //lfo B params
  lfoB1.amplitude(lfoBamp);
  lfoB1.frequency(lfoBfreq);

  lfoB2.amplitude(lfoBamp);
  lfoB2.frequency(lfoBfreq);

  lfoB3.amplitude(lfoBamp);
  lfoB3.frequency(lfoBfreq);

  lfoB4.amplitude(lfoBamp);
  lfoB4.frequency(lfoBfreq);

  lfoB5.amplitude(lfoBamp);
  lfoB5.frequency(lfoBfreq);

  lfoB6.amplitude(lfoBamp);
  lfoB6.frequency(lfoBfreq);



  //LFO A DESTINATION
  if (lfoAdest == 0) {      //lfo - pitch
    patchCord7.connect();   //vcoA
    patchCord8.connect();   //vcoB
    patchCord9.connect();   //vcoC
    patchCord10.connect();  //sub
    patchCord15.connect();  //vcoA
    patchCord16.connect();  //vcoB
    patchCord17.connect();  //vcoC
    patchCord18.connect();  //sub
    patchCord20.connect();  //vcoA
    patchCord21.connect();  //vcoB
    patchCord22.connect();  //vcoC
    patchCord23.connect();  //sub
    patchCord25.connect();  //vcoA
    patchCord26.connect();  //vcoB
    patchCord27.connect();  //vcoC
    patchCord28.connect();  //sub
    patchCord36.connect();  //vcoA
    patchCord37.connect();  //vcoB
    patchCord38.connect();  //vcoC
    patchCord39.connect();  //sub
    patchCord41.connect();  //vcoA
    patchCord42.connect();  //vcoB
    patchCord43.connect();  //vcoC
    patchCord44.connect();  //sub

    patchCord11.disconnect();  //filter
    patchCord19.disconnect();  //filter
    patchCord24.disconnect();  //filter
    patchCord29.disconnect();  //filter
    patchCord40.disconnect();  //filter
    patchCord45.disconnect();  //filter

    patchCord12.disconnect();  //lfoAread.
  }
  if (lfoAdest == 1) {  //lfo - filter

    patchCord7.disconnect();   //vcoA
    patchCord8.disconnect();   //vcoB
    patchCord9.disconnect();   //vcoC
    patchCord10.disconnect();  //sub
    patchCord15.disconnect();  //vcoA
    patchCord16.disconnect();  //vcoB
    patchCord17.disconnect();  //vcoC
    patchCord18.disconnect();  //sub
    patchCord20.disconnect();  //vcoA
    patchCord21.disconnect();  //vcoB
    patchCord22.disconnect();  //vcoC
    patchCord23.disconnect();  //sub
    patchCord25.disconnect();  //vcoA
    patchCord26.disconnect();  //vcoB
    patchCord27.disconnect();  //vcoC
    patchCord28.disconnect();  //sub
    patchCord36.disconnect();  //vcoA
    patchCord37.disconnect();  //vcoB
    patchCord38.disconnect();  //vcoC
    patchCord39.disconnect();  //sub
    patchCord41.disconnect();  //vcoA
    patchCord42.disconnect();  //vcoB
    patchCord43.disconnect();  //vcoC
    patchCord44.disconnect();  //sub

    patchCord11.connect();  //filter
    patchCord19.connect();  //filter
    patchCord24.connect();  //filter
    patchCord29.connect();  //filter
    patchCord40.connect();  //filter
    patchCord45.connect();  //filter

    patchCord12.disconnect();  //lfoAread.
  }
  if (lfoAdest == 2) {  //lfo - amp

    patchCord7.disconnect();   //vcoA
    patchCord8.disconnect();   //vcoB
    patchCord9.disconnect();   //vcoC
    patchCord10.disconnect();  //sub
    patchCord15.disconnect();  //vcoA
    patchCord16.disconnect();  //vcoB
    patchCord17.disconnect();  //vcoC
    patchCord18.disconnect();  //sub
    patchCord20.disconnect();  //vcoA
    patchCord21.disconnect();  //vcoB
    patchCord22.disconnect();  //vcoC
    patchCord23.disconnect();  //sub
    patchCord25.disconnect();  //vcoA
    patchCord26.disconnect();  //vcoB
    patchCord27.disconnect();  //vcoC
    patchCord28.disconnect();  //sub
    patchCord36.disconnect();  //vcoA
    patchCord37.disconnect();  //vcoB
    patchCord38.disconnect();  //vcoC
    patchCord39.disconnect();  //sub
    patchCord41.disconnect();  //vcoA
    patchCord42.disconnect();  //vcoB
    patchCord43.disconnect();  //vcoC
    patchCord44.disconnect();  //sub

    patchCord11.disconnect();  //filter
    patchCord19.disconnect();  //filter
    patchCord24.disconnect();  //filter
    patchCord29.disconnect();  //filter
    patchCord40.disconnect();  //filter
    patchCord45.disconnect();  //filter

    patchCord12.connect();  //lfoAread.
  }

  // e_dly_reverb start here

  //Main ENVELOPE
  env1.attack(envAtt);
  env2.attack(envAtt);
  env3.attack(envAtt);
  env4.attack(envAtt);
  env5.attack(envAtt);
  env6.attack(envAtt);

  env1.decay(envDec);
  env2.decay(envDec);
  env3.decay(envDec);
  env4.decay(envDec);
  env5.decay(envDec);
  env6.decay(envDec);

  env1.sustain(envSus);
  env2.sustain(envSus);
  env3.sustain(envSus);
  env4.sustain(envSus);
  env5.sustain(envSus);
  env6.sustain(envSus);

  env1.release(envRel);
  env2.release(envRel);
  env3.release(envRel);
  env4.release(envRel);
  env5.release(envRel);
  env6.release(envRel);



  //delay
  dlyL.delay(0, dlyTimeL);
  dlyMixL.gain(1, dlyAmt * 0.9);

  dlyR.delay(0, dlyTimeR);
  dlyMixR.gain(1, (dlyAmt / 1.4) * 0.9);


  //reverb
  fxL.gain(1, revMix);
  fxR.gain(1, revMix);

  reverb.roomsize(revSize);


  //output gain reduction
  fxL.gain(0, outGain - revMix / 1.6);
  fxL.gain(2, outGain - revMix / 1.6);

  fxR.gain(0, outGain - revMix / 1.6);
  fxR.gain(2, outGain - revMix / 1.6);
}
