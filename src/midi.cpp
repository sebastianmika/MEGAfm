#include <Arduino.h>
#include <EEPROM.h>
#include "megafm.h"
#include "leds.h"
#include "arp.h"
#include "lfo.h"
#include "pitchEngine.h"
#include "preset.h"
#include "voice.h"
#include "midi.h"
#include "pots.h"
#include "buttons.h"
#include "midi_pedal.hpp"
#include "FM.h"
#include "ISR.h"

static byte voiceSlot;
static bool ch3Alt;
static const float vibIncrements[8] = {5.312, 7.968, 10.625, 14.166, 15.937, 31.875, 42.5, 63.75};
static float vibIndexF;

static bool firstCC[80];
static byte lastCC[80];

static int arpClockCounter;
static byte syncLfoCounter;

static bool arpClearFlag = false;

void initFirstCC() {
	for (int i = 0; i < 80; i++) {
		firstCC[i] = true;
	}
}

void resyncArpLfo() {
	if (arpClockEnable) {
		if (arpMidiSpeed != arpMidiSpeedPending || resyncArp) {
			arpMidiSpeed = arpMidiSpeedPending;
			arpClockCounter = 0;
			resyncArp = false;
		}
	}

	for (int i = 0; i < 3; i++) {
		if (lfoClockEnable[i]) {
			if (lfoClockSpeedPending[i] != lfoClockSpeedPendingLast[i]) {
				lfoClockSpeed[i] = lfoClockSpeedPending[i];
				lfoClockSpeedPendingLast[i] = lfoClockSpeedPending[i];
				lfoStepF[i] = 0;
			}
		}
	}
}

void handleAftertouch(byte channel, byte val) {
	leftDot();
	if (mpe) {
		if (channel > 1 && channel < 14) {
			channel -= 2;
			polyPressure[channel] = val << 1;
			lastMpeVoice = channel;
		}
	} else {

		if (channel == inputChannel) {
			if (lfoAt) {
				// MONO AT IS SET TO OVERIDE LFO3
				for (int i = 0; i < 12; i++) {
					polyPressure[i] = val << 1;
				}
			}
		}
	}
}

void handlePolyAT(byte channel, byte note, byte val) {

	leftDot();
	if (channel == inputChannel && lfoAt) {

		for (int i = 0; i < 12; i++) {
			if (notey[i] == note - 10) {
				polyPressure[i] = val << 1;
				lastMpeVoice = i;
				latestChannel = i;
			}
		}
	}
}

void handleClock() {
	if (sync) {
		////////////////////////////////////
		////////      VIBRATO        ///////
		////////////////////////////////////
		if (vibratoClockEnable) {
			if (fmData[48]) {
				vibIndexF += vibIncrements[fmData[48] >> 5];
				if (vibIndexF > 255) {
					vibIndexF -= 256;
				}
				vibIndex = int(vibIndexF);
			}
		}

		masterClockCounter++;
		if (masterClockCounter >= 48) {
			leftDot();
			masterClockCounter = 0;

			resyncArpLfo();
		}

		////////////////////////////////////
		////////        ARP          ///////
		///////////////////////////////////

		if ((arpClockEnable) && (arpMode) && (arpMode != 7)) {
			arpClockCounter++;
			if ((arpClockCounter >= kMidiArpTicks[arpMidiSpeed])) {
				arpClockCounter = 0;
				if (!emptyStack) {
					arpFire();
				}
			}
		}

		// if Any LFOClock Enable
		if ((lfoClockEnable[0]) || (lfoClockEnable[1]) || (lfoClockEnable[2])) {

			// activate the LFO
			lfoTick();

			syncLfoCounter++;
			if (syncLfoCounter == 24) {
				syncLfoCounter = 0;
			}

			for (int i = 0; i < 3; i++) {
				if (lfoClockEnable[i]) {
					lfoStepF[i] += kLfoClockRates[lfoClockSpeed[i]];
					if (lfoStepF[i] >= 255) {
						if (looping[i]) {
							if (selectedLfo == i) {
								lfoBlink();
							}
							lfoStepF[i] = 0;
							lfoNewRand[i] = 1;
						} else {
							lfoStepF[i] = 255;
						}
					}
					lfoStep[i] = int(lfoStepF[i]);
				}
			}
		}
	}
}

void handleBendy(byte channel, int bend) {
	leftDot();

	if (mpe) {

		if (channel > 1 && channel < 14) {

			// channel number comes in at 2-13 (set end ch. of MPE controler to 13 (master ch. to 1))
			channel -= 2; // offset to 0-11

			bendy = 0;

			if (bend != 0) {
				if (bend < 0) {
					mpeBend[channel] = bend;
					mpeBend[channel] /= 8192;
					mpeBend[channel] *= 48;
				} else if (bend > 0) {
					mpeBend[channel] = bend;
					mpeBend[channel] /= 8191;
					mpeBend[channel] *= 48;
				}
			}

			setNote(channel, notey[channel]);
		}
	} else {
		if (channel == inputChannel) {
			//-8192 to 8191
			bendy = 0;
			if (bend != 0) {
				if (bend < 0) {
					bendy = bend;
					bendy /= 8192;
					bendy *= bendDown;
				} else if (bend > 0) {
					bendy = bend;
					bendy /= 8191;
					bendy *= bendUp;
				}
			}

			bendyCounter = 4;
		}
	}
}

void handleStop() { sync = false; }

void handleStart() {
	if (vibratoClockEnable)
		vibIndex = 0;
	resyncArpLfo();
	masterClockCounter = 0;
	seqStep = 0;
	arpClockCounter = 0;
	sync = true;

	for (int i = 0; i < 3; i++) {
		if (lfoClockEnable[i]) {
			lfoStepF[i] = lfoStep[i] = 0;
		}
	}
}

void handleContinue() { handleStart(); }

void handleProgramChange(byte channel, byte program) {
	if ((program < 99) && (channel == inputChannel)) {

		preset = program;
		loadPreset();
	}
}

/// Try to find the next free voice slot. If there is none, returns the next voice slot, independent of whether it's
/// free or not. Considers only the first `nSlots` slots for search.
static byte findNextFreeVoiceSlot(byte prevSlot, bool voiceSlots[], byte nSlots) {
	for (byte i = 0; i < nSlots; i++) {
		byte slot = (prevSlot + 1 + i) % nSlots;
		if (voiceSlots[slot] == false) {
			return slot;
		}
	}

	return (prevSlot + 1) % nSlots;
}

int nVoicesForMode(VoiceMode voiceMode) {
	switch (voiceMode) {
		case kVoicingPoly12:
			return 12;
		case kVoicingWide6:
			return 6;
		case kVoicingWide4:
			return 4;
		case kVoicingWide3:
			return 3;
		case kVoicingUnison:
			return 1;
		case kVoicingDualCh3:
			return 1;
		default:
			return 1; // should not happen
	}
}

static void handleNoteOn(byte channel, byte note, byte velocity);
static void handleNoteOff(byte channel, byte note);

static MidiPedalAdapter pedal_adapter(handleNoteOn, handleNoteOff);

static void handleNoteOn(byte channel, byte note, byte velocity) {
	// byte distanceFromNewNote; // unused
	if (channel == inputChannel)
		heldNotes[note] = true;

	if (setupMode) {

		if (bendRoot == -1) {
			bendRoot = note;

			digit(0, 10);
			digit(1, 23);
			delay(500);
			lastNumber = -1;
			ledNumber(inputChannel);
			delay(750);

			if (channel != inputChannel) {
				inputChannel = channel;
				EEPROM.write(3951, inputChannel);
			}

		} else {
			if (note > bendRoot) {
				bendUp = note - bendRoot;
				if (bendUp > 48) {
					bendUp = 48;
				}
				EEPROM.write(3959, byte(bendUp));

				digit(0, 13);
				digit(1, 14);
				delay(500);
				lastNumber = -1;
				ledNumber(bendUp);
				delay(750);
			} else if (note < bendRoot) {
				bendDown = bendRoot - note;
				if (bendDown > 48) {
					bendDown = 48;
				}
				EEPROM.write(3958, byte(bendDown));

				digit(0, 15);
				digit(1, 19);
				delay(500);
				lastNumber = -1;
				ledNumber(bendDown);
				delay(750);
			}
		}
	} else {
		if (mpe) {
			note += 3;
			if (channel > 1 && channel < 14) {
				channel -= 2;

				ym.noteOff(channel);
				notey[channel] = note;
				setNote(channel, notey[channel]);
				ym.noteOn(channel);

				if (lfoVel) {

					for (int i = 0; i < 12; i++)
						if (i == channel)
							polyVel[i] = velocity << 1;
				}
				latestChannel = channel;
			}
		} else {

			if (channel == inputChannel) {

				note += 3;
				rootNote = note;

				if (velocity > 0) {

					// Retrigger LFOs
					for (int i = 0; i < 3; i++) {
						if ((retrig[i]) || ((!retrig[i]) && (!looping[i]) && (heldKeys == 0))) {
							lfoStep[i] = 0;
						}
					}

					// Reset Arpeggiator
					if (arpMode == kArpSequence1) {
						arpStep = seqStep = 0;
						arpIndex = 0;
					} else if (arpMode == kArpSequence2) {
						arpCounter = 1023;
					} // next manual arp step

					int nVoices, ymfChannelsPerVoice;
					switch (voiceMode) {
						case kVoicingPoly12:
						case kVoicingWide6:
						case kVoicingWide4:
						case kVoicingWide3:

							if (arpMode) {
								// ARP

								heldKeys++;

								if (heldKeys == 1) {
									arpClearFlag = false;
									clearNotes();
									heldKeys = 1;
								}

								if ((heldKeys == 1) && (!sync)) {
									arpCounter = 1023;
								} // only retrigger arp on first key or if arp is stopped

								addNote(note);

							} else {
								// NO ARP

								nVoices = nVoicesForMode(voiceMode);
								ymfChannelsPerVoice = 12 / nVoices;

								voiceSlot = findNextFreeVoiceSlot(voiceSlot, voiceSlots, nVoices);
								voiceSlots[voiceSlot] = 1;
								// if gliding jump to last pitch associated to keycounter
								if (glide) {
									for (int i = 0; i < ymfChannelsPerVoice; i++) {
										setNote(ymfChannelsPerVoice * voiceSlot + i, lastNotey[heldKeys]);
										skipGlide(ymfChannelsPerVoice * voiceSlot + i);
									}
								}

								noteOfVoice[voiceSlot] = note;
								latestChannel = voiceSlot;

								lastNotey[heldKeys] = note;

								for (int i = 0; i < ymfChannelsPerVoice; i++) {
									setNote(ymfChannelsPerVoice * voiceSlot + i, noteOfVoice[voiceSlot]);
									ym.noteOff(ymfChannelsPerVoice * voiceSlot + i);
									ym.noteOn(ymfChannelsPerVoice * voiceSlot + i);
								}

								if (heldKeys < 127)
									heldKeys++;
							}
							if (lfoVel && velocity) {
								// set velocity amount to channel
								for (int ch = 0; ch < 12; ch++) {
									if ((notey[ch] == note) || arpMode) {
										polyVel[ch] = velocity << 1;
									}
								}
							}
							break;

						case kVoicingDualCh3:
							////   ////   ////   ////   ////   ////   ////   ////   ////   ////   ////   ////   ////
							///////   ////
							// dual CH3
							////   ////   ////   ////   ////   ////   ////   ////   ////   ////   ////   ////   ////
							///////   ////
							if (arpMode) {
								// ARP

								heldKeys++;

								if (heldKeys == 1) {
									arpClearFlag = false;
									clearNotes();
									heldKeys = 1;
								}

								if ((heldKeys == 1) && (!sync)) {
									arpCounter = 1023;
								} // only retrigger arp on first key or if arp is stopped

								addNote(note);

							} else {
								// NO ARP

								if (stereoCh3) {
									// fire at the same time

									//                noteToChannel[note] = 4; unused?
									ym.noteOff(4);
									setNote(4, note);
									ym.noteOn(4); // CHIP1
									setNote(2, note);

									//                noteToChannel[note] = 5; unused?
									ym.noteOff(5);
									setNote(5, note);
									ym.noteOn(5); // CHIP2
									setNote(8, note);

								} else {

									ch3Alt = !ch3Alt;

									if (ch3Alt) {

										//                  noteToChannel[note] = 4; // unused?
										ym.noteOff(4);
										setNote(4, note);
										ym.noteOn(4); // CHIP1
										setNote(2, note);

									} else {

										//                  noteToChannel[note] = 5; // unused?
										ym.noteOff(5);
										setNote(5, note);
										ym.noteOn(5); // CHIP2
										setNote(8, note);
									}
								}
							}
							if (lfoVel && velocity) {
								// set velocity amount to channel
								for (int ch = 0; ch < 12; ch++) {
									if ((notey[ch] == note - 10) || arpMode) {
										polyVel[ch] = velocity << 1;
									}
								}
							}
							latestChannel = 0;
							break;

						case kVoicingUnison:
							////   ////   ////   ////   ////   ////   ////   ////   ////   ////   ////   ////   ////
							///////   ////
							// unison
							////   ////   ////   ////   ////   ////   ////   ////   ////   ////   ////   ////   ////
							///////   ////
							if (arpMode) {
								// ARP

								heldKeys++;

								if (heldKeys == 1) {
									arpClearFlag = false;
									clearNotes();
									heldKeys = 1;
								}

								if ((heldKeys == 1) && (!sync)) {
									arpCounter = 1023;
								} // only retrigger arp on first key or if arp is stopped

								addNote(note);

							} else {
								// NO ARP

								heldKeys++;

								lastNote = note;
								addNote(note);

								for (int i = 0; i < 12; i++) {
									if ((lfoVel && velocity) || arpMode)
										polyVel[i] = velocity << 1;
									ym.noteOff(i);
									setNote(i, note);
									ym.noteOn(i);
								}
								latestChannel = 0;
							}

							break;
						default:
							break;
					}

					if (seqRec) {
						if (!seqLength)
							rootNote1 = note;
						seq[seqLength] = note - rootNote1 + 127;
						if (seq[seqLength] == 255) {
							seq[seqLength]--;
						}

						seqLength++;
						if (seqLength > 15)
							seqLength = 0;

						ledNumber(seqLength + 1);

						for (int i = 0; i < 12; i++) {
							ym.noteOff(i);
							setNote(i, note);
							ym.noteOn(i);
						}
					}

				} else {
					handleNoteOff(channel, note);
				}
				leftDot();
			}
		}
	}
}

static void handleNoteOff(byte channel, byte note) {
	if (channel == inputChannel)
		heldNotes[note] = false;
	if (setupMode) {

		if (note == bendRoot) {
			bendRoot = -1;
		}
	} else {

		if (mpe) {

			note += 3;

			if (channel > 1 && channel < 14)
				ym.noteOff(channel - 2);
		} else {

			if (channel == inputChannel) {

				note += 3;

				int nVoices, ymfChannelsPerVoice;
				switch (voiceMode) {
					case kVoicingPoly12:
					case kVoicingWide6:
					case kVoicingWide4:
					case kVoicingWide3:

						if (arpMode) {
							// ARP

							heldKeys--;
							removeNote(note);

						} else {
							// no arp
							nVoices = nVoicesForMode(voiceMode);
							ymfChannelsPerVoice = 12 / nVoices;

							heldKeys--;
							// scan through the noteOfVoices and kill the voice associated to it
							for (int v = 0; v < nVoices; v++) {
								if ((voiceSlots[v]) && (noteOfVoice[v] == note)) {
									voiceSlots[v] = 0;
									for (int i = 0; i < ymfChannelsPerVoice; i++) {
										ym.noteOff(ymfChannelsPerVoice * v + i);
									}
								}
							}
						}
						break;

					case kVoicingDualCh3: // dual CH3
						////   ////   ////   ////   ////   ////   ////   ////   ////   ////   ////   ////   ////   ////
						///////
						// dual CH3
						////   ////   ////   ////   ////   ////   ////   ////   ////   ////   ////   ////   ////   ////
						///////
						if (stereoCh3) {
							// fire at the same time

							ym.noteOff(4); // CHIP1
							ym.noteOff(5); // CHIP2

						} else {

							ch3Alt = !ch3Alt;

							if (ch3Alt) {

								ym.noteOff(4); // CHIP1

							} else {

								ym.noteOff(5); // CHIP2
							}
						}

						break;

					case kVoicingUnison: // unison
						if (arpMode) {
							// ARP

							heldKeys--;
							removeNote(note);

						} else {
							// NO ARP

							heldKeys--;
							removeNote(note);

							// LAST KEY UP?
							if (heldKeys < 1) {

								heldKeys = 0;
								clearNotes();

								for (int i = 0; i < 12; i++) {
									ym.noteOff(i);
									pedalOff[i] = 1; // ToDo: not sure this is being used
								}

							} else {
								// NOT LAST KEY, CHANGE NOTE

								switch (notePriority) {
									case NOTE_PRIORITY_LOWEST:
										note = getLow();
										break; // LOWEST
									case NOTE_PRIORITY_HIGHEST:
										note = getHigh();
										break; // HIGHEST
									case NOTE_PRIORITY_LAST:
										note = getLast();
										break; // LAST
								}
								rootNote = note;
								for (int i = 0; i < 12; i++) {
									setNote(i, note);
								}
							}
						}
						break;

						leftDot();
				}
				if (heldKeys < 1) {
					heldKeys = 0;

					clearNotes();
					for (int i = 0; i < 12; i++) {
						ym.noteOff(i);
					}
				}
			}
		}
	}
}

void sendCCForce(byte number, int value) { sendControlChange(number, value, masterChannelOut); }

void sendCC(byte number, int value) {

	if (firstCC[number] || (lastCC[number] != value)) {
		firstCC[number] = false;
		lastCC[number] = value;
		rightDot();
		sendControlChange(number, value, masterChannelOut);
	}
}

void sendNRPN(int msg, int value) {
	rightDot();
	sendControlChange(99, msg >> 7, masterChannelOut); // NRPN MSB
	sendControlChange(98, msg & 0x7F, masterChannelOut); // NRPN LSB
	sendControlChange(6, value >> 7, masterChannelOut); // Data Entry MSB
	sendControlChange(38, value & 0x7F, masterChannelOut); // Data Entry LSB
}

void sendMidiButt(byte number, int value) {
	rightDot();
	sendCC(number, value);
}

byte lastData1, lastData2;

void setThru() {
	byte temp = EEPROM.read(3950);
	bitWrite(temp, 0, !thru);
	EEPROM.update(3950, temp);
}

void setLFO1Clock() {
	byte temp = EEPROM.read(3953);
	bitWrite(temp, 0, lfoClockEnable[0]);
	EEPROM.update(3953, temp);
}

void setLFO2Clock() {
	byte temp = EEPROM.read(3953);
	bitWrite(temp, 1, lfoClockEnable[1]);
	EEPROM.update(3953, temp);
}

void setLFO3Clock() {
	byte temp = EEPROM.read(3953);
	bitWrite(temp, 2, lfoClockEnable[2]);
	EEPROM.update(3953, temp);
}

void setVibratoClock() {
	byte temp = EEPROM.read(3953);
	bitWrite(temp, 3, vibratoClockEnable);
	EEPROM.update(3953, temp);
}

void setArpClock() {
	byte temp = EEPROM.read(3953);
	bitWrite(temp, 4, arpClockEnable);
	EEPROM.update(3953, temp);
}

void setFatSpreadMode() {
	byte temp = EEPROM.read(3968);
	bitWrite(temp, 0, fatSpreadMode);
	EEPROM.write(3968, fatSpreadMode);
}

void setLFO1Vel() { EEPROM.update(3961, lfoVel); }

void setLFO2Mod() { EEPROM.update(3962, lfoMod); }

void setLFO3Aftertouch() { EEPROM.update(3963, lfoAt); }

void setIgnoreVolume() {
	byte temp = EEPROM.read(3950);
	bitWrite(temp, 1, ignoreVolume);
	EEPROM.update(3950, temp);
}

void setMPEMode() { EEPROM.update(3960, mpe); }

void setPickupMode() { EEPROM.update(3954, pickupMode); }

void setStereoCh3() { EEPROM.update(3966, stereoCh3); }

void setFatMode() {
	byte temp = EEPROM.read(3953);
	bitWrite(temp, 5, fatMode);
	EEPROM.update(3953, temp);
}

void setNotePriority() { EEPROM.write(3967, notePriority); }

void setBrightness(byte brightness) {
	mydisplay.setIntensity(0, brightness); // 15 = brightest
	EEPROM.write(3965, brightness);
}

void showOnOff(bool on) {
	if (on) {
		// On
		digit(0, 0);
		digit(1, 19);
	} else {
		// OF
		digit(0, 0);
		digit(1, 12);
	}
	lastNumber = -1;
	showPresetNumberTimeout = 12000;
}

void setOperatorEnvelopeMode(byte op, kEnvelopeMode mode) {
	switch (mode) {
		case kEnvelopeOff:
			if (bitRead(SSEG[op], 1))
				// turn off
				setSSEG(op, 1, 0);
			break;
		case kEnvelopeOnce:
			if (!bitRead(SSEG[op], 1))
				// turn on, set mode
				setSSEG(op, 1, 1);
			setSSEG(op, 0, 0);
			break;
		case kEnvelopPingPong:
			if (!bitRead(SSEG[op], 1))
				// turn on, set mode
				setSSEG(op, 1, 1);
			setSSEG(op, 0, 1);
			break;
	}
}

void setLFOShape(byte lfo, byte value) {
	switch (value) {
		case 0:
			// square
			invertedSquare[lfo] = false;
			lfoShape[lfo] = kSquare;
			break;
		case 1:
			// inverted square
			invertedSquare[lfo] = true;
			lfoShape[lfo] = kSquare;
			break;
		case 2:
			// triangle
			lfoShape[lfo] = kTriangle;
			break;
		case 3:
			// saw
			invertedSaw[lfo] = false;
			lfoShape[lfo] = kSaw;
			break;
		case 4:
			// inverted saw
			invertedSaw[lfo] = true;
			lfoShape[lfo] = kSaw;
			break;
		case 5:
			// random
			lfoShape[lfo] = kRandom;
			noiseTableLength[lfo] = 2;
			break;
		case 6:
			lfoShape[lfo] = kRandom;
			noiseTableLength[lfo] = 8;
		case 7:
			lfoShape[lfo] = kRandom;
			noiseTableLength[lfo] = 16;
		case 8:
			lfoShape[lfo] = kRandom;
			noiseTableLength[lfo] = 32;
	}
	showLfoWaveform(lfo);
}

kEnvelopeMode getOperatorEnvelopeMode(byte op) {
	if (bitRead(SSEG[op], 1)) {
		if (bitRead(SSEG[op], 0)) {
			return kEnvelopPingPong;
		} else {
			return kEnvelopeOnce;
		}
	}
	return kEnvelopeOff;
}

int nrpn_msg = 0;
int nrpn_data = 0;
byte nrpn_state = 0;

void handleNRPN(int msg, int int_val) {	
	// NRPN values are sent as 14 bit values, but we only mostly only use the lower 8 bits 
	byte byte_val = (byte) int_val;
	bool bool_val = (int_val > 0);

	if ((msg >= 100) && (msg <= 114)) {
		if ((msg >= 100) && (msg <= 102)) {
			// Shape LFO1: 100, LFO2: 101, LFO3: 102
			selectedLfo = (byte) (msg - 100);
			setLFOShape(selectedLfo, byte_val);
		} else if ((msg >= 103) && (msg <= 105)) {
			// Looping LFO1: 103, LFO2: 104, LFO3: 105
			selectedLfo = (byte) (msg - 103);
			looping[selectedLfo] = bool_val;
		} else if ((msg >= 106) && (msg <= 108)) {
			// Retrig LFO1: 106, LFO2: 107, LFO3: 108
			selectedLfo = (byte) (msg - 106);
			retrig[selectedLfo] = bool_val;
		} else if ((msg >= 109) && (msg <= 111)) {
			// MIDI Sync LFO1: 109, LFO2: 110, LFO3: 111
			selectedLfo = (byte) (msg - 109);
			lfoClockEnable[selectedLfo] = bool_val;
			switch (selectedLfo) {
				case 0:
					setLFO1Clock();
					break;
				case 1:
					setLFO2Clock();
					break;
				case 2:
					setLFO3Clock();
					break;
			}
			showOnOff(bool_val);
		} else if ((msg >= 112) && (msg <= 114)) {
			selectedLfo = (byte) (msg - 112);
			if (msg == 112) {
				lfoVel = bool_val;
				setLFO1Vel();
			} else if (msg == 113) {
				lfoMod = bool_val;
				setLFO2Mod();
			} else if (msg == 114) {
				lfoAt = bool_val;
				setLFO3Aftertouch();
			}
			showOnOff(bool_val);
			lastLfoSetting[selectedLfo] = bool_val;
		}
		lfoLedOn();
		showLfo();
	} else if (msg == 200) {
		// Set brightness (0-15)
		if (byte_val < 16)
			setBrightness(byte_val);
	} else if (msg == 201) {
		// Set MIDI thru (0 = off, >0 = on)
		thru = bool_val;
		setThru();
		showOnOff(thru);
	} else if (msg == 202) {
		// Set Pickup Mide (0 = off, >0 = on)
		pickupMode = bool_val;
		setPickupMode();
		showOnOff(pickupMode);
	} else if (msg == 203) {
		// Set Stereo Channel 3 Mode (0 = off, >0 = on)
		stereoCh3 = bool_val;
		setStereoCh3();
		showOnOff(stereoCh3);
	} else if (msg == 204) {
		// Set MPE Mode (0 = off, >0 = on)
		mpe = bool_val;
		setMPEMode();
		showOnOff(mpe);
	} else if (msg == 205) {
		// Set Fat Spread Mode (0 = off, >0 = on)
		fatSpreadMode = bool_val;
		setFatSpreadMode();
		showOnOff(fatSpreadMode);
	} else if (msg == 206) {
		// Set Ignore Preset Volume (0 = off, >0 = on)
		ignoreVolume = bool_val;
		setIgnoreVolume();
		showOnOff(ignoreVolume);
	} else if (msg == 207) {
		// Set Fat Mode (0 = semitone, >0 = octave)
		digit(0, 1);
		if (bool_val) {
			fatMode = FAT_MODE_SEMITONE;
			digit(1, 5);
		} else {
			fatMode = FAT_MODE_OCTAVE;
			digit(1, 27);
		}
		setFatMode();
	} else if (msg == 208) {
		// Set Voice Mode (0-5 = (Poly12, Wide6, DualCh3, Unison, Wide4, Wide3)
		if (byte_val <= 5) {
			if (!mpe) {
				voiceMode = VoiceMode(byte_val);
				showVoiceMode(voiceMode);
				resetVoices();
			}
		}
	} else if (msg == 209) {
		// Set octave offset (0-3)
		if (byte_val < 4) {
			octOffset = byte_val;
			ledNumber(octOffset);
		}
	} else if (msg == 220) {
		// Set Tune (0-255)
		// fine = byte_val;
		// updateFine();
		// if (fine > 127) {
		// 	ledNumber(map(fine, 128, 255, 0, 32));
		// } else if (fine < 128) {
		// 	ledNumber(map(fine, 128, 0, 0, 32));
		// }
		voiceHeld = true;
		movedPot(KNOB_VOLUME, byte_val, 1);
		voiceHeld = false;
	} else if (msg == 221) {
		// Set glide (0-15)
		// if (byte_val < 16) {
		// 	glide = byte_val >> 3;
		// 	updateGlideIncrements();
		// 	ledNumber(byte_val);
		// }
		voiceHeld = true;
		movedPot(KNOB_FAT, byte_val, 1);
		voiceHeld = false;
	}  else if (msg == 222) {
		// LFO 1 Rate
		movedPot(KNOB_LFO1_RATE, byte_val, 1);
	} else if (msg == 223) {
		// LFO 2 Rate
		movedPot(KNOB_LFO2_RATE, byte_val, 1);
	} else if (msg == 224) {
		// LFO 3 Rate
		movedPot(KNOB_LFO3_RATE, byte_val, 1);
	} else if (msg == 225) {
		// LFO 1 Depth
		movedPot(KNOB_LFO1_DEPTH, byte_val, 1);
	} else if (msg == 226) {
		// LFO 2 Depth
		movedPot(KNOB_LFO2_DEPTH, byte_val, 1);
	} else if (msg == 227) {
		// LFO 3 Depth
		movedPot(KNOB_LFO3_DEPTH, byte_val, 1);
	} else if (msg == 228) {
		// Fat
		movedPot(KNOB_FAT, byte_val, 1);
	} else if (msg == 229) {
		// Volume
		movedPot(KNOB_VOLUME, byte_val, 1);
	} else if (msg == 230) {
		// Feedback
		movedPot(KNOB_FEEDBACK, byte_val, 1);
	} else if (msg == 231) {
		// Algorithm
		movedPot(KNOB_ALGO, byte_val, 1);
	} else if (msg == 232) {
		notePriority = byte_val;
		setNotePriority();
	} else if (msg == 300) {
		if (byte_val < 8) {
			arpMode = ArpMode(byte_val);
			showArpMode();
			resetVoices();
		}
	} else if (msg == 301) {
		arpClockEnable = bool_val;
		setArpClock();
		showOnOff(arpClockEnable);
	} else if (msg == 302) {
		// Arp Rate
		movedPot(KNOB_ARP_RATE, byte_val, 1);
	} else if (msg == 303) {
		// Arp Range
		movedPot(KNOB_ARP_RANGE, byte_val, 1);
	} else if (msg == 500) {
		vibratoClockEnable = bool_val;
		setVibratoClock();
		showOnOff(vibratoClockEnable);
	} else if (msg == 501) {
		// Vibrato Rate
		movedPot(KNOB_VIB_RATE, byte_val, 1);
	} else if (msg == 502) {
		// Vibrato Depth
		movedPot(KNOB_VIB_DEPTH, byte_val, 1);
	} else if ((msg >= 1000) &&  (msg <= 1002)) {
		// Link LFO to target
		// msg 1000 = LFO1, 1001 = LFO2, 1002 = LFO3
		//
		// bit 0 = linked if 1 else unlinked
		// bit 1-7 = target pot (0-50)
		//
		// Example:
		// val is even (0, 2, 4, ...): not linked
		// val is odd (1, 3, 5, ...): linked
		// val is 0 or 1: target pot 0
		// val is 2 or 3: target pot 1
		//
		// So to link to pot 10, val should be 21 (10*2 + 1) and to unlink it val should be 20 (10*2 + 0)12
		byte lfo = msg - 1000;
		bool isLinked = bitRead(byte_val, 0);
		byte targetPot = byte_val >> 1;
		if (targetPot < 51) {
			linked[lfo][targetPot] = isLinked;
			showLink();
		}
	} else if (msg == 2000) {
		// Operator 1 Detune
		movedPot(FADER_DETUNE_1, byte_val, 1);
	} else if (msg == 2001) {
		// Operator 1 Multiplier
		movedPot(FADER_MULT_1, byte_val, 1);
	} else if (msg == 2002) {
		// Operator 1 Level
		movedPot(FADER_LEVEL_1, byte_val, 1);
	} else if (msg == 2003) {
		// Operator 1 Attack
		movedPot(FADER_ATTACK_1, byte_val, 1);
	} else if (msg == 2004) {
		// Operator 1 Decay Rate
		movedPot(FADER_DECAY_1, byte_val, 1);
	} else if (msg == 2005) {
		// Operator 1 Sustain Level
		movedPot(FADER_SUSTAIN_1, byte_val, 1);
	} else if (msg == 2006) {
		// Operator 1 Sustain Rate
		movedPot(FADER_SUSTAIN_RATE_1, byte_val, 1);
	} else if (msg == 2007) {
		// Operator 1 Release Rate
		movedPot(FADER_RELEASE_1, byte_val, 1);
	} else if (msg == 2008) {
		// Set envelope mode for operator 1, 0-2 = (off, forward, ping pong)
		if (byte_val < 3) {
			setOperatorEnvelopeMode(0, kEnvelopeMode(byte_val));
		}
	} else if (msg == 2009) {
		// Set rate scaling for operators 1 (0-3)
		// if (byte_val < 4) {
		// 	updateFMifNecessary(3);
		// 	fmBase[3] = byte_val << 6; // 0-3 becomes 0-192 (4 steps: 0, 64, 128, 192)
		// 	ledNumber(byte_val);
		// }
		loopHeld = true;
		movedPot(FADER_DETUNE_1, byte_val, 1);
		loopHeld = false;
	} else if (msg == 3000) {
		// Operator 2 Detune
		movedPot(FADER_DETUNE_2, byte_val, 1);
	} else if (msg == 3001) {
		// Operator 2 Multiplier
		movedPot(FADER_MULT_2, byte_val, 1);
	} else if (msg == 3002) {
		// Operator 2 Level
		movedPot(FADER_LEVEL_2, byte_val, 1);
	} else if (msg == 3003) {
		// Operator 2 Attack
		movedPot(FADER_ATTACK_2, byte_val, 1);
	} else if (msg == 3004) {
		// Operator 2 Decay Rate
		movedPot(FADER_DECAY_2, byte_val, 1);
	} else if (msg == 3005) {
		// Operator 2 Sustain Level
		movedPot(FADER_SUSTAIN_2, byte_val, 1);
	} else if (msg == 3006) {
		// Operator 2 Sustain Rate
		movedPot(FADER_SUSTAIN_RATE_2, byte_val, 1);
	} else if (msg == 3007) {
		// Operator 2 Release Rate
		movedPot(FADER_RELEASE_2, byte_val, 1);
	} else if (msg == 3008) {
		// Set envelope mode for operator 2, 0-2 = (off, forward, ping pong)
		if (byte_val < 3) {
			setOperatorEnvelopeMode(1, kEnvelopeMode(byte_val));
		}
	} else if (msg == 3009) {
		// Set rate scaling for operators 2 (0-3)
		// if (byte_val < 4) {
		// 	updateFMifNecessary(12);
		// 	fmBase[12] = byte_val << 6; // 0-3 becomes 0-192 (4 steps: 0, 64, 128, 192)
		// 	ledNumber(byte_val);
		// }
		loopHeld = true;
		movedPot(FADER_DETUNE_2, byte_val, 1);
		loopHeld = false;
	} else if (msg == 4000) {
		// Operator 3 Detune
		movedPot(FADER_DETUNE_3, byte_val, 1);
	} else if (msg == 4001) {
		// Operator 3 Multiplier
		movedPot(FADER_MULT_3, byte_val, 1);
	} else if (msg == 4002) {
		// Operator 3 Level
		movedPot(FADER_LEVEL_3, byte_val, 1);
	} else if (msg == 4003) {
		// Operator 3 Attack
		movedPot(FADER_ATTACK_3, byte_val, 1);
	} else if (msg == 4004) {
		// Operator 3 Decay Rate
		movedPot(FADER_DECAY_3, byte_val, 1);
	} else if (msg == 4005) {
		// Operator 3 Sustain Level
		movedPot(FADER_SUSTAIN_3, byte_val, 1);
	} else if (msg == 4006) {
		// Operator 3 Sustain Rate
		movedPot(FADER_SUSTAIN_RATE_3, byte_val, 1);
	} else if (msg == 4007) {
		// Operator 3 Release Rate
		movedPot(FADER_RELEASE_3, byte_val, 1);
	} else if (msg == 4008) {
		// Set envelope mode for operator 3, 0-2 = (off, forward, ping pong)
		if (byte_val < 3) {
			setOperatorEnvelopeMode(2, kEnvelopeMode(byte_val));
		}
	} else if (msg == 4009) {
		// Set rate scaling for operators 3 (0-3)
		// if (byte_val < 4) {
		// 	updateFMifNecessary(21);
		// 	fmBase[21] = byte_val << 6; // 0-3 becomes 0-192 (4 steps: 0, 64, 128, 192)
		// 	ledNumber(byte_val);
		// }
		loopHeld = true;
		movedPot(FADER_DETUNE_3, byte_val, 1);
		loopHeld = false;
	} else if (msg == 5000) {
		// Operator 4 Detune
		movedPot(FADER_DETUNE_4, byte_val, 1);
	} else if (msg == 5001) {
		// Operator 4 Multiplier
		movedPot(FADER_MULT_4, byte_val, 1);
	} else if (msg == 5002) {
		// Operator 4 Level
		movedPot(FADER_LEVEL_4, byte_val, 1);
	} else if (msg == 5003) {
		// Operator 4 Attack
		movedPot(FADER_ATTACK_4, byte_val, 1);
	} else if (msg == 5004) {
		// Operator 4 Decay Rate
		movedPot(FADER_DECAY_4, byte_val, 1);
	} else if (msg == 5005) {
		// Operator 4 Sustain Level
		movedPot(FADER_SUSTAIN_4, byte_val, 1);
	} else if (msg == 5006) {
		// Operator 4 Sustain Rate
		movedPot(FADER_SUSTAIN_RATE_4, byte_val, 1);
	} else if (msg == 5007) {
		// Operator 4 Release Rate
		movedPot(FADER_RELEASE_4, byte_val, 1);
	} else if (msg == 5008) {
		// Set envelope mode for operator 4, 0-2 = (off, forward, ping pong)
		if (byte_val < 3) {
			setOperatorEnvelopeMode(3, kEnvelopeMode(byte_val));
		}
	} else if (msg == 5009) {
		// Set rate scaling for operators 4 (0-3)
		// if (byte_val < 4) {
		// 	updateFMifNecessary(30);
		// 	fmBase[30] = byte_val << 6; // 0-3 becomes 0-192 (4 steps: 0, 64, 128, 192)
		// 	ledNumber(byte_val);
		// }
		loopHeld = true;
		movedPot(FADER_DETUNE_4, byte_val, 1);
		loopHeld = false;
	} 
}


void HandleControlChange(byte channel, byte number, byte val) {
	byte temp;

	if (number == 74) {

		if (mpe && channel > 1 && channel < 14) {
			channel -= 2;
			// this is mpe pressure.
			polyPressure[channel] = val << 1;
			lastMpeVoice = channel;
		}
	} else {
		if (toolMode && channel == 16) {
			switch (number) {
				case 1:
					if (val) {
						thru = 0;
					} else {
						thru = 1;
					}
					setThru();
					break;
				case 2:
					if (val) {
						ignoreVolume = 1;
					} else {
						ignoreVolume = 0;
					}
					setIgnoreVolume();
					break;
				case 3:
					if (val) {
						lfoClockEnable[0] = true;
					} else {
						lfoClockEnable[0] = false;
					}
					setLFO1Clock();
					break;
				case 4:
					if (val) {
						lfoClockEnable[1] = true;
					} else {
						lfoClockEnable[1] = false;
					}
					setLFO2Clock();
					break;
				case 5:
					if (val) {
						lfoClockEnable[2] = true;
					} else {
						lfoClockEnable[2] = false;
					}
					setLFO3Clock();
					break;
				case 6:
					if (val) {
						vibratoClockEnable = true;
					} else {
						vibratoClockEnable = false;
					}
					setVibratoClock();
					break;
				case 7:
					if (val) {
						arpClockEnable = true;
					} else {
						arpClockEnable = false;
					}
					setArpClock();
					break;
				case 18:
					mydisplay.setIntensity(0, constrain(val, 1, 15));
					EEPROM.update(3965, constrain(val, 0, 15));
					break;
				case 19:
					if (val) {
						fatMode = true;
					} else {
						fatMode = false;
					}
					setFatMode();
					break;

				case 14:
					if (val) {
						newFat = true;
					} else {
						newFat = false;
					}
					temp = EEPROM.read(3953);
					bitWrite(temp, 6, newFat);
					EEPROM.update(3953, temp);
					break;

				case 8:
					if (val) {
						pickupMode = true;
					} else {
						pickupMode = false;
					}
					setPickupMode();
					break;
				case 9:
					if (val) {
						mpe = true;
					} else {
						mpe = false;
					}
					setMPEMode();
					break;
				case 10:
					if (val) {
						lfoVel = true;
					} else {
						lfoVel = false;
					}
					setLFO1Vel();
					break;
				case 11:
					if (val) {
						lfoMod = true;
					} else {
						lfoMod = false;
					}
					setLFO2Mod();
					break;
				case 12:
					if (val) {
						lfoAt = true;
					} else {
						lfoAt = false;
					}
					setLFO3Aftertouch();
					break;
				case 13:
					if (val) {
						stereoCh3 = true;
					} else {
						stereoCh3 = false;
					}
					setStereoCh3();
					break;

				case 15:
					inputChannel = constrain(val, 1, 16);
					EEPROM.update(3951, inputChannel);
					break;

				case 16:
					bendUp = constrain(val, 1, 48);
					EEPROM.update(3959, bendUp);
					break;

				case 17:
					bendDown = constrain(val, 1, 48);
					EEPROM.update(3958, bendDown);
					break;

				case 20:
					if (val) {
						newWide = true;
					} else {
						newWide = false;
					}
					EEPROM.update(3970, newWide);
					break;
			}
		}

		if (channel == 16 && lastData1 == 19 && lastData2 == 82 && number == 19 && val == 82) {
			// transmit all the settings to tool. Tool expects noteOff messages on CH16 (yeah I couldn't get webMidi
			// to parse sysex... )

			sendTool(0, 3); // 3 is MEGAFM
			sendTool(82, kVersion0);
			sendTool(83, kVersion1);

			sendTool(1, thru);
			sendTool(2, ignoreVolume);
			sendTool(3, lfoClockEnable[0]);
			sendTool(4, lfoClockEnable[1]);
			sendTool(5, lfoClockEnable[2]);
			sendTool(6, vibratoClockEnable);
			sendTool(7, arpClockEnable);
			sendTool(8, pickupMode);

			sendTool(9, mpe);
			sendTool(10, lfoVel);
			sendTool(11, lfoMod);
			sendTool(12, lfoAt);
			sendTool(13, stereoCh3);
			sendTool(14, newFat); // new fat tuning
			sendTool(15, inputChannel);
			sendTool(16, bendUp);
			sendTool(17, bendDown);
			sendTool(18, EEPROM.read(3965));
			sendTool(19, fatMode);
			sendTool(20, newWide);
			toolMode = true; // MEGAfm is listening to new settings (CC on CH16)
		}
		// did we receive 1982 twice on channel 16?

		if ((lastSentCC[0] == number) && (lastSentCC[1] == val)) {
			// ignore same CC and DATA as sent to avoid feedback
		} else {
			

			leftDot();
			
			if (number == 64) {
				if (mpe) {
					for (int ch = 0; ch < 16; ch++)
						pedal_adapter.set_pedal(ch, val >> 6);
				} else {
					pedal_adapter.set_pedal(channel, val >> 6);
				}
			}

			else if (channel == inputChannel) {
				if (number == 0) {
					if (val <= 5) {
						if (bank != val) {
							bank = val;
							handleProgramChange(inputChannel, preset);
						}
					}
				} else if (number == 1) { // Modulation
					if (lfoMod) {
						if (fmBase[38]) {
							fmBase[39] = val << 1;
							fmBaseLast[39] = fmBase[39] - 1;
							ledNumberTimeOut = 20;
						} else {
							lfo[1] = val << 1;
							applyLfo();
						}
					}
				} else if (number == 99) {
				// NRPN BEGIN ////////////////////////////////////
					nrpn_msg = val;
					nrpn_state = 1;
				} else if ((number == 98) && (nrpn_state == 1)) {
					nrpn_msg = (nrpn_msg << 7) | val;
					nrpn_state = 2;
				} else if ((number == 6) && (nrpn_state == 2)) {
					nrpn_data = val;
					nrpn_state = 3;
				} else if ((number == 38) && (nrpn_state == 3)) {
					nrpn_data = (nrpn_data << 7) | val;
					handleNRPN(nrpn_msg, nrpn_data);
					// Handle NRPN with 14 bit message
					nrpn_msg = 0;
					nrpn_data = 0;
					nrpn_state = 0;
				// NRPN DONE //////////////////////////////////////
				} else {
					byte pot = number;
					// Rewrite CC -> POT for some
					if (number == 50)
						pot = 0;
					else if (number == 51)
						pot = 7;
					else if (number == 49)
						pot = 8;
					else if (number == 7)
						pot = 1;

					if (kAllCC) {
						movedPot(pot, val << 1, 1);
					} else {
						if ((pot != 19) && (pot != 40) && (pot != 16) && (pot != 38)) {
							movedPot(pot, val << 1, 1);
						}
					}
				}
			}
		}
		lastData1 = number;
		lastData2 = val;
	}
}

void dumpPreset() {

	for (int number = 0; number < 58; number++) {
		switch (number) {
			// OP1
			case 18:
				sendCCForce(number, fmBase[0] >> 1);
				break; // detune
			case 27:
				sendCCForce(number, fmBase[1] >> 1);
				break; // multiple
			case 19:
				sendCCForce(number, fmBase[2] >> 1);
				break; // op level
			case 29:
				sendCCForce(number, fmBase[4] >> 1);
				break; // attack
			case 21:
				sendCCForce(number, fmBase[5] >> 1);
				break; // decay1
			case 25:
				sendCCForce(number, fmBase[7] >> 1);
				break; // sustain
			case 17:
				sendCCForce(number, fmBase[6] >> 1);
				break; // sustain rate
			case 30:
				sendCCForce(number, fmBase[8] >> 1);
				break; // release
			// OP2
			case 31:
				sendCCForce(number, fmBase[18] >> 1);
				break; // detune
			case 32:
				sendCCForce(number, fmBase[19] >> 1);
				break; // multiple
			case 40:
				sendCCForce(number, fmBase[20] >> 1);
				break; // op level
			case 36:
				sendCCForce(number, fmBase[22] >> 1);
				break; // attack
			case 44:
				sendCCForce(number, fmBase[23] >> 1);
				break; // decay1
			case 42:
				sendCCForce(number, fmBase[25] >> 1);
				break; // sustain
			case 34:
				sendCCForce(number, fmBase[24] >> 1);
				break; // sustain rate
			case 11:
				sendCCForce(number, fmBase[26] >> 1);
				break; // release
			// OP3
			case 20:
				sendCCForce(number, fmBase[9] >> 1);
				break; // detune
			case 24:
				sendCCForce(number, fmBase[10] >> 1);
				break; // multiple
			case 16:
				sendCCForce(number, fmBase[11] >> 1);
				break; // op level
			case 8:
				sendCCForce(49, fmBase[13] >> 1);
				break; // attack
			case 0:
				sendCCForce(50, fmBase[14] >> 1);
				break; // decay1
			case 7:
				sendCCForce(51, fmBase[16] >> 1);
				break; // sustain
			case 45:
				sendCCForce(number, fmBase[15] >> 1);
				break; // sustain rate
			case 37:
				sendCCForce(number, fmBase[17] >> 1);
				break; // release
			// OP4
			case 47:
				sendCCForce(number, fmBase[27] >> 1);
				break; // detune
			case 39:
				sendCCForce(number, fmBase[28] >> 1);
				break; // multiple
			case 38:
				sendCCForce(number, fmBase[29] >> 1);
				break; // op level
			case 46:
				sendCCForce(number, fmBase[31] >> 1);
				break; // attack
			case 33:
				sendCCForce(number, fmBase[32] >> 1);
				break; // decay1
			case 41:
				sendCCForce(number, fmBase[34] >> 1);
				break; // sustain
			case 43:
				sendCCForce(number, fmBase[33] >> 1);
				break; // sustain rate
			case 35:
				sendCCForce(number, fmBase[35] >> 1);
				break; // release

			case 1:
				sendCCForce(7, 128 - lastVol);
				break; // volume
			case 4:
				sendCCForce(number, (1 + (fmBase[42] >> 5)));
				break; // algo
			case 3:
				sendCCForce(number, fmBase[43] >> 1);
				break; // feedback
			case 28:
				sendCCForce(number, fmBase[50] >> 1);
				break; // fat 1-127
			case 15:
				sendCCForce(number, fmBase[36] >> 1);
				break; // lfo 1 rate
			case 12:
				sendCCForce(number, fmBase[37] >> 1);
				break; // lfo 1 depth
			case 10:
				sendCCForce(number, fmBase[38] >> 1);
				break; // lfo 2 rate
			case 9:
				sendCCForce(number, fmBase[39] >> 1);
				break; // lfo 2 depth
			case 14:
				sendCCForce(number, fmBase[40] >> 1);
				break; // lfo 3 rate
			case 2:
				sendCCForce(number, fmBase[41] >> 1);
				break; // lfo 3 depth
			case 6:
				sendCCForce(number, fmBase[46] >> 1);
				break; /// arp rate
			case 5:
				sendCCForce(number, fmBase[47] >> 1);
				break; // arp range
			case 48:
				sendCCForce(number, fmBase[48] >> 1);
				break; // vibrato rate WAS 7
			case 13:
				sendCCForce(number, fmBase[49] >> 1);
				break; // vibrato depth
		}
	}
	// send other settings
	sendCCForce(78, 20 + (fmBase[3] >> 6));           // op1 rate scaling
	sendCCForce(78, 30 + (fmBase[12] >> 6));          // op2 rate scaling
	sendCCForce(78, 40 + (fmBase[21] >> 6));          // op3 rate scaling
	sendCCForce(78, 50 + (fmBase[30] >> 6));          // op4 rate scaling
	sendCCForce(78, 25 + getOperatorEnvelopeMode(0)); // op1 envelope mode
	sendCCForce(78, 35 + getOperatorEnvelopeMode(1)); // op2 envelope mode
	sendCCForce(78, 45 + getOperatorEnvelopeMode(2)); // op3 envelope mode
	sendCCForce(78, 55 + getOperatorEnvelopeMode(3)); // op4 envelope mode
	sendCCForce(70, 56 + arpClockEnable);             // arp clock on/off
	sendCCForce(70, 65 + vibratoClockEnable);         // vibrato clock on/off
	sendCCForce(70, 85 + arpMode);

	for (int i = 0; i < 3; i++) {
		byte shape = lfoShape[i];
		byte val = 0;
		if ((shape == 0))
			val = invertedSquare[i];
		else if (shape == 1)
			val = 2;
		else if (shape == 2)
			val = 3 + invertedSaw[i];
		else if (shape == 3) {
			val = 5;
			val += noiseTableLength[i] - 2; // 0 for 8 steps, 1 for 16 steps, 2 for 32 steps
		}
		sendCCForce(70, val + 16 * i);
		sendCCForce(70, 8 + looping[i] + 16 * i);
		sendCCForce(70, 6 + retrig[i] + 16 * i);
		sendCCForce(70, 10 + 16 * i + lfoClockEnable[i]);

		for (int targetPot = 0; targetPot < 51; targetPot++)
			// skip unused pots
			if ((targetPot != 3) && (targetPot != 12) && (targetPot != 21) && (targetPot != 23) && (targetPot != 30) &&
			    (targetPot != 45))
				sendCCForce(71 + i, 2 * targetPot + linked[i][targetPot]);
	}

	sendCCForce(70, 12 + lfoVel);
	sendCCForce(70, 28 + lfoMod);
	sendCCForce(70, 44 + lfoAt);
	sendCCForce(70, 63 + fatMode);
	sendCCForce(70, 58 + (1 - ignoreVolume));
	sendCCForce(70, 69 + EEPROM.read(3965)); // brightness

	sendCCForce(78, voiceMode);
	sendCCForce(78, 10 + octOffset);
	sendCCForce(75, glide << 3); // Todo: check
	sendCCForce(76, fine >> 1);

	sendCCForce(70, 48 + thru);          // midi thru on/off
	sendCCForce(70, 50 + pickupMode);    // pickup mode on/off
	sendCCForce(70, 60 + notePriority);  // note priority
	sendCCForce(70, 52 + stereoCh3);     // stereo ch3 on/off
	sendCCForce(70, 54 + mpe);           // mpe mode on/off
	sendCCForce(70, 67 + fatSpreadMode); // fat spread mode up/down or up/up
}

static byte mStatus;
static byte mData;
static byte mChannel;
static int midiNoteOffset = -13;

void sendControlChange(byte number, byte value, byte channel) {
	if (!thru) {
		lastSentCC[0] = number;
		lastSentCC[1] = value;
		Serial.write(175 + channel);
		Serial.write(number);
		Serial.write(value);
	}
}

void sendNoteOff(byte note, byte velocity, byte channel) {
	if (!thru) {
		Serial.write(127 + channel);
		Serial.write(note);
		Serial.write(velocity);
	}
}

void sendTool(byte note, byte velocity) {
	Serial.write(143);
	Serial.write(note);
	Serial.write(velocity);
}

void sendNoteOn(byte note, byte velocity, byte channel) {
	if (!thru) {
		Serial.write(143 + channel);
		Serial.write(note);
		Serial.write(velocity);
	}
}

#define MAX_SYSEX_DATA_LENGTH 2048
int sysExDataIndex = 0;
byte sysExData[MAX_SYSEX_DATA_LENGTH];

void handleSysExByte(byte command, byte b) {
	// TODO: handle decoded sysex payload byte
	(void)command;
	(void)b;
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

	mStatus = 0;
	mData = 0;
	lastNumber = -1;
	showPresetNumberTimeout = 12000;

	// Need at least 3 manufacturer + 1 command + 2 length bytes
	if (sysExDataIndex < 6) {
		sysExDataIndex = 0;
		return;
	}

	byte command = sysExData[3];
	int length = ((int)sysExData[4] << 7) | sysExData[5];
	int offset = 6;

	for (int i = 0; i < length; ) {
		if (offset >= sysExDataIndex) break;
		byte lsb_bits = sysExData[offset++];
		for (int j = 0; j < 7 && i < length; j++, i++) {
			if (offset >= sysExDataIndex) break;
			byte b = (sysExData[offset++] << 1) | ((lsb_bits >> j) & 1);
			handleSysExByte(command, b);
		}
	}

	sysExDataIndex = 0;
}

void abortSysEx() {
	mStatus = 0;
	sysExDataIndex = 0;
	mData = 0;
	digit(0, 20); // -
	digit(1, 20); // -
	lastNumber = -1;
	showPresetNumberTimeout = 12000;
}

void midiRead() {
	while (Serial.available()) {
		byte input = Serial.read();
		if (thru) {

			Serial.write(input);
		}

		if (input > 127) {
			// Status
			if ((mStatus == 8) && (input == 247))  {  // input == F7
				// In SysEx and receivd Sysex end; handle data and end sysex mode
				handleSysEx();
			} else if (mStatus == 8) {
				abortSysEx();
			}
			switch (input) {
				case 248:
					handleClock();
					break; // clock
				case 250:
					handleStart();
					break; // start
				case 251:
					handleContinue();
					break; // continue
				case 252:
					handleStop();
					break; // stop

				case 128 ... 143:
					mChannel = input - 127;
					mStatus = 2;
					mData = 255;
					break; // noteOff
				case 144 ... 159:
					mChannel = input - 143;
					mStatus = 1;
					mData = 255;
					break; // noteOn
				case 160 ... 175:
					mChannel = input - 159;
					mStatus = 7;
					mData = 255;
					break; // Poly AfterTouch
				case 176 ... 191:
					mChannel = input - 175;
					mStatus = 3;
					mData = 255;
					break; // CC
				case 192 ... 207:
					mChannel = input - 191;
					mStatus = 6;
					mData = 0;
					break; // program Change
				case 208 ... 223:
					mChannel = input - 207;
					mStatus = 5;
					mData = 0;
					break; // Aftertouch
				case 224 ... 239:
					mChannel = input - 223;
					mStatus = 4;
					mData = 255;
					break; // Pitch Bend
				case 240:  // F0
					// SysEx start
					mStatus = 8;
					mData = 0;
					sysExDataIndex = 0;
					digit(0, 24); // all on
					digit(1, 21); // blank
					break;
				// case 247:  // F7
				//  SysEx end; handeled above
				// 	break;
				default:
					mStatus = 0;
					mData = 255;
					break;
			}
		} else {
			// Data
			if (mData == 255) {
				mData = input;
			} // data byte 1
			else {
				// data byte 2
				switch (mStatus) {
					case 1:
						if (input) {

							if (chord) {
								for (int i = 0; i < 128; i++) {
									if (chordNotes[i]) {
										pedal_adapter.note_on(mChannel, i + mData + midiNoteOffset - chordRoot, input);
									}
								}
							} else {
								pedal_adapter.note_on(mChannel, mData + midiNoteOffset, input);
							}
							// handleNoteOn(mChannel, mData + midiNoteOffset, input);
						} else {

							if (chord) {
								for (int i = 0; i < 128; i++) {
									if (chordNotes[i]) {
										pedal_adapter.note_off(mChannel, i + mData + midiNoteOffset - chordRoot);
									}
								}
							} else {
								pedal_adapter.note_off(mChannel, mData + midiNoteOffset);
							}
							// handleNoteOff(mChannel, mData + midiNoteOffset);
						}
						mData = 255;


						break; // noteOn
					case 2:

						if (chord) {
							for (int i = 0; i < 128; i++) {
								if (chordNotes[i]) {
									pedal_adapter.note_off(mChannel, i + mData + midiNoteOffset - chordRoot);
								}
							}
						} else {
							pedal_adapter.note_off(mChannel, mData + midiNoteOffset);
						}
						// handleNoteOff(mChannel, mData + midiNoteOffset);
						mData = 255;
						break;
					case 3:
						HandleControlChange(mChannel, mData, input);
						mData = 255;
						break;
					case 4:
						handleBendy(mChannel, (input << 7) + mData - 8192);
						mData = 255;
						break;
					case 5:
						handleAftertouch(mChannel, input);
						mData = 255;
						break;
					case 6:
						handleProgramChange(mChannel, input);
						mData = 255;
						break;

					case 7:
						handlePolyAT(mChannel, mData, input);
						mData = 255;
						break;
					case 8:
						if (sysExDataIndex == 3) {
							// check if it's a SysEx message for us ( - we do the same as the firmware and take the default 
							// from hex2sys = \x00\x21\x44 = 00 33 68)
							if (sysExData[0] != 0 || sysExData[1] != 33 || sysExData[2] != 68) {
								// not for us, ignore the rest of the message
								abortSysEx();
								break;
							}
						} else if (sysExDataIndex == MAX_SYSEX_DATA_LENGTH) {
							abortSysEx();
							break;
						}
						sysExData[sysExDataIndex++] = input;
						mData = 0;
						break; // SysEx
					default:
						break;
				}
			}
		}
	}
}
