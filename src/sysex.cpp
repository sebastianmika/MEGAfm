#include <Arduino.h>
#include <EEPROM.h>
#include "megafm.h"

#include "midi.h"
#include "nrpn.h"
#include "leds.h"
#include "setters.h"
#include "sysex.h"

#define MAX_SYSEX_DATA_LENGTH 2048
int sysExDataIndex = 0;
byte sysExData[MAX_SYSEX_DATA_LENGTH];

// Helpers for streaming 8-bit payload bytes into the 7-bit SysEx encoding.
// Every 7 payload bytes are packed into 8 encoded bytes: 1 LSB byte + 7 high-7-bit bytes.
static byte sysexEncodeBuf[7];
static int sysexEncodeBufLen = 0;

static void sysexFlushBuf() {
	if (sysexEncodeBufLen == 0)
		return;
	byte lsb = 0;
	for (int j = 0; j < sysexEncodeBufLen; j++)
		lsb |= (sysexEncodeBuf[j] & 1) << j;
	Serial.write(lsb);
	for (int j = 0; j < sysexEncodeBufLen; j++)
		Serial.write(sysexEncodeBuf[j] >> 1);
	sysexEncodeBufLen = 0;
}

static void sysexWriteByte(byte b) {
	sysexEncodeBuf[sysexEncodeBufLen++] = b;
	if (sysexEncodeBufLen == 7)
		sysexFlushBuf();
}

// Encodes one NRPN message as 4 payload bytes matching the format handleSysExByte() expects:
//   byte 0: msg >> 7   (CC99: parameter MSB)
//   byte 1: msg & 0x7F (CC98: parameter LSB)
//   byte 2: val & 0x7F (CC38: data LSB)
//   byte 3: val >> 7   (CC6:  data MSB)
static void sysexWriteNRPN(int msg, int val) {
	sysexWriteByte(msg >> 7);
	sysexWriteByte(msg & 0x7F);
	sysexWriteByte(val & 0x7F);
	sysexWriteByte(val >> 7);
}

// State for NRPN messages embedded in sysex payload.
// Each message is 4 consecutive bytes: CC99 value, CC98 value, CC38 value, CC6 value.
static byte sysExNrpnState = 0;
static int sysExNrpnMsg = 0;
static int sysExNrpnData = 0;

static void resetSysExByteState() {
	sysExNrpnState = 0;
	sysExNrpnMsg = 0;
	sysExNrpnData = 0;
}

void handleSysExByte(byte command, byte b) {
	if (command == 91) {
		// Treat every 4 consecutive bytes as a NRPN message:
		//   byte 0: CC 99 value (NRPN parameter MSB)
		//   byte 1: CC 98 value (NRPN parameter LSB)
		//   byte 2: CC 38 value (NRPN data LSB)
		//   byte 3: CC  6 value (NRPN data MSB)
		switch (sysExNrpnState) {
			case 0: // CC 99: parameter MSB
				sysExNrpnMsg = b;
				sysExNrpnState = 1;
				break;
			case 1: // CC 98: parameter LSB
				sysExNrpnMsg = (sysExNrpnMsg << 7) | b;
				sysExNrpnState = 2;
				break;
			case 2: // CC 38: data LSB
				sysExNrpnData = b;
				sysExNrpnState = 3;
				break;
			case 3: // CC 6: data MSB
				sysExNrpnData = (b << 7) | sysExNrpnData;
				handleNRPN(sysExNrpnMsg, sysExNrpnData);
				resetSysExByteState();
				break;
		}
	}
}

void abortSysEx(bool error) {
	resetMidiReadStatus();
	sysExDataIndex = 0;
	resetSysExByteState();
	if (error) {
		digit(0, 20); // -
		digit(1, 20); // -
	}
	lastNumber = -1;
	showPresetNumberTimeout = 12000;
}

void handleSysEx() {
	// Handle the received SysEx data in sysExData array with length sysExDataIndex
	//
	// The data has to have our manufacturer ids in bytes 0-2 to be considered valid
	// (checked during receive). Layout of sysExData[]:
	//
	// [0]:    0   (manufacturer ID byte 1)
	// [1]:    33  (manufacturer ID byte 2)
	// [2]:    68  (manufacturer ID byte 3)
	// [3]:    command (e.g. 91 for preset dump)
	// [4]:    len MSB  \  14-bit count of payload bytes
	// [5]:    len LSB  /
	// [6]:    LSBs of block 0  (bit j = LSB of payload byte j)
	// [7-13]: upper 7 bits of payload bytes 0-6 (each shifted right by 1 during encode)
	// [14]:   LSBs of block 1
	// [15-21]: upper 7 bits of payload bytes 7-13
	// ...
	//
	// Decoding: byte[j] = (encoded[j] << 1) | ((lsb_bits >> j) & 1)

	resetMidiReadStatus();

	// Need at least 3 manufacturer + 1 command + 2 length bytes
	if (sysExDataIndex < 6) {
		sysExDataIndex = 0;
		return;
	}

	byte command = sysExData[3];
	int length = ((int)sysExData[4] << 7) | sysExData[5];

	if (command == 91) {
		if (length % 4 != 0) {
			// Invalid preset dump (NRPN messages are 4 bytes each)
			abortSysEx(true);
			return;
		} else {
			digit(0, 5); // S for "SysEx"
			digit(1, 0); // Step 0
		}
	}

	int offset = 6;

	resetSysExByteState();

	for (int i = 0; i < length;) {
		if (offset >= sysExDataIndex)
			break;
		byte lsb_bits = sysExData[offset++];
		for (int j = 0; j < 7 && i < length; j++, i++) {
			if (offset >= sysExDataIndex)
				break;
			byte b = (sysExData[offset++] << 1) | ((lsb_bits >> j) & 1);
			handleSysExByte(command, b);
		}
		// Show progress in digit 1 as %X0 of length bytes processed (10-90)
		int progress = i / length * 10;
		digit(1, progress);
	}

	lastNumber = -1;
	showPresetNumberTimeout = 12000;
	sysExDataIndex = 0;
}

void dumpPresetAsSysEx() {
	if (thru)
		return;

	// Total NRPN count breakdown:
	//   15  LFO settings (shape/looping/retrig/clock/vel-mod-at)
	//   13  Knob parameters (fine, glide, LFO rates/depths, fat, volume, feedback, algo, notePriority)
	//   10  Global settings (brightness, thru, pickup, stereoCh3, mpe, fatSpread, ignoreVol, fatMode, voiceMode,
	//   octOffset)
	//    4  Arp (mode, clock, rate, range)
	//    3  Vibrato (clock, rate, depth)
	//  135  LFO links (3 LFOs × 45 pots, skipping unused slots 3,12,21,23,30,45)
	//   40  Operators (4 ops × 10 params each)
	// ----
	//  220  total
	const int kNRPNCount = 220;
	const int payloadLen = kNRPNCount * 4;

	// SysEx header: F0 00 21 44 [command=91] [len_msb] [len_lsb]
	Serial.write(0xF0);
	Serial.write(0);
	Serial.write(33);
	Serial.write(68);
	Serial.write(91);
	Serial.write(payloadLen >> 7);
	Serial.write(payloadLen & 0x7F);

	sysexEncodeBufLen = 0;

	// LFO shapes (100-102)
	for (int i = 0; i < 3; i++) {
		byte shape = lfoShape[i];
		byte val;
		if (shape == kSquare)
			val = invertedSquare[i] ? 1 : 0;
		else if (shape == kTriangle)
			val = 2;
		else if (shape == kSaw)
			val = 3 + (invertedSaw[i] ? 1 : 0);
		else // kRandom
			val = 5 + (noiseTableLength[i] - 2);
		sysexWriteNRPN(100 + i, val);
	}

	// LFO looping (103-105), retrig (106-108), MIDI sync (109-111)
	for (int i = 0; i < 3; i++)
		sysexWriteNRPN(103 + i, looping[i]);
	for (int i = 0; i < 3; i++)
		sysexWriteNRPN(106 + i, retrig[i]);
	for (int i = 0; i < 3; i++)
		sysexWriteNRPN(109 + i, lfoClockEnable[i]);

	// LFO vel/mod/at (112-114)
	sysexWriteNRPN(112, lfoVel);
	sysexWriteNRPN(113, lfoMod);
	sysexWriteNRPN(114, lfoAt);

	// Fine tune (220) and Glide (221)
	// fine is 0-255; movedPot(KNOB_VOLUME, fine) with voiceHeld restores it directly.
	// glide is 0-15; movedPot(KNOB_FAT, glide<<4) with voiceHeld restores it (glide = data>>4).
	sysexWriteNRPN(220, fine);
	sysexWriteNRPN(221, glide << 4);

	// LFO rates and depths
	sysexWriteNRPN(222, fmBase[36]); // LFO1 rate
	sysexWriteNRPN(223, fmBase[38]); // LFO2 rate
	sysexWriteNRPN(224, fmBase[40]); // LFO3 rate
	sysexWriteNRPN(225, fmBase[37]); // LFO1 depth
	sysexWriteNRPN(226, fmBase[39]); // LFO2 depth
	sysexWriteNRPN(227, fmBase[41]); // LFO3 depth

	// Fat, Volume, Feedback, Algorithm
	// volume: movedPot(KNOB_VOLUME, data) stores lastVol = 128-(data>>1), so data = (128-lastVol)<<1
	sysexWriteNRPN(228, fmBase[50]);
	sysexWriteNRPN(229, (128 - lastVol) << 1);
	sysexWriteNRPN(230, fmBase[43]);
	sysexWriteNRPN(231, fmBase[42]);

	// Note priority
	sysexWriteNRPN(232, notePriority);

	// Global settings
	sysexWriteNRPN(200, EEPROM.read(3965)); // brightness (0-15)
	sysexWriteNRPN(201, thru);              // MIDI thru
	sysexWriteNRPN(202, pickupMode);        // pickup mode
	sysexWriteNRPN(203, stereoCh3);         // stereo ch3
	sysexWriteNRPN(204, mpe);               // MPE mode
	sysexWriteNRPN(205, fatSpreadMode);     // fat spread mode
	sysexWriteNRPN(206, ignoreVolume);      // ignore preset volume
	// fatMode: FAT_MODE_SEMITONE=false, FAT_MODE_OCTAVE=true.
	// NRPN 207 handler: bool_val=true → semitone, bool_val=false → octave. So send !fatMode.
	sysexWriteNRPN(207, !fatMode);
	sysexWriteNRPN(208, (byte)voiceMode); // voice mode (0-5)
	sysexWriteNRPN(209, octOffset);       // octave offset (0-3)

	// Arp
	sysexWriteNRPN(300, (byte)arpMode);  // arp mode (0-7)
	sysexWriteNRPN(301, arpClockEnable); // arp MIDI clock sync
	sysexWriteNRPN(302, fmBase[46]);     // arp rate
	sysexWriteNRPN(303, fmBase[47]);     // arp range

	// Vibrato
	sysexWriteNRPN(500, vibratoClockEnable); // vibrato MIDI clock sync
	sysexWriteNRPN(501, fmBase[48]);         // vibrato rate
	sysexWriteNRPN(502, fmBase[49]);         // vibrato depth

	// LFO links (1000-1002): one NRPN per (lfo, targetPot), value = (targetPot<<1)|linked
	for (int i = 0; i < 3; i++) {
		for (int targetPot = 0; targetPot < 51; targetPot++) {
			if ((targetPot != 3) && (targetPot != 12) && (targetPot != 21) && (targetPot != 23) && (targetPot != 30) &&
			    (targetPot != 45))
				sysexWriteNRPN(1000 + i, (targetPot << 1) | linked[i][targetPot]);
		}
	}

	// Operator 1 (2000-2009)
	sysexWriteNRPN(2000, fmBase[0]);                  // detune
	sysexWriteNRPN(2001, fmBase[1]);                  // multiple
	sysexWriteNRPN(2002, fmBase[2]);                  // level
	sysexWriteNRPN(2003, fmBase[4]);                  // attack
	sysexWriteNRPN(2004, fmBase[5]);                  // decay
	sysexWriteNRPN(2005, fmBase[7]);                  // sustain
	sysexWriteNRPN(2006, fmBase[6]);                  // sustain rate
	sysexWriteNRPN(2007, fmBase[8]);                  // release
	sysexWriteNRPN(2008, getOperatorEnvelopeMode(0)); // envelope mode (0-2)
	sysexWriteNRPN(2009, fmBase[3]);                  // rate scaling (raw: 0,64,128,192)

	// Operator 2 (3000-3009)
	sysexWriteNRPN(3000, fmBase[18]);                 // detune
	sysexWriteNRPN(3001, fmBase[19]);                 // multiple
	sysexWriteNRPN(3002, fmBase[20]);                 // level
	sysexWriteNRPN(3003, fmBase[22]);                 // attack
	sysexWriteNRPN(3004, fmBase[23]);                 // decay
	sysexWriteNRPN(3005, fmBase[25]);                 // sustain
	sysexWriteNRPN(3006, fmBase[24]);                 // sustain rate
	sysexWriteNRPN(3007, fmBase[26]);                 // release
	sysexWriteNRPN(3008, getOperatorEnvelopeMode(1)); // envelope mode (0-2)
	sysexWriteNRPN(3009, fmBase[12]);                 // rate scaling (raw: 0,64,128,192)

	// Operator 3 (4000-4009)
	sysexWriteNRPN(4000, fmBase[9]);                  // detune
	sysexWriteNRPN(4001, fmBase[10]);                 // multiple
	sysexWriteNRPN(4002, fmBase[11]);                 // level
	sysexWriteNRPN(4003, fmBase[13]);                 // attack
	sysexWriteNRPN(4004, fmBase[14]);                 // decay
	sysexWriteNRPN(4005, fmBase[16]);                 // sustain
	sysexWriteNRPN(4006, fmBase[15]);                 // sustain rate
	sysexWriteNRPN(4007, fmBase[17]);                 // release
	sysexWriteNRPN(4008, getOperatorEnvelopeMode(2)); // envelope mode (0-2)
	sysexWriteNRPN(4009, fmBase[21]);                 // rate scaling (raw: 0,64,128,192)

	// Operator 4 (5000-5009)
	sysexWriteNRPN(5000, fmBase[27]);                 // detune
	sysexWriteNRPN(5001, fmBase[28]);                 // multiple
	sysexWriteNRPN(5002, fmBase[29]);                 // level
	sysexWriteNRPN(5003, fmBase[31]);                 // attack
	sysexWriteNRPN(5004, fmBase[32]);                 // decay
	sysexWriteNRPN(5005, fmBase[34]);                 // sustain
	sysexWriteNRPN(5006, fmBase[33]);                 // sustain rate
	sysexWriteNRPN(5007, fmBase[35]);                 // release
	sysexWriteNRPN(5008, getOperatorEnvelopeMode(3)); // envelope mode (0-2)
	sysexWriteNRPN(5009, fmBase[30]);                 // rate scaling (raw: 0,64,128,192)

	sysexFlushBuf();
	Serial.write(0xF7); // SysEx end

	digit(0, 14); // P
	digit(1, 21); // blank
	lastNumber = -1;
	showPresetNumberTimeout = 12000;
}
