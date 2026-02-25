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
	if (on)
		digit(1, 19);
	else
		digit(1, 12);
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
					if (val < 5) {
						if (bank != val) {
							bank = val;
							handleProgramChange(inputChannel, preset - 1);
						}
					}
				} else if (number == 50) {
					movedPot(0, val << 1, 1);
				} else if (number == 42) {
					movedPot(42, val << 5, 1);
				} else if (number == 51) {
					movedPot(7, val << 1, 1);
				} else if (number == 49) {
					movedPot(8, val << 1, 1);
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
				} else if (number == 7) {
					if (kAllCC) {
						// Volume - scale to 0-255
						movedPot(1, val << 1, 1);
					}
				} else if (number == 64) {
					pedal_adapter.set_pedal(channel, val >= 64);
					// if (val > 63) {
					//	pedalDown();
					// } else {
					//	pedalUp();
					// }
				} else if (number == 70) {
					/*
					                                Lfo1	Lfo2	Lfo3
					    Square						00		16		32
					    InvSquare					01		17		33
					    Tri							02		18		34
					    Saw							03		19		35
					    InvSaw						04		20		36
					    Random						05		21		37

					    Retriger Off				06		22		38
					    Retriger On					07		23		39
					    Loop Off					08		24		40
					    Loop On 					09		25		41

					    Midi Sync Off				10		26		42
					    Midi Sync On				11		27		43
					    Midi X Off					12		28		44
					    Midi X On					13		29		45

					    unused						14		30		46
					    unused						15		31		47
					    -----------------------------------------------------
					    Midi Thru Off				48
					    Midi Thru On				49
					    Param Pickup Off			50
					    Param Pickup On				51
					    Ch3 Modo					52
					    Ch3 Duo						53

					    MPE Off						54
					    MPE On						55

					    Arp Midi Sync Off			56
					    Arp Midi Sync On			57

					    Read Vol Off				58
					    Read Vol On					59

					    Note Prio Low				60
					    Note Prio High				61
					    Note Prio Last				62

					    Fat Mode Range Semi			63
					    Fat Mode Range Octave		64

					    Vibrato Midi Sync Off		65
					    Vibrato Midi Sync On		66

					    Fat Spread Mode Up/Down		67
					    Fat Spread Mode Up/Up		68

					    LED Brightness				69 - 84

					    Arp Mode 0-7				85 - 92 (off, up, down, up/down, rnd1, rnd2, seq1, seq2)
					*/
					if (val <= 47) {
						byte selectedLfo = val / 16;
						byte action = val % 16;
						switch (action) {
							case 0:
								// square
								invertedSquare[selectedLfo] = false;
								lfoShape[selectedLfo] = 0;
								break;
							case 1:
								// inverted square
								invertedSquare[selectedLfo] = true;
								lfoShape[selectedLfo] = 0;
								break;
							case 2:
								// triangle
								lfoShape[selectedLfo] = 1;
								break;
							case 3:
								// saw
								invertedSaw[selectedLfo] = false;
								lfoShape[selectedLfo] = 2;
								break;
							case 4:
								// inverted saw
								invertedSaw[selectedLfo] = true;
								lfoShape[selectedLfo] = 2;
								break;
							case 5:
								// random
								lfoShape[selectedLfo] = 3;
								break;
							case 6:
							case 7:
								// retrig on/off
								retrig[selectedLfo] = (action == 7);
								break;
							case 8:
							case 9:
								// loop on/off
								looping[selectedLfo] = (action == 9);
								break;
							case 10:
							case 11:
								// midi sync off
								lfoClockEnable[selectedLfo] = (action == 11);
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
								showOnOff(action == 11);
								break;
							case 12:
							case 13:
								switch (selectedLfo) {
									case 0:
										// lfo 1 velocity midi off
										lfoVel = (action == 13);
										setLFO1Vel();
										break;
									case 1:
										// lfo 2 mod wheel midi off
										lfoMod = (action == 13);
										setLFO2Mod();
										break;
									case 2:
										// lfo 3 aftertouch midi off
										lfoAt = (action == 13);
										setLFO3Aftertouch();
										break;
								}
								// digit(0, 0);
								showOnOff(action == 13);
								lastLfoSetting[selectedLfo] = (action == 13);
								break;
							case 14:
							case 15:
								// unused
								break;
						}
						lfoLedOn();
						showLfo();
					} else if ((val >= 69) && (val <= 84)) {
						setBrightness(val - 69);
					} else if ((val >= 85) && (val <= 92)) {
						arpMode = ArpMode(val - 85);
						showArpMode();
						resetVoices();
					} else {
						switch (val) {
							case 48:
							case 49:
								thru = (val == 49);
								setThru();
								showOnOff(val == 49);
								break;
							case 50:
							case 51:
								pickupMode = (val == 51);
								setPickupMode();
								showOnOff(val == 51);
								break;
							case 52:
							case 53:
								stereoCh3 = (val == 53);
								setStereoCh3();
								showOnOff(val == 53);
								break;
							case 54:
							case 55:
								mpe = (val == 55);
								setMPEMode();
								showOnOff(val == 55);
								break;
							case 56:
							case 57:
								arpClockEnable = (val == 57);
								setArpClock();
								showOnOff(val == 57);
								break;
							case 58:
							case 59:
								ignoreVolume = (val == 59);
								setIgnoreVolume();
								showOnOff(val == 59);
								break;
							case 60:
								notePriority = NOTE_PRIORITY_LOWEST;
								setNotePriority();
								break;
							case 61:
								notePriority = NOTE_PRIORITY_HIGHEST;
								setNotePriority();
								break;
							case 62:
								notePriority = NOTE_PRIORITY_LAST;
								setNotePriority();
								break;
							case 63:
							case 64:
								if (val == 63)
									fatMode = FAT_MODE_SEMITONE;
								else
									fatMode = FAT_MODE_OCTAVE;
								setFatMode();
								digit(0, 1);
								if (val == 63)
									digit(1, 5);
								else
									digit(1, 27);
								break;
							case 65:
							case 66:
								vibratoClockEnable = (val == 66);
								setVibratoClock();
								showOnOff(val == 66);
								break;
							case 67:
							case 68:
								fatSpreadMode = (val == 68);
								setFatSpreadMode();
								showOnOff(val == 68);
								break;
						}
					}
				} else if ((number >= 71) && (number <= 73)) {
					// Link LFO to target
					// CC 71 = LFO1, CC72 = LFO2, CC73 = LFO3
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

					byte selectedLfo = number - 71;
					bool isLinked = bitRead(val, 0);
					byte targetPot = val >> 1;
					if (targetPot < 51) {
						linked[selectedLfo][targetPot] = isLinked;
						showLink();
					}
				} else if (number == 74) {
					// Set voice mode (0-5)
					// Set octave offset (10-13 --> 0-3)
					// Set rate scaling
					//   * op1: 20-23
					//   * op2: 30-33
					//   * op3: 40-43
					//   * op4: 50-53
					if (val <= 5) {
						if (!mpe) {
							voiceMode = VoiceMode(val);
							showVoiceMode(voiceMode);
							resetVoices();
						}
					} else if (val >= 10 && val <= 13) {
						octOffset = val - 10;
						ledNumber(octOffset);
					} else if (val >= 20 && val <= 23) {
						// Set rate scaling for operators 1
						updateFMifNecessary(3);
						fmBase[3] = (val - 20) << 6; // 0-3 becomes 0-192 (4 steps: 0, 64, 128, 192)
						ledNumber(val - 20);
					} else if (val >= 30 && val <= 33) {
						// Set rate scaling for operators 2
						updateFMifNecessary(12);
						fmBase[12] = (val - 30) << 6;
						ledNumber(val - 30);
					} else if (val >= 40 && val <= 43) {
						// Set rate scaling for operators 3
						updateFMifNecessary(21);
						fmBase[21] = (val - 40) << 6;
						ledNumber(val - 40);
					} else if (val >= 50 && val <= 53) {
						// Set rate scaling for operators 4
						updateFMifNecessary(30);
						fmBase[30] = (val - 50) << 6;
						ledNumber(val - 50);
					}
				} else if (number == 75) {
					// Set glide
					glide = val >> 3; // 0-127 becomes 0-15
					updateGlideIncrements();
					ledNumber(val >> 1);
				} else if (number == 76) {
					fine = val << 1; // 0-127 becomes 0-254
					updateFine();
					if (fine > 127) {
						ledNumber(map(fine, 128, 255, 0, 32));
					} else if (fine < 128) {
						ledNumber(map(fine, 128, 0, 0, 32));
					}
				} else {
					if (kAllCC) {
						movedPot(number, val << 1, 1);
					} else {
						if ((number != 19) && (number != 40) && (number != 16) && (number != 38)) {
							movedPot(number, val << 1, 1);
						}
					}
				}
			}
			lastData1 = number;
			lastData2 = val;
		}
	}
}

void midiOut(byte note) {
	rightDot();
	// midiB.sendNoteOff(lastNote+1,127,masterChannelOut);
	// midiB.sendNoteOn(note+1,velocityLast,masterChannelOut);
	lastNote = note;
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
	sendCCForce(74, 20 + (fmBase[3] >> 6));   // op1 rate scaling
	sendCCForce(74, 30 + (fmBase[12] >> 6));  // op2 rate scaling
	sendCCForce(74, 40 + (fmBase[21] >> 6));  // op3 rate scaling
	sendCCForce(74, 50 + (fmBase[30] >> 6));  // op4 rate scaling
	sendCCForce(70, 56 + arpClockEnable);     // arp clock on/off
	sendCCForce(70, 65 + vibratoClockEnable); // vibrato clock on/off
	sendCCForce(74, 85 + arpMode);

	for (int i = 0; i < 3; i++) {
		byte shape = lfoShape[i];
		byte val = 0;
		if ((shape == 0))
			val = invertedSquare[i];
		else if (shape == 1)
			val = 2;
		else if (shape == 2)
			val = 3 + invertedSaw[i];
		else if (shape == 3)
			val = 5;
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

	sendCCForce(74, voiceMode);
	sendCCForce(74, 10 + octOffset);
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

void midiRead() {
	while (Serial.available()) {
		byte input = Serial.read();
		if (thru) {

			Serial.write(input);
		}

		if (input > 127) {
			// Status
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
					default:
						break;
				}
			}
		}
	}
}
