/*
 * Raspberry Pi PIC Programmer using GPIO connector
 * https://github.com/WallaceIT/picberry
 * Copyright 2014 Francesco Valla
 *
 * PIC16F178x support added 2024
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

#include <iostream>

#include "../common.h"
#include "device.h"

using namespace std;

class pic16f178x : public Pic {

  public:
    uint16_t cw2_mask;

    pic16f178x(void) : cw2_mask(0x3FFF) {};

    void enter_program_mode(void);
    void exit_program_mode(void);
    bool setup_pe(void) { return true; };
    bool read_device_id(void);
    void bulk_erase(void);
    void dump_configuration_registers(void);
    void read(char *outfile, uint32_t start, uint32_t count);
    void write(char *infile);
    uint8_t blank_check(void);

  protected:
    void send_cmd(uint8_t cmd, unsigned int delay);
    uint16_t read_data(void);
    void write_data(uint16_t data);
    void reset_mem_location(void);

    /*
     * DEVICES SECTION
     *                  ID       NAME            MEMSIZE
     *
     * device_id is DEV<8:0> from the 14-bit register at 0x8006,
     * i.e. (raw_read >> 5) & 0x1FF.
     *
     * CW2 masks:
     *   non-LF  0x3F23  (LPBOR bit 11 present; unimpl bit 5 reads 1)
     *   LF      0x3F03  (LPBOR bit 11 present; VCAPEN bit 5 programmable)
     */
    pic_device piclist[2] = {
        {0x153, "PIC16F1786",  0x2000},
        {0x158, "PIC16LF1786", 0x2000}
    };

    uint16_t cw2_mask_table[2] = {
        0x3F23,  /* PIC16F1786  */
        0x3F03   /* PIC16LF1786 */
    };
};
