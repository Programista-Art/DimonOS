#ifndef ARCH_X86_IO_H
#define ARCH_X86_IO_H

#include <stdint.h>

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void io_wait(void) {
    outb(0x80, 0);
}

static inline void cli(void) {
    __asm__ volatile("cli" : : : "memory");
}

static inline void sti(void) {
    __asm__ volatile("sti" : : : "memory");
}

static inline void hlt(void) {
    __asm__ volatile("hlt" : : : "memory");
}

#endif /* ARCH_X86_IO_H */
