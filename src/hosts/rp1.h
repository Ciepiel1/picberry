extern "C" {
#include <wiringPi.h>
};
/*
 * Raspberry Pi PIC Programmer using GPIO connector
 * https://github.com/WallaceIT/picberry
 * Copyright 2016 Francesco Valla
 *
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

/* GPIO setup macros. Always use GPIO_IN(x) before using GPIO_OUT(x) */
#define GPIO_IN(g) pinMode(g, INPUT)
#define GPIO_OUT(g) pinMode(g, OUTPUT)

#define GPIO_SET(g) digitalWrite(g, HIGH)
#define GPIO_CLR(g) digitalWrite(g, LOW)

/* reads pin level */
#define GPIO_LEV(g) digitalRead(g)

/* default GPIO <-> PIC connections */
#define DEFAULT_PIC_DATA 24 /* PGD - I/O */
#define DEFAULT_PIC_CLK 25  /* PGC - Output */
#define DEFAULT_PIC_MCLR 23 /* ~MCLR - Output */
