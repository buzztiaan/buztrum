#include <ShiftOutX.h>
#include <ShiftPinNo.h>
#include <Wire.h>
#include <Adafruit_MPR121.h>
#include <MIDI.h>
#include <Pin.h>
#include <PinGroup.h> 

Adafruit_MPR121 cap = Adafruit_MPR121();
MIDI_CREATE_DEFAULT_INSTANCE();

// where are the shift registers
int latchPin = 8;
int clockPin = 12;
int dataPin = 11;

// where are the columns connected
Pin buttonPins[] = {2,3,4};

// something to store the buttons in (12 = C , 0 = B)
int majbuttons[12];
int minbuttons[12];
int svtbuttons[12];

// store the current state of strings
int allstrings[6];

// where is the MPR121 interrupt pin
int touchinterruptPin = 7;

// Keeps track of the last pins touched
// so we know when buttons are 'released'
uint16_t lasttouched = 0;
uint16_t currtouched = 0;

Pin strumPin = Pin(touchinterruptPin);


// STOLEN FROM StrumController.c ; see; https://github.com/hotchk155/Voici-Le-Strum

// CHORD SHAPES
enum {
  CHORD_NONE  = 0b000,
  CHORD_MAJ   = 0b001,
  CHORD_MIN = 0b010,
  CHORD_DOM7  = 0b100,
  CHORD_MAJ7  = 0b101,
  CHORD_MIN7  = 0b110,
  CHORD_AUG   = 0b111,
  CHORD_DIM = 0b011
};

// CHORD ROOT NOTES
enum {
  ROOT_C,
  ROOT_CSHARP,
  ROOT_D,
  ROOT_DSHARP,
  ROOT_E,
  ROOT_F,
  ROOT_FSHARP,
  ROOT_G,
  ROOT_GSHARP,
  ROOT_A,
  ROOT_ASHARP,
  ROOT_B
};

// CHORD EXTENSIONS
enum {
  ADD_NONE,
  SUS_4,
  ADD_6,
  ADD_9
};

// CONTROLLING FLAGS
enum {
  OPT_PLAYONMAKE      = 0x0001, // start playing a note when stylus makes contact with pad
  OPT_STOPONBREAK     = 0x0002, // stop playing a note when stylus breaks contact with pad
  OPT_PLAYONBREAK     = 0x0004, // start playing a note when stylus breaks contact with pad 
  OPT_STOPONMAKE      = 0x0008, // stop playing a note when stylus makes contact with pad 
  OPT_DRONE       = 0x0010, // play chord triad on MIDI channel 2 as soon as chord button is pressed and stop when released
  OPT_GUITAR        = 0x0020, // use guitar voicing
  OPT_GUITAR2       = 0x0040, // use octave shifted guitar chord map on strings 10-16
  OPT_GUITARBASSNOTES   = 0x0080, // enable bottom guitar strings (that are usually damped) but can provide alternating bass notes
  OPT_SUSTAIN       = 0x0100, // do not kill all strings when chord button is released
  OPT_SUSTAINCOMMON   = 0x0200, // when switching to a new chord, allow common notes to sustain (do not retrig) on strings
  OPT_SUSTAINDRONE    = 0x0400, // do not kill drone chord when chord button is released
  OPT_SUSTAINDRONECOMMON  = 0x0800, // when switching to a new chord, allow common notes to sustain (do not retrig) on drone chord
  OPT_ADDNOTES      = 0x1000, // enable adding of sus4, add6, add9 to chord
  OPT_CHROMATIC     = 0x2000, // map strings to chromatic scale from C instead of chord
  OPT_DIATONIC      = 0x4000, // map strings to diatonic major scale 
  OPT_PENTATONIC      = 0x8000  // map strings to pentatonic scale 
};

enum {
  SETTING_REVERSESTRUM  = 0x0001, // reverse strum direction
  SETTING_CIRCLEOF5THS  = 0x0002  // accordion button layout
};

// Byte type
typedef unsigned char byte;

// This structure is used to define a specific chord setup
typedef struct 
{
  byte chordType; // The chord shape
  byte rootNote;  // root note from 0 (C)
  byte extension; // added note if applicable
} CHORD_SELECTION;

// special note value
#define NO_NOTE 0xff

// bit mapped register of which strings are currently connected to the stylus 
unsigned long strings = 0;

#define NO_SELECTION 0xff

// The first column containing a pressed chord button 
byte rootNoteColumn = NO_SELECTION;

// The first column containing a pressed chord button during the last key scan
byte lastRootNoteColumn = NO_SELECTION;

// Define the information relating to string play
byte playChannel = 1;
byte playVelocity = 127;
byte playNotes[16];

// Define the information relating to chord button drone
byte droneChannel = 2;
byte droneVelocity = 127;
byte droneNotes[16];

// This structure records the previous chord selection so we can
// detected if it has changed
CHORD_SELECTION lastChordSelection = { CHORD_NONE, NO_NOTE, ADD_NONE };


// Basic strum
const unsigned int patch_BasicStrum = 
  OPT_PLAYONBREAK     |   
  OPT_STOPONMAKE      |   
  OPT_SUSTAINCOMMON   ;

// Guitar strum
const unsigned int patch_GuitarStrum = 
  OPT_PLAYONBREAK     |   
  OPT_STOPONMAKE      |   
  OPT_GUITAR        |
  OPT_GUITAR2       |
  OPT_SUSTAINCOMMON   |
  OPT_ADDNOTES      ;

// Guitar sustained
const unsigned int patch_GuitarSustain = 
  OPT_PLAYONBREAK     |   
  OPT_STOPONMAKE      |   
  OPT_GUITAR        |
  OPT_GUITAR2       |
  OPT_SUSTAIN       |
  OPT_SUSTAINCOMMON   |
  OPT_ADDNOTES      ;

// Chords and melody
const unsigned int patch_OrganButtons = 
  OPT_PLAYONBREAK     |   
  OPT_STOPONMAKE      |   
  OPT_SUSTAIN       |
  OPT_SUSTAINCOMMON   |
  OPT_DRONE       |
  OPT_SUSTAINDRONE    |
  OPT_SUSTAINDRONECOMMON  ;

// Chords with adds and melody
const unsigned int patch_OrganButtonsAddedNotes = 
  OPT_PLAYONBREAK     |   
  OPT_STOPONMAKE      |   
  OPT_SUSTAIN       |
  OPT_SUSTAINCOMMON   |
  OPT_DRONE       |
  OPT_SUSTAINDRONE    |
  OPT_SUSTAINDRONECOMMON  |
  OPT_ADDNOTES  ;

// Chords and melody
const unsigned int patch_OrganButtonsAddedNotesRetrig = 
  OPT_PLAYONBREAK     |   
  OPT_STOPONMAKE      |   
  OPT_SUSTAIN       |
  OPT_SUSTAINCOMMON   |
  OPT_DRONE       |
  OPT_SUSTAINDRONE    |
  OPT_ADDNOTES  ;

// Chords and chromatic scale
const unsigned int patch_OrganButtonsChromatic = 
  OPT_PLAYONBREAK     |   
  OPT_STOPONMAKE      |   
  OPT_CHROMATIC     |
  OPT_SUSTAIN       |
  OPT_DRONE       |
  OPT_SUSTAINDRONE    |
  OPT_SUSTAINDRONECOMMON  ;

const unsigned int DefaultSettings = 0;

unsigned int options = patch_GuitarStrum;
unsigned int settings = DefaultSettings | OPT_GUITARBASSNOTES;



////////////////////////////////////////////////////////////
//
// GUITAR CHORD SHAPE DEFINITIONS
//
////////////////////////////////////////////////////////////
void guitarCShape(byte ofs, byte extension, byte *chord)
{
  if(options & OPT_GUITARBASSNOTES)
    chord[0] = 43 + ofs;
  chord[1] = 48 + ofs;
  chord[2] = 52 + ofs + (extension == SUS_4);
  chord[3] = 55 + ofs + 2 * (extension == ADD_6);
  chord[4] = 60 + ofs + 2 * (extension == ADD_9);
  chord[5] = 64 + ofs + (extension == SUS_4);
}
void guitarC7Shape(byte ofs, byte extension, byte *chord)
{
  if(options & OPT_GUITARBASSNOTES)
    chord[0] = 43 + ofs;
  chord[1] = 48 + ofs;
  chord[2] = 52 + ofs + (extension == SUS_4);
  chord[3] = 58 + ofs - (extension == ADD_6);
  chord[4] = 60 + ofs + 2 * (extension == ADD_9);
  chord[5] = 64 + ofs + (extension == SUS_4);
}
void guitarAShape(byte ofs, byte extension, byte *chord)
{
  if(options & OPT_GUITARBASSNOTES)
    chord[0] = 40 + ofs;
  chord[1] = 45 + ofs;
  chord[2] = 52 + ofs + 2 * (extension == ADD_6);
  chord[3] = 57 + ofs + 2 * (extension == ADD_9);;
  chord[4] = 61 + ofs + (extension == SUS_4);
  chord[5] = 64 + ofs;
}
void guitarAmShape(byte ofs, byte extension, byte *chord)
{
  if(options & OPT_GUITARBASSNOTES)
    chord[0] = 40 + ofs;
  chord[1] = 45 + ofs;
  chord[2] = 52 + ofs + 2 * (extension == ADD_6);
  chord[3] = 57 + ofs + 2 * (extension == ADD_9);;
  chord[4] = 60 + ofs  + 2 * (extension == SUS_4);
  chord[5] = 64 + ofs;
}
void guitarA7Shape(byte ofs, byte extension, byte *chord)
{
  if(options & OPT_GUITARBASSNOTES)
    chord[0] = 40 + ofs;
  chord[1] = 45 + ofs;
  chord[2] = 52 + ofs + 2 * (extension == ADD_6);
  chord[3] = 55 + ofs + 4 * (extension == ADD_9);;
  chord[4] = 61 + ofs + (extension == SUS_4);
  chord[5] = 64 + ofs;
}
void guitarDShape(byte ofs, byte extension, byte *chord)
{
  if(options & OPT_GUITARBASSNOTES)
    chord[1] = 45 + ofs;
  chord[2] = 50 + ofs;
  chord[3] = 57 + ofs + 2 * (extension == ADD_6);
  chord[4] = 62 + ofs;
  chord[5] = 66 + ofs  + (extension == SUS_4) - 2*(extension == ADD_9);
}
void guitarDmShape(byte ofs, byte extension, byte *chord)
{
  if(options & OPT_GUITARBASSNOTES)
    chord[1] = 45 + ofs;
  chord[2] = 50 + ofs;
  chord[3] = 57 + ofs + 2 * (extension == ADD_6);
  chord[4] = 62 + ofs;
  chord[5] = 65 + ofs  + 2 * (extension == SUS_4) - (extension == ADD_9);
}
void guitarD7Shape(byte ofs, byte extension, byte *chord)
{
  if(options & OPT_GUITARBASSNOTES)
    chord[1] = 45 + ofs;
  chord[2] = 50 + ofs;
  chord[3] = 57 + ofs + 2 * (extension == ADD_6);
  chord[4] = 60 + ofs;
  chord[5] = 66 + ofs  + (extension == SUS_4)- 2*(extension == ADD_9);
}
void guitarEShape(byte ofs, byte extension, byte *chord)
{
  chord[0] = 40 + ofs;
  chord[1] = 47 + ofs;
  chord[2] = 52 + ofs + 2 * (extension == ADD_9);
  chord[3] = 56 + ofs  + (extension == SUS_4);
  chord[4] = 59 + ofs + 2 * (extension == ADD_6);
  chord[5] = 64 + ofs;
}
void guitarEmShape(byte ofs, byte extension, byte *chord)
{
  chord[0] = 40 + ofs;
  chord[1] = 47 + ofs;
  chord[2] = 52 + ofs + 2 * (extension == ADD_9);
  chord[3] = 55 + ofs  + 2 * (extension == SUS_4);
  chord[4] = 59 + ofs + 2 * (extension == ADD_6);
  chord[5] = 64 + ofs;
}
void guitarE7Shape(byte ofs, byte extension, byte *chord)
{
  chord[0] = 40 + ofs;
  chord[1] = 47 + ofs;
  chord[2] = 50 + ofs + 4 * (extension == ADD_9);
  chord[3] = 56 + ofs  + (extension == SUS_4);
  chord[4] = 59 + ofs + 2 * (extension == ADD_6);
  chord[5] = 64 + ofs;
}
void guitarGShape(byte ofs, byte extension, byte *chord)
{
  chord[0] = 43 + ofs;
  chord[1] = 47 + ofs  + (extension == SUS_4);
  chord[2] = 50 + ofs + 2 * (extension == ADD_6);
  chord[3] = 55 + ofs  + 2*(extension == ADD_9);
  chord[4] = 59 + ofs + (extension == SUS_4);
  chord[5] = 67 + ofs;
}

////////////////////////////////////////////////////////////
//
// GUITAR CHORD MAPPING
//
////////////////////////////////////////////////////////////
byte guitarChord(CHORD_SELECTION *pChordSelection, byte transpose, byte *chord)
{ 
  memset(chord, NO_NOTE, 16);
  switch(pChordSelection->chordType)
  {
    case CHORD_MAJ:
      switch(pChordSelection->rootNote)
      {
      case ROOT_C:    guitarCShape(0, pChordSelection->extension, chord); break;
      case ROOT_CSHARP:   guitarAShape(4, pChordSelection->extension, chord); break;
      case ROOT_D:    guitarDShape(0, pChordSelection->extension, chord); break;
      case ROOT_DSHARP: guitarAShape(6, pChordSelection->extension, chord); break;
      case ROOT_E:    guitarEShape(0, pChordSelection->extension, chord); break;
      case ROOT_F:    guitarEShape(1, pChordSelection->extension, chord); break;
      case ROOT_FSHARP: guitarEShape(2, pChordSelection->extension, chord); break;
      case ROOT_G:    guitarGShape(0, pChordSelection->extension, chord); break;
      case ROOT_GSHARP: guitarEShape(4, pChordSelection->extension, chord); break;
      case ROOT_A:    guitarAShape(0, pChordSelection->extension, chord); break;
      case ROOT_ASHARP:   guitarAShape(1, pChordSelection->extension, chord); break;
      case ROOT_B:      guitarAShape(2, pChordSelection->extension, chord); break;
      }
      break;
    case CHORD_MIN:
      switch(pChordSelection->rootNote)
      {
      case ROOT_C:    guitarAmShape(3, pChordSelection->extension, chord);  break;
      case ROOT_CSHARP:   guitarAmShape(4, pChordSelection->extension, chord);  break;
      case ROOT_D:    guitarDmShape(0, pChordSelection->extension, chord);  break;
      case ROOT_DSHARP: guitarAmShape(6, pChordSelection->extension, chord);  break;
      case ROOT_E:    guitarEmShape(0, pChordSelection->extension, chord);  break;
      case ROOT_F:    guitarEmShape(1, pChordSelection->extension, chord);  break;
      case ROOT_FSHARP: guitarEmShape(2, pChordSelection->extension, chord);  break;
      case ROOT_G:    guitarEmShape(3, pChordSelection->extension, chord);  break;
      case ROOT_GSHARP: guitarEmShape(4, pChordSelection->extension, chord);  break;
      case ROOT_A:    guitarAmShape(0, pChordSelection->extension, chord);  break;
      case ROOT_ASHARP:   guitarAmShape(1, pChordSelection->extension, chord);  break;
      case ROOT_B:      guitarAmShape(2, pChordSelection->extension, chord);  break;
      }
      break;
    case CHORD_DOM7:
      switch(pChordSelection->rootNote)
      {
      case ROOT_C:    guitarC7Shape(0, pChordSelection->extension, chord);  break;
      case ROOT_CSHARP:   guitarA7Shape(4, pChordSelection->extension, chord);  break;
      case ROOT_D:    guitarD7Shape(0, pChordSelection->extension, chord);  break;
      case ROOT_DSHARP: guitarA7Shape(6, pChordSelection->extension, chord);  break;
      case ROOT_E:    guitarE7Shape(0, pChordSelection->extension, chord);  break;
      case ROOT_F:    guitarE7Shape(1, pChordSelection->extension, chord);  break;
      case ROOT_FSHARP: guitarE7Shape(2, pChordSelection->extension, chord);  break;
      case ROOT_G:    guitarE7Shape(3, pChordSelection->extension, chord);  break;
      case ROOT_GSHARP: guitarE7Shape(4, pChordSelection->extension, chord);  break;
      case ROOT_A:    guitarA7Shape(0, pChordSelection->extension, chord);  break;
      case ROOT_ASHARP:   guitarA7Shape(1, pChordSelection->extension, chord);  break;
      case ROOT_B:      guitarA7Shape(2, pChordSelection->extension, chord);  break;
      }
      break;
    default:
      return 0;
  } 
  for(int i=0;i<16;++i)
    if(chord[i] != NO_NOTE)
      chord[i] += transpose;
  return 6;
}

////////////////////////////////////////////////////////////
//
// MAKE A CHORD BY "STACKING TRIADS"
//
////////////////////////////////////////////////////////////
byte stackTriads(CHORD_SELECTION *pChordSelection, byte maxReps, byte transpose, byte size, byte *chord)
{
  byte struc[5];
  byte len = 0;

  memset(chord, NO_NOTE, 16);
  
  // root
  struc[len++] = 0; 
  
  // added 2/9
  if(pChordSelection->extension == ADD_9)   
    struc[len++] = 2;
    
  // sus 4
  if(pChordSelection->extension == SUS_4) {   
    struc[len++] = 5;
  } else {
    switch(pChordSelection->chordType)    
    {   
      // minor 3rd
    case CHORD_MIN: case CHORD_MIN7: case CHORD_DIM: // minor 3rd
      struc[len++] = 3;
      break;
    default: // major 3rd
      struc[len++] = 4;
      break;    
    }
  }
  
  // 5th
  switch(pChordSelection->chordType)    
  {
  case CHORD_AUG:
    struc[len++] = 8;
    break;
  case CHORD_DIM:
    struc[len++] = 6;
    break;
  default:
    struc[len++] = 7;
    break;
  }
  
  if(pChordSelection->extension == ADD_6)   
    struc[len++] = 9;
      
  // 7th
  switch(pChordSelection->chordType)    
  {
  case CHORD_DOM7: case CHORD_MIN7:
    struc[len++] = 10;
    break;
  case CHORD_MAJ7:
    struc[len++] = 11;
    break;        
  }

  // fill the chord array with MIDI notes
  byte root = pChordSelection->rootNote + transpose;
  int from = 0;
  int to = 0;
  while(to < size)
  {
    chord[to++] = root+struc[from];   
    if(++from >= len)
    {
      if(!--maxReps)
        return to;
      root+=12;
      from = 0;
    }
  }
  return to;
}

////////////////////////////////////////////////////////////
//
// MAKE A SCALE BY MASKING NOTES
//
////////////////////////////////////////////////////////////
byte makeScale(int root, byte transpose, unsigned long mask, byte *chord)
{
  memset(chord,NO_NOTE,16);
  unsigned long b = 0;
  while(root < transpose + 16)
  {
      //         210987654321
    if(!b) b = 0b100000000000;    
    if(mask & b)
    {
      if(root >= transpose)
        chord[root - transpose] = root;
    }
    ++root;
    b>>=1;
  } 
}


////////////////////////////////////////////////////////////
//
// START PLAYING THE NOTES OF THE NEW CHORD
//
////////////////////////////////////////////////////////////
void playChordNotes(byte *oldNotes, byte *newNotes, byte channel, byte velocity, byte sustainCommon)
{
  int i,j;
  
  // Start by silencing old notes which are not in the new chord
  for(i=0;i<16;++i)
  {   
    if(NO_NOTE != oldNotes[i])
    {
      if(sustainCommon)
      {
        for(j=0;j<16;++j)
        {
          if(oldNotes[i] == newNotes[j])
            break;
        }
        if(j==16)
        {
          MIDI.sendNoteOff(oldNotes[i], 0 , channel);
          oldNotes[i] = NO_NOTE;
        }
      }
      else
      {
        MIDI.sendNoteOff(oldNotes[i], 0 , channel);
        oldNotes[i] = NO_NOTE;
      }
    } 
  }
  
  // Now play notes which are not already playing
  if(velocity)
  {
    for(i=0;i<16;++i)
    {   
      if(NO_NOTE != newNotes[i])
      {
        for(j=0;j<16;++j)
        {
          if(oldNotes[j] == newNotes[i])
            break;
        }
        if(j==16)
        {
          MIDI.sendNoteOn(newNotes[i], velocity, channel);
        }
      }
    }
  }

  // remember the notes
  memcpy(oldNotes, newNotes, 16);
}


////////////////////////////////////////////////////////////
//
// RELEASE THE NOTES OF A CHORD
//
////////////////////////////////////////////////////////////
void releaseChordNotes(byte *oldNotes, byte channel, byte sustain)
{
  int i;

  // override allowed by sustain option
  if(sustain)
    return;
    
  // Silence notes 
  for(i=0;i<16;++i)
  {   
    if(NO_NOTE != oldNotes[i])
    {
      MIDI.sendNoteOff(oldNotes[i], 0, channel);
      oldNotes[i] = NO_NOTE;
    } 
  }

}


////////////////////////////////////////////////////////////
//
// CALCULATE NOTES FOR A CHORD SHAPE, MAP THEM TO THE STRINGS
// AND START PLAYING DRONE IF APPROPRIATE
//
////////////////////////////////////////////////////////////
void changeToChord(CHORD_SELECTION *pChordSelection)
{  
  
  int i,j;
  byte chord[16];
  byte chordLen;
  byte notes[16];
    
  // is the new chord a "no chord"
  if(CHORD_NONE == pChordSelection->chordType)
  {
    releaseChordNotes(playNotes, playChannel, !!(options & OPT_SUSTAIN));
    releaseChordNotes(droneNotes, droneChannel, !!(options & OPT_SUSTAINDRONE));    
  }
  else  
  {     
    // are we in guitar mode?
    if(options & OPT_GUITAR)
    {
      // build the guitar chord, using stacked triads
      // as a fallback if there is no chord mapping
      chordLen = guitarChord(pChordSelection, 12, chord);
      if(!chordLen)
      {
        stackTriads(pChordSelection, -1, 60, 6, chord);
        chordLen = 6;
      }
        
      // double up the guitar chords
      if(options & OPT_GUITAR2)
      {
        for(i=0;i<6;++i)
          if(chord[i] != NO_NOTE)
            chord[10+i] = 12+chord[i];
        chordLen = 16;
      }
    }
    // should we have a chromatic scale mapped to the strings?
    else if(options & OPT_CHROMATIC)
    {
      makeScale(pChordSelection->rootNote, 48, 0b111111111111, chord);
      chordLen=16;
    }
    // diatonic major or minor
    else if(options & OPT_DIATONIC)
    {
      if((pChordSelection->chordType == CHORD_MIN)||(pChordSelection->chordType == CHORD_MIN7))
        makeScale(pChordSelection->rootNote, 48, 0b101101011010, chord);
      else
        makeScale(pChordSelection->rootNote, 48, 0b101011010101, chord);
      chordLen=16;
    }
    // pentatonic 
    else if(options & OPT_PENTATONIC)
    {
      makeScale(pChordSelection->rootNote, 48, 0b101010010100, chord);
      chordLen=16;
    }
    else  
    {
      // stack triads
      stackTriads(pChordSelection, -1, 36, 16, chord);
      chordLen = 16;
    }
  
    // copy chord to notes and pad with null notes
    memset(notes, NO_NOTE, 16);
    memcpy(notes, chord, chordLen);
    
    // damp notes which are not a part of the new chord
    playChordNotes(playNotes, notes, playChannel, 0, !!(options & OPT_SUSTAINCOMMON));

    // deal with drone
    if(options & OPT_DRONE)
    {
      // for the drone chord we only play the triad (not stacked)
      stackTriads(pChordSelection, 1, 36, 16, notes);
      playChordNotes(droneNotes, notes, droneChannel, droneVelocity, !!(options & OPT_SUSTAINDRONECOMMON));
    }
  }
  
  // Store the chord, so we can recognise when it changes
  lastChordSelection = *pChordSelection;
  
}


byte mapRootNote(byte col)
{
  if(!(settings & SETTING_CIRCLEOF5THS))
    return col;
  switch(col)
  {
    case 0: return ROOT_CSHARP;
    case 1: return ROOT_GSHARP;
    case 2: return ROOT_DSHARP;
    case 3: return ROOT_ASHARP;
    case 4: return ROOT_F;
    case 5: return ROOT_C;
    case 6: return ROOT_G;
    case 7: return ROOT_D;
    case 8: return ROOT_A;    
    case 9: return ROOT_E;
    case 10: return ROOT_B;
    case 11: return ROOT_FSHARP;
  }
  return NO_NOTE;
}

void presetPatch(unsigned int o)
{
  options = o;
}


void loadUserPatch() {}
void saveUserPatch() {}

void toggleOption(unsigned long o)
{
  if(options & o)
  {
    options &= ~o;
  }
  else
  {
    options |= o;
  }
}

void clearOptions(unsigned long o)
{
 options &= ~o;
}

void toggleSetting(unsigned int o)
{
 settings ^= o;
}

void stopAllNotes(byte channel)
{
  for(int i=0;i<128;++i)
    MIDI.sendNoteOff(i,0,channel);
}

////////////////////////////////////////////////////////////
//
// POLL INPUT AND MANAGE THE SENDING OF MIDI INFO
//
////////////////////////////////////////////////////////////
void pollIO()
{

  
  rootNoteColumn = NO_SELECTION;
  CHORD_SELECTION chordSelection = { CHORD_NONE,  NO_NOTE, ADD_NONE };
  unsigned long b = 1;
  byte stringCount = 0;
  
  // scan for each string
  for(int i=0;i<12;++i)
  {     
    int whichString = (!!(settings & SETTING_REVERSESTRUM))? (11-i) : i;
    
    digitalWrite(latchPin, LOW);
    uint16_t neededval = 1 << whichString;
    shiftOut_16(dataPin, clockPin, MSBFIRST, neededval);
    digitalWrite(latchPin, HIGH);
    
    // clock pulse to shift the bit (the first bit does not appear until the
    // second clock pulse, since we tied shift and store clock lines together)

    // Allow inputs to settle
    delay(1);
    
    // did we get a signal back on any of the  keyboard scan rows?
    if(getValue(buttonPins) != LOW)
    {
      // Is this the first column with a button held 
      if(rootNoteColumn == NO_SELECTION)
      {
        // This logic allows more buttons to be registered without clearing
        // old buttons if the root note is unchanged. This is to ensure that
        // new chord shapes are not applied as the user releases the buttons
        rootNoteColumn = i;         
        chordSelection.rootNote = mapRootNote(i);
        if(i == lastRootNoteColumn)
          chordSelection.chordType = lastChordSelection.chordType;
        chordSelection.chordType |= ((buttonPins[0].getValue() == HIGH)? CHORD_MAJ:CHORD_NONE)|((buttonPins[1].getValue() == HIGH)? CHORD_MIN:CHORD_NONE)|((buttonPins[2].getValue() == HIGH)? CHORD_DOM7:CHORD_NONE);         
      } 
      // Check for chord extension, which is where an additional
      // button is held in a column to the right of the root column
      else if((options & OPT_ADDNOTES) && (chordSelection.extension == ADD_NONE))
      {
        if(buttonPins[0].getValue() == HIGH)
          chordSelection.extension = SUS_4;
        else if(buttonPins[1].getValue() == HIGH)
          chordSelection.extension = ADD_6;
        else if(buttonPins[2].getValue() == HIGH)
          chordSelection.extension = ADD_9;
      }
    }

    currtouched = cap.touched();

    // if MODE is pressed the stylus is used to change the MIDI velocity
    if(0) // TODO ADD MODE BUTTON
    {
      if(0) // used to do stylus through shiftregister, lol
        playVelocity = 0x0f | (whichString<<4);
    }
    // otherwise check whether we got a signal back from the stylus (meaning that
    // it's touching this string)
    else if(currtouched & b) // used to do stylus through shiftregister, lol
    {
     ++stringCount;
      
      // string is being touched... was
      // it being touched before?
      if(!(strings & b))
      {
        // remember this string is being touched
        strings |= b;
        
        // does it map to a real note?
        if(playNotes[i] != NO_NOTE)
        {
          //digitalWrite(13, HIGH);
          // play or damp the note as needed
          if(options & OPT_PLAYONMAKE)
            MIDI.sendNoteOn(playNotes[i], playVelocity, playChannel);            
          else
          if(options & OPT_STOPONMAKE)
            MIDI.sendNoteOff(playNotes[i], 0, playChannel);
        }
      }
    }
    // stylus not touching string now, but was it 
    // touching the string before?
    else if(strings & b)
    {
      // remember string is not being touched
      strings &= ~b;
      
      // does it map to a real note?
      if(playNotes[i] != NO_NOTE)
      {
        // play or damp the note as needed
        if(options & OPT_PLAYONBREAK)
          MIDI.sendNoteOn(playNotes[i], playVelocity, playChannel);            
        else
        if(options & OPT_STOPONBREAK)
          MIDI.sendNoteOff(playNotes[i], 0, playChannel);
      }
    } 
    
    // shift the masking bit  
    b<<=1;    
  } 
    

  if(0) // THIS NEEDS A MODE BUTTON grlmb
  {   
    // MODE is pressed, has a chord button been newly pressed?
    if(rootNoteColumn != lastRootNoteColumn)
    {
      switch(chordSelection.chordType)
      {
      case CHORD_MAJ: // ROW 1
        switch(rootNoteColumn)
        {
        case 0: presetPatch(patch_BasicStrum); break;
        case 2: presetPatch(patch_GuitarStrum); break;
        case 4: presetPatch(patch_GuitarSustain); break;
        case 5: presetPatch(patch_OrganButtons); break;
        case 7: presetPatch(patch_OrganButtonsAddedNotes); break;
        case 9: presetPatch(patch_OrganButtonsAddedNotesRetrig); break;
        case 11: loadUserPatch(); break;
        }
        break;
        
      case CHORD_MIN: // ROW 2
        switch(rootNoteColumn)
        {
        case 0: toggleOption(OPT_PLAYONMAKE); break;
        case 1: toggleOption(OPT_PLAYONBREAK); break;
        case 2: toggleOption(OPT_GUITAR); break;
        case 3: toggleOption(OPT_ADDNOTES); break;
        case 4: toggleOption(OPT_SUSTAIN); break;
        case 5: toggleOption(OPT_CHROMATIC); clearOptions(OPT_DIATONIC|OPT_PENTATONIC); break;
        case 7: toggleOption(OPT_DRONE); break;
        case 8: toggleOption(OPT_SUSTAINDRONE); break;
        case 10: toggleSetting(SETTING_REVERSESTRUM); break;
        case 11: saveUserPatch(); break;
        }
        break;  
        
      case CHORD_DOM7: // ROW3
        switch(rootNoteColumn)
        {
        case 0: toggleOption(OPT_STOPONMAKE); break;
        case 1: toggleOption(OPT_STOPONBREAK); break;
        case 2: toggleOption(OPT_GUITAR2); break;
        case 3: toggleOption(OPT_GUITARBASSNOTES); break;
        case 4: toggleOption(OPT_SUSTAINCOMMON); break;
        case 5: toggleOption(OPT_DIATONIC); clearOptions(OPT_CHROMATIC|OPT_PENTATONIC); break;
        case 6: toggleOption(OPT_PENTATONIC); clearOptions(OPT_DIATONIC|OPT_CHROMATIC); break;
        case 8: toggleOption(OPT_SUSTAINDRONECOMMON); break;
        case 10: toggleSetting(SETTING_CIRCLEOF5THS); break;
        case 11:
          // MIDI Panic
          stopAllNotes(playChannel);
          if(options & OPT_DRONE)
            stopAllNotes(droneChannel);
          break;
        }
        break;    
      } 
    }
  }
  else
  {
    // has the chord changed? note that if the stylus is bridging 2 strings we will not change
    // the chord selection. This is because this situation can confuse the keyboard matrix
    // causing unwanted chord changed
    if((stringCount < 2) && 0 != memcmp(&chordSelection, &lastChordSelection, sizeof(CHORD_SELECTION)))
      changeToChord(&chordSelection); 
  }
  
  // remember the root note for this keyboard scan
  lastRootNoteColumn = rootNoteColumn;
}



void setup() {
  pinMode(latchPin, OUTPUT);
  pinMode(clockPin, OUTPUT);
  pinMode(dataPin, OUTPUT);

  setInput(buttonPins);
  strumPin.setInput();
  
  if (!cap.begin(0x5A)) {
    while (1) {
      delay(250);
      digitalWrite(13,HIGH);
      delay(50);
      digitalWrite(13,LOW);
    }
  }

  MIDI.begin(1);
  Serial.begin(115200); // remove this for 'real MIDI' , or lease as is to use ttymidi over serial
}

void loop() {
  // put your main code here, to run repeatedly:
  
  pollIO();

}

