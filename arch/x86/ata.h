#ifndef ARCH_X86_ATA_H
#define ARCH_X86_ATA_H

#include <stdint.h>
#include "../../dimon64.h"

int ata_detect(Dimon64BlockDevice *device);

#endif
