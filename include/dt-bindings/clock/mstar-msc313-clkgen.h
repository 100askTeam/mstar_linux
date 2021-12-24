/* SPDX-License-Identifier: GPL-2.0-only OR BSD-2-Clause */
/*
 * Output definitions for the MStar/SigmaStar SC GP
 *
 * Copyright (C) 2021 Daniel Palmer <daniel@thingy.jp>
 */

#ifndef _DT_BINDINGS_CLOCK_MSTAR_MSC313_CLKGEN_H
#define _DT_BINDINGS_CLOCK_MSTAR_MSC313_CLKGEN_H

#define MSC313_CLKGEN_MUXES		0

#define MSC313_CLKGEN_DEGLITCHES	1

#define MSC313_CLKGEN_MIU		0
#define MSC313_CLKGEN_DDR_SYN		1
#define MSC313_CLKGEN_UART0		2
#define MSC313_CLKGEN_UART1		3
#define MSC313_CLKGEN_SPI		4
#define MSC313_CLKGEN_MSPI0		5
#define MSC313_CLKGEN_MSPI1		6
#define MSC313_CLKGEN_FUART0_SYNTH_IN	7
#define MSC313_CLKGEN_FUART		8
#define MSC313_CLKGEN_MIIC0		9
#define MSC313_CLKGEN_MIIC1		10
#define MSC313_CLKGEN_EMAC_AHB		11
#define MSC313_CLKGEN_SDIO		12
#define MSC313_CLKGEN_BDMA		13
#define MSC313_CLKGEN_AESDMA		14
#define MSC313_CLKGEN_ISP		15
#define MSC313_CLKGEN_JPE		16
#define MSC313_CLKGEN_MOP		17

#define SSD20XD_CLKGEN_SATA		18
#define SSD20XD_CLKGEN_DEC_PCLK		19
#define SSD20XD_CLKGEN_DEC_ACLK		20
#define SSD20XD_CLKGEN_DEC_BCLK		21
#define SSD20XD_CLKGEN_DEC_CCLK		22

#define MSC313_CLKGEN_GATES		2

#define MSC313_CLKGEN_DIVIDERS		3

#endif