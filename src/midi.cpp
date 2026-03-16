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
#include "nrpn.h"
#include "pots.h"
#include "buttons.h"
#include "midi_pedal.hpp"
#include "FM.h"
#include "ISR.h"
#include "setters.h"

static byte voiceSlot;
static bool ch3Alt;
static float vibIndexF;

static byte lastCC[80];

static int arpClockCounter;
static byte syncLfoCounter;

static bool arpClearFlag = false;

void initLastCC() {
	for (int i = 0; i < 80; i++) {
		lastCC[i] = 255;
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
				vibIndexF += kLfoClockRates[map(fmData[48], 0, 255, 0, 12)];
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
	if ((program < 100) && (channel == inputChannel)) {

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

	if (lastCC[number] != value) {
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

void HandleControlChange(byte channel, byte number, byte val) {
	byte temp;

	// Update the deduplication cache with the received value so that a subsequent
	// sendCC() with the same value is suppressed — prevents echoing a received
	// message back to the sender.
	if (number < 80)
		lastCC[number] = val;

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
					if (val < 6) {
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

					movedPot(pot, val << 1, 1);
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
				sendCC(number, fmBase[0] >> 1);
				break; // detune
			case 27:
				sendCC(number, fmBase[1] >> 1);
				break; // multiple
			case 19:
				sendCC(number, fmBase[2] >> 1);
				break; // op level
			case 29:
				sendCC(number, fmBase[4] >> 1);
				break; // attack
			case 21:
				sendCC(number, fmBase[5] >> 1);
				break; // decay1
			case 25:
				sendCC(number, fmBase[7] >> 1);
				break; // sustain
			case 17:
				sendCC(number, fmBase[6] >> 1);
				break; // sustain rate
			case 30:
				sendCC(number, fmBase[8] >> 1);
				break; // release
			// OP2
			case 31:
				sendCC(number, fmBase[18] >> 1);
				break; // detune
			case 32:
				sendCC(number, fmBase[19] >> 1);
				break; // multiple
			case 40:
				sendCC(number, fmBase[20] >> 1);
				break; // op level
			case 36:
				sendCC(number, fmBase[22] >> 1);
				break; // attack
			case 44:
				sendCC(number, fmBase[23] >> 1);
				break; // decay1
			case 42:
				sendCC(number, fmBase[25] >> 1);
				break; // sustain
			case 34:
				sendCC(number, fmBase[24] >> 1);
				break; // sustain rate
			case 11:
				sendCC(number, fmBase[26] >> 1);
				break; // release
			// OP3
			case 20:
				sendCC(number, fmBase[9] >> 1);
				break; // detune
			case 24:
				sendCC(number, fmBase[10] >> 1);
				break; // multiple
			case 16:
				sendCC(number, fmBase[11] >> 1);
				break; // op level
			case 8:
				sendCC(49, fmBase[13] >> 1);
				break; // attack
			case 0:
				sendCC(50, fmBase[14] >> 1);
				break; // decay1
			case 7:
				sendCC(51, fmBase[16] >> 1);
				break; // sustain
			case 45:
				sendCC(number, fmBase[15] >> 1);
				break; // sustain rate
			case 37:
				sendCC(number, fmBase[17] >> 1);
				break; // release
			// OP4
			case 47:
				sendCC(number, fmBase[27] >> 1);
				break; // detune
			case 39:
				sendCC(number, fmBase[28] >> 1);
				break; // multiple
			case 38:
				sendCC(number, fmBase[29] >> 1);
				break; // op level
			case 46:
				sendCC(number, fmBase[31] >> 1);
				break; // attack
			case 33:
				sendCC(number, fmBase[32] >> 1);
				break; // decay1
			case 41:
				sendCC(number, fmBase[34] >> 1);
				break; // sustain
			case 43:
				sendCC(number, fmBase[33] >> 1);
				break; // sustain rate
			case 35:
				sendCC(number, fmBase[35] >> 1);
				break; // release

			case 1:
				sendCC(7, vol >> 1);
				break; // volume //SEND FINE!!!!!!!!!!!!
			case 4:
				sendCC(number, (1 + (fmBase[42] >> 5)));
				break; // algo
			case 3:
				sendCC(number, fmBase[43] >> 1);
				break; // feedback
			case 28:
				sendCC(number, fmBase[50] >> 1);
				break; // fat 1-127 // SEND GLIDE!!!!!!!!!!!!
			case 15:
				sendCC(number, fmBase[36] >> 1);
				break; // lfo 1 rate
			case 12:
				sendCC(number, fmBase[37] >> 1);
				break; // lfo 1 depth
			case 10:
				sendCC(number, fmBase[38] >> 1);
				break; // lfo 2 rate
			case 9:
				sendCC(number, fmBase[39] >> 1);
				break; // lfo 2 depth
			case 14:
				sendCC(number, fmBase[40] >> 1);
				break; // lfo 3 rate
			case 2:
				sendCC(number, fmBase[41] >> 1);
				break; // lfo 3 depth
			case 6:
				sendCC(number, fmBase[46] >> 1);
				break; /// arp rate
			case 5:
				sendCC(number, fmBase[47] >> 1);
				break; // arp range
			case 48:
				sendCC(number, fmBase[48] >> 1);
				break; // vibrato rate WAS 7
			case 13:
				sendCC(number, fmBase[49] >> 1);
				break; // vibrato depth
		}
	}
}

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


////////////////////////////////////////////////////////////////////////
// SYSEX IMPLEMENTATION
////////////////////////////////////////////////////////////////////////

#define MAX_SYSEX_DATA_LENGTH 2048
int sysExDataIndex = 0;
byte sysExData[MAX_SYSEX_DATA_LENGTH];

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
	mStatus = 0;
	sysExDataIndex = 0;
	mData = 0;
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

	mStatus = 0;
	mData = 0;

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

////////////////////////////////////////////////////////////////////////
// SYSEX IMPLEMENTATION END
////////////////////////////////////////////////////////////////////////


void midiRead() {
	while (Serial.available()) {
		byte input = Serial.read();
		if (thru) {

			Serial.write(input);
		}

		if (input > 127) {
			// Status
			if ((mStatus == 8) && (input == 247)) { // input == F7
				// In SysEx and receivd Sysex end; handle data and end sysex mode
				handleSysEx();
				abortSysEx(false);
			} else if (mStatus == 8) {
				// In SysEx but received a non-F7 status byte; invalid message, exit SysEx mode
				abortSysEx(true);
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
							// check if it's a SysEx message for us ( - we do the same as the firmware and take the
							// default from hex2sys = \x00\x21\x44 = 00 33 68)
							if (sysExData[0] != 0 || sysExData[1] != 33 || sysExData[2] != 68) {
								// not for us, ignore the rest of the message
								abortSysEx(true);
								break;
							}
						} else if (sysExDataIndex == MAX_SYSEX_DATA_LENGTH) {
							abortSysEx(true);
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

