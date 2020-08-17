/*

 reinette II plus, a french Apple II emulator, using SDL2
 and powered by puce6502 - a MOS 6502 cpu emulator by the same author
 Last modified 13th of August 2020
 Copyright (c) 2020 Arthur Ferreira (arthur.ferreira2gmail.com)

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.

*/

#include <stdio.h>
#include <SDL2/SDL.h>
#include "puce6502.h"


//================================================================ SOFT SWITCHES

uint8_t KBD   = 0;                                                              // $C000, $C010 ascii value of keyboard input
bool    SPKR  = false;                                                          // $C030 Speaker toggle
bool    TEXT  = true;                                                           // $C050 CLRTEXT  / $C051 SETTEXT
bool    MIXED = false;                                                          // $C052 CLRMIXED / $C053 SETMIXED
uint8_t PAGE  = 1;                                                              // $C054 PAGE1    / $C055 PAGE2
bool    HIRES = false;                                                          // $C056 GR       / $C057 HGR


//====================================================================== PADDLES

uint8_t PB0  = 0;                                                               // $C061 Push Button 0 (bit 7) / Open Apple
uint8_t PB1  = 0;                                                               // $C062 Push Button 1 (bit 7) / Solid Apple
uint8_t PB2  = 0;                                                               // $C063 Push Button 2 (bit 7) / shift mod !!!
float GCP[2] = {127, 127};                                                      // GC Position ranging from 0 (left) to 255 right
float GCC[2] = {0};                                                             // GC0 and GC1 Countdown
int   GCD[2] = {0};                                                             // GC0 and GC1 Directions (left/down or right/up)
int   GCA[2] = {0};                                                             // GC0 and GC1 Action (push or release)
uint8_t GCActionSpeed = 16;                                                     // Game Controller speed at which it goes to the edges
uint8_t GCReleaseSpeed = 16;                                                    // Game Controller speed at which it returns to center
const float  GCFreq = 0.15;                                                     // the freq at which the 556 timer decreases the GC values
long long int GCCrigger;                                                        // the tick at which the GCs have been reseted


inline float paddle(int pdl){
  GCC[pdl] -= (ticks - GCCrigger) * GCFreq;
  if (GCC[pdl] < 0) GCC[pdl] = 0;
  return(GCC[pdl]);
}

inline void resetPaddles(){
  GCC[0] = GCP[0] * GCP[0];
  GCC[1] = GCP[1] * GCP[1];
  GCCrigger = ticks;
}


//======================================================================== AUDIO

#define audioBufferSize 4096                                                    // found to be large enought
Sint8 audioBuffer[2][audioBufferSize] = {0};                                    // see main() for more details
SDL_AudioDeviceID audioDevice;
bool muted = false;                                                             // mute/unmute


//====================================================================== DISK ][

uint8_t slot6[256] = {0};                                                       // P5A disk ][ PROM in slot 6
int curDrv = 0;                                                                 // only one can be enabled at a time

struct drive{
  char filename[512];                                                           // the full disk image path
  bool readOnly;                                                                // based on the image file attributes
  uint8_t data[232960];                                                         // nibblelized disk image
  bool motorOn;                                                                 // motor status
  bool writeMode;                                                               // writes to file are not implemented
  uint8_t track;                                                                // current track position
  uint16_t nibble;                                                              // ptr to nibble under head position
} disk[2] = {0};                                                                // two disk ][ drive units


int insertFloppy(SDL_Window *wdo, char *filename, int drv){
  int i, a, b;
  char title[1024];

  FILE *f = fopen(filename, "rb");                                              // open it in read binary
  if (!f){
    printf("Could not open %s\n", filename);
    return(0);
  }
  if (fread(disk[drv].data, 1, 232960, f) != 232960){                           // load it into memory and check size
    printf("Floppy image should be exactly 232960 Bytes long\n");
    return(0);
  }
  fclose(f);

  sprintf(disk[drv].filename,"%s", filename);                                   // update disk filename

  f = fopen(filename, "ab");                                                    // check if file is writeable
  if (f){
    disk[drv].readOnly = false;                                                 // f will be NULL if open in W failed
    fclose(f);
  }
  else disk[drv].readOnly = true;

  i = a = 0;
  while (disk[0].filename[i] != 0)                                              // find start of filename for disk0
    if (disk[0].filename[i++] == '\\') a = i;

  i = b = 0;
  while (disk[1].filename[i] != 0)                                              // find start of filename for disk1
    if (disk[1].filename[i++] == '\\') b = i;

  sprintf(title, "reinette II+   D1: %s   D2: %s", disk[0].filename + a, \
                                                   disk[1].filename + b);
  SDL_SetWindowTitle(wdo, title);                                               // updates window title

  return(1);
}

int saveFloppy(int drive){
  if (disk[drive].filename[0] && !disk[drive].readOnly){                        // is there's a floppy ? is it writable ?
    FILE *f = fopen(disk[drive].filename, "wb");
    if (f){                                                                     // open in write/binary succeeded
      if (fwrite(disk[drive].data, 1, 232960, f) != 232960){                    // check we could write the full lenght
        printf("Write failed\n");
        return(0);                                                              // failed to write
      }
      fclose(f);                                                                // release the ressource
    }
  }
  return(1);                                                                    // success
}

void stepMotor(uint16_t address){
  static bool phases[2][4]   = {0};                                             // phases states (for both drives)
  static bool phasesB[2][4]  = {0};                                             // phases states Before
  static bool phasesBB[2][4] = {0};                                             // phases states Before Before
  static int pIdx[2]         = {0};                                             // phase index (for both drives)
  static int pIdxB[2]        = {0};                                             // phase index Before
  static int halfTrackPos[2] = {0};

  address &= 7;
  int phase = address >> 1;

  phasesBB[curDrv][pIdxB[curDrv]] = phasesB[curDrv][pIdxB[curDrv]];
  phasesB[curDrv][pIdx[curDrv]]   = phases[curDrv][pIdx[curDrv]];
  pIdxB[curDrv] = pIdx[curDrv];
  pIdx[curDrv]  = phase;

  if (!(address & 1)){                                                          // head not moving (PHASE x OFF)
    phases[curDrv][phase] = false;
    return;
  }

  if ((phasesBB[curDrv][(phase + 1) & 3]) && (--halfTrackPos[curDrv] < 0))      // head is moving in
    halfTrackPos[curDrv] = 0;

  if ((phasesBB[curDrv][(phase - 1) & 3]) && (++halfTrackPos[curDrv] > 140))    // head is moving out
    halfTrackPos[curDrv] = 140;

  phases[curDrv][phase] = true;                                                 // update track#
  disk[curDrv].track = (halfTrackPos[curDrv] + 1) / 2;
  disk[curDrv].nibble = 0;                                                      // not sure this is necessary ?
}


//========================================== MEMORY MAPPED SOFT SWITCHES HANDLER
// this function is called from readMem and writeMem in puce6502
// it complements both functions when address 1is between 0xC000 and 0xCFFF

uint8_t softSwitches(uint16_t address, uint8_t value){
  static uint8_t dLatch = 0;                                                    // disk ][ I/O reg
  static long long int lastTick = 0LL;

  if ((address >> 8) == 0xC6) return(slot6[address - 0xC600]);                  // disk ][ PROM

  switch (address){
    case 0xC000: return(KBD);                                                   // KEYBOARD
    case 0xC010: KBD &= 0x7F; return(KBD);                                      // key STROBE

    case 0xC020:                                                                // TAPEOUT (shall we listen it ?)
    case 0xC030:                                                                // SPEAKER
    case 0xC033:                                                                // apple invader uses it to output sound !
      if (!muted){
        SPKR = !SPKR;                                                           // toggle speaker state
        Uint32 length = (ticks - lastTick) / 10.65625;                          // 1023000Hz / 96000Hz = 10.65625
        lastTick = ticks;
        if (length > audioBufferSize) length = audioBufferSize;
        SDL_QueueAudio(audioDevice, audioBuffer[SPKR], length | 1);             // | 1 TO HEAR HIGH FREQ SOUNDS
      }
      break;

    case 0xC050: TEXT  = false; break;                                          // Graphics
    case 0xC051: TEXT  = true;  break;                                          // Text
    case 0xC052: MIXED = false; break;                                          // Mixed off
    case 0xC053: MIXED = true;  break;                                          // Mixed on
    case 0xC054: PAGE  = 1;     break;                                          // Page 1
    case 0xC055: PAGE  = 2;     break;                                          // Page 2
    case 0xC056: HIRES = false; break;                                          // HiRes off
    case 0xC057: HIRES = true;  break;                                          // HiRes on

    case 0xC061: return(PB0);                                                   // Push Button 0
    case 0xC062: return(PB1);                                                   // Push Button 1
    case 0xC063: return(PB2);                                                   // Push Button 2
    case 0xC064: return(paddle(0) != 0 ? 0x80: 0x00);                           // Paddle 0
    case 0xC065: return(paddle(1) != 0 ? 0x80: 0x00);                           // Paddle 1
    case 0xC070: resetPaddles(); break;                                         // paddle timer RST

    case 0xC0E0:                                                                // PHASE0OFF
    case 0xC0E1:                                                                // PHASE0ON
    case 0xC0E2:                                                                // PHASE1OFF
    case 0xC0E3:                                                                // PHASE1ON
    case 0xC0E4:                                                                // PHASE2OFF
    case 0xC0E5:                                                                // PHASE2ON
    case 0xC0E6:                                                                // PHASE3OFF
    case 0xC0E7: stepMotor(address); break;                                     // PHASE3ON

    case 0xC0E8: disk[curDrv].motorOn = false; break;                           // MOTOROFF
    case 0xC0E9: disk[curDrv].motorOn = true;  break;                           // MOTORON

    case 0xC0EA:                                                                // DRIVE0EN
      disk[0].motorOn = disk[1].motorOn || disk[0].motorOn;
      disk[1].motorOn = false;
      curDrv = 0;
      break;
    case 0xC0EB:                                                                // DRIVE1EN
      disk[1].motorOn = disk[0].motorOn || disk[1].motorOn;
      disk[0].motorOn = false;
      curDrv = 1;
      break;

    case 0xC0EC:                                                                // Shift Data Latch
      if (disk[curDrv].writeMode)                                               // writting
        disk[curDrv].data[disk[curDrv].track*0x1A00+disk[curDrv].nibble]=dLatch;
      else                                                                      // reading
        dLatch=disk[curDrv].data[disk[curDrv].track*0x1A00+disk[curDrv].nibble];
      disk[curDrv].nibble = (disk[curDrv].nibble + 1) % 0x1A00;                 // turn floppy
      return(dLatch);

    case 0xC0ED: dLatch = value; break;                                         // Load Data Latch

    case 0xC0EE:                                                                // latch for READ
      disk[curDrv].writeMode = false;
      return(disk[curDrv].readOnly ? 0x80 : 0);                                 // check protection

    case 0xC0EF: disk[curDrv].writeMode = true; break;                          // latch for WRITE

    // default: printf("Uncaught Soft Switch access at %04X\n", address);
  }

  return(0);                                                                    // catch all
}



//========================================================== PROGRAM ENTRY POINT

int main(int argc, char *argv[]){

  // SDL INITIALIZATION

  int zoom = 2;
  const double frameDelay = 1000.0 / 60.0;                                      // targeting 60 FPS
  double fps = 60;
  Uint32 frameStart = 0, frameTime = 0, frame = 0;
  uint8_t tries = 0;                                                            // disk ][ speed-up
  SDL_Event event;
  SDL_bool paused = false, running = true, ctrl, shift, alt;

  if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[DEBUG] > %s", SDL_GetError());
    return(-1);
  }

  SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

  SDL_Window *wdo = SDL_CreateWindow("reinette II+", SDL_WINDOWPOS_CENTERED, \
                 SDL_WINDOWPOS_CENTERED, 280*zoom, 192*zoom, SDL_WINDOW_OPENGL);

  SDL_Renderer *rdr = SDL_CreateRenderer(wdo, -1, SDL_RENDERER_ACCELERATED);    // | SDL_RENDERER_PRESENTVSYNC);
  SDL_SetRenderDrawBlendMode(rdr, SDL_BLENDMODE_NONE);                          // SDL_BLENDMODE_BLEND);
  SDL_RenderSetScale(rdr, zoom, zoom);


  // SDL AUDIO INITIALIZATION

  SDL_AudioSpec desired = {96000, AUDIO_S8, 1, 0, 4096, 0, 0, NULL, NULL};
  audioDevice = SDL_OpenAudioDevice(NULL, 0, &desired, NULL, SDL_FALSE);
  SDL_PauseAudioDevice(audioDevice, muted);

  for (int i=1; i<audioBufferSize; i++){                                        // two audio buffers,
    audioBuffer[true][i]  =  120;                                               // one used when SPKR is true
    audioBuffer[false][i] = -120;                                               // the other when SPKR is false
  }

  // LOAD NORMAL AND REVERSE CHARACTERS BITMAPS

  SDL_Surface *tmpSurface = SDL_LoadBMP("assets/font-normal.bmp");              // load the normal font
  SDL_Texture *normCharTexture = SDL_CreateTextureFromSurface(rdr, tmpSurface);
  SDL_FreeSurface(tmpSurface);

  tmpSurface = SDL_LoadBMP("assets/font-reverse.bmp");                          // load the reverse font
  SDL_Texture *revCharTexture = SDL_CreateTextureFromSurface(rdr, tmpSurface);
  SDL_FreeSurface(tmpSurface);


  // VARIABLES USED IN THE VIDEO PRODUCTION

  uint16_t vRamBase = 0x0400;                                                   // can be $400, $800, $2000 or $4000
  uint16_t previousDots[192][40] = {0};                                         // check which Hi-Res 7 dots needs redraw
  uint8_t previousBit[192][40] = {0};                                           // the last bit value of the byte before.
  uint8_t lineLimit;
  uint8_t glyph;                                                                // a TEXT character, or 2 blocks in GR
  uint8_t colorIdx = 0;                                                         // to index the color arrays
  bool monochrome = false;
  enum characterAttribute {A_NORMAL, A_INVERSE, A_FLASH} glyphAttr;             // character attribute in TEXT

  SDL_Rect drvRect[2] = { {272, 188, 4, 4}, {276, 188, 4, 4} };                 // disk drive status squares
  SDL_Rect pixelGR = {0, 0, 7, 4};                                              // a block in LoRes
  SDL_Rect dstRect = {0, 0, 7, 8};                                              // the dst character in rdr
  SDL_Rect charRects[128];                                                      // the src from the norm and rev textures
  for (int c=0; c<128; c++){                                                    // index of the array = ascii code
    charRects[c].x = 7 * c;
    charRects[c].y = 0;
    charRects[c].w = 7;
    charRects[c].h = 8;
  }

  const int color[16][3] = {                                                    // the 16 low res colors
    {  0,   0,   0}, {226,  57,  86}, { 28, 116,205}, {126, 110, 173},
    { 31, 129, 128}, {137, 130, 122}, { 86, 168,228}, {144, 178, 223},
    {151,  88,  34}, {234, 108,  21}, {158, 151,143}, {255, 206, 240},
    {144, 192,  49}, {255, 253, 166}, {159, 210,213}, {255, 255, 255}};

  const int hcolor[16][3] = {                                                   // the high res colors (2 light levels)
    {  0,   0,   0}, {144, 192,  49}, {126, 110, 173}, {255, 255, 255},
    {  0,   0,   0}, {234, 108,  21}, { 86, 168, 228}, {255, 255, 255},
    {  0,   0,   0}, { 63,  55,  86}, { 72,  96,  25}, {255, 255, 255},
    {  0,   0,   0}, { 43,  84, 114}, {117,  54,  10}, {255, 255, 255}};

  const int offsetGR[24] = {                                                    // helper for TEXT and GR video generation
    0x000, 0x080, 0x100, 0x180, 0x200, 0x280, 0x300, 0x380,                     // lines 0-7
    0x028, 0x0A8, 0x128, 0x1A8, 0x228, 0x2A8, 0x328, 0x3A8,                     // lines 8-15
    0x050, 0x0D0, 0x150, 0x1D0, 0x250, 0x2D0, 0x350, 0x3D0};                    // lines 16-23

  const int offsetHGR[192] = {                                                  // helper for HGR video generation
    0x0000, 0x0400, 0x0800, 0x0C00, 0x1000, 0x1400, 0x1800, 0x1C00,             // 0-7
    0x0080, 0x0480, 0x0880, 0x0C80, 0x1080, 0x1480, 0x1880, 0x1C80,             // 8-15
    0x0100, 0x0500, 0x0900, 0x0D00, 0x1100, 0x1500, 0x1900, 0x1D00,             // 16-23
    0x0180, 0x0580, 0x0980, 0x0D80, 0x1180, 0x1580, 0x1980, 0x1D80,
    0x0200, 0x0600, 0x0A00, 0x0E00, 0x1200, 0x1600, 0x1A00, 0x1E00,
    0x0280, 0x0680, 0x0A80, 0x0E80, 0x1280, 0x1680, 0x1A80, 0x1E80,
    0x0300, 0x0700, 0x0B00, 0x0F00, 0x1300, 0x1700, 0x1B00, 0x1F00,
    0x0380, 0x0780, 0x0B80, 0x0F80, 0x1380, 0x1780, 0x1B80, 0x1F80,
    0x0028, 0x0428, 0x0828, 0x0C28, 0x1028, 0x1428, 0x1828, 0x1C28,
    0x00A8, 0x04A8, 0x08A8, 0x0CA8, 0x10A8, 0x14A8, 0x18A8, 0x1CA8,
    0x0128, 0x0528, 0x0928, 0x0D28, 0x1128, 0x1528, 0x1928, 0x1D28,
    0x01A8, 0x05A8, 0x09A8, 0x0DA8, 0x11A8, 0x15A8, 0x19A8, 0x1DA8,
    0x0228, 0x0628, 0x0A28, 0x0E28, 0x1228, 0x1628, 0x1A28, 0x1E28,
    0x02A8, 0x06A8, 0x0AA8, 0x0EA8, 0x12A8, 0x16A8, 0x1AA8, 0x1EA8,
    0x0328, 0x0728, 0x0B28, 0x0F28, 0x1328, 0x1728, 0x1B28, 0x1F28,
    0x03A8, 0x07A8, 0x0BA8, 0x0FA8, 0x13A8, 0x17A8, 0x1BA8, 0x1FA8,
    0x0050, 0x0450, 0x0850, 0x0C50, 0x1050, 0x1450, 0x1850, 0x1C50,
    0x00D0, 0x04D0, 0x08D0, 0x0CD0, 0x10D0, 0x14D0, 0x18D0, 0x1CD0,
    0x0150, 0x0550, 0x0950, 0x0D50, 0x1150, 0x1550, 0x1950, 0x1D50,
    0x01D0, 0x05D0, 0x09D0, 0x0DD0, 0x11D0, 0x15D0, 0x19D0, 0x1DD0,
    0x0250, 0x0650, 0x0A50, 0x0E50, 0x1250, 0x1650, 0x1A50, 0x1E50,
    0x02D0, 0x06D0, 0x0AD0, 0x0ED0, 0x12D0, 0x16D0, 0x1AD0, 0x1ED0,             // 168-183
    0x0350, 0x0750, 0x0B50, 0x0F50, 0x1350, 0x1750, 0x1B50, 0x1F50,             // 176-183
    0x03D0, 0x07D0, 0x0BD0, 0x0FD0, 0x13D0, 0x17D0, 0x1BD0, 0x1FD0};            // 184-191


    // VM INITIALIZATION

    FILE *f = fopen("rom/appleII+.rom", "rb");                                  // load the Apple II+ ROM
    if (!f){
      printf("Could not open appleII+.rom\n");                                  // file not found
      return(1);                                                                // exit
    }
    if (fread(rom, 1, ROMSIZE, f) != ROMSIZE){                                  // the file is too small
      printf("appleII+.rom should be 12 KB\n");
      return(1);                                                                // exit
    }
    fclose(f);

    f = fopen("rom/diskII.rom", "rb");                                          // load the P5A disk ][ PROM
    if (!f){
      printf("Could not open diskII.rom\n");                                    // file not found
      return(1);                                                                // exit
    }
    if (fread(slot6, 1, 256, f) != 256){                                        // file too small
      printf("diskII.rom should be 256 Bytes\n");
      return(1);                                                                // exit
    }
    fclose(f);

    if (argc > 1) insertFloppy(wdo, argv[1], 0);                                // load .nib in parameter into drive 0

    puce6502Reset();                                                            // reset the 6502


  //================================================================== MAIN LOOP

  while (running){

    frameStart = SDL_GetTicks();                                                // start of a new frame

    if (!paused){
      puce6502Exec((long long int)(1023000.0 / fps));                           // using actualized frame rate
      while (disk[curDrv].motorOn && ++tries)                                   // until motor is off or i reaches 255+1=0
        puce6502Exec(10000);                                                    // artificial drive speed up
    }


    //=============================================================== USER INPUT

    while (SDL_PollEvent(&event)){

      alt   = SDL_GetModState() & KMOD_ALT   ? true : false;
      ctrl  = SDL_GetModState() & KMOD_CTRL  ? true : false;
      shift = SDL_GetModState() & KMOD_SHIFT ? true : false;
      PB0   = alt   ? 0xFF : 0x00;                                              // update push button 0
      PB1   = ctrl  ? 0xFF : 0x00;                                              // update push button 1
      PB2   = shift ? 0xFF : 0x00;                                              // update push button 2

      if (event.type == SDL_QUIT) running = false;                              // WM sent TERM signal

      // if (event.type == SDL_WINDOWEVENT){                                       // pause if the window loses focus
      //   if(event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
      //     paused = true;
      // if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
      //   paused = false;
      // }

      if (event.type == SDL_DROPFILE){                                          // user dropped a file
        char* filename = event.drop.file;                                       // get full pathname
        insertFloppy(wdo, filename, alt);                                       // if ALT : drv 1 else drv 0
        SDL_free(filename);                                                     // free filename memory
        paused = false;                                                         // might already be the case
        if (!(alt || ctrl))                                                     // unless ALT or CTRL were
          puce6502Goto(0xC600);                                                 // pressed, force reboot
      }

      if (event.type == SDL_KEYDOWN)                                            // a key has been pressed
        switch (event.key.keysym.sym){

          // EMULATOR CONTROL :

          case SDLK_F1: saveFloppy(0); break;                                   // save disk 0 back to host
          case SDLK_F2: saveFloppy(1); break;                                   // save disk 1 back to host

          case SDLK_F3: if ((zoom-=2) < 0) zoom = 0;                            // zoom out
          case SDLK_F4: if (++zoom > 8) zoom = 8;                               // zoom in
            SDL_SetWindowSize(wdo, 280 * zoom, 192 * zoom);
            SDL_RenderSetScale(rdr, zoom, zoom);
            break;

          case SDLK_F5: if (GCReleaseSpeed > 1  ) GCReleaseSpeed -= 2; break;  // PDL Trim
          case SDLK_F6: if (GCReleaseSpeed < 127) GCReleaseSpeed += 2; break;  // PDL Trim

          case SDLK_F7:                                                         // paste txt from clipboard
          if (SDL_HasClipboardText()){
            char *clipboardText = SDL_GetClipboardText();
            int c = 0;
            while (clipboardText[c]){                                           // all chars until ascii NUL
              KBD = clipboardText[c++] | 0x80;                                  // set bit7
              if (KBD == 0x8A) KBD = 0x8D;                                      // Line Feed to Carriage Ret
              puce6502Exec(400000);                                             // to process each char
            }
            SDL_free(clipboardText);
          }
          break;

          case SDLK_F8:  muted      = !muted;      break;                       // mute
          case SDLK_F9:  monochrome = !monochrome; break;                       // ...
          case SDLK_F10: paused     = !paused;     break;                       // pause / un-pause
          case SDLK_F12: running    = false;       break;                       // goodbye

          case SDLK_F11:                                                        // reset
            if (ctrl)
              puce6502Break();
            else {
              puce6502Reset();
              softSwitches(0xC0E9,0);                                           // drive0En
              softSwitches(0xC0E8,0);                                           // motorOff
            }
            break;

          // EMULATED KEYS :

          case SDLK_a:            KBD = ctrl ? 0x81: 0xC1;   break;             // a
          case SDLK_b:            KBD = ctrl ? 0x82: 0xC2;   break;             // b STX
          case SDLK_c:            KBD = ctrl ? 0x83: 0xC3;   break;             // c ETX
          case SDLK_d:            KBD = ctrl ? 0x84: 0xC4;   break;             // d EOT
          case SDLK_e:            KBD = ctrl ? 0x85: 0xC5;   break;             // e
          case SDLK_f:            KBD = ctrl ? 0x86: 0xC6;   break;             // f ACK
          case SDLK_g:            KBD = ctrl ? 0x87: 0xC7;   break;             // g BELL
          case SDLK_h:            KBD = ctrl ? 0x88: 0xC8;   break;             // h BS
          case SDLK_i:            KBD = ctrl ? 0x89: 0xC9;   break;             // i HTAB
          case SDLK_j:            KBD = ctrl ? 0x8A: 0xCA;   break;             // j LF
          case SDLK_k:            KBD = ctrl ? 0x8B: 0xCB;   break;             // k VTAB
          case SDLK_l:            KBD = ctrl ? 0x8C: 0xCC;   break;             // l FF
          case SDLK_m:            KBD = ctrl ? 0x8D: 0xCD;   break;             // m CR
          case SDLK_n:            KBD = ctrl ? 0x8E: 0xCE;   break;             // n
          case SDLK_o:            KBD = ctrl ? 0x8F: 0xCF;   break;             // o
          case SDLK_p:            KBD = ctrl ? 0x90: 0xD0;   break;             // p
          case SDLK_q:            KBD = ctrl ? 0x91: 0xD1;   break;             // q
          case SDLK_r:            KBD = ctrl ? 0x92: 0xD2;   break;             // r
          case SDLK_s:            KBD = ctrl ? 0x93: 0xD3;   break;             // s ESC
          case SDLK_t:            KBD = ctrl ? 0x94: 0xD4;   break;             // t
          case SDLK_u:            KBD = ctrl ? 0x95: 0xD5;   break;             // u NAK
          case SDLK_v:            KBD = ctrl ? 0x96: 0xD6;   break;             // v
          case SDLK_w:            KBD = ctrl ? 0x97: 0xD7;   break;             // w
          case SDLK_x:            KBD = ctrl ? 0x98: 0xD8;   break;             // x CANCEL
          case SDLK_y:            KBD = ctrl ? 0x99: 0xD9;   break;             // y
          case SDLK_z:            KBD = ctrl ? 0x9A: 0xDA;   break;             // z
          case SDLK_RETURN:       KBD = ctrl ? 0x8A: 0x8D;   break;             // LF CR
          case SDLK_LEFTBRACKET:  KBD = ctrl ? 0x9B: 0xDB;   break;             // [ {
          case SDLK_BACKSLASH:    KBD = ctrl ? 0x9C: 0xDC;   break;             // \ |
          case SDLK_RIGHTBRACKET: KBD = ctrl ? 0x9D: 0xDD;   break;             // ] }
          case SDLK_BACKSPACE:    KBD = ctrl ? 0xDF: 0x88;   break;             // BS
          case SDLK_0:            KBD = shift? 0xA9: 0xB0;   break;             // 0 )
          case SDLK_1:            KBD = shift? 0xA1: 0xB1;   break;             // 1 !
          case SDLK_2:            KBD = shift? 0xC0: 0xB2;   break;             // 2
          case SDLK_3:            KBD = shift? 0xA3: 0xB3;   break;             // 3 #
          case SDLK_4:            KBD = shift? 0xA4: 0xB4;   break;             // 4 $
          case SDLK_5:            KBD = shift? 0xA5: 0xB5;   break;             // 5 %
          case SDLK_6:            KBD = shift? 0xDE: 0xB6;   break;             // 6 ^
          case SDLK_7:            KBD = shift? 0xA6: 0xB7;   break;             // 7 &
          case SDLK_8:            KBD = shift? 0xAA: 0xB8;   break;             // 8 *
          case SDLK_9:            KBD = shift? 0xA8: 0xB9;   break;             // 9 (
          case SDLK_QUOTE:        KBD = shift? 0xA2: 0xA7;   break;             // ' "
          case SDLK_EQUALS:       KBD = shift? 0xAB: 0xBD;   break;             // = +
          case SDLK_SEMICOLON:    KBD = shift? 0xBA: 0xBB;   break;             // ; :
          case SDLK_COMMA:        KBD = shift? 0xBC: 0xAC;   break;             // , <
          case SDLK_PERIOD:       KBD = shift? 0xBE: 0xAE;   break;             // . >
          case SDLK_SLASH:        KBD = shift? 0xBF: 0xAF;   break;             // / ?
          case SDLK_MINUS:        KBD = shift? 0xDF: 0xAD;   break;             // - _
          case SDLK_BACKQUOTE:    KBD = shift? 0xFE: 0xE0;   break;             // ` ~
          case SDLK_LEFT:         KBD = 0x88;                break;             // BS
          case SDLK_RIGHT:        KBD = 0x95;                break;             // NAK
          case SDLK_SPACE:        KBD = 0xA0;                break;
          case SDLK_ESCAPE:       KBD = 0x9B;                break;

          // EMULATED JOYSTICK :

          case SDLK_KP_1:         GCD[0] = -1; GCA[0] = 1;   break;             // pdl0 <-
          case SDLK_KP_3:         GCD[0] =  1; GCA[0] = 1;   break;             // pdl0 ->
          case SDLK_KP_5:         GCD[1] = -1; GCA[1] = 1;   break;             // pdl1 <-
          case SDLK_KP_2:         GCD[1] =  1; GCA[1] = 1;   break;             // pdl1 ->
        }

      if (event.type == SDL_KEYUP)
        switch (event.key.keysym.sym){
          case SDLK_KP_1:         GCD[0] =  1; GCA[0] = 0;   break;             // pdl0 ->
          case SDLK_KP_3:         GCD[0] = -1; GCA[0] = 0;   break;             // pdl0 <-
          case SDLK_KP_5:         GCD[1] =  1; GCA[1] = 0;   break;             // pdl1 ->
          case SDLK_KP_2:         GCD[1] = -1; GCA[1] = 0;   break;             // pdl1 <-
        }
    }

    for (int pdl=0; pdl<2; pdl++){                                              // update the two paddles positions
      if (GCA[pdl]) {                                                           // actively pushing the stick
        GCP[pdl] += GCD[pdl] * GCActionSpeed;
        if (GCP[pdl] > 255) GCP[pdl] = 255;
        if (GCP[pdl] < 0  ) GCP[pdl] = 0;
      }
      else {                                                                    // the stick is return back to center
        GCP[pdl] += GCD[pdl] * GCReleaseSpeed;
        if (GCD[pdl] ==  1 && GCP[pdl] > 127) GCP[pdl] = 127;
        if (GCD[pdl] == -1 && GCP[pdl] < 127) GCP[pdl] = 127;
      }
    }

    //============================================================= VIDEO OUTPUT

    // HIGH RES GRAPHICS

    if (!TEXT && HIRES){
      uint16_t word;
      uint8_t bits[16], bit, pbit, colorSet, even;
      vRamBase = PAGE * 0x2000;                                                 // PAGE is 1 or 2
      lineLimit = MIXED ? 160 : 192;

      for (int line=0; line<lineLimit; line++){                                 // for every line
        for (int col=0; col<40; col += 2){                                      // for every 7 horizontal dots
          int x = col * 7;
          even = 0;

          word = (uint16_t)(ram[ vRamBase + offsetHGR[line] + col + 1 ]) << 8;  // store the two next bytes into 'word'
          word +=           ram[ vRamBase + offsetHGR[line] + col ];            // in reverse order

          if (previousDots[line][col] != word || !frame){                       // check if this group of 7 dots need a redraw
                                                                                // or refresh the full screen every 1/2 second
            for (bit=0; bit<16; bit++)                                          // store all bits 'word' into 'bits'
              bits[bit] = (word >> bit) & 1;
            colorSet = bits[7] * 4;                                             // select the right color set
            pbit = previousBit[line][col];                                      // the bit value of the left dot
            bit = 0;                                                            // starting at 1st bit of 1st byte

            while (bit < 15){                                                   // until we reach bit7 of 2nd byte
              if (bit == 7){                                                    // moving into the second byte
                colorSet = bits[15] * 4;                                        // update the color set
                bit++;                                                          // skip bit 7
              }
              if (monochrome)
                colorIdx = bits[bit] * 3;                                       // black if bit==0, white if bit==1
              else
                colorIdx = even + colorSet + (bits[bit] << 1) + (pbit);
              SDL_SetRenderDrawColor(rdr, hcolor[colorIdx][0], \
                hcolor[colorIdx][1], hcolor[colorIdx][2], SDL_ALPHA_OPAQUE);
              SDL_RenderDrawPoint(rdr, x++, line);
              pbit = bits[bit++];                                               // proceed to the next pixel
              even = even ? 0 : 8;                                              // one pixel every two is darker
            }

            previousDots[line][col] = word;                                     // update the video cache
            if ((col < 37) && (previousBit[line][col + 2] != pbit)){            // check color franging effect on the dot after
              previousBit[line][col + 2] = pbit;                                // set pbit and clear the
              previousDots[line][col + 2] = -1;                                 // video cache for next dot
            }
          }  // if (previousDots[line][col] ...
        }
      }
    }

    // lOW RES GRAPHICS

    else if (!TEXT){                                                            // and not in HIRES
      vRamBase = PAGE * 0x0400;
      lineLimit = MIXED ? 20 : 24;

      for (int col=0; col<40; col++){                                           // for each column
        pixelGR.x = col * 7;
        for (int line=0; line<lineLimit; line++){                               // for each row
          pixelGR.y = line * 8;                                                 // first block

          glyph = ram[vRamBase + offsetGR[line] + col];                         // read video memory

          colorIdx = glyph & 0x0F;                                              // first nibble
          SDL_SetRenderDrawColor(rdr, color[colorIdx][0], \
              color[colorIdx][1], color[colorIdx][2], SDL_ALPHA_OPAQUE);
          SDL_RenderFillRect(rdr, &pixelGR);

          pixelGR.y += 4;                                                       // second block
          colorIdx = (glyph & 0xF0) >> 4;                                       // second nibble
          SDL_SetRenderDrawColor(rdr, color[colorIdx][0], \
              color[colorIdx][1], color[colorIdx][2], SDL_ALPHA_OPAQUE);
          SDL_RenderFillRect(rdr, &pixelGR);
        }
      }
    }

    // TEXT 40 COLUMNS

    if (TEXT || MIXED){
      vRamBase = PAGE * 0x0400;
      lineLimit = TEXT ? 0 : 20;

      for (int col=0; col<40; col++){                                           // for each column
        dstRect.x = col * 7;
        for (int line=lineLimit; line<24; line++){                              // for each row
          dstRect.y = line * 8;

          glyph = ram[vRamBase + offsetGR[line] + col];                         // read video memory

          if (glyph > 0x7F) glyphAttr = A_NORMAL;                               // is NORMAL ?
          else if (glyph < 0x40) glyphAttr = A_INVERSE;                         // is INVERSE ?
          else glyphAttr = A_FLASH;                                             // it's FLASH !

          glyph &= 0x7F;                                                        // unset bit 7

          if (glyph > 0x5F) glyph &= 0x3F;                                      // shifts to match
          if (glyph < 0x20) glyph |= 0x40;                                      // the ASCII codes

          if (glyphAttr == A_NORMAL || (glyphAttr == A_FLASH && frame < 15))
            SDL_RenderCopy(rdr, normCharTexture, &charRects[glyph], &dstRect);
          else
            SDL_RenderCopy(rdr, revCharTexture,  &charRects[glyph], &dstRect);
        }
      }
    }

    //====================================================== DISPLAY DISK STATUS

    if (disk[curDrv].motorOn){                                                  // drive is active
      if (disk[curDrv].writeMode)
        SDL_SetRenderDrawColor(rdr, 255, 0, 0, 85);                             // red for writes
      else
        SDL_SetRenderDrawColor(rdr, 0, 255, 0, 85);                             // green for reads

      SDL_RenderFillRect(rdr, &drvRect[curDrv]);                                // square actually
    }

    //========================================================= SDL RENDER FRAME

    if (++frame > 30) frame = 0;

    frameTime = SDL_GetTicks() - frameStart;                                    // frame duration
    if (frameTime < frameDelay) {                                               // do we have time ?
      SDL_Delay(frameDelay - frameTime);                                        // wait 'vsync'
      SDL_RenderPresent(rdr);                                                   // swap buffers
      frameTime = SDL_GetTicks() - frameStart;                                  // update frameTime
    }
    fps = (frameDelay / (double)frameTime) * 60.0;                              // calculate the actual frame rate

  }  // while (running)

  //================================================ RELEASE RESSOURSES AND EXIT

  SDL_AudioQuit();
  SDL_Quit();
  return(0);
}
