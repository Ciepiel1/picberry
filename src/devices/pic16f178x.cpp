/*
 * Raspberry Pi PIC Programmer using GPIO connector
 * https://github.com/WallaceIT/picberry
 * Copyright 2014 Francesco Valla
 *
 * PIC16F178x support added 2024
 *
 * Programming spec: DS41457E - PIC16(L)F178X Memory Programming Specification
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <cstdlib>
#include <iostream>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "pic16f178x.h"

/* Timing constants (all in microseconds unless noted).
 * Values match DS41457E Table 8-1. */
#define DELAY_SETUP       1       /* TDS: data setup before clock  (min 100 ns) */
#define DELAY_HOLD        1       /* TDH: data hold after clock    (min 100 ns) */
#define DELAY_TENTS       1       /* TENTS: CLK/DAT setup before VDD/MCLR (min 100 ns) */
#define DELAY_TENTH       500     /* TENTH: CLK/DAT hold after MCLR rise  (min 250 µs) */
#define DELAY_TCKH        1       /* TCKH: clock high pulse width  (min 100 ns) */
#define DELAY_TCKL        1       /* TCKL: clock low pulse width   (min 100 ns) */
#define DELAY_TDLY        1       /* TDLY: inter-command delay     (min 1 µs) */
#define DELAY_TERAB       5000    /* TERAB: bulk erase time        (max 5 ms) */
#define DELAY_TEXIT       1       /* TEXIT: exit programming delay (min 1 µs) */
#define DELAY_TPINT_DATA  2500    /* TPINT: internally timed prog, program memory (max 2.5 ms) */
#define DELAY_TPINT_CONF  5000    /* TPINT: internally timed prog, config words  (max 5 ms) */

/* 6-bit command opcodes (Table 4-1, DS41457E) */
#define COMM_LOAD_CONFIG          0x00
#define COMM_LOAD_FOR_PROG        0x02
#define COMM_READ_FROM_PROG       0x04
#define COMM_INC_ADDR             0x06
#define COMM_RESET_ADDR           0x16
#define COMM_BEGIN_IN_TIMED_PROG  0x08
#define COMM_BULK_ERASE           0x09

/* Row size in 14-bit words; all PIC16F178x devices use 32-word rows */
#define LATCH_SIZE 32

/* Enter HV programming mode.
 * HV-first sequence per DS41457E section 4.1.1:
 *   Hold CLK and DAT low, apply VIHH to MCLR, then raise VDD.
 * Because MCLR is GPIO-only (no external HV supply is switched here),
 * this sequence just asserts the pin-direction and timing.
 * The caller's circuit must supply VIHH externally. */
void pic16f178x::enter_program_mode(void) {
    GPIO_IN(pic_mclr);
    delay_us(1);
    GPIO_OUT(pic_mclr);
    delay_us(DELAY_TENTS);

    GPIO_CLR(pic_mclr);
    delay_us(1);
    GPIO_CLR(pic_clk);
    delay_us(DELAY_TENTH);
    GPIO_CLR(pic_data);
    delay_us(DELAY_TCKH);
    delay_us(DELAY_TCKH);
    GPIO_CLR(pic_clk);
    delay_us(DELAY_TCKH);
}

void pic16f178x::exit_program_mode(void) {
    GPIO_CLR(pic_clk);
    GPIO_CLR(pic_data);
    GPIO_IN(pic_mclr);
}

/* Send a 6-bit command to the PIC, LSB first.
 * Timing matches srcRPI-validated pic10f322 sequence. */
void pic16f178x::send_cmd(uint8_t cmd, unsigned int delay) {
    delay_us(1);
    for (int i = 0; i < 6; i++) {
        GPIO_SET(pic_clk);
        delay_us(DELAY_TCKH);
        if ((cmd >> i) & 0x01)
            GPIO_SET(pic_data);
        else
            GPIO_CLR(pic_data);
        delay_us(DELAY_TCKH);
        GPIO_CLR(pic_clk);
        delay_us(DELAY_TCKL);
    }
    GPIO_CLR(pic_data);
    delay_us(delay);
}

/* Read one 16-clock frame (start bit + 14 data bits + stop bit), return
 * the 14-bit payload shifted right by 1 (strips start bit). */
uint16_t pic16f178x::read_data(void) {
    uint16_t data = 0x0000;

    GPIO_IN(pic_data);
    delay_us(DELAY_TCKL);
    for (int i = 0; i < 16; i++) {
        GPIO_SET(pic_clk);
        data |= (GPIO_LEV(pic_data) & 0x00000001) << i;
        GPIO_CLR(pic_clk);
        delay_us(DELAY_TCKL);
        delay_us(DELAY_TCKL);
    }
    GPIO_IN(pic_data);
    delay_us(1);
    GPIO_OUT(pic_data);
    data >>= 1;
    return data;
}

/* Write one 16-clock frame (start bit + 14 data bits + stop bit).
 * Data is shifted left by 1 to insert a leading start bit of 0. */
void pic16f178x::write_data(uint16_t data) {
    data <<= 1;
    for (int i = 0; i < 16; i++) {
        GPIO_SET(pic_clk);
        delay_us(1);
        if ((data >> i) & 0x0001)
            GPIO_SET(pic_data);
        else
            GPIO_CLR(pic_data);
        delay_us(DELAY_SETUP);
        GPIO_CLR(pic_clk);
        delay_us(DELAY_HOLD);
    }
    GPIO_CLR(pic_data);
    delay_us(1);
}

/* Reset PC to 0x0000 */
void pic16f178x::reset_mem_location(void) {
    send_cmd(COMM_RESET_ADDR, DELAY_TDLY);
}

/* Read device ID from address 0x8006.
 * Load Configuration sets PC = 0x8000; increment 6 times reaches 0x8006. */
bool pic16f178x::read_device_id(void) {
    bool found = false;

    send_cmd(COMM_LOAD_CONFIG, DELAY_TDLY);
    write_data(0x3FFF);  /* dummy word */

    for (int i = 0; i < 6; i++)
        send_cmd(COMM_INC_ADDR, DELAY_TDLY);

    send_cmd(COMM_READ_FROM_PROG, DELAY_TDLY);

    uint16_t id = read_data();
    device_id  = (id >> 5) & 0x1FF;
    device_rev =  id & 0x1F;

    for (unsigned int i = 0; i < sizeof(piclist) / sizeof(piclist[0]); i++) {
        if (piclist[i].device_id == device_id) {
            strcpy(name, piclist[i].name);
            mem.code_memory_size    = piclist[i].code_memory_size;
            mem.program_memory_size = 0x0F80018;
            mem.location = (uint16_t *)calloc(mem.program_memory_size, sizeof(uint16_t));
            mem.filled   = (bool *)calloc(mem.program_memory_size, sizeof(bool));
            cw2_mask     = cw2_mask_table[i];
            found        = true;
            break;
        }
    }

    return found;
}

/* Blank check: verify all program memory words are 0x3FFF,
 * then check Config Word 1 and Config Word 2. */
uint8_t pic16f178x::blank_check(void) {
    uint16_t addr, data;
    uint8_t ret = 0;
    unsigned int lcounter = 0;

    if (!flags.debug)
        cout << "[ 0%]";

    reset_mem_location();

    for (addr = 0; addr < mem.code_memory_size; addr++) {
        send_cmd(COMM_READ_FROM_PROG, DELAY_TDLY);
        data = read_data() & 0x3FFF;
        send_cmd(COMM_INC_ADDR, DELAY_TDLY);

        if (data != 0x3FFF) {
            fprintf(stderr, "Chip not Blank! Address: 0x%x, Read: 0x%x.\n", addr, data);
            ret = 1;
            break;
        }

        if (lcounter != addr * 100 / mem.code_memory_size) {
            lcounter = addr * 100 / mem.code_memory_size;
            fprintf(stdout, "\b\b\b\b\b[%2d%%]", lcounter);
        }
    }

    /* Check configuration words (at 0x8007 and 0x8008) */
    send_cmd(COMM_LOAD_CONFIG, DELAY_TDLY);
    write_data(0x3FFF);

    addr = 0x8000;
    for (int i = 0; i < 7; i++) {
        send_cmd(COMM_INC_ADDR, DELAY_TDLY);
        addr++;
    }

    /* Config Word 1 at 0x8007 */
    send_cmd(COMM_READ_FROM_PROG, DELAY_TDLY);
    data = read_data() & 0x3FFF;
    if (data != 0x3FFF) {
        fprintf(stderr, "Chip not Blank! Address: 0x%x, Read: 0x%x.\n", addr, data);
        ret = 1;
    }

    /* Config Word 2 at 0x8008 */
    addr++;
    send_cmd(COMM_INC_ADDR, DELAY_TDLY);
    send_cmd(COMM_READ_FROM_PROG, DELAY_TDLY);
    data = read_data() & cw2_mask;
    if (data != cw2_mask) {
        fprintf(stderr, "Chip not Blank! Address: 0x%x, Read: 0x%x.\n", addr, data);
        ret = 1;
    }

    if (!flags.debug)
        cout << "\b\b\b\b\b";

    return ret;
}

/* Bulk erase: resets PC first, then issues the erase command.
 * Calibration Words (0x8009-0x8013) are unaffected by bulk erase per spec. */
void pic16f178x::bulk_erase(void) {
    send_cmd(COMM_RESET_ADDR, DELAY_TDLY);
    send_cmd(COMM_BULK_ERASE, DELAY_TERAB);
    if (flags.client)
        fprintf(stdout, "@FIN");
}

/* Read all program memory and both config words to a HEX file. */
void pic16f178x::read(char *outfile, uint32_t start, uint32_t count) {
    uint16_t addr, data;
    unsigned int lcounter = 0;

    if (!flags.debug)
        cout << "[ 0%]";
    if (flags.client)
        fprintf(stdout, "@000");

    reset_mem_location();

    for (addr = 0; addr < mem.code_memory_size; addr++) {
        send_cmd(COMM_READ_FROM_PROG, DELAY_TDLY);
        data = read_data() & 0x3FFF;
        send_cmd(COMM_INC_ADDR, DELAY_TDLY);

        if (flags.debug)
            fprintf(stdout, "  addr = 0x%04X  data = 0x%04X\n", addr, data);

        if (data != 0x3FFF) {
            mem.location[addr] = data;
            mem.filled[addr]   = 1;
        }

        if (lcounter != addr * 100 / mem.code_memory_size) {
            lcounter = addr * 100 / mem.code_memory_size;
            if (flags.client)
                fprintf(stdout, "RED@%2d\n", lcounter);
            if (!flags.debug)
                fprintf(stdout, "\b\b\b\b%2d%%]", lcounter);
        }
    }

    /* Read configuration memory */
    send_cmd(COMM_LOAD_CONFIG, DELAY_TDLY);
    write_data(0x3FFF);

    addr = 0x8000;
    for (int i = 0; i < 7; i++) {
        send_cmd(COMM_INC_ADDR, DELAY_TDLY);
        addr++;
    }

    /* Config Word 1 at 0x8007 */
    send_cmd(COMM_READ_FROM_PROG, DELAY_TDLY);
    data = read_data() & 0x3FFF;
    if (flags.debug)
        fprintf(stdout, "  addr = 0x%04X  data = 0x%04X\n", addr, data);
    if (data != 0x3FFF) {
        mem.location[addr] = data;
        mem.filled[addr]   = 1;
    }

    /* Config Word 2 at 0x8008 */
    addr++;
    send_cmd(COMM_INC_ADDR, DELAY_TDLY);
    send_cmd(COMM_READ_FROM_PROG, DELAY_TDLY);
    data = read_data() & cw2_mask;
    if (flags.debug)
        fprintf(stdout, "  addr = 0x%04X  data = 0x%04X\n", addr, data);
    if (data != cw2_mask) {
        mem.location[addr] = data;
        mem.filled[addr]   = 1;
    }

    if (!flags.debug)
        cout << "\b\b\b\b\b";
    if (flags.client)
        fprintf(stdout, "@FIN");

    write_inhx(&mem, outfile);
}

/* Bulk erase then write program memory and config words from a HEX file. */
void pic16f178x::write(char *infile) {
    uint16_t data, fileconf;
    uint32_t addr;
    unsigned int lcounter = 0;

    read_inhx(infile, &mem);
    bulk_erase();

    if (!flags.debug)
        cout << "[ 0%]";
    if (flags.client)
        fprintf(stdout, "@000");

    reset_mem_location();

    /* Write program memory in 32-word (LATCH_SIZE) rows */
    for (addr = 0; addr < mem.code_memory_size; addr += LATCH_SIZE) {
        if (flags.debug)
            fprintf(stdout, "Current address 0x%08X\n", addr);

        /* Load all latches except the last */
        for (int i = 0; i < LATCH_SIZE - 1; i++) {
            uint16_t word = mem.filled[addr + i] ? mem.location[addr + i] : 0x3FFF;
            if (flags.debug)
                fprintf(stdout, "  Writing 0x%04X to address 0x%06X\n", word, addr + i);
            send_cmd(COMM_LOAD_FOR_PROG, DELAY_TDLY);
            write_data(word);
            send_cmd(COMM_INC_ADDR, DELAY_TDLY);
        }

        /* Load last latch, then trigger programming */
        uint16_t last = mem.filled[addr + LATCH_SIZE - 1]
                        ? mem.location[addr + LATCH_SIZE - 1]
                        : 0x3FFF;
        if (flags.debug)
            fprintf(stdout, "  Writing 0x%04X to address 0x%06X and programming...\n",
                    last, addr + LATCH_SIZE - 1);
        send_cmd(COMM_LOAD_FOR_PROG, DELAY_TDLY);
        write_data(last);
        send_cmd(COMM_BEGIN_IN_TIMED_PROG, DELAY_TPINT_DATA);
        send_cmd(COMM_INC_ADDR, DELAY_TDLY);

        if (lcounter != addr * 100 / mem.code_memory_size) {
            lcounter = addr * 100 / mem.code_memory_size;
            if (flags.client)
                fprintf(stdout, "@%03d", lcounter);
            if (!flags.debug)
                fprintf(stdout, "\b\b\b\b\b[%2d%%]", lcounter);
        }
    }

    if (!flags.debug)
        cout << "\b\b\b\b\b\b";
    if (flags.client)
        fprintf(stdout, "@100");

    /* Write configuration words (one word at a time, internally timed) */
    send_cmd(COMM_LOAD_CONFIG, DELAY_TDLY);
    write_data(0x3FFF);

    addr = 0x8000;
    for (int i = 0; i < 7; i++) {
        send_cmd(COMM_INC_ADDR, DELAY_TDLY);
        addr++;
    }

    /* Config Word 1 at 0x8007 */
    if (mem.filled[addr]) {
        send_cmd(COMM_LOAD_FOR_PROG, DELAY_TDLY);
        write_data(mem.location[addr]);
        send_cmd(COMM_BEGIN_IN_TIMED_PROG, DELAY_TPINT_CONF);
    }

    /* Config Word 2 at 0x8008 */
    addr++;
    send_cmd(COMM_INC_ADDR, DELAY_TDLY);
    if (mem.filled[addr]) {
        send_cmd(COMM_LOAD_FOR_PROG, DELAY_TDLY);
        write_data(mem.location[addr]);
        send_cmd(COMM_BEGIN_IN_TIMED_PROG, DELAY_TPINT_CONF);
    }

    /* Verify */
    if (!flags.noverify) {
        if (!flags.debug)
            cout << "[ 0%]";
        if (flags.client)
            fprintf(stdout, "@000");
        lcounter = 0;

        reset_mem_location();

        for (addr = 0; addr < mem.code_memory_size; addr++) {
            send_cmd(COMM_READ_FROM_PROG, DELAY_TDLY);
            data = read_data() & 0x3FFF;
            send_cmd(COMM_INC_ADDR, DELAY_TDLY);

            if (flags.debug)
                fprintf(stdout, "addr = 0x%06X:  pic = 0x%04X, file = 0x%04X\n",
                        addr, data,
                        mem.filled[addr] ? mem.location[addr] : 0x3FFF);

            if ((data != mem.location[addr]) & mem.filled[addr]) {
                fprintf(stderr,
                        "Error at addr = 0x%06X:  pic = 0x%04X, file = 0x%04X.\nExiting...",
                        addr, data, mem.location[addr]);
                return;
            }

            if (lcounter != addr * 100 / mem.code_memory_size) {
                lcounter = addr * 100 / mem.code_memory_size;
                if (flags.client)
                    fprintf(stdout, "@%03d", lcounter);
                if (!flags.debug)
                    fprintf(stdout, "\b\b\b\b\b[%2d%%]", lcounter);
            }
        }

        /* Verify Config Word 1 */
        send_cmd(COMM_LOAD_CONFIG, DELAY_TDLY);
        write_data(0x3FFF);

        addr = 0x8000;
        for (int i = 0; i < 7; i++) {
            send_cmd(COMM_INC_ADDR, DELAY_TDLY);
            addr++;
        }

        send_cmd(COMM_READ_FROM_PROG, DELAY_TDLY);
        /* Mask LVP bit (bit 13) — cannot be programmed via LVP entry */
        uint16_t cw1_mask = 0x3FFF & ~(1 << 13);
        data     = read_data() & cw1_mask;
        fileconf = mem.location[addr] & cw1_mask;
        if ((data != fileconf) & mem.filled[addr]) {
            fprintf(stderr,
                    "Error at addr = 0x%06X:  pic = 0x%04X, file = 0x%04X.\nExiting...",
                    addr, data, fileconf);
            return;
        }

        /* Verify Config Word 2 */
        addr++;
        send_cmd(COMM_INC_ADDR, DELAY_TDLY);
        send_cmd(COMM_READ_FROM_PROG, DELAY_TDLY);
        /* Also mask LVP bit in CW2 mask */
        uint16_t eff_mask = cw2_mask & ~(1 << 13);
        data     = read_data() & eff_mask;
        fileconf = mem.location[addr] & eff_mask;
        if ((data != fileconf) & mem.filled[addr]) {
            fprintf(stderr,
                    "Error at addr = 0x%06X:  pic = 0x%04X, file = 0x%04X.\nExiting...",
                    addr, data, fileconf);
            return;
        }

        if (!flags.debug)
            cout << "\b\b\b\b\b";
        if (flags.client)
            fprintf(stdout, "@FIN");
    } else {
        if (flags.client)
            fprintf(stdout, "@FIN");
    }
}

/* Dump Config Word 1 and Config Word 2 to stdout. */
void pic16f178x::dump_configuration_registers(void) {
    send_cmd(COMM_LOAD_CONFIG, DELAY_TDLY);
    write_data(0x3FFF);

    for (int i = 0; i < 7; i++)
        send_cmd(COMM_INC_ADDR, DELAY_TDLY);

    cout << "Configuration Words:" << endl;
    send_cmd(COMM_READ_FROM_PROG, DELAY_TDLY);
    fprintf(stdout, " - CONFIG1 = 0x%04x\n", read_data() & 0x3FFF);

    send_cmd(COMM_INC_ADDR, DELAY_TDLY);
    send_cmd(COMM_READ_FROM_PROG, DELAY_TDLY);
    fprintf(stdout, " - CONFIG2 = 0x%04x\n", read_data() & cw2_mask);
}
