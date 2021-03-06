/* Copyright (c) 2013-2016 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "mbc.h"

#include "gb/gb.h"
#include "gb/memory.h"

#include <time.h>

mLOG_DEFINE_CATEGORY(GB_MBC, "GB MBC");

static void _GBMBCNone(struct GB* gb, uint16_t address, uint8_t value) {
	UNUSED(gb);
	UNUSED(address);
	UNUSED(value);

	mLOG(GB_MBC, GAME_ERROR, "Wrote to invalid MBC");
}

static void _GBMBC1(struct GB*, uint16_t address, uint8_t value);
static void _GBMBC2(struct GB*, uint16_t address, uint8_t value);
static void _GBMBC3(struct GB*, uint16_t address, uint8_t value);
static void _GBMBC5(struct GB*, uint16_t address, uint8_t value);
static void _GBMBC6(struct GB*, uint16_t address, uint8_t value);
static void _GBMBC7(struct GB*, uint16_t address, uint8_t value);
static void _GBHuC3(struct GB*, uint16_t address, uint8_t value);

void GBMBCSwitchBank(struct GBMemory* memory, int bank) {
	size_t bankStart = bank * GB_SIZE_CART_BANK0;
	if (bankStart + GB_SIZE_CART_BANK0 > memory->romSize) {
		mLOG(GB_MBC, GAME_ERROR, "Attempting to switch to an invalid ROM bank: %0X", bank);
		bankStart &= (memory->romSize - 1);
		bank = bankStart / GB_SIZE_CART_BANK0;
	}
	memory->romBank = &memory->rom[bankStart];
	memory->currentBank = bank;
}

void GBMBCSwitchSramBank(struct GB* gb, int bank) {
	size_t bankStart = bank * GB_SIZE_EXTERNAL_RAM;
	GBResizeSram(gb, (bank + 1) * GB_SIZE_EXTERNAL_RAM + (gb->sramSize & 0xFF));
	gb->memory.sramBank = &gb->memory.sram[bankStart];
	gb->memory.sramCurrentBank = bank;
}

void GBMBCInit(struct GB* gb) {
	const struct GBCartridge* cart = (const struct GBCartridge*) &gb->memory.rom[0x100];
	switch (cart->ramSize) {
	case 0:
		gb->sramSize = 0;
		break;
	case 1:
		gb->sramSize = 0x800;
		break;
	default:
	case 2:
		gb->sramSize = 0x2000;
		break;
	case 3:
		gb->sramSize = 0x8000;
		break;
	}

	if (gb->memory.mbcType == GB_MBC_AUTODETECT) {
		const struct GBCartridge* cart = (const struct GBCartridge*) &gb->memory.rom[0x100];
		switch (cart->type) {
		case 0:
		case 8:
		case 9:
			gb->memory.mbcType = GB_MBC_NONE;
			break;
		case 1:
		case 2:
		case 3:
			gb->memory.mbcType = GB_MBC1;
			break;
		case 5:
		case 6:
			gb->memory.mbcType = GB_MBC2;
			break;
		case 0x0F:
		case 0x10:
			gb->memory.mbcType = GB_MBC3_RTC;
			break;
		case 0x11:
		case 0x12:
		case 0x13:
			gb->memory.mbcType = GB_MBC3;
			break;
		default:
			mLOG(GB_MBC, WARN, "Unknown MBC type: %02X", cart->type);
			// Fall through
		case 0x19:
		case 0x1A:
		case 0x1B:
			gb->memory.mbcType = GB_MBC5;
			break;
		case 0x1C:
		case 0x1D:
		case 0x1E:
			gb->memory.mbcType = GB_MBC5_RUMBLE;
			break;
		case 0x20:
			gb->memory.mbcType = GB_MBC6;
			break;
		case 0x22:
			gb->memory.mbcType = GB_MBC7;
			break;
		case 0xFE:
			gb->memory.mbcType = GB_HuC3;
			break;
		}
	}
	switch (gb->memory.mbcType) {
	case GB_MBC_NONE:
		gb->memory.mbc = _GBMBCNone;
		break;
	case GB_MBC1:
		gb->memory.mbc = _GBMBC1;
		break;
	case GB_MBC2:
		gb->memory.mbc = _GBMBC2;
		gb->sramSize = 0x200;
		break;
	case GB_MBC3:
		gb->memory.mbc = _GBMBC3;
		gb->sramSize += 0x48;
		break;
	default:
		mLOG(GB_MBC, WARN, "Unknown MBC type: %02X", cart->type);
		// Fall through
	case GB_MBC5:
		gb->memory.mbc = _GBMBC5;
		break;
	case GB_MBC6:
		mLOG(GB_MBC, WARN, "unimplemented MBC: MBC6");
		gb->memory.mbc = _GBMBC6;
		break;
	case GB_MBC7:
		gb->memory.mbc = _GBMBC7;
		break;
	case GB_MMM01:
		mLOG(GB_MBC, WARN, "unimplemented MBC: MMM01");
		gb->memory.mbc = _GBMBC1;
		break;
	case GB_HuC1:
		mLOG(GB_MBC, WARN, "unimplemented MBC: HuC-1");
		gb->memory.mbc = _GBMBC1;
		break;
	case GB_HuC3:
		gb->memory.mbc = _GBHuC3;
		break;
	case GB_MBC3_RTC:
		gb->memory.mbc = _GBMBC3;
		break;
	case GB_MBC5_RUMBLE:
		gb->memory.mbc = _GBMBC5;
		break;
	}

	GBResizeSram(gb, gb->sramSize);
}

static void _latchRtc(struct GBMemory* memory) {
	time_t t;
	struct mRTCSource* rtc = memory->rtc;
	if (rtc) {
		if (rtc->sample) {
			rtc->sample(rtc);
		}
		t = rtc->unixTime(rtc);
	} else {
		t = time(0);
	}
	struct tm date;
	localtime_r(&t, &date);
	memory->rtcRegs[0] = date.tm_sec;
	memory->rtcRegs[1] = date.tm_min;
	memory->rtcRegs[2] = date.tm_hour;
	memory->rtcRegs[3] = date.tm_yday; // TODO: Persist day counter
	memory->rtcRegs[4] &= 0xF0;
	memory->rtcRegs[4] |= date.tm_yday >> 8;
}

void _GBMBC1(struct GB* gb, uint16_t address, uint8_t value) {
	struct GBMemory* memory = &gb->memory;
	int bank = value & 0x1F;
	switch (address >> 13) {
	case 0x0:
		switch (value) {
		case 0:
			memory->sramAccess = false;
			break;
		case 0xA:
			memory->sramAccess = true;
			GBMBCSwitchSramBank(gb, memory->sramCurrentBank);
			break;
		default:
			// TODO
			mLOG(GB_MBC, STUB, "MBC1 unknown value %02X", value);
			break;
		}
		break;
	case 0x1:
		if (!bank) {
			++bank;
		}
		GBMBCSwitchBank(memory, bank | (memory->currentBank & 0x60));
		break;
	case 0x2:
		bank &= 3;
		if (!memory->mbcState.mbc1.mode) {
			GBMBCSwitchBank(memory, (bank << 5) | (memory->currentBank & 0x1F));
		} else {
			GBMBCSwitchSramBank(gb, bank);
		}
		break;
	case 0x3:
		memory->mbcState.mbc1.mode = value & 1;
		if (memory->mbcState.mbc1.mode) {
			GBMBCSwitchBank(memory, memory->currentBank & 0x1F);
		} else {
			GBMBCSwitchSramBank(gb, 0);
		}
		break;
	default:
		// TODO
		mLOG(GB_MBC, STUB, "MBC1 unknown address: %04X:%02X", address, value);
		break;
	}
}

void _GBMBC2(struct GB* gb, uint16_t address, uint8_t value) {
	struct GBMemory* memory = &gb->memory;
	int bank = value & 0xF;
	switch (address >> 13) {
	case 0x0:
		switch (value) {
		case 0:
			memory->sramAccess = false;
			break;
		case 0xA:
			memory->sramAccess = true;
			GBMBCSwitchSramBank(gb, memory->sramCurrentBank);
			break;
		default:
			// TODO
			mLOG(GB_MBC, STUB, "MBC1 unknown value %02X", value);
			break;
		}
		break;
	case 0x1:
		if (!bank) {
			++bank;
		}
		GBMBCSwitchBank(memory, bank);
		break;
	default:
		// TODO
		mLOG(GB_MBC, STUB, "MBC2 unknown address: %04X:%02X", address, value);
		break;
	}}

void _GBMBC3(struct GB* gb, uint16_t address, uint8_t value) {
	struct GBMemory* memory = &gb->memory;
	int bank = value & 0x7F;
	switch (address >> 13) {
	case 0x0:
		switch (value) {
		case 0:
			memory->sramAccess = false;
			break;
		case 0xA:
			memory->sramAccess = true;
			GBMBCSwitchSramBank(gb, memory->sramCurrentBank);
			break;
		default:
			// TODO
			mLOG(GB_MBC, STUB, "MBC3 unknown value %02X", value);
			break;
		}
		break;
	case 0x1:
		if (!bank) {
			++bank;
		}
		GBMBCSwitchBank(memory, bank);
		break;
	case 0x2:
		if (value < 4) {
			GBMBCSwitchSramBank(gb, value);
			memory->rtcAccess = false;
		} else if (value >= 8 && value <= 0xC) {
			memory->activeRtcReg = value - 8;
			memory->rtcAccess = true;
		}
		break;
	case 0x3:
		if (memory->rtcLatched && value == 0) {
			memory->rtcLatched = false;
		} else if (!memory->rtcLatched && value == 1) {
			_latchRtc(memory);
			memory->rtcLatched = true;
		}
		break;
	}
}

void _GBMBC5(struct GB* gb, uint16_t address, uint8_t value) {
	struct GBMemory* memory = &gb->memory;
	int bank;
	switch (address >> 12) {
	case 0x0:
	case 0x1:
		switch (value) {
		case 0:
			memory->sramAccess = false;
			break;
		case 0xA:
			memory->sramAccess = true;
			GBMBCSwitchSramBank(gb, memory->sramCurrentBank);
			break;
		default:
			// TODO
			mLOG(GB_MBC, STUB, "MBC5 unknown value %02X", value);
			break;
		}
		break;
	case 0x2:
		bank = (memory->currentBank & 0x100) | value;
		GBMBCSwitchBank(memory, bank);
		break;
	case 0x3:
		bank = (memory->currentBank & 0xFF) | ((value & 1) << 8);
		GBMBCSwitchBank(memory, bank);
		break;
	case 0x4:
	case 0x5:
		if (memory->mbcType == GB_MBC5_RUMBLE && memory->rumble) {
			memory->rumble->setRumble(memory->rumble, (value >> 3) & 1);
			value &= ~8;
		}
		GBMBCSwitchSramBank(gb, value & 0xF);
		break;
	default:
		// TODO
		mLOG(GB_MBC, STUB, "MBC5 unknown address: %04X:%02X", address, value);
		break;
	}
}

void _GBMBC6(struct GB* gb, uint16_t address, uint8_t value) {
	// TODO
	mLOG(GB_MBC, STUB, "MBC6 unimplemented");
	UNUSED(gb);
	UNUSED(address);
	UNUSED(value);
}

void _GBMBC7(struct GB* gb, uint16_t address, uint8_t value) {
	struct GBMemory* memory = &gb->memory;
	int bank = value & 0x7F;
	switch (address >> 13) {
	case 0x1:
		GBMBCSwitchBank(memory, bank);
		break;
	case 0x2:
		if (value < 0x10) {
			GBMBCSwitchSramBank(gb, value);
		}
		break;
	default:
		// TODO
		mLOG(GB_MBC, STUB, "MBC7 unknown address: %04X:%02X", address, value);
		break;
	}
}

uint8_t GBMBC7Read(struct GBMemory* memory, uint16_t address) {
	struct GBMBC7State* mbc7 = &memory->mbcState.mbc7;
	switch (address & 0xF0) {
	case 0x00:
	case 0x10:
	case 0x60:
	case 0x70:
		return 0;
	case 0x20:
		if (memory->rotation && memory->rotation->readTiltX) {
			int32_t x = -memory->rotation->readTiltX(memory->rotation);
			x >>= 21;
			x += 2047;
			return x;
		}
		return 0xFF;
	case 0x30:
		if (memory->rotation && memory->rotation->readTiltX) {
			int32_t x = -memory->rotation->readTiltX(memory->rotation);
			x >>= 21;
			x += 2047;
			return x >> 8;
		}
		return 7;
	case 0x40:
		if (memory->rotation && memory->rotation->readTiltY) {
			int32_t y = -memory->rotation->readTiltY(memory->rotation);
			y >>= 21;
			y += 2047;
			return y;
		}
		return 0xFF;
	case 0x50:
		if (memory->rotation && memory->rotation->readTiltY) {
			int32_t y = -memory->rotation->readTiltY(memory->rotation);
			y >>= 21;
			y += 2047;
			return y >> 8;
		}
		return 7;
	case 0x80:
		return (mbc7->sr >> 16) & 1;
	default:
		return 0xFF;
	}
}

void GBMBC7Write(struct GBMemory* memory, uint16_t address, uint8_t value) {
	if ((address & 0xF0) != 0x80) {
		return;
	}
	struct GBMBC7State* mbc7 = &memory->mbcState.mbc7;
	GBMBC7Field old = memory->mbcState.mbc7.field;
	mbc7->field = GBMBC7FieldClearIO(value);
	if (!GBMBC7FieldIsCS(old) && GBMBC7FieldIsCS(value)) {
		if (mbc7->state == GBMBC7_STATE_WRITE) {
			if (mbc7->writable) {
				memory->sramBank[mbc7->address * 2] = mbc7->sr >> 8;
				memory->sramBank[mbc7->address * 2 + 1] = mbc7->sr;
			}
			mbc7->sr = 0x1FFFF;
			mbc7->state = GBMBC7_STATE_NULL;
		} else {
			mbc7->state = GBMBC7_STATE_IDLE;
		}
	}
	if (!GBMBC7FieldIsSK(old) && GBMBC7FieldIsSK(value)) {
		if (mbc7->state > GBMBC7_STATE_IDLE && mbc7->state != GBMBC7_STATE_READ) {
			mbc7->sr <<= 1;
			mbc7->sr |= GBMBC7FieldGetIO(value);
			++mbc7->srBits;
		}
		switch (mbc7->state) {
		case GBMBC7_STATE_IDLE:
			if (GBMBC7FieldIsIO(value)) {
				mbc7->state = GBMBC7_STATE_READ_COMMAND;
				mbc7->srBits = 0;
				mbc7->sr = 0;
			}
			break;
		case GBMBC7_STATE_READ_COMMAND:
			if (mbc7->srBits == 2) {
				mbc7->state = GBMBC7_STATE_READ_ADDRESS;
				mbc7->srBits = 0;
				mbc7->command = mbc7->sr;
			}
			break;
		case GBMBC7_STATE_READ_ADDRESS:
			if (mbc7->srBits == 8) {
				mbc7->state = GBMBC7_STATE_COMMAND_0 + mbc7->command;
				mbc7->srBits = 0;
				mbc7->address = mbc7->sr;
				if (mbc7->state == GBMBC7_STATE_COMMAND_0) {
					switch (mbc7->address >> 6) {
					case 0:
						mbc7->writable = false;
						mbc7->state = GBMBC7_STATE_NULL;
						break;
					case 3:
						mbc7->writable = true;
						mbc7->state = GBMBC7_STATE_NULL;
						break;
					}
				}
			}
			break;
		case GBMBC7_STATE_COMMAND_0:
			if (mbc7->srBits == 16) {
				switch (mbc7->address >> 6) {
				case 0:
					mbc7->writable = false;
					mbc7->state = GBMBC7_STATE_NULL;
					break;
				case 1:
					mbc7->state = GBMBC7_STATE_WRITE;
					if (mbc7->writable) {
						int i;
						for (i = 0; i < 256; ++i) {
							memory->sramBank[i * 2] = mbc7->sr >> 8;
							memory->sramBank[i * 2 + 1] = mbc7->sr;
						}
					}
					break;
				case 2:
					mbc7->state = GBMBC7_STATE_WRITE;
					if (mbc7->writable) {
						int i;
						for (i = 0; i < 256; ++i) {
							memory->sramBank[i * 2] = 0xFF;
							memory->sramBank[i * 2 + 1] = 0xFF;
						}
					}
					break;
				case 3:
					mbc7->writable = true;
					mbc7->state = GBMBC7_STATE_NULL;
					break;
				}
			}
			break;
		case GBMBC7_STATE_COMMAND_SR_WRITE:
			if (mbc7->srBits == 16) {
				mbc7->srBits = 0;
				mbc7->state = GBMBC7_STATE_WRITE;
			}
			break;
		case GBMBC7_STATE_COMMAND_SR_READ:
			if (mbc7->srBits == 1) {
				mbc7->sr = memory->sramBank[mbc7->address * 2] << 8;
				mbc7->sr |= memory->sramBank[mbc7->address * 2 + 1];
				mbc7->srBits = 0;
				mbc7->state = GBMBC7_STATE_READ;
			}
			break;
		case GBMBC7_STATE_COMMAND_SR_FILL:
			if (mbc7->srBits == 16) {
				mbc7->sr = 0xFFFF;
				mbc7->srBits = 0;
				mbc7->state = GBMBC7_STATE_WRITE;
			}
			break;
		default:
			break;
		}
	} else if (GBMBC7FieldIsSK(old) && !GBMBC7FieldIsSK(value)) {
		if (mbc7->state == GBMBC7_STATE_READ) {
			mbc7->sr <<= 1;
			++mbc7->srBits;
			if (mbc7->srBits == 16) {
				mbc7->srBits = 0;
				mbc7->state = GBMBC7_STATE_NULL;
			}
		}
	}
}

void _GBHuC3(struct GB* gb, uint16_t address, uint8_t value) {
	struct GBMemory* memory = &gb->memory;
	int bank = value & 0x3F;
	if (address & 0x1FFF) {
		mLOG(GB_MBC, STUB, "HuC-3 unknown value %04X:%02X", address, value);
	}

	switch (address >> 13) {
	case 0x0:
		switch (value) {
		case 0xA:
			memory->sramAccess = true;
			GBMBCSwitchSramBank(gb, memory->sramCurrentBank);
			break;
		default:
			memory->sramAccess = false;
			break;
		}
		break;
	case 0x1:
		GBMBCSwitchBank(memory, bank);
		break;
	case 0x2:
		GBMBCSwitchSramBank(gb, bank);
		break;
	default:
		// TODO
		mLOG(GB_MBC, STUB, "HuC-3 unknown address: %04X:%02X", address, value);
		break;
	}
}
