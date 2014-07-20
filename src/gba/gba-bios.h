#ifndef GBA_BIOS_H
#define GBA_BIOS_H

#include "common.h"

#include "arm.h"

void GBASwi16(struct ARMCore* cpu, int immediate);
void GBASwi32(struct ARMCore* cpu, int immediate);

uint32_t GBAChecksum(uint32_t* memory, size_t size);
const uint32_t GBA_BIOS_CHECKSUM;
const uint32_t GBA_DS_BIOS_CHECKSUM;

#endif
