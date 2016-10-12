#include "common.h"

static void z80_mem_write(struct SMS *sms, uint64_t timestamp, uint16_t addr, uint8_t val)
{
	if(addr >= 0xC000) {
		//printf("%p ram[%04X] = %02X\n", sms, addr&0x1FFF, val);
		sms->ram[addr&0x1FFF] = val;
	}

	if(addr >= 0xFFFC && sms_rom_is_banked) {
		sms->paging[addr&3] = val;
	}
}

static uint8_t z80_mem_read(struct SMS *sms, uint64_t timestamp, uint16_t addr)
{
	if(addr >= 0xC000) {
		//printf("%p %02X = ram[%04X]\n", sms, sms->ram[addr&0x1FFF], addr&0x1FFF);
		return sms->ram[addr&0x1FFF];
	} else if(addr < 0x0400 || !sms_rom_is_banked) {
		return sms_rom[addr];
	} else {
		return sms_rom[((uint32_t)(addr&0x3FFF))
			+((sms->paging[(addr>>14)+1]&0x1F)<<14)];
	}
}

static void z80_io_write(struct SMS *sms, uint64_t timestamp, uint16_t addr, uint8_t val)
{
	int port = ((addr>>5)&6)|(addr&1);

	switch(port)
	{
		case 0: // Memory control
			// TODO!
			break;

		case 1: // I/O port control
			// TODO!
			break;

		case 2: // PSG / V counter
		case 3: // PSG / H counter
			// TODO!
			break;

		case 4: // VDP data
			// TODO!
			break;

		case 5: // VDP control
			// TODO!
			break;

		case 6: // I/O port A
			// also SDSC 0xFC control
			if((addr&0xFF) == 0xFC) {
				// TODO
			}
			break;
		case 7: // I/O port B
			// also SDSC 0xFD data
			if((addr&0xFF) == 0xFD) {
				fputc((val >= 0x20 && val <= 0x7E)
					|| val == '\n' || val == '\r' || val == '\t'
					? val : '?'
				, stdout);
				fflush(stdout);
			}
			break;

		default:
			assert(!"UNREACHABLE");
			abort();
	}
}

static uint8_t z80_io_read(struct SMS *sms, uint64_t timestamp, uint16_t addr)
{
	int port = ((addr>>5)&6)|(addr&1);

	switch(port)
	{
		case 0: // Memory control
			return 0xAB;

		case 1: // I/O port control
			// TODO!
			return 0xFF;

		case 2: // V counter
			// TODO!
			return 0xFF;

		case 3: // H counter
			// TODO!
			return 0xFF;

		case 4: // VDP data
			// TODO!
			return 0xFF;

		case 5: // VDP control
			// TODO!
			return 0xFF;

		case 6: // I/O port A
			return sms_input_fetch(sms, timestamp, 0);

		case 7: // I/O port B
			return sms_input_fetch(sms, timestamp, 1);

		default:
			assert(!"UNREACHABLE");
			abort();
			return 0xFF;
	}
}

void z80_reset(struct Z80 *z80)
{
	z80->iff1 = 0;
	z80->iff2 = 0;
	z80->pc = 0;
	z80->i = 0;
	z80->r = 0;
	z80->sp = 0xFFFF;
	z80->gpr[RF] = 0xFF;
	z80->gpr[RA] = 0xFF;
	z80->halted = 0;
	z80->im = 0;
	z80->noni = 0;
	Z80_ADD_CYCLES(z80, 3);
}

uint16_t z80_pair(uint8_t h, uint8_t l)
{
	return (((uint16_t)h)<<8) + ((uint16_t)l);
}

uint16_t z80_pair_pbe(uint8_t *p)
{
	return z80_pair(p[0], p[1]);
}

static uint8_t z80_fetch_op_m1(struct Z80 *z80, struct SMS *sms)
{
	uint8_t op = z80_mem_read(sms, z80->timestamp, z80->pc++);
	Z80_ADD_CYCLES(z80, 4);
	return op;
}

static uint8_t z80_fetch_op_x(struct Z80 *z80, struct SMS *sms)
{
	uint8_t op = z80_mem_read(sms, z80->timestamp, z80->pc++);
	Z80_ADD_CYCLES(z80, 3);
	return op;
}

static uint8_t z80_parity(uint8_t r)
{
	r ^= (r>>4);
	r ^= (r>>2);
	r ^= (r>>1);
	return (r<<2)&0x04;
}

static uint8_t z80_inc8(struct Z80 *z80, uint8_t a)
{
	uint8_t r = a+1;
	uint8_t h = ((a&0xF)+1)&0x10;
	uint8_t z = (r == 0 ? 0x40 : 0x00);
	uint8_t v = (r == 0x80 ? 0x04 : 0x00);
	z80->gpr[RF] = (r&0xA8) | (z80->gpr[RF]&0x01) | h | v | z;
	return r;
}

static uint8_t z80_dec8(struct Z80 *z80, uint8_t a)
{
	uint8_t r = a-1;
	uint8_t h = ((a&0xF)-1)&0x10;
	uint8_t z = (r == 0 ? 0x40 : 0x00);
	uint8_t v = (r == 0x7F ? 0x04 : 0x00);
	z80->gpr[RF] = (r&0xA8) | (z80->gpr[RF]&0x01) | h | v | z | 0x02;
	return r;
}

static uint8_t z80_add8(struct Z80 *z80, uint8_t a, uint8_t b)
{
	uint8_t r = a+b;
	uint8_t h = ((a&0xF)+(b&0xF))&0x10;
	uint8_t c = (r < a ? 0x01 : 0x00);
	uint8_t z = (r == 0 ? 0x40 : 0x00);
	uint8_t v = 0; // TODO!
	z80->gpr[RF] = (r&0xA8) | h | c | v | z;
	return r;
}

static uint8_t z80_adc8(struct Z80 *z80, uint8_t a, uint8_t b)
{
	if((z80->gpr[RF]&0x01) != 0 && b == 0xFF) {
		// FIXME
		z80_add8(z80, a, 0);
		z80->gpr[RF] |= 0x01;
		return a;
	} else {
		return z80_add8(z80, a, b+(z80->gpr[RF]&0x01));
	}
}

static uint8_t z80_sub8(struct Z80 *z80, uint8_t a, uint8_t b)
{
	uint8_t r = a-b;
	uint8_t h = ((a&0xF)-(b&0xF))&0x10;
	uint8_t c = (r > a ? 0x01 : 0x00);
	uint8_t z = (r == 0 ? 0x40 : 0x00);
	uint8_t v = 0; // TODO!
	z80->gpr[RF] = (r&0xA8) | h | c | v | z | 0x02;
	return r;
}

static uint8_t z80_sbc8(struct Z80 *z80, uint8_t a, uint8_t b)
{
	if((z80->gpr[RF]&0x01) != 0 && b == 0xFF) {
		// FIXME
		z80_sub8(z80, a, 0);
		z80->gpr[RF] |= 0x01;
		return a;
	} else {
		return z80_sub8(z80, a, b+(z80->gpr[RF]&0x01));
	}
}

static uint8_t z80_and8(struct Z80 *z80, uint8_t a, uint8_t b)
{
	uint8_t r = a&b;
	uint8_t z = (r == 0 ? 0x40 : 0x00);
	uint8_t p = z80_parity(r);
	z80->gpr[RF] = (r&0xA8) | p | z | 0x10;
	return r;
}

static uint8_t z80_xor8(struct Z80 *z80, uint8_t a, uint8_t b)
{
	uint8_t r = a^b;
	uint8_t z = (r == 0 ? 0x40 : 0x00);
	uint8_t p = z80_parity(r);
	z80->gpr[RF] = (r&0xA8) | p | z | 0x00;
	return r;
}

static uint8_t z80_or8(struct Z80 *z80, uint8_t a, uint8_t b)
{
	uint8_t r = a|b;
	uint8_t z = (r == 0 ? 0x40 : 0x00);
	uint8_t p = z80_parity(r);
	z80->gpr[RF] = (r&0xA8) | p | z | 0x00;
	return r;
}

static void z80_op_jr_cond(struct Z80 *z80, struct SMS *sms, bool cond)
{
	uint16_t offs = (uint16_t)(int16_t)(int8_t)z80_fetch_op_x(z80, sms);
	if(cond) {
		z80->pc += offs;
		Z80_ADD_CYCLES(z80, 5);
	}
}

static void z80_op_jp_cond(struct Z80 *z80, struct SMS *sms, bool cond)
{
	uint16_t pcl = (uint16_t)z80_fetch_op_x(z80, sms);
	uint16_t pch = (uint16_t)z80_fetch_op_x(z80, sms);
	if(cond) {
		z80->pc = pcl+(pch<<8);
	}
}

static void z80_op_ret(struct Z80 *z80, struct SMS *sms)
{
	uint8_t pcl = z80_mem_read(sms, z80->timestamp, z80->sp++);
	Z80_ADD_CYCLES(z80, 3);
	uint8_t pch = z80_mem_read(sms, z80->timestamp, z80->sp++);
	Z80_ADD_CYCLES(z80, 3);
	//printf("RET %04X [%04X]\n", z80->pc, z80->sp);
	z80->pc = z80_pair(pch, pcl);
	//printf("->  %04X\n", z80->pc);
}

static void z80_op_ret_cond(struct Z80 *z80, struct SMS *sms, bool cond)
{
	Z80_ADD_CYCLES(z80, 1);
	if(cond) {
		z80_op_ret(z80, sms);
	}
}

static void z80_op_call_cond(struct Z80 *z80, struct SMS *sms, bool cond)
{
	uint16_t pcl = (uint16_t)z80_fetch_op_x(z80, sms);
	uint16_t pch = (uint16_t)z80_fetch_op_x(z80, sms);
	if(cond) {
		z80_mem_write(sms, z80->timestamp, --z80->sp, (uint8_t)(z80->pc>>8));
		Z80_ADD_CYCLES(z80, 4);
		z80_mem_write(sms, z80->timestamp, --z80->sp, (uint8_t)(z80->pc>>0));
		Z80_ADD_CYCLES(z80, 3);
		z80->pc = pcl+(pch<<8);
	}
}

void z80_run(struct Z80 *z80, struct SMS *sms, uint64_t timestamp)
{
	// Don't jump back into the past
	if(!TIME_IN_ORDER(z80->timestamp, timestamp)) {
		return;
	}

	// If halted, don't waste time fetching ops
	if(z80->halted) {
		z80->timestamp = timestamp;
		return;
	}

	// Run ops
	uint64_t lstamp = z80->timestamp;
	while(z80->timestamp < timestamp) {
		//if(false && z80->pc != 0x215A) {
		if(false) {
			printf("%020lld: %04X: %02X: A=%02X SP=%04X\n"
				, (unsigned long long)((z80->timestamp-lstamp)/(uint64_t)3)
				, z80->pc, z80->gpr[RF], z80->gpr[RA], z80->sp);
		}

		lstamp = z80->timestamp;
		// Fetch
		int ix = -1;
		uint8_t op;

		for(;;) {
			op = z80_fetch_op_m1(z80, sms);

			if((op|0x20) == 0xFD) {
				ix = (op&0x20)>>5;
			} else {
				break;
			}
		}

		if(op == 0xED) {
			op = z80_fetch_op_m1(z80, sms);
			//printf("*ED %04X %02X %04X\n", z80->pc-2, op, z80->sp);
			// Decode
			switch(op) {
				//
				// X=1
				//

				// Z=3
				case 0x43: { // LD (nn), BC
					uint8_t nl = z80_fetch_op_x(z80, sms);
					uint8_t nh = z80_fetch_op_x(z80, sms);
					uint16_t addr = z80_pair(nh, nl);
					z80_mem_write(sms, z80->timestamp,
						addr, z80->gpr[RC]);
					Z80_ADD_CYCLES(z80, 3);
					addr++;
					z80_mem_write(sms, z80->timestamp,
						addr, z80->gpr[RB]);
					Z80_ADD_CYCLES(z80, 3);
				} break;
				case 0x53: { // LD (nn), DE
					uint8_t nl = z80_fetch_op_x(z80, sms);
					uint8_t nh = z80_fetch_op_x(z80, sms);
					uint16_t addr = z80_pair(nh, nl);
					z80_mem_write(sms, z80->timestamp,
						addr, z80->gpr[RE]);
					Z80_ADD_CYCLES(z80, 3);
					addr++;
					z80_mem_write(sms, z80->timestamp,
						addr, z80->gpr[RD]);
					Z80_ADD_CYCLES(z80, 3);
				} break;
				// 0x63 appears to be an alias to LD (nn), HL/IX/IY
				// I don't know if IX/IY works there, though
				case 0x73: { // LD (nn), SP
					uint8_t nl = z80_fetch_op_x(z80, sms);
					uint8_t nh = z80_fetch_op_x(z80, sms);
					uint16_t addr = z80_pair(nh, nl);
					//printf("ADDR %04X\n", addr);
					z80_mem_write(sms, z80->timestamp,
						addr,
						(uint8_t)(z80->sp>>0));
					Z80_ADD_CYCLES(z80, 3);
					addr++;
					z80_mem_write(sms, z80->timestamp,
						addr,
						(uint8_t)(z80->sp>>8));
					Z80_ADD_CYCLES(z80, 3);
				} break;

				case 0x4B: { // LD BC, (nn)
					uint8_t nl = z80_fetch_op_x(z80, sms);
					uint8_t nh = z80_fetch_op_x(z80, sms);
					uint16_t addr = z80_pair(nh, nl);
					z80->gpr[RC] = z80_mem_read(sms, z80->timestamp, addr);
					Z80_ADD_CYCLES(z80, 3);
					addr++;
					z80->gpr[RB] = z80_mem_read(sms, z80->timestamp, addr);
					Z80_ADD_CYCLES(z80, 3);
				} break;
				case 0x5B: { // LD DE, (nn)
					uint8_t nl = z80_fetch_op_x(z80, sms);
					uint8_t nh = z80_fetch_op_x(z80, sms);
					uint16_t addr = z80_pair(nh, nl);
					z80->gpr[RE] = z80_mem_read(sms, z80->timestamp, addr);
					Z80_ADD_CYCLES(z80, 3);
					addr++;
					z80->gpr[RD] = z80_mem_read(sms, z80->timestamp, addr);
					Z80_ADD_CYCLES(z80, 3);
				} break;
				// 0x6B
				case 0x7B: { // LD SP, (nn)
					uint8_t nl = z80_fetch_op_x(z80, sms);
					uint8_t nh = z80_fetch_op_x(z80, sms);
					uint16_t addr = z80_pair(nh, nl);
					//printf("ADDR %04X\n", addr);
					uint8_t spl = z80_mem_read(sms, z80->timestamp, addr);
					Z80_ADD_CYCLES(z80, 3);
					addr++;
					uint8_t sph = z80_mem_read(sms, z80->timestamp, addr);
					Z80_ADD_CYCLES(z80, 3);
					z80->sp = z80_pair(sph, spl);
				} break;

				// Z=6: IM x
				case 0x46: z80->im = 0; break;
				case 0x4E: z80->im = 0; break;
				case 0x56: z80->im = 3; break; // FIXME: find actual mode for this
				case 0x5E: z80->im = 3; break;
				case 0x66: z80->im = 1; break;
				case 0x6E: z80->im = 1; break;
				case 0x76: z80->im = 2; break;
				case 0x7E: z80->im = 2; break;

				//
				// X=2
				//

				// Z=0
				case 0xA0: { // LDI
					uint16_t dr = z80_pair_pbe(&z80->gpr[RD]);
					uint16_t sr = z80_pair_pbe(&z80->gpr[RH]);
					uint8_t dat = z80_mem_read(sms, z80->timestamp, sr);
					Z80_ADD_CYCLES(z80, 3);
					z80_mem_write(sms, z80->timestamp, dr, dat);
					Z80_ADD_CYCLES(z80, 3+2);
					z80->gpr[RF] = (z80->gpr[RF]&0xC1)
						| ((dat+z80->gpr[RA])&0x08)
						| (((dat+z80->gpr[RA])<<4)&0x20);

					if((++z80->gpr[RE]) == 0) { z80->gpr[RD]++; }
					if((++z80->gpr[RL]) == 0) { z80->gpr[RH]++; }
					if((z80->gpr[RC]--) == 0) {
						z80->gpr[RB]--;
					} else if(z80->gpr[RC] == 0 && z80->gpr[RB] == 0) {
						break;
					}
					z80->gpr[RF] |= 0x04;
				} break;
				case 0xB0: { // LDIR
					uint16_t dr = z80_pair_pbe(&z80->gpr[RD]);
					uint16_t sr = z80_pair_pbe(&z80->gpr[RH]);
					uint8_t dat = z80_mem_read(sms, z80->timestamp, sr);
					Z80_ADD_CYCLES(z80, 3);
					z80_mem_write(sms, z80->timestamp, dr, dat);
					Z80_ADD_CYCLES(z80, 3+2);
					z80->gpr[RF] = (z80->gpr[RF]&0xC1)
						| ((dat+z80->gpr[RA])&0x08)
						| (((dat+z80->gpr[RA])<<4)&0x20);

					if((++z80->gpr[RE]) == 0) { z80->gpr[RD]++; }
					if((++z80->gpr[RL]) == 0) { z80->gpr[RH]++; }
					if((z80->gpr[RC]--) == 0) {
						z80->gpr[RB]--;
					} else if(z80->gpr[RC] == 0 && z80->gpr[RB] == 0) {
						break;
					}
					z80->gpr[RF] |= 0x04;
					z80->pc -= 2;
					Z80_ADD_CYCLES(z80, 5);
				} break;

				case 0xA8: { // LDD
					uint16_t dr = z80_pair_pbe(&z80->gpr[RD]);
					uint16_t sr = z80_pair_pbe(&z80->gpr[RH]);
					uint8_t dat = z80_mem_read(sms, z80->timestamp, sr);
					Z80_ADD_CYCLES(z80, 3);
					z80_mem_write(sms, z80->timestamp, dr, dat);
					Z80_ADD_CYCLES(z80, 3+2);
					z80->gpr[RF] = (z80->gpr[RF]&0xC1)
						| ((dat+z80->gpr[RA])&0x08)
						| (((dat+z80->gpr[RA])<<4)&0x20);

					if((z80->gpr[RE]--) == 0) { z80->gpr[RD]--; }
					if((z80->gpr[RL]--) == 0) { z80->gpr[RH]--; }
					if((z80->gpr[RC]--) == 0) {
						z80->gpr[RB]--;
					} else if(z80->gpr[RC] == 0 && z80->gpr[RB] == 0) {
						break;
					}
					z80->gpr[RF] |= 0x04;
				} break;
				case 0xB8: { // LDDR
					uint16_t dr = z80_pair_pbe(&z80->gpr[RD]);
					uint16_t sr = z80_pair_pbe(&z80->gpr[RH]);
					uint8_t dat = z80_mem_read(sms, z80->timestamp, sr);
					Z80_ADD_CYCLES(z80, 3);
					z80_mem_write(sms, z80->timestamp, dr, dat);
					Z80_ADD_CYCLES(z80, 3+2);
					z80->gpr[RF] = (z80->gpr[RF]&0xC1)
						| ((dat+z80->gpr[RA])&0x08)
						| (((dat+z80->gpr[RA])<<4)&0x20);

					if((z80->gpr[RE]--) == 0) { z80->gpr[RD]--; }
					if((z80->gpr[RL]--) == 0) { z80->gpr[RH]--; }
					if((z80->gpr[RC]--) == 0) {
						z80->gpr[RB]--;
					} else if(z80->gpr[RC] == 0 && z80->gpr[RB] == 0) {
						break;
					}
					z80->gpr[RF] |= 0x04;
					z80->pc -= 2;
					Z80_ADD_CYCLES(z80, 5);
				} break;

				case 0xA1: { // CPI
					// TODO: F --3-1---
					uint16_t sr = z80_pair_pbe(&z80->gpr[RH]);
					uint8_t dat = z80_mem_read(sms, z80->timestamp, sr);
					Z80_ADD_CYCLES(z80, 3);
					uint8_t c = z80->gpr[RF];
					z80_sub8(z80, z80->gpr[RA], dat);
					Z80_ADD_CYCLES(z80, 3+2);
					z80->gpr[RF] = (z80->gpr[RF]&0xFE)
						| 0x02
						| c;

					if((++z80->gpr[RL]) == 0) { z80->gpr[RH]++; }
					if((z80->gpr[RC]--) == 0) {
						z80->gpr[RB]--;
					} else if(z80->gpr[RC] == 0 && z80->gpr[RB] == 0) {
						break;
					}
					z80->gpr[RF] |= 0x04;
				} break;
				case 0xB1: { // CPIR
					// TODO: F --3-1---
					uint16_t sr = z80_pair_pbe(&z80->gpr[RH]);
					uint8_t dat = z80_mem_read(sms, z80->timestamp, sr);
					Z80_ADD_CYCLES(z80, 3);
					uint8_t c = z80->gpr[RF];
					z80_sub8(z80, z80->gpr[RA], dat);
					Z80_ADD_CYCLES(z80, 3+2);
					z80->gpr[RF] = (z80->gpr[RF]&0xFE)
						| 0x02
						| c;

					if((++z80->gpr[RL]) == 0) { z80->gpr[RH]++; }
					if((z80->gpr[RC]--) == 0) {
						z80->gpr[RB]--;
					} else if(z80->gpr[RC] == 0 && z80->gpr[RB] == 0) {
						break;
					}
					z80->gpr[RF] |= 0x04;
					if(z80->gpr[RA] != dat) {
						z80->pc -= 2;
						Z80_ADD_CYCLES(z80, 5);
					}
				} break;

				case 0xA9: { // CPD
					// TODO: F --3-1---
					uint16_t sr = z80_pair_pbe(&z80->gpr[RH]);
					uint8_t dat = z80_mem_read(sms, z80->timestamp, sr);
					Z80_ADD_CYCLES(z80, 3);
					uint8_t c = z80->gpr[RF];
					z80_sub8(z80, z80->gpr[RA], dat);
					Z80_ADD_CYCLES(z80, 3+2);
					z80->gpr[RF] = (z80->gpr[RF]&0xFE)
						| 0x02
						| c;

					if((z80->gpr[RL]--) == 0) { z80->gpr[RH]--; }
					if((z80->gpr[RC]--) == 0) {
						z80->gpr[RB]--;
					} else if(z80->gpr[RC] == 0 && z80->gpr[RB] == 0) {
						break;
					}
					z80->gpr[RF] |= 0x04;
				} break;
				case 0xB9: { // CPDR
					// TODO: F --3-1---
					uint16_t sr = z80_pair_pbe(&z80->gpr[RH]);
					uint8_t dat = z80_mem_read(sms, z80->timestamp, sr);
					Z80_ADD_CYCLES(z80, 3);
					uint8_t c = z80->gpr[RF];
					z80_sub8(z80, z80->gpr[RA], dat);
					Z80_ADD_CYCLES(z80, 3+2);
					z80->gpr[RF] = (z80->gpr[RF]&0xFE)
						| 0x02
						| c;

					if((z80->gpr[RL]--) == 0) { z80->gpr[RH]--; }
					if((z80->gpr[RC]--) == 0) {
						z80->gpr[RB]--;
					} else if(z80->gpr[RC] == 0 && z80->gpr[RB] == 0) {
						break;
					}
					z80->gpr[RF] |= 0x04;
					if(z80->gpr[RA] != dat) {
						z80->pc -= 2;
						Z80_ADD_CYCLES(z80, 5);
					}
				} break;

				default:
					// TODO!
					fprintf(stderr, "OP: ED %02X\n", op);
					fflush(stderr); abort();
					break;
			} continue;
		}

		if(op == 0xCB) {
			//int oy = (op>>3)&7;
			int oz = op&7;
			int ox = (op>>6);

			if(false && ix >= 0) {
				// TODO!
				fprintf(stderr, "OP: %02X CB %02X\n", (ix<<5)|0xDD, op);
				fflush(stderr); abort();
				break;
			}

			uint8_t val;

			// Read
			// FIXME: timing sucks here
			if((oz&~1)==4 && ix >= 0) {
				val = z80->idx[ix&1][oz&1];

			} else if(oz == 6) {
				if(ix < 0) {
					val = z80_mem_read(sms, z80->timestamp,
						z80_pair_pbe(&z80->gpr[RH]));
					Z80_ADD_CYCLES(z80, 4);
				}

			} else {
				val = z80->gpr[oz];
			}

			if(ix >= 0) {
				uint16_t addr = z80_pair_pbe(z80->idx[ix&1]);
				addr += (uint16_t)(uint8_t)(int8_t)z80_fetch_op_x(z80, sms);
				Z80_ADD_CYCLES(z80, 1);
				val = z80_mem_read(sms, z80->timestamp, addr);
				Z80_ADD_CYCLES(z80, 4);
				z80->wz[0] = (uint8_t)(addr>>8);
				z80->wz[1] = (uint8_t)(addr>>0);
			} 

			op = z80_fetch_op_m1(z80, sms);
			//printf("*CB %04X %2d %02X %04X\n", z80->pc, ix, op, z80->sp);

			// ALU
			switch(op&~7) {
				case 0x00: // RLC
					z80->gpr[RF] = ((val>>7)&0x01);
					val = ((val<<1)|((z80->gpr[RA]>>7)&1));
					z80->gpr[RF] |= (val&0xA8) | z80_parity(val);
					break;
				case 0x08: // RRC
					z80->gpr[RF] = (val&0x01);
					val = ((val<<7)|((z80->gpr[RA]>>1)&0x7F));
					z80->gpr[RF] |= (val&0xA8) | z80_parity(val);
					break;

				case 0x10: { // RL
					uint8_t c = z80->gpr[RF]&0x01;
					z80->gpr[RF] = ((val>>7)&0x01);
					val = (val<<1)|c;
					z80->gpr[RF] |= (val&0xA8) | z80_parity(val);
				} break;
				case 0x18: { // RR
					uint8_t c = z80->gpr[RF]&0x01;
					z80->gpr[RF] = (val&0x01);
					val = ((c<<7)|((z80->gpr[RA]>>1)&0x7F));
					z80->gpr[RF] |= (val&0xA8) | z80_parity(val);
				} break;

				case 0x20: // SLA
					z80->gpr[RF] = ((val>>7)&0x01);
					val = (val<<1)|1;
					z80->gpr[RF] |= (val&0xA8) | z80_parity(val);
					break;
				case 0x28: // SRA
					z80->gpr[RF] = (val&0x01);
					val = (uint8_t)(((int8_t)val)>>1);
					z80->gpr[RF] |= (val&0xA8) | z80_parity(val);
					break;
				case 0x30: // SLL
					z80->gpr[RF] = ((val>>7)&0x01);
					val = (val<<1);
					z80->gpr[RF] |= (val&0xA8) | z80_parity(val);
					break;
				case 0x38: // SRL
					z80->gpr[RF] = (val&0x01);
					val = (val>>1)&0x7F;
					z80->gpr[RF] |= (val&0xA8) | z80_parity(val);
					break;

				case 0x40: { // BIT 0, r
					uint8_t sf = z80->gpr[RF]&0x01;
					z80_and8(z80, val, 0x01);
					z80->gpr[RF] &= ~0x01;
					z80->gpr[RF] |= sf;
				} break;
				case 0x48: { // BIT 1, r
					uint8_t sf = z80->gpr[RF]&0x01;
					z80_and8(z80, val, 0x02);
					z80->gpr[RF] &= ~0x01;
					z80->gpr[RF] |= sf;
				} break;
				case 0x50: { // BIT 2, r
					uint8_t sf = z80->gpr[RF]&0x01;
					z80_and8(z80, val, 0x04);
					z80->gpr[RF] &= ~0x01;
					z80->gpr[RF] |= sf;
				} break;
				case 0x58: { // BIT 3, r
					uint8_t sf = z80->gpr[RF]&0x01;
					z80_and8(z80, val, 0x08);
					z80->gpr[RF] &= ~0x01;
					z80->gpr[RF] |= sf;
				} break;
				case 0x60: { // BIT 4, r
					uint8_t sf = z80->gpr[RF]&0x01;
					z80_and8(z80, val, 0x10);
					z80->gpr[RF] &= ~0x01;
					z80->gpr[RF] |= sf;
				} break;
				case 0x68: { // BIT 5, r
					uint8_t sf = z80->gpr[RF]&0x01;
					z80_and8(z80, val, 0x20);
					z80->gpr[RF] &= ~0x01;
					z80->gpr[RF] |= sf;
				} break;
				case 0x70: { // BIT 6, r
					uint8_t sf = z80->gpr[RF]&0x01;
					z80_and8(z80, val, 0x40);
					z80->gpr[RF] &= ~0x01;
					z80->gpr[RF] |= sf;
				} break;
				case 0x78: { // BIT 7, r
					uint8_t sf = z80->gpr[RF]&0x01;
					z80_and8(z80, val, 0x80);
					z80->gpr[RF] &= ~0x01;
					z80->gpr[RF] |= sf;
				} break;

				case 0x80: val &= ~0x01; break; // RES 0, r
				case 0x88: val &= ~0x02; break; // RES 1, r
				case 0x90: val &= ~0x04; break; // RES 2, r
				case 0x98: val &= ~0x08; break; // RES 3, r
				case 0xA0: val &= ~0x10; break; // RES 4, r
				case 0xA8: val &= ~0x20; break; // RES 5, r
				case 0xB0: val &= ~0x40; break; // RES 6, r
				case 0xB8: val &= ~0x80; break; // RES 7, r

				case 0xC0: val |= 0x01; break; // SET 0, r
				case 0xC8: val |= 0x02; break; // SET 1, r
				case 0xD0: val |= 0x04; break; // SET 2, r
				case 0xD8: val |= 0x08; break; // SET 3, r
				case 0xE0: val |= 0x10; break; // SET 4, r
				case 0xE8: val |= 0x20; break; // SET 5, r
				case 0xF0: val |= 0x40; break; // SET 6, r
				case 0xF8: val |= 0x80; break; // SET 7, r

				default:
					// TODO!
					fprintf(stderr, "OP: CB %02X\n", op);
					fflush(stderr); abort();
					break;
			}

			// Write
			if(ox == 1) {
				// BIT - Do nothing here

			} else if((oz&~1)==4 && ix >= 0) {
				z80->idx[ix&1][oz&1] = val;

			} else if(oz == 6) {
				if(ix < 0) {
					z80_mem_write(sms, z80->timestamp,
						z80_pair_pbe(&z80->gpr[RH]),
						val);
					Z80_ADD_CYCLES(z80, 3);
				}

			} else {
				z80->gpr[oz] = val;
			}

			if(ix >= 0) {
				uint16_t addr = z80_pair_pbe(&z80->wz[0]);
				z80_mem_write(sms, z80->timestamp,
					addr,
					val);
				Z80_ADD_CYCLES(z80, 3);
			}

			//printf("*CB DONE %04X\n", z80->pc);

			continue;
		}

		//printf("%04X %02X %04X\n", z80->pc-1, op, z80->sp);

		// X=1 decode (LD y, z)
		if((op>>6) == 1 && op != 0x76) {
			int oy = (op>>3)&7;
			int oz = op&7;

			uint8_t val;

			// Read
			if((oz&~1)==4 && ix >= 0 && oy != 6) {
				val = z80->idx[ix&1][oz&1];

			} else if(oz == 6) {
				if(ix >= 0) {
					uint16_t addr = z80_pair_pbe(z80->idx[ix&1]);
					addr += (uint16_t)(uint8_t)(int8_t)z80_fetch_op_x(z80, sms);
					Z80_ADD_CYCLES(z80, 5);
					val = z80_mem_read(sms, z80->timestamp, addr);
					Z80_ADD_CYCLES(z80, 3);
				} else {
					val = z80_mem_read(sms, z80->timestamp,
						z80_pair_pbe(&z80->gpr[RH]));
					Z80_ADD_CYCLES(z80, 3);
				}

			} else {
				val = z80->gpr[oz];
			}

			// Write
			if((oy&~1)==4 && ix >= 0 && oz != 6) {
				z80->idx[ix&1][oy&1] = val;

			} else if(oy == 6) {
				if(ix >= 0) {
					uint16_t addr = z80_pair_pbe(z80->idx[ix&1]);
					addr += (uint16_t)(uint8_t)(int8_t)z80_fetch_op_x(z80, sms);
					Z80_ADD_CYCLES(z80, 5);
					z80_mem_write(sms, z80->timestamp,
						addr,
						val);
					Z80_ADD_CYCLES(z80, 3);
				} else {
					z80_mem_write(sms, z80->timestamp,
						z80_pair_pbe(&z80->gpr[RH]),
						val);
					Z80_ADD_CYCLES(z80, 3);
				}

			} else {
				z80->gpr[oy] = val;
			}

			continue;
		} 

		// X=2 decode (ALU[y] A, z)
		if((op>>6) == 2) {
			int oy = (op>>3)&7;
			int oz = op&7;

			uint8_t val;

			// Read
			if((oz&~1)==4 && ix >= 0) {
				val = z80->idx[ix&1][oz&1];

			} else if(oz == 6) {
				if(ix >= 0) {
					uint16_t addr = z80_pair_pbe(z80->idx[ix&1]);
					addr += (uint16_t)(uint8_t)(int8_t)z80_fetch_op_x(z80, sms);
					Z80_ADD_CYCLES(z80, 5);
					val = z80_mem_read(sms, z80->timestamp, addr);
					Z80_ADD_CYCLES(z80, 3);
				} else {
					val = z80_mem_read(sms, z80->timestamp,
						z80_pair_pbe(&z80->gpr[RH]));
					Z80_ADD_CYCLES(z80, 3);
				}

			} else {
				val = z80->gpr[oz];
			}

			// ALU
			switch(oy) {
				case 0: { // ADD A, r
					z80->gpr[RA] = z80_add8(z80, z80->gpr[RA], val);
				} break;
				case 1: { // ADC A, r
					z80->gpr[RA] = z80_adc8(z80, z80->gpr[RA], val);
				} break;
				case 2: { // SUB r
					z80->gpr[RA] = z80_sub8(z80, z80->gpr[RA], val);
				} break;
				case 3: { // SBC A, r
					z80->gpr[RA] = z80_sbc8(z80, z80->gpr[RA], val);
				} break;
				case 4: { // AND r
					z80->gpr[RA] = z80_and8(z80, z80->gpr[RA], val);
				} break;
				case 5: { // XOR r
					z80->gpr[RA] = z80_xor8(z80, z80->gpr[RA], val);
				} break;
				case 6: { // OR r
					z80->gpr[RA] = z80_or8(z80, z80->gpr[RA], val);
				} break;
				case 7: { // CP r
					z80_sub8(z80, z80->gpr[RA], val);
					z80->gpr[RF] = (z80->gpr[RF]&~0x28)
						| (val&0x28);
				} break;
			}

			continue;
		}

		// Decode
		switch(op) {
			//
			// X=0
			//

			// Z=0
			case 0x00: // NOP
				break;
			case 0x18: // JR d
				z80_op_jr_cond(z80, sms, true);
				break;
			case 0x20: // JR NZ, d
				z80_op_jr_cond(z80, sms, (z80->gpr[RF]&0x40) == 0);
				break;
			case 0x28: // JR Z, d
				z80_op_jr_cond(z80, sms, (z80->gpr[RF]&0x40) != 0);
				break;
			case 0x30: // JR NC, d
				z80_op_jr_cond(z80, sms, (z80->gpr[RF]&0x01) == 0);
				break;
			case 0x38: // JR C, d
				z80_op_jr_cond(z80, sms, (z80->gpr[RF]&0x01) != 0);
				break;

			// Z=1
			case 0x01: // LD BC, nn
				z80->gpr[RC] = z80_fetch_op_x(z80, sms);
				z80->gpr[RB] = z80_fetch_op_x(z80, sms);
				break;
			case 0x11: // LD DE, nn
				z80->gpr[RE] = z80_fetch_op_x(z80, sms);
				z80->gpr[RD] = z80_fetch_op_x(z80, sms);
				break;
			case 0x21: if(ix >= 0) {
					// LD Iz, nn
					z80->idx[ix&1][1] = z80_fetch_op_x(z80, sms);
					z80->idx[ix&1][0] = z80_fetch_op_x(z80, sms);
				} else {
					// LD HL, nn
					z80->gpr[RL] = z80_fetch_op_x(z80, sms);
					z80->gpr[RH] = z80_fetch_op_x(z80, sms);
				} break;
			case 0x31: { // LD SP, nn
				uint16_t spl = (uint16_t)z80_fetch_op_x(z80, sms);
				uint16_t sph = (uint16_t)z80_fetch_op_x(z80, sms);
				z80->sp = z80_pair(sph, spl);
			} break;

			case 0x09: if(ix >= 0) {
					// ADD Iz, BC
					uint16_t hl = z80_pair_pbe(z80->idx[ix&1]);
					uint16_t q = z80_pair_pbe(&z80->gpr[RB]);
					hl += q;
					z80->idx[ix&1][0] = (uint8_t)(hl>>8);
					z80->idx[ix&1][1] = (uint8_t)(hl>>0);
					Z80_ADD_CYCLES(z80, 11);
				} else {
					// ADD HL, BC
					uint16_t hl = z80_pair_pbe(&z80->gpr[RH]);
					uint16_t q = z80_pair_pbe(&z80->gpr[RB]);
					hl += q;
					z80->gpr[RH] = (uint8_t)(hl>>8);
					z80->gpr[RL] = (uint8_t)(hl>>0);
					Z80_ADD_CYCLES(z80, 11);
				} break;
			case 0x19: if(ix >= 0) {
					// ADD Iz, DE
					uint16_t hl = z80_pair_pbe(z80->idx[ix&1]);
					uint16_t q = z80_pair_pbe(&z80->gpr[RD]);
					hl += q;
					z80->idx[ix&1][0] = (uint8_t)(hl>>8);
					z80->idx[ix&1][1] = (uint8_t)(hl>>0);
					Z80_ADD_CYCLES(z80, 11);
				} else {
					// ADD HL, DE
					uint16_t hl = z80_pair_pbe(&z80->gpr[RH]);
					uint16_t q = z80_pair_pbe(&z80->gpr[RD]);
					hl += q;
					z80->gpr[RH] = (uint8_t)(hl>>8);
					z80->gpr[RL] = (uint8_t)(hl>>0);
					Z80_ADD_CYCLES(z80, 11);
				} break;
			case 0x29: if(ix >= 0) {
					// ADD Iz, Iz
					z80->idx[ix&1][0] <<= 1; 
					z80->idx[ix&1][0] |= (z80->idx[ix&1][1]>>7);
					z80->idx[ix&1][1] <<= 1;
					Z80_ADD_CYCLES(z80, 11);
				} else {
					// ADD HL, HL
					z80->gpr[RH] <<= 1; 
					z80->gpr[RH] |= (z80->gpr[RL]>>7);
					z80->gpr[RL] <<= 1;
					Z80_ADD_CYCLES(z80, 11);
				} break;
			case 0x39: if(ix >= 0) {
					// ADD Iz, SP
					uint16_t hl = z80_pair_pbe(z80->idx[ix&1]);
					uint16_t q = z80->sp;
					hl += q;
					z80->idx[ix&1][0] = (uint8_t)(hl>>8);
					z80->idx[ix&1][1] = (uint8_t)(hl>>0);
					Z80_ADD_CYCLES(z80, 11);
				} else {
					// ADD HL, SP
					uint16_t hl = z80_pair_pbe(&z80->gpr[RH]);
					uint16_t q = z80->sp;
					hl += q;
					z80->gpr[RH] = (uint8_t)(hl>>8);
					z80->gpr[RL] = (uint8_t)(hl>>0);
					Z80_ADD_CYCLES(z80, 11);
				} break;

			// Z=2
			case 0x02: // LD (BC), A
				z80_mem_write(sms, z80->timestamp,
					z80_pair_pbe(&z80->gpr[RB]),
					z80->gpr[RA]);
				Z80_ADD_CYCLES(z80, 3);
				break;
			case 0x12: // LD (DE), A
				z80_mem_write(sms, z80->timestamp,
					z80_pair_pbe(&z80->gpr[RD]),
					z80->gpr[RA]);
				Z80_ADD_CYCLES(z80, 3);
				break;
			case 0x0A: // LD A, (BC)
				z80->gpr[RA] = z80_mem_read(sms, z80->timestamp,
					z80_pair_pbe(&z80->gpr[RB]));
				Z80_ADD_CYCLES(z80, 3);
				break;
			case 0x1A: // LD A, (DE)
				z80->gpr[RA] = z80_mem_read(sms, z80->timestamp,
					z80_pair_pbe(&z80->gpr[RD]));
				Z80_ADD_CYCLES(z80, 3);
				break;

			case 0x22:
				if(ix >= 0) {
					// LD Iz, (nn)
					uint8_t nl = z80_fetch_op_x(z80, sms);
					uint8_t nh = z80_fetch_op_x(z80, sms);
					uint16_t addr = z80_pair(nh, nl);
					z80_mem_write(sms, z80->timestamp, addr, z80->idx[ix&1][1]);
					Z80_ADD_CYCLES(z80, 3);
					addr++;
					z80_mem_write(sms, z80->timestamp, addr, z80->idx[ix&1][0]);
					Z80_ADD_CYCLES(z80, 3);
				} else {
					// LD (nn), HL
					uint8_t nl = z80_fetch_op_x(z80, sms);
					uint8_t nh = z80_fetch_op_x(z80, sms);
					uint16_t addr = z80_pair(nh, nl);
					z80_mem_write(sms, z80->timestamp, addr, z80->gpr[RL]);
					Z80_ADD_CYCLES(z80, 3);
					addr++;
					z80_mem_write(sms, z80->timestamp, addr, z80->gpr[RH]);
					Z80_ADD_CYCLES(z80, 3);
				} break;
			case 0x32: { // LD (nn), A
				uint8_t nl = z80_fetch_op_x(z80, sms);
				uint8_t nh = z80_fetch_op_x(z80, sms);
				uint16_t addr = z80_pair(nh, nl);
				z80_mem_write(sms, z80->timestamp, addr, z80->gpr[RA]);
				Z80_ADD_CYCLES(z80, 3);
			} break;
			case 0x2A:
				if(ix >= 0) {
					// LD Iz, (nn)
					uint8_t nl = z80_fetch_op_x(z80, sms);
					uint8_t nh = z80_fetch_op_x(z80, sms);
					uint16_t addr = z80_pair(nh, nl);
					z80->idx[ix&1][1] = z80_mem_read(sms, z80->timestamp, addr);
					Z80_ADD_CYCLES(z80, 3);
					addr++;
					z80->idx[ix&1][0] = z80_mem_read(sms, z80->timestamp, addr);
					Z80_ADD_CYCLES(z80, 3);
				} else {
					// LD HL, (nn)
					uint8_t nl = z80_fetch_op_x(z80, sms);
					uint8_t nh = z80_fetch_op_x(z80, sms);
					uint16_t addr = z80_pair(nh, nl);
					z80->gpr[RL] = z80_mem_read(sms, z80->timestamp, addr);
					Z80_ADD_CYCLES(z80, 3);
					addr++;
					z80->gpr[RH] = z80_mem_read(sms, z80->timestamp, addr);
					Z80_ADD_CYCLES(z80, 3);
				} break;
			case 0x3A: { // LD A, (nn)
				uint8_t nl = z80_fetch_op_x(z80, sms);
				uint8_t nh = z80_fetch_op_x(z80, sms);
				uint16_t addr = z80_pair(nh, nl);
				z80->gpr[RA] = z80_mem_read(sms, z80->timestamp, addr);
				Z80_ADD_CYCLES(z80, 3);
			} break;

			// Z=3
			case 0x03: // INC BC
				if((++z80->gpr[RC]) == 0) { z80->gpr[RB]++; }
				Z80_ADD_CYCLES(z80, 2);
				break;
			case 0x13: // INC DE
				if((++z80->gpr[RE]) == 0) { z80->gpr[RD]++; }
				Z80_ADD_CYCLES(z80, 2);
				break;
			case 0x23: if(ix >= 0) {
					// INC Iz
					if((++z80->idx[ix&1][1]) == 0) { z80->idx[ix&1][0]++; }
					Z80_ADD_CYCLES(z80, 2);
				} else {
					// INC HL
					if((++z80->gpr[RL]) == 0) { z80->gpr[RH]++; }
					Z80_ADD_CYCLES(z80, 2);
				} break;
			case 0x33: // INC SP
				z80->sp++;
				Z80_ADD_CYCLES(z80, 2);
				break;

			case 0x0B: // DEC BC
				if((z80->gpr[RC]--) == 0) { z80->gpr[RB]--; }
				Z80_ADD_CYCLES(z80, 2);
				break;
			case 0x1B: // DEC DE
				if((z80->gpr[RE]--) == 0) { z80->gpr[RD]--; }
				Z80_ADD_CYCLES(z80, 2);
				break;
			case 0x2B: if(ix >= 0) {
					// DEC Iz
					if((z80->idx[ix&1][1]--) == 0) { z80->idx[ix&1][0]--; }
					Z80_ADD_CYCLES(z80, 2);
				} else {
					// DEC HL
					if((z80->gpr[RL]--) == 0) { z80->gpr[RH]--; }
					Z80_ADD_CYCLES(z80, 2);
				} break;
			case 0x3B: // DEC SP
				z80->sp--;
				Z80_ADD_CYCLES(z80, 2);
				break;

			// Z=4
			case 0x04: // INC B
				z80->gpr[RB] = z80_inc8(z80, z80->gpr[RB]);
				break;
			case 0x0C: // INC C
				z80->gpr[RC] = z80_inc8(z80, z80->gpr[RC]);
				break;
			case 0x14: // INC D
				z80->gpr[RD] = z80_inc8(z80, z80->gpr[RD]);
				break;
			case 0x1C: // INC E
				z80->gpr[RE] = z80_inc8(z80, z80->gpr[RE]);
				break;
			case 0x24: if(ix >= 0) {
					// INC IzH
					z80->idx[ix&1][0] = z80_inc8(z80, z80->idx[ix&1][0]);
				} else {
					// INC H
					z80->gpr[RH] = z80_inc8(z80, z80->gpr[RH]);
				} break;
			case 0x2C: if(ix >= 0) {
					// INC IzL
					z80->idx[ix&1][1] = z80_inc8(z80, z80->idx[ix&1][1]);
				} else {
					// INC L
					z80->gpr[RL] = z80_inc8(z80, z80->gpr[RL]);
				} break;
			case 0x34: if(ix >= 0) {
					// INC (Iz+d)
					uint16_t addr = z80_pair_pbe(z80->idx[ix&1]);
					addr += (uint16_t)(uint8_t)(int8_t)z80_fetch_op_x(z80, sms);
					Z80_ADD_CYCLES(z80, 5);
					uint8_t val = z80_mem_read(sms, z80->timestamp, addr);
					Z80_ADD_CYCLES(z80, 4);
					val = z80_inc8(z80, val);
					z80_mem_write(sms, z80->timestamp, addr, val);
					Z80_ADD_CYCLES(z80, 3);
				} else {
					// INC (HL)
					uint16_t addr = z80_pair_pbe(&z80->gpr[RH]);
					uint8_t val = z80_mem_read(sms, z80->timestamp, addr);
					Z80_ADD_CYCLES(z80, 4);
					val = z80_inc8(z80, val);
					z80_mem_write(sms, z80->timestamp, addr, val);
					Z80_ADD_CYCLES(z80, 3);
				} break;
			case 0x3C: // INC A
				z80->gpr[RA] = z80_inc8(z80, z80->gpr[RA]);
				break;

			// Z=5
			case 0x05: // DEC B
				z80->gpr[RB] = z80_dec8(z80, z80->gpr[RB]);
				break;
			case 0x0D: // DEC C
				z80->gpr[RC] = z80_dec8(z80, z80->gpr[RC]);
				break;
			case 0x15: // DEC D
				z80->gpr[RD] = z80_dec8(z80, z80->gpr[RD]);
				break;
			case 0x1D: // DEC E
				z80->gpr[RE] = z80_dec8(z80, z80->gpr[RE]);
				break;
			case 0x25: if(ix >= 0) {
					// DEC IzH
					z80->idx[ix&1][0] = z80_dec8(z80, z80->idx[ix&1][0]);
				} else {
					// DEC H
					z80->gpr[RH] = z80_dec8(z80, z80->gpr[RH]);
				} break;
			case 0x2D: if(ix >= 0) {
					// DEC IzL
					z80->idx[ix&1][1] = z80_dec8(z80, z80->idx[ix&1][1]);
				} else {
					// DEC L
					z80->gpr[RL] = z80_dec8(z80, z80->gpr[RL]);
				} break;
			case 0x35: if(ix >= 0) {
					// DEC (Iz+d)
					uint16_t addr = z80_pair_pbe(z80->idx[ix&1]);
					addr += (uint16_t)(uint8_t)(int8_t)z80_fetch_op_x(z80, sms);
					Z80_ADD_CYCLES(z80, 5);
					uint8_t val = z80_mem_read(sms, z80->timestamp, addr);
					Z80_ADD_CYCLES(z80, 4);
					val = z80_dec8(z80, val);
					z80_mem_write(sms, z80->timestamp, addr, val);
					Z80_ADD_CYCLES(z80, 3);
				} else {
					// DEC (HL)
					uint16_t addr = z80_pair_pbe(&z80->gpr[RH]);
					uint8_t val = z80_mem_read(sms, z80->timestamp, addr);
					Z80_ADD_CYCLES(z80, 4);
					val = z80_dec8(z80, val);
					z80_mem_write(sms, z80->timestamp, addr, val);
					Z80_ADD_CYCLES(z80, 3);
				} break;
			case 0x3D: // DEC A
				z80->gpr[RA] = z80_dec8(z80, z80->gpr[RA]);
				break;

			// Z=6
			case 0x06: // LD B, n
				z80->gpr[RB] = z80_fetch_op_x(z80, sms);
				break;
			case 0x0E: // LD C, n
				z80->gpr[RC] = z80_fetch_op_x(z80, sms);
				break;
			case 0x16: // LD D, n
				z80->gpr[RD] = z80_fetch_op_x(z80, sms);
				break;
			case 0x1E: // LD E, n
				z80->gpr[RE] = z80_fetch_op_x(z80, sms);
				break;
			case 0x26: if(ix >= 0) {
					// LD IxH, n
					z80->idx[ix&1][0] = z80_fetch_op_x(z80, sms);
				} else {
					// LD H, n
					z80->gpr[RH] = z80_fetch_op_x(z80, sms);
				} break;
			case 0x2E: if(ix >= 0) {
					// LD IxL, n
					z80->idx[ix&1][1] = z80_fetch_op_x(z80, sms);
				} else {
					// LD L, n
					z80->gpr[RL] = z80_fetch_op_x(z80, sms);
				} break;
			case 0x36: if(ix >= 0) {
					// 
					uint16_t addr = z80_pair_pbe(z80->idx[ix&1]);
					addr += (uint16_t)(int16_t)(int8_t)z80_fetch_op_x(z80, sms);
					Z80_ADD_CYCLES(z80, 6);
					z80_mem_write(sms, z80->timestamp,
						addr,
						z80_fetch_op_x(z80, sms));
					Z80_ADD_CYCLES(z80, 3);
			
				} else {
					// LD (HL), n
					z80_mem_write(sms, z80->timestamp,
						z80_pair_pbe(&z80->gpr[RH]),
						z80_fetch_op_x(z80, sms));
					Z80_ADD_CYCLES(z80, 3);
				} break;
			case 0x3E: // LD A, n
				z80->gpr[RA] = z80_fetch_op_x(z80, sms);
				break;

			// Z=7
			case 0x07: // RLCA
				z80->gpr[RF] = (z80->gpr[RF]&0xC4)
					| ((z80->gpr[RA]>>7)&0x01);
				z80->gpr[RA] = ((z80->gpr[RA]<<1)|((z80->gpr[RA]>>7)&1));
				z80->gpr[RF] |= (z80->gpr[RA]&0x28);
				break;
			case 0x0F: // RRCA
				z80->gpr[RF] = (z80->gpr[RF]&0xC4)
					| ((z80->gpr[RA])&0x01);
				z80->gpr[RA] = ((z80->gpr[RA]<<7)|((z80->gpr[RA]>>1)&0x7F));
				z80->gpr[RF] |= (z80->gpr[RA]&0x28);
				break;
			case 0x17: { // RLA
				uint8_t c = z80->gpr[RF]&0x01;
				z80->gpr[RF] = (z80->gpr[RF]&0xC4)
					 |((z80->gpr[RA]>>7)&0x01);
				z80->gpr[RA] = (z80->gpr[RA]<<1)|c;
				z80->gpr[RF] |= (z80->gpr[RA]&0x28);
			} break;
			case 0x1F: { // RRA
				uint8_t c = z80->gpr[RF]&0x01;
				z80->gpr[RF] = (z80->gpr[RF]&0xC4)
					| ((z80->gpr[RA])&0x01);
				z80->gpr[RA] = ((c<<7)|((z80->gpr[RA]>>1)&0x7F));
				z80->gpr[RF] |= (z80->gpr[RA]&0x28);
			} break;

			//
			// X=3
			//

			// Z=0
			case 0xC0: // RET NZ
				z80_op_ret_cond(z80, sms, (z80->gpr[RF]&0x40) == 0);
				break;
			case 0xC8: // RET Z
				z80_op_ret_cond(z80, sms, (z80->gpr[RF]&0x40) != 0);
				break;
			case 0xD0: // RET NC
				z80_op_ret_cond(z80, sms, (z80->gpr[RF]&0x01) == 0);
				break;
			case 0xD8: // RET C
				z80_op_ret_cond(z80, sms, (z80->gpr[RF]&0x01) != 0);
				break;

			// Z=1
			case 0xC1: // POP BC
				z80->gpr[RC] = z80_mem_read(sms, z80->timestamp, z80->sp++);
				Z80_ADD_CYCLES(z80, 3);
				z80->gpr[RB] = z80_mem_read(sms, z80->timestamp, z80->sp++);
				Z80_ADD_CYCLES(z80, 3);
				break;
			case 0xD1: // POP DE
				z80->gpr[RE] = z80_mem_read(sms, z80->timestamp, z80->sp++);
				Z80_ADD_CYCLES(z80, 3);
				z80->gpr[RD] = z80_mem_read(sms, z80->timestamp, z80->sp++);
				Z80_ADD_CYCLES(z80, 3);
				break;
			case 0xE1: if(ix >= 0) {
					// POP Iz
					z80->idx[ix&1][1] = z80_mem_read(sms, z80->timestamp, z80->sp++);
					Z80_ADD_CYCLES(z80, 3);
					z80->idx[ix&1][0] = z80_mem_read(sms, z80->timestamp, z80->sp++);
					Z80_ADD_CYCLES(z80, 3);
				} else {
					// POP HL
					z80->gpr[RL] = z80_mem_read(sms, z80->timestamp, z80->sp++);
					Z80_ADD_CYCLES(z80, 3);
					z80->gpr[RH] = z80_mem_read(sms, z80->timestamp, z80->sp++);
					Z80_ADD_CYCLES(z80, 3);
				} break;
			case 0xF1: // POP AF
				z80->gpr[RF] = z80_mem_read(sms, z80->timestamp, z80->sp++);
				Z80_ADD_CYCLES(z80, 3);
				z80->gpr[RA] = z80_mem_read(sms, z80->timestamp, z80->sp++);
				Z80_ADD_CYCLES(z80, 3);
				break;

			case 0xC9: // RET
				z80_op_ret(z80, sms);
				break;

			// Z=2
			case 0xC2: // JP NZ, d
				z80_op_jp_cond(z80, sms, (z80->gpr[RF]&0x40) == 0);
				break;
			case 0xCA: // JP Z, d
				z80_op_jp_cond(z80, sms, (z80->gpr[RF]&0x40) != 0);
				break;
			case 0xD2: // JP NC, d
				z80_op_jp_cond(z80, sms, (z80->gpr[RF]&0x01) == 0);
				break;
			case 0xDA: // JP C, d
				z80_op_jp_cond(z80, sms, (z80->gpr[RF]&0x01) != 0);
				break;

			// Z=3
			case 0xC3: // JP nn
				z80_op_jp_cond(z80, sms, true);
				break;

			case 0xD3: { // OUT (n), A
				uint16_t port = z80_fetch_op_x(z80, sms);
				port &= 0x00FF;
				port |= (port << 8);
				//printf("IO WRITE %04X %02X\n", port, z80->gpr[RA]);
				z80_io_write(sms, z80->timestamp, port, z80->gpr[RA]);
				Z80_ADD_CYCLES(z80, 4);
			} break;
			case 0xDB: { // IN A, (n)
				uint16_t port = z80_fetch_op_x(z80, sms);
				port &= 0x00FF;
				port |= (port << 8);
				z80->gpr[RA] = z80_io_read(sms, z80->timestamp, port);
				Z80_ADD_CYCLES(z80, 4);
			} break;

			case 0xEB: { // EX DE, HL
				uint8_t t;
				t = z80->gpr[RH];
				z80->gpr[RH] = z80->gpr[RD];
				z80->gpr[RD] = t;
				t = z80->gpr[RL];
				z80->gpr[RL] = z80->gpr[RE];
				z80->gpr[RE] = t;
			} break;

			case 0xF3: // DI
				z80->iff1 = 0;
				z80->iff2 = 0;
				break;
			case 0xFB: // EI
				z80->iff1 = 1;
				z80->iff2 = 1;
				break;

			// Z=4
			case 0xC4: // CALL NZ, d
				z80_op_call_cond(z80, sms, (z80->gpr[RF]&0x40) == 0);
				break;
			case 0xCC: // CALL Z, d
				z80_op_call_cond(z80, sms, (z80->gpr[RF]&0x40) != 0);
				break;
			case 0xD4: // CALL NC, d
				z80_op_call_cond(z80, sms, (z80->gpr[RF]&0x01) == 0);
				break;
			case 0xDC: // CALL C, d
				z80_op_call_cond(z80, sms, (z80->gpr[RF]&0x01) != 0);
				break;

			// Z=5
			case 0xC5: // PUSH BC
				z80_mem_write(sms, z80->timestamp, --z80->sp, z80->gpr[RB]);
				Z80_ADD_CYCLES(z80, 4);
				z80_mem_write(sms, z80->timestamp, --z80->sp, z80->gpr[RC]);
				Z80_ADD_CYCLES(z80, 3);
				break;
			case 0xD5: // PUSH DE
				z80_mem_write(sms, z80->timestamp, --z80->sp, z80->gpr[RD]);
				Z80_ADD_CYCLES(z80, 4);
				z80_mem_write(sms, z80->timestamp, --z80->sp, z80->gpr[RE]);
				Z80_ADD_CYCLES(z80, 3);
				break;
			case 0xE5: if(ix >= 0) {
					// PUSH Iz
					z80_mem_write(sms, z80->timestamp, --z80->sp, z80->idx[ix&1][0]);
					Z80_ADD_CYCLES(z80, 4);
					z80_mem_write(sms, z80->timestamp, --z80->sp, z80->idx[ix&1][1]);
					Z80_ADD_CYCLES(z80, 3);
				} else {
					// PUSH HL
					z80_mem_write(sms, z80->timestamp, --z80->sp, z80->gpr[RH]);
					Z80_ADD_CYCLES(z80, 4);
					z80_mem_write(sms, z80->timestamp, --z80->sp, z80->gpr[RL]);
					Z80_ADD_CYCLES(z80, 3);
				} break;
			case 0xF5: // PUSH AF
				z80_mem_write(sms, z80->timestamp, --z80->sp, z80->gpr[RA]);
				Z80_ADD_CYCLES(z80, 4);
				z80_mem_write(sms, z80->timestamp, --z80->sp, z80->gpr[RF]);
				Z80_ADD_CYCLES(z80, 3);
				break;

			case 0xCD: // CALL nn
				z80_op_call_cond(z80, sms, true);
				break;

			// Z=6
			case 0xC6: { // ADD A, n
				uint8_t val = z80_fetch_op_x(z80, sms);
				z80->gpr[RA] = z80_add8(z80, z80->gpr[RA], val);
			} break;
			case 0xCE: { // ADC A, n
				uint8_t val = z80_fetch_op_x(z80, sms);
				z80->gpr[RA] = z80_adc8(z80, z80->gpr[RA], val);
			} break;
			case 0xD6: { // SUB n
				uint8_t val = z80_fetch_op_x(z80, sms);
				z80->gpr[RA] = z80_sub8(z80, z80->gpr[RA], val);
			} break;
			case 0xDE: { // SBC A, n
				uint8_t val = z80_fetch_op_x(z80, sms);
				z80->gpr[RA] = z80_sbc8(z80, z80->gpr[RA], val);
			} break;
			case 0xE6: { // AND n
				uint8_t val = z80_fetch_op_x(z80, sms);
				z80->gpr[RA] = z80_and8(z80, z80->gpr[RA], val);
			} break;
			case 0xEE: { // XOR n
				uint8_t val = z80_fetch_op_x(z80, sms);
				z80->gpr[RA] = z80_xor8(z80, z80->gpr[RA], val);
			} break;
			case 0xF6: { // OR n
				uint8_t val = z80_fetch_op_x(z80, sms);
				z80->gpr[RA] = z80_or8(z80, z80->gpr[RA], val);
			} break;
			case 0xFE: { // CP n
				uint8_t val = z80_fetch_op_x(z80, sms);
				z80_sub8(z80, z80->gpr[RA], val);
				z80->gpr[RF] = (z80->gpr[RF]&~0x28)
					| (val&0x28);
			} break;

			default:
				// TODO!
				fprintf(stderr, "OP: %02X [%04X]\n", op, z80->pc-1);
				fflush(stderr); abort();
				break;
		}
	}
}

void z80_init(struct Z80 *z80)
{
	*z80 = (struct Z80){ .timestamp=0 };
	z80_reset(z80);
}
