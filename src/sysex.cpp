#include <Arduino.h>
#include <EEPROM.h>
#include "megafm.h"

#include "midi.h"
#include "nrpn.h"
#include "leds.h"
#include "setters.h"
#include "sysex.h"

int sysExDataIndex = 0;
byte sysExData[MAX_SYSEX_DATA_LENGTH];

static void sysexWriteNRPN(int msg, int val) {
	Serial.write(msg >> 7);   // CC 99 for NRPN parameter MSB
	Serial.write(msg & 0x7F); // CC 98 for NRPN parameter LSB
	Serial.write(val >> 7);   // CC 6 for NRPN data MSB
	Serial.write(val & 0x7F); // CC 38 for NRPN data LSB
}

void sysExReset() { sysExDataIndex = 0; }

void sysExAppendByte(byte b) {
	if (sysExDataIndex < MAX_SYSEX_DATA_LENGTH) {
		sysExData[sysExDataIndex++] = b;
	} else {
		sysExExitStatus(SYSEX_STATUS_BYTE_ERROR);
	}
}

void sysExExitStatus(byte error) {
	resetMidiReadStatus();
	sysExDataIndex = 0;
	digit(0, 5);     // S(ysEx)
	digit(1, error); // error code, 0=ok, 1=byte error, 2=header mismatch, 3=length error, 4=dump length error
	lastNumber = -1;
	showPresetNumberTimeout = 12000;
}

void handleIncomingSysEx() {
	// Handle the received SysEx data in sysExData array with length sysExDataIndex
	//
	// 240 0 33 68 [command] [payload bytes...] 247
	//
	// That 0 33 68 is the manufacturer ID (hex 00 21 44) has already
	// been checked in midi.cpp before calling this function

	// Need at least start and end byte, 3 manufacturer, 1 command
	if (sysExDataIndex < 6) {
		sysExExitStatus(SYSEX_STATUS_LENGTH_ERROR);
		digit(0, sysExDataIndex);
		return;
	}

	// check if it's a SysEx message for us ( - we do the same as the firmware and take the
	// default from hex2sys = \x00\x21\x44 = 00 33 68)
	if (sysExData[1] != 0 || sysExData[2] != 33 || sysExData[3] != 68) {
		// not for us, ignore the rest of the message
		sysExExitStatus(SYSEX_STATUS_HEADER_MISMATCH);
		return;
	}

	if (sysExData[4] == 91) {
		if ((sysExDataIndex - 6) % 4 != 0) {
			// Invalid preset dump (NRPN messages are 4 bytes each)
			sysExExitStatus(SYSEX_STATUS_DUMP_LENGTH_ERROR);
			return;
		}

		for (int i = 5; i < sysExDataIndex - 1; i += 4) {
			// Decode NRPN from byte MSB i and LSB i+1
			int nrpn = (sysExData[i] << 7) | sysExData[i + 1];
			// Decode value from byte MSB i+3 and LSB i+2
			int value = (sysExData[i + 2] << 7) | sysExData[i + 3];
			handleNRPN(nrpn, value);
		}

		sysExExitStatus(SYSEX_STATUS_OK);
	} else {
		// Unknown command, ignore
		sysExExitStatus(SYSEX_STATUS_UNKNOWN_COMMAND);
	}
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

	// SysEx header: F0 00 21 44 [command=92 = preset dump]
	Serial.write(0xF0);
	Serial.write(0);
	Serial.write(33);
	Serial.write(68);
	Serial.write(92);

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
	sysexWriteNRPN(300, arpMode);        // arp mode (0-7)
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
			// Exclude operator rate scaling (3/12/21/30) and unused pot/fmBase 45
			if ((targetPot != 3) && (targetPot != 12) && (targetPot != 21) && (targetPot != 30) && (targetPot != 45))
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

	Serial.write(0xF7); // SysEx end

	digit(0, 5); // S(ysEx)
	digit(1, 0); // O(ut)
	lastNumber = -1;
	showPresetNumberTimeout = 12000;
}
