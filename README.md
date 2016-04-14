# buztrum
A Voici-Le-Strum clone to arduino.

It's depending on a lot of external libraries and a bit a mess, but works sorta ok :)

Libraries:

  * https://github.com/adafruit/Adafruit_MPR121
  * http://playground.arduino.cc/Main/ShiftOutX
  * https://github.com/FortySevenEffects/arduino_midi_library
  * https://github.com/fenichelar/Pin/ ( not sure if this is actually needed, but its fun)

The main strumming surface is just 6 strings, which are interfaced to a MPR121.

The chord buttons are read similar to the original design, using two 595s

Currently i'm using ttymidi to just use a uart cable to power + interface it.

  * http://www.varal.org/ttymidi/

  * https://github.com/hotchk155/Voici-Le-Strum/
  * http://six4pix.com/lestrum/

This follows the same licensing as original, CC-BY-NC-SA 
