#include <Arduino.h>
#include <EEPROM.h>
#include "megafm.h"
#include "leds.h"
#include "pickup.h"
#include "FM.h"
#include "midi.h"
#include "pitchEngine.h"
#include "lfo.h"
#include "loop.h"

void movedPot(byte number, byte data, bool isMidi) {
	if (secPast) {

		bool isFader = false; // only display SSGEG if we move a fader
		// comes in as 8bit
		if ((!seqRec) && (!sendReceive)) {
			if (setupMode) {

				ledSet(10, 0);
				ledSet(11, 0);
				ledSet(12, 0);

				switch (number) {

					case KNOB_ARP_RATE:
						setupChanged = true;
						if (data > 127) {
							arpClockEnable = 1;
							digit(0, 0);
							digit(1, 19);
						} else {
							arpClockEnable = 0;
							digit(0, 0);
							digit(1, 12);
						}
						if (!isMidi)
							sendCC(70, 56 + arpClockEnable);
						break; // arp rate
					case KNOB_LFO1_RATE:
						setupChanged = true;
						if (data > 127) {
							lfoClockEnable[0] = 1;
							digit(0, 0);
							digit(1, 19);
						} else {
							lfoClockEnable[0] = 0;
							digit(0, 0);
							digit(1, 12);
						}
						if (!isMidi)
							sendCC(70, 10 + lfoClockEnable[0]);
						break; // lfo 1 rate
					case KNOB_LFO2_RATE:
						setupChanged = true;
						if (data > 127) {
							lfoClockEnable[1] = 1;
							digit(0, 0);
							digit(1, 19);
						} else {
							lfoClockEnable[1] = 0;
							digit(0, 0);
							digit(1, 12);
						}
						if (!isMidi)
							sendCC(70, 10 + 16 + lfoClockEnable[1]);
						break; // lfo 2 rate
					case KNOB_LFO3_RATE:
						setupChanged = true;
						if (data > 127) {
							lfoClockEnable[2] = 1;
							digit(0, 0);
							digit(1, 19);
						} else {
							lfoClockEnable[2] = 0;
							digit(0, 0);
							digit(1, 12);
						}
						if (!isMidi)
							sendCC(70, 10 + 32 + lfoClockEnable[2]);
						break; // lfo 3 rate
					case KNOB_VIB_RATE:
						setupChanged = true;
						if (data > 127) {
							vibratoClockEnable = 1;
							digit(0, 0);
							digit(1, 19);
						} else {
							ledSet(9, 0);
							vibratoClockEnable = 0;
							digit(0, 0);
							digit(1, 12);
						}
						if (!isMidi)
							sendCC(70, 65 + vibratoClockEnable);
						break; // vib rate

					case KNOB_LFO1_DEPTH:
						setupChanged = true;
						if (data > 127) {
							lfoVel = 1;
							digit(0, 0);
							digit(1, 19);
						} else {
							lfoVel = 0;
							digit(0, 0);
							digit(1, 12);
						}
						if (lfoVel != lastLfoSetting[0]) {
							lastLfoSetting[0] = !lastLfoSetting[0];
							digit(0, 13);
							digit(1, 18);
							delay(500);
							if (data > 127) {
								digit(0, 0);
								digit(1, 19);
							} else {
								digit(0, 0);
								digit(1, 12);
							}
						}
						if (!isMidi)
							sendCC(70, 12 + lfoVel);
						break; // lfo 1 depth

					case KNOB_LFO2_DEPTH:
						setupChanged = true;
						if (data > 127) {
							lfoMod = 1;
							digit(0, 0);
							digit(1, 19);
						} else {
							lfoMod = 0;
							digit(0, 0);
							digit(1, 12);
						}
						if (lfoMod != lastLfoSetting[1]) {
							lastLfoSetting[1] = !lastLfoSetting[1];
							digit(0, 10);
							digit(1, 1);
							delay(500);
							if (data > 127) {
								digit(0, 0);
								digit(1, 19);
							} else {
								digit(0, 0);
								digit(1, 12);
							}
						}
						if (!isMidi)
							sendCC(70, 28 + lfoMod);
						break; // lfo 2 depth

					case KNOB_LFO3_DEPTH:
						setupChanged = true;
						if (data > 127) {
							lfoAt = 1;
							digit(0, 0);
							digit(1, 19);
						} else {
							lfoAt = 0;
							digit(0, 0);
							digit(1, 12);
						}
						if (lfoAt != lastLfoSetting[2]) {
							lastLfoSetting[2] = !lastLfoSetting[2];
							digit(0, 17);
							digit(1, 26);
							delay(500);
							if (data > 127) {
								digit(0, 0);
								digit(1, 19);
							} else {
								digit(0, 0);
								digit(1, 12);
							}
						}
						if (!isMidi)
							sendCC(70, 44 + lfoAt);
						break; // lfo 3 depth

					case KNOB_FAT:
						setupChanged = true;
						if (data < 128) {
							fatMode = FAT_MODE_SEMITONE;
							digit(0, 1);
							digit(1, 5);
						} else {
							fatMode = FAT_MODE_OCTAVE;
							digit(0, 1);
							digit(1, 27);
						}
						if (!isMidi)
							sendCC(70, 63 + fatMode);
						break; // fat

					case KNOB_VOLUME:
						setupChanged = true;
						if (data > 127) {
							ignoreVolume = 0;
							digit(0, 0);
							digit(1, 19);
						} else {
							ignoreVolume = 1;
							digit(0, 0);
							digit(1, 12);
						}
						if (!isMidi)
							sendCC(70, 58 + ignoreVolume);
						break; // volume preset

					case KNOB_VIB_DEPTH:
						byte brightness = data >> 4;
						if (brightness > 15)
							brightness = 10;                   // default;
						mydisplay.setIntensity(0, brightness); // 15 = brightest
						EEPROM.write(3965, brightness);
						if (!isMidi)
							sendCC(70, 69 + brightness);
						break;
				}

			} else {

				// not setup mode
				if ((pickupMode) && (!pickup[number])) {
					// param hasn't been picked up yet, tel user if its too high or low
					doPickup(number, data);

				} else {

					switch (number) {

						// OP1
						case FADER_DETUNE_1:
							showPickupAnimation = false;
							if (loopHeld) {
								loopChanged = true;
								updateFMifNecessary(3);
								fmBase[3] = data;
								ledNumber(data >> 6);
								if (!isMidi) {
									sendCC(78, 20 + (data >> 6));
								}
							} else {
								fmBase[0] = data;
								updateFMifNecessary(0);
								if (voiceMode == kVoicingDualCh3) {
									ledNumber(data >> 2);
								} else {
									ledNumber(-3 + (data >> 5));
								}
								if (!isMidi) {
									isFader = true;
									targetPot = 0;
									sendCC(number, data >> 1);
								}
							}

							break; // detune
						case FADER_MULT_1:
							showPickupAnimation = false;
							fmBase[1] = data;
							updateFMifNecessary(1);
							showNumber(1, data);
							if (!isMidi) {
								isFader = true;
								targetPot = 1;
								sendCC(number, data >> 1);
							}
							break; // multiple
						case FADER_LEVEL_1:
							showPickupAnimation = false;
							fmBase[2] = data;
							updateFMifNecessary(2);
							showNumber(2, data);
							if (!isMidi) {
								isFader = true;
								targetPot = 2;
								sendCC(number, data >> 1);
							}
							break; // op level
						case FADER_ATTACK_1:
							showPickupAnimation = false;
							fmBase[4] = data;
							updateFMifNecessary(4);
							showNumber(4, data);
							if (!isMidi) {
								isFader = true;
								targetPot = 4;
								sendCC(number, data >> 1);
							}
							break; // attack
						case FADER_DECAY_1:
							showPickupAnimation = false;
							fmBase[5] = data;
							updateFMifNecessary(5);
							showNumber(5, data);
							if (!isMidi) {
								isFader = true;
								targetPot = 5;
								sendCC(number, data >> 1);
							}
							break; // decay1
						case FADER_SUSTAIN_1:
							showPickupAnimation = false;
							fmBase[7] = data;
							updateFMifNecessary(7);
							showNumber(7, data);
							if (!isMidi) {
								isFader = true;
								targetPot = 7;
								sendCC(number, data >> 1);
							}
							break; // sustain
						case FADER_SUSTAIN_RATE_1:
							showPickupAnimation = false;
							fmBase[6] = data;
							updateFMifNecessary(9);
							showNumber(6, data);
							if (!isMidi) {
								isFader = true;
								targetPot = 6;
								sendCC(number, data >> 1);
							}
							break; // sustain rate
						case FADER_RELEASE_1:
							showPickupAnimation = false;
							fmBase[8] = data;
							updateFMifNecessary(8);
							showNumber(8, data);
							if (!isMidi) {
								isFader = true;
								targetPot = 8;
								sendCC(number, data >> 1);
							}
							break; // release

							// OP2
						case FADER_DETUNE_2:
							showPickupAnimation = false;
							if (loopHeld) {
								loopChanged = true;
								updateFMifNecessary(12);
								fmBase[12] = data;
								ledNumber(data >> 6);
								if (!isMidi) {
									sendCC(78, 30 + (data >> 6));
								}
							} else {
								fmBase[18] = data;
								updateFMifNecessary(18);
								if (voiceMode == kVoicingDualCh3) {
									ledNumber(data >> 2);
								} else {
									ledNumber(-3 + (data >> 5));
								}
								if (!isMidi) {
									isFader = true;
									targetPot = 18;
									sendCC(number, data >> 1);
								}
							}
							break; // detune
						case FADER_MULT_2:
							showPickupAnimation = false;
							fmBase[19] = data;
							updateFMifNecessary(19);
							showNumber(19, data);
							if (!isMidi) {
								isFader = true;
								targetPot = 19;
								sendCC(number, data >> 1);
							}
							break; // multiple
						case FADER_LEVEL_2:
							showPickupAnimation = false;
							fmBase[20] = data;
							updateFMifNecessary(20);
							showNumber(20, data);
							if (!isMidi) {
								isFader = true;
								targetPot = 20;
								sendCC(number, data >> 1);
							}
							break; // op level
						case FADER_ATTACK_2:
							showPickupAnimation = false;
							fmBase[22] = data;
							updateFMifNecessary(22);
							showNumber(22, data);
							if (!isMidi) {
								isFader = true;
								targetPot = 22;
								sendCC(number, data >> 1);
							}
							break; // attack WAS 59
						case FADER_DECAY_2:
							showPickupAnimation = false;
							fmBase[23] = data;
							updateFMifNecessary(23);
							showNumber(23, data);
							if (!isMidi) {
								isFader = true;
								targetPot = 23;
								sendCC(number, data >> 1);
							}
							break; // decay1 WAS 50
						case FADER_SUSTAIN_2:
							showPickupAnimation = false;
							fmBase[25] = data;
							updateFMifNecessary(25);
							showNumber(25, data);
							if (!isMidi) {
								isFader = true;
								targetPot = 25;
								sendCC(number, data >> 1);
							}
							break; // sustain WAS 60
						case FADER_SUSTAIN_RATE_2:
							showPickupAnimation = false;
							fmBase[24] = data;
							updateFMifNecessary(24);
							showNumber(24, data);
							if (!isMidi) {
								isFader = true;
								targetPot = 24;
								sendCC(number, data >> 1);
							}
							break; // sustain rate WAS 55
						case FADER_RELEASE_2:
							showPickupAnimation = false;
							fmBase[26] = data;
							updateFMifNecessary(26);
							showNumber(26, data);
							if (!isMidi) {
								isFader = true;
								targetPot = 26;
								sendCC(number, data >> 1);
							}
							break; // release WAS 52

							// OP3
						case FADER_DETUNE_3:
							showPickupAnimation = false;

							if (loopHeld) {
								loopChanged = true;
								updateFMifNecessary(21);
								fmBase[21] = data;
								ledNumber(data >> 6);
								if (!isMidi) {
									sendCC(78, 40 + (data >> 6));
								}
							} else {
								fmBase[9] = data;
								updateFMifNecessary(9);
								if (voiceMode == kVoicingDualCh3) {
									ledNumber(data >> 2);
								} else {
									ledNumber(-3 + (data >> 5));
								}
								if (!isMidi) {
									isFader = true;
									targetPot = 9;
									sendCC(number, data >> 1);
								}
							}
							break; // detune
						case FADER_MULT_3:
							showPickupAnimation = false;
							fmBase[10] = data;
							updateFMifNecessary(10);
							showNumber(10, data);
							if (!isMidi) {
								isFader = true;
								targetPot = 10;
								sendCC(number, data >> 1);
							}
							break; // multiple
						case FADER_LEVEL_3:
							showPickupAnimation = false;
							fmBase[11] = data;
							updateFMifNecessary(11);
							showNumber(11, data);
							if (!isMidi) {
								isFader = true;
								targetPot = 11;
								sendCC(number, data >> 1);
							}
							break; // op level
						case FADER_ATTACK_3:
							showPickupAnimation = false;
							fmBase[13] = data;
							updateFMifNecessary(13);
							showNumber(13, data);
							if (!isMidi) {
								isFader = true;
								targetPot = 13;
								sendCC(49, data >> 1);
							}
							break; // attack
						case FADER_DECAY_3:
							showPickupAnimation = false;
							fmBase[14] = data;
							updateFMifNecessary(14);
							showNumber(14, data);
							if (!isMidi) {
								isFader = true;
								targetPot = 14;
								sendCC(50, data >> 1);
							}
							break; // decay1
						case FADER_SUSTAIN_3:
							showPickupAnimation = false;
							fmBase[16] = data;
							updateFMifNecessary(16);
							showNumber(16, data);
							if (!isMidi) {
								isFader = true;
								targetPot = 16;
								sendCC(51, data >> 1);
							}
							break; // sustain
						case FADER_SUSTAIN_RATE_3:
							showPickupAnimation = false;
							fmBase[15] = data;
							updateFMifNecessary(15);
							showNumber(15, data);
							if (!isMidi) {
								isFader = true;
								targetPot = 15;
								sendCC(number, data >> 1);
							}
							break; // sustain rate
						case FADER_RELEASE_3:
							showPickupAnimation = false;
							fmBase[17] = data;
							updateFMifNecessary(17);
							showNumber(17, data);
							if (!isMidi) {
								isFader = true;
								targetPot = 17;
								sendCC(number, data >> 1);
							}
							break; // release

							// OP4
						case FADER_DETUNE_4:
							showPickupAnimation = false;
							if (loopHeld) {
								loopChanged = true;
								updateFMifNecessary(30);
								fmBase[30] = data;
								ledNumber(data >> 6);
								if (!isMidi) {
									sendCC(78, 50 + (data >> 6));
								}
							} else {
								fmBase[27] = data;
								updateFMifNecessary(27);
								if (voiceMode == kVoicingDualCh3) {
									ledNumber(data >> 2);
								} else {
									ledNumber(-3 + (data >> 5));
								}
								if (!isMidi) {
									isFader = true;
									targetPot = 27;
									sendCC(number, data >> 1);
								}
							}
							break; // detune
						case FADER_MULT_4:
							showPickupAnimation = false;
							fmBase[28] = data;
							updateFMifNecessary(28);
							showNumber(28, data);
							if (!isMidi) {
								isFader = true;
								targetPot = 28;
								sendCC(number, data >> 1);
							}
							break; // multiple
						case FADER_LEVEL_4:
							showPickupAnimation = false;
							fmBase[29] = data;
							updateFMifNecessary(29);
							showNumber(29, data);
							if (!isMidi) {
								isFader = true;
								targetPot = 29;
								sendCC(number, data >> 1);
							}
							break; // op level
						case FADER_ATTACK_4:
							showPickupAnimation = false;
							fmBase[31] = data;
							updateFMifNecessary(31);
							showNumber(31, data);
							if (!isMidi) {
								isFader = true;
								targetPot = 31;
								sendCC(number, data >> 1);
							}
							break; // attack
						case FADER_DECAY_4:
							showPickupAnimation = false;
							fmBase[32] = data;
							updateFMifNecessary(32);
							showNumber(32, data);
							if (!isMidi) {
								isFader = true;
								targetPot = 32;
								sendCC(number, data >> 1);
							}
							break; // decay1
						case FADER_SUSTAIN_4:
							showPickupAnimation = false;
							fmBase[34] = data;
							updateFMifNecessary(34);
							showNumber(34, data);
							if (!isMidi) {
								isFader = true;
								targetPot = 34;
								sendCC(number, data >> 1);
							}
							break; // sustain
						case FADER_SUSTAIN_RATE_4:
							showPickupAnimation = false;
							fmBase[33] = data;
							updateFMifNecessary(33);
							showNumber(33, data);
							if (!isMidi) {
								isFader = true;
								targetPot = 33;
								sendCC(number, data >> 1);
							}
							break; // sustain rate
						case FADER_RELEASE_4:
							showPickupAnimation = false;
							fmBase[35] = data;
							updateFMifNecessary(35);
							showNumber(35, data);
							if (!isMidi) {
								isFader = true;
								targetPot = 35;
								sendCC(number, data >> 1);
							}
							break; // release

						case KNOB_VOLUME:
							showPickupAnimation = false;
							setupCounter = 0; // prevent entering setup
							if (voiceHeld) {
								movedFineKnob = true;

								fine = finePot = data;
								updateFine();

								if (fine > 127) {
									ledNumber(map(fine, 128, 255, 0, 32));
								} else if (fine < 128) {
									ledNumber(map(fine, 128, 0, 0, 32));
								}
								if (!isMidi)
									sendCC(76, data >> 1);
							} else {

								vol = 128 - (data >> 1);
								if (lastVol != vol) {
									changeVol = true;
									lastVol = vol;
									ledNumber(data >> 2);
									volumeCounter = 20;
									if (!isMidi) {
										sendCC(7, data >> 1);
									}
								}
							}
							break; // volume

						case KNOB_ALGO:
							showPickupAnimation = false;
							if (isMidi) {
								data -= 1;
							}
							fmBase[42] = data;
							updateFMifNecessary(42);
							showNumber(42, data);
							if (!isMidi) {
								targetPot = 42;
								sendCC(number, (1 + (data >> 5)));
							}
							break; // algo

						case KNOB_FEEDBACK:
							showPickupAnimation = false;
							fmBase[43] = data;
							updateFMifNecessary(43);
							showNumber(43, data);
							if (!isMidi) {
								targetPot = 43;
								sendCC(number, (data >> 1));
							}
							break; // feedback

						case KNOB_FAT:
							showPickupAnimation = false;
							setupCounter = 0; // prevent entering setup

							if (voiceHeld && movedFineKnob) {
								fine = finePot + map(data, 0, 255, -10, 10);
								fine = constrain(fine, 0, 255);
								updateFine();

							} else {

								if (voiceHeld) {
									glide = data >> 4;
									updateGlideIncrements();
									fineChanged = true;
									ledNumber(data >> 2);
									if (!isMidi)
										sendCC(75, data >> 1);
								} else {
									fmBase[50] = data;
									updateFMifNecessary(50);
									ledNumber(data >> 2);
									if (!isMidi) {
										targetPot = 50;
										sendCC(number, data >> 1);
									}
								}
							}
							break; // fat 1-127

						case KNOB_LFO1_RATE:
							showPickupAnimation = false;
							fmBase[36] = data;
							if (data < 5 && lfoVel) {
								fmBase[36] = data = 0;
							}
							updateFMifNecessary(36);
							if (lfoVel) {
								digit(0, 13);
								digit(1, 18);
							} else {
								selectedLfo = 0;
								if ((lfoClockEnable[0]) && (sync)) {
									byte valueToShow = map(data, 0, 255, 0, 9);
									if (valueToShow > 2) {
										ledNumber(kArpRateDisplay[valueToShow]);
									} // it's less than a bar, show division
									else {
										digit(0, 25); // b
										switch (valueToShow) {
											case 0:
												digit(1, 4);
												break;
											case 1:
												digit(1, 2);
												break;
											case 2:
												digit(1, 1);
												break;
										}
									} // show number of bars (b1 or b2 or b4)

								} else {
									ledNumber(data >> 2);
								}
								if (!isMidi) {
									targetPot = 36;
									sendCC(number, data >> 1);
								}
							}
							break; // lfo 1 rate
						case KNOB_LFO1_DEPTH:
							showPickupAnimation = false;
							fmBase[37] = data;
							updateFMifNecessary(37);
							selectedLfo = 0;
							showNumber(37, data);
							if (!isMidi) {
								targetPot = 37;
								sendCC(number, data >> 1);
							}
							break; // lfo 1 depth

						case KNOB_LFO2_RATE:
							showPickupAnimation = false;
							fmBase[38] = data;
							updateFMifNecessary(38);
							if ((lfoMod) && (!data)) {
								digit(0, 10);
								digit(1, 1);
							} else {
								selectedLfo = 1;
								if ((lfoClockEnable[1]) && (sync)) {
									ledNumber(kArpRateDisplay[data >> 5]);
								} else {
									ledNumber(data >> 2);
								}
								if (!isMidi) {
									targetPot = 38;
									sendCC(number, data >> 1);
								}
							}
							break; // lfo 2 rate
						case KNOB_LFO2_DEPTH:
							showPickupAnimation = false;
							fmBase[39] = data;
							updateFMifNecessary(39);
							selectedLfo = 1;
							showNumber(39, data);
							if (!isMidi) {
								targetPot = 39;
								sendCC(number, data >> 1);
							}
							break; // lfo 2 depth

						case KNOB_LFO3_RATE:
							showPickupAnimation = false;
							fmBase[40] = data;
							updateFMifNecessary(40);

							if (data < 5 && lfoAt) {
								fmBase[40] = data = 0;
							}

							if (lfoAt) {
								digit(0, 17);
								digit(1, 26);
							} else {
								selectedLfo = 2;
								if ((lfoClockEnable[2]) && (sync)) {
									ledNumber(kArpRateDisplay[data >> 5]);
								} else {
									ledNumber(data >> 2);
								}
								if (!isMidi) {
									targetPot = 40;
									sendCC(number, data >> 1);
								}
							}
							break; // lfo 3 rate
						case KNOB_LFO3_DEPTH:
							showPickupAnimation = false;
							fmBase[41] = data;
							updateFMifNecessary(41);
							selectedLfo = 2;
							showNumber(41, data);
							if (!isMidi) {
								targetPot = 41;
								sendCC(number, data >> 1);
							}
							break; // lfo 3 depth

						case KNOB_ARP_RATE:
							showPickupAnimation = false;
							fmBase[46] = data;
							updateFMifNecessary(46);
							if (sync) {
								ledNumber(kArpRateDisplay[map(data, 0, 255, 0, 10)]);
								arpMidiSpeedPending = map(data, 0, 255, 0, 10);
							} else {
								ledNumber(data >> 2);
							}
							if (!isMidi) {
								targetPot = 46;
								sendCC(number, data >> 1);
							}
							break; /// arp rate
						case KNOB_ARP_RANGE:
							showPickupAnimation = false;
							// This looks wrong; in midi.cpp we already do data << 1 before coming here; this makes this
							// go up to 16384; Now, if this is NOT midi data but pot - which seems to go 0-255 looking
							// at the midi sending of data >> 1 - there is a mismatch.
							if (isMidi) {
								data = data << 6;
							}
							fmBase[47] = data;
							updateFMifNecessary(47);
							showNumber(47, data);
							if (!isMidi) {
								targetPot = 47;
								sendCC(number, data >> 1);
							}
							break; // arp range

						case KNOB_VIB_RATE:
							showPickupAnimation = false;
							fmBase[48] = data;
							updateFMifNecessary(48);
							if ((vibratoClockEnable) && (sync)) {
								ledNumber(data >> 5);
							} else {
								ledNumber(data >> 3);
							}
							if (!isMidi) {
								targetPot = 48;
								sendCC(number, data >> 1);
							}
							break; // vibrato rate WAS 7
						case KNOB_VIB_DEPTH:
							showPickupAnimation = false;
							fmBase[49] = data;
							updateFMifNecessary(49);
							showNumber(49, data);
							if (!isMidi) {
								targetPot = 49;
								sendCC(number, data >> 1);
							}
							break; // vibrato depth
					}
				}

				// update display
				if (targetPotLast != targetPot) {
					targetPotLast = targetPot;
					showLink();
					applyLfo();
				}
				if (selectedLfoLast != selectedLfo) {
					selectedLfoLast = selectedLfo;
					showSSEGCounter = 0; // quit the SSEG display
					showLfo();
				} // show lfo because we moved to another

				if (isFader) {
					if (targetPot < 9) {
						lastOperator = 0;
						showSSEG();
					} else if ((targetPot > 8) && (targetPot < 18)) {
						lastOperator = 1;
						showSSEG();
					} else if ((targetPot > 17) && (targetPot < 27)) {
						lastOperator = 2;
						showSSEG();
					} else if ((targetPot > 26) && (targetPot < 36)) {
						lastOperator = 3;
						showSSEG();
					}
				}
			}
		}
	}
}