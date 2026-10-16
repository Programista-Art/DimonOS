#include "ata.h"
#include "io.h"
#include <stdint.h>
#include <stddef.h>

#define DISK_ERR_NONE     0
#define DISK_ERR_NODISK   1
#define DISK_ERR_RANGE    2
#define DISK_ERR_RAM      3
#define DISK_ERR_READONLY 4
#define DISK_ERR_IO       5
#define DISK_SECTOR_SIZE  512

#define ATA_PRIMARY_BASE      0x1F0
#define ATA_PRIMARY_CTRL      0x3F6
#define ATA_REG_DATA          0x00
#define ATA_REG_ERROR         0x01
#define ATA_REG_FEATURES      0x01
#define ATA_REG_SECCOUNT      0x02
#define ATA_REG_LBA_LOW       0x03
#define ATA_REG_LBA_MID       0x04
#define ATA_REG_LBA_HIGH      0x05
#define ATA_REG_DRIVE         0x06
#define ATA_REG_STATUS        0x07
#define ATA_REG_COMMAND       0x07
#define ATA_CMD_IDENTIFY      0xEC
#define ATA_CMD_READ_PIO      0x20
#define ATA_CMD_WRITE_PIO     0x30
#define ATA_CMD_FLUSH_CACHE   0xE7
#define ATA_STAT_BSY          0x80
#define ATA_STAT_DRQ          0x08
#define ATA_STAT_DF           0x20
#define ATA_STAT_ERR          0x01
#define ATA_CTL_nIEN          0x02
#define ATA_TIMEOUT_POLLS     8000000u
#define ATA_SECTOR_WORDS      256

void serial_printf(const char *fmt, ...);

typedef struct {
    uint8_t  exists;
    uint8_t  writable;
    uint8_t  flush_supported;
    uint32_t sectors;
} ata_context;

static int ata_wait_not_busy(uint32_t timeout) {
    while (timeout--) {
        uint8_t status = inb(ATA_PRIMARY_BASE + ATA_REG_STATUS);
        if ((status & ATA_STAT_BSY) == 0) return 1;
    }
    return 0;
}

static int ata_wait_drq(uint32_t timeout) {
    while (timeout--) {
        uint8_t status = inb(ATA_PRIMARY_BASE + ATA_REG_STATUS);
        if (status & ATA_STAT_BSY) continue;
        if (status & (ATA_STAT_DF | ATA_STAT_ERR)) return 0;
        if (status & ATA_STAT_DRQ) return 1;
    }
    return 0;
}

static int ata_select_master(uint32_t lba) {
    if (!ata_wait_not_busy(ATA_TIMEOUT_POLLS)) return 0;
    outb(ATA_PRIMARY_BASE + ATA_REG_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    io_wait();
    return ata_wait_not_busy(ATA_TIMEOUT_POLLS);
}

static void ata_transfer_words(uint16_t *buffer, uint32_t words) {
    for (uint32_t i = 0; i < words; i++) buffer[i] = inw(ATA_PRIMARY_BASE + ATA_REG_DATA);
}

static int ata_read_sector(uint32_t lba, uint8_t *buffer) {
    if (!ata_select_master(lba)) {
        serial_printf("[ATA] read LBA %u: select timeout (status 0x%02X).\n",
                      lba, inb(ATA_PRIMARY_BASE + ATA_REG_STATUS));
        return DISK_ERR_IO;
    }

    outb(ATA_PRIMARY_BASE + ATA_REG_SECCOUNT, 1);
    outb(ATA_PRIMARY_BASE + ATA_REG_LBA_LOW,  (uint8_t)(lba & 0xFF));
    outb(ATA_PRIMARY_BASE + ATA_REG_LBA_MID,  (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_BASE + ATA_REG_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_PRIMARY_BASE + ATA_REG_COMMAND,  ATA_CMD_READ_PIO);

    if (!ata_wait_drq(ATA_TIMEOUT_POLLS)) {
        serial_printf("[ATA] read LBA %u: DRQ timeout (status 0x%02X err 0x%02X).\n",
                      lba, inb(ATA_PRIMARY_BASE + ATA_REG_STATUS),
                      inb(ATA_PRIMARY_BASE + ATA_REG_ERROR));
        return DISK_ERR_IO;
    }

    ata_transfer_words((uint16_t *)buffer, ATA_SECTOR_WORDS);
    io_wait();

    uint8_t status = inb(ATA_PRIMARY_BASE + ATA_REG_STATUS);
    if (status & (ATA_STAT_DF | ATA_STAT_ERR)) {
        serial_printf("[ATA] read LBA %u: post-transfer error (status 0x%02X err 0x%02X).\n",
                      lba, status, inb(ATA_PRIMARY_BASE + ATA_REG_ERROR));
        return DISK_ERR_IO;
    }
    return DISK_ERR_NONE;
}

static int ata_write_sector(uint32_t lba, const uint8_t *buffer) {
    if (!ata_select_master(lba)) return DISK_ERR_IO;

    outb(ATA_PRIMARY_BASE + ATA_REG_SECCOUNT, 1);
    outb(ATA_PRIMARY_BASE + ATA_REG_LBA_LOW,  (uint8_t)(lba & 0xFF));
    outb(ATA_PRIMARY_BASE + ATA_REG_LBA_MID,  (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_BASE + ATA_REG_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_PRIMARY_BASE + ATA_REG_COMMAND,  ATA_CMD_WRITE_PIO);

    if (!ata_wait_drq(ATA_TIMEOUT_POLLS)) return DISK_ERR_IO;

    const uint16_t *src = (const uint16_t *)buffer;
    for (uint32_t i = 0; i < ATA_SECTOR_WORDS; i++) {
        outw(ATA_PRIMARY_BASE + ATA_REG_DATA, src[i]);
        io_wait();
    }
    io_wait();

    if (!ata_wait_not_busy(ATA_TIMEOUT_POLLS)) return DISK_ERR_IO;
    uint8_t status = inb(ATA_PRIMARY_BASE + ATA_REG_STATUS);
    if (status & (ATA_STAT_DF | ATA_STAT_ERR)) return DISK_ERR_IO;
    return DISK_ERR_NONE;
}

static int ata_flush(void) {
    if (!ata_select_master(0)) return DISK_ERR_IO;
    outb(ATA_PRIMARY_BASE + ATA_REG_COMMAND, ATA_CMD_FLUSH_CACHE);
    if (!ata_wait_not_busy(ATA_TIMEOUT_POLLS)) return DISK_ERR_IO;
    uint8_t status = inb(ATA_PRIMARY_BASE + ATA_REG_STATUS);
    if (status & (ATA_STAT_DF | ATA_STAT_ERR)) return DISK_ERR_IO;
    return DISK_ERR_NONE;
}

static int ata_blk_read(void *context, uint32_t lba, uint32_t count, void *buffer) {
    ata_context *ctx = (ata_context *)context;
    if (!ctx || !ctx->exists) {
        serial_printf("[ATA] blk_read: no device (ctx=%p).\n", context);
        return DISK_ERR_NODISK;
    }
    if (count == 0) return DISK_ERR_NONE;
    /* 28-bit LBA PIO only: reject addresses the command cannot encode. */
    if (lba > 0x0FFFFFFFu || count > 0x0FFFFFFFu ||
        lba >= ctx->sectors || (uint64_t)lba + count > ctx->sectors) return DISK_ERR_RANGE;
    if (!buffer) return DISK_ERR_RAM;
    if ((uintptr_t)buffer & 1u) return DISK_ERR_RAM;

    uint8_t *out = (uint8_t *)buffer;
    for (uint32_t i = 0; i < count; i++) {
        int rc = ata_read_sector(lba + i, out + (size_t)i * DISK_SECTOR_SIZE);
        if (rc != DISK_ERR_NONE) return rc;
    }
    return DISK_ERR_NONE;
}

static int ata_blk_write(void *context, uint32_t lba, uint32_t count, const void *buffer) {
    ata_context *ctx = (ata_context *)context;
    if (!ctx || !ctx->exists) return DISK_ERR_NODISK;
    if (count == 0) return DISK_ERR_NONE;
    if (lba > 0x0FFFFFFFu || count > 0x0FFFFFFFu ||
        lba >= ctx->sectors || (uint64_t)lba + count > ctx->sectors) return DISK_ERR_RANGE;
    if (!buffer) return DISK_ERR_RAM;
    if ((uintptr_t)buffer & 1u) return DISK_ERR_RAM;
    if (!ctx->writable) return DISK_ERR_READONLY;

    const uint8_t *in = (const uint8_t *)buffer;
    for (uint32_t i = 0; i < count; i++) {
        int rc = ata_write_sector(lba + i, in + (size_t)i * DISK_SECTOR_SIZE);
        if (rc != DISK_ERR_NONE) return rc;
    }

    int rc = ata_flush();
    if (rc != DISK_ERR_NONE) {
        /* Flush failure is a truthful I/O error, but the device stays
         * writable: the caller decides whether to retry or report. */
        return rc;
    }
    return DISK_ERR_NONE;
}

static int ata_blk_flush(void *context) {
    ata_context *ctx = (ata_context *)context;
    if (!ctx || !ctx->exists) return DISK_ERR_NODISK;
    if (!ctx->writable) return DISK_ERR_READONLY;
    if (!ctx->flush_supported) return DISK_ERR_NONE;
    int rc = ata_flush();
    if (rc != DISK_ERR_NONE) return rc;
    return DISK_ERR_NONE;
}

int ata_detect(Dimon64BlockDevice *device) {
    if (!device) return DISK_ERR_NODISK;

    device->context = 0;
    device->sectors = 0;
    device->read    = 0;
    device->write   = 0;
    device->flush   = 0;

    static ata_context context;
    context.exists = 0;
    context.writable = 0;
    context.flush_supported = 0;

    outb(ATA_PRIMARY_CTRL, 0x00);
    io_wait();

    uint8_t status = inb(ATA_PRIMARY_BASE + ATA_REG_STATUS);
    if (status == 0xFF) {
        serial_printf("[ATA] PRIMARY MASTER: no drive (status 0xFF floating bus).\n");
        return DISK_ERR_NODISK;
    }

    outb(ATA_PRIMARY_BASE + ATA_REG_DRIVE, 0xE0);
    for (uint32_t i = 0; i < 2000; i++) io_wait();

    uint16_t identify[ATA_SECTOR_WORDS];

    outb(ATA_PRIMARY_BASE + ATA_REG_SECCOUNT, 0);
    outb(ATA_PRIMARY_BASE + ATA_REG_LBA_LOW, 0);
    outb(ATA_PRIMARY_BASE + ATA_REG_LBA_MID, 0);
    outb(ATA_PRIMARY_BASE + ATA_REG_LBA_HIGH, 0);
    outb(ATA_PRIMARY_BASE + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    status = inb(ATA_PRIMARY_BASE + ATA_REG_STATUS);
    if (status == 0x00) {
        serial_printf("[ATA] PRIMARY MASTER: IDENTIFY rejected (status 0x00).\n");
        return DISK_ERR_NODISK;
    }

    if (!ata_wait_not_busy(ATA_TIMEOUT_POLLS)) {
        serial_printf("[ATA] PRIMARY MASTER: timeout waiting BSY clear after IDENTIFY.\n");
        return DISK_ERR_NODISK;
    }
    status = inb(ATA_PRIMARY_BASE + ATA_REG_STATUS);
    if (status & (ATA_STAT_DF | ATA_STAT_ERR)) {
        serial_printf("[ATA] PRIMARY MASTER: IDENTIFY failed (status 0x%02X error 0x%02X).\n",
                      status, inb(ATA_PRIMARY_BASE + ATA_REG_ERROR));
        return DISK_ERR_NODISK;
    }
    if (!ata_wait_drq(ATA_TIMEOUT_POLLS)) {
        serial_printf("[ATA] PRIMARY MASTER: IDENTIFY produced no data (status 0x%02X).\n", status);
        return DISK_ERR_NODISK;
    }

    ata_transfer_words(identify, ATA_SECTOR_WORDS);

    uint32_t user_sectors = ((uint32_t)identify[61] << 16) | identify[60];
    if (user_sectors == 0) {
        serial_printf("[ATA] PRIMARY MASTER: drive reports zero user sectors.\n");
        return DISK_ERR_NODISK;
    }
    /* Only the explicitly configured legacy IDE primary-master PIO device is
     * supported. Slaves, secondary channels, ATAPI/CD-ROM, SATA/AHCI, NVMe
     * and USB are never probed here. */
    if (user_sectors > 0x0FFFFFFFu) user_sectors = 0x0FFFFFFFu;

    context.exists = 1;
    context.writable = 1;
    context.flush_supported = 1;
    context.sectors = user_sectors;

    device->context = &context;
    device->sectors = user_sectors;
    device->read    = ata_blk_read;
    device->write   = ata_blk_write;
    device->flush   = ata_blk_flush;

    serial_printf("[ATA] PRIMARY MASTER: IDENTIFY OK, LBA28 capacity %u sectors (%u MiB).\n",
                  user_sectors, (unsigned)((uint64_t)user_sectors >> 11));

    return DISK_ERR_NONE;
}
