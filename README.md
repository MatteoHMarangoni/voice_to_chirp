# Voice_to_chirp
Educational DIY electronics project that samples a human voice and plays it back at higher speed, making it resemble a bird call. 
It’s designed for kids to build and enjoy while discovering how electronics and sound work together.

## Usage
Download this repository.
Build the circuit using the schematics provided.
Upload the code to your board (ESP32S3 - Lolin S3 Pro).
Have fun chirping!

## Functionality
Right button: RECORD.
Left button: PLAYBACK.

The voice to chirp is achieved by speeding up the sample with a factor 3 and playing it twice in a row.

## Hardware:
We use the ESP32S3 Lolin S3 Pro microcontroller and a few simple components:

** Total list of components: **
* INMP441 microphone
* MAX98357A amplifier
* LED
* 200 Ω ohm resistor
* 2x push buttons
* Jumper wires

## Schematics
This is the full circuit diagram. 
![Circuit Schematic](Images/Afbeelding4.png)
