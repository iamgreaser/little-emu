//include "cpu/z80/all.h"

void Z80NAME(reset)(struct Z80 *z80)
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
	Z80_ADD_CYCLES(z80, 3);
}

static uint16_t Z80NAME(pair)(uint8_t h, uint8_t l)
{
	return (((uint16_t)h)<<8) + ((uint16_t)l);
}

static uint16_t Z80NAME(pair_pbe)(uint8_t *p)
{
	return Z80NAME(pair)(p[0], p[1]);
}

static uint8_t Z80NAME(fetch_op_m1)(struct Z80 *z80, Z80_STATE_PARAMS)
{
	uint8_t op = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, z80->pc++);
	Z80_ADD_CYCLES(z80, 4);
	z80->r = (z80->r&0x80) + ((z80->r+1)&0x7F); // TODO: confirm
	return op;
}

static uint8_t Z80NAME(fetch_op_x)(struct Z80 *z80, Z80_STATE_PARAMS)
{
	uint8_t op = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, z80->pc++);
	Z80_ADD_CYCLES(z80, 3);
	return op;
}

static uint16_t Z80NAME(fetch_ix_d)(struct Z80 *z80, Z80_STATE_PARAMS, int ix)
{
	uint16_t addr = Z80NAME(pair_pbe)(z80->idx[ix&1]);
	addr += (uint16_t)(int16_t)(int8_t)Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
	Z80_ADD_CYCLES(z80, 5);
	z80->wz[0] = (uint8_t)(addr>>8);
	return addr;
}

static uint8_t Z80NAME(parity)(uint8_t r)
{
	r ^= (r>>4);
	r ^= (r>>2);
	r ^= (r>>1);
	r = ~r;
	return (r<<2)&0x04;
}

static uint8_t Z80NAME(inc8)(struct Z80 *z80, uint8_t a)
{
	uint8_t r = a+1;
	uint8_t h = ((a&0xF)+1)&0x10;
	uint8_t z = (r == 0 ? 0x40 : 0x00);
	uint8_t v = (r == 0x80 ? 0x04 : 0x00);
	z80->gpr[RF] = (r&0xA8) | (z80->gpr[RF]&0x01) | h | v | z;
	return r;
}

static uint8_t Z80NAME(dec8)(struct Z80 *z80, uint8_t a)
{
	uint8_t r = a-1;
	uint8_t h = ((a&0xF)-1)&0x10;
	uint8_t z = (r == 0 ? 0x40 : 0x00);
	uint8_t v = (r == 0x7F ? 0x04 : 0x00);
	z80->gpr[RF] = (r&0xA8) | (z80->gpr[RF]&0x01) | h | v | z | 0x02;
	return r;
}

static uint8_t Z80NAME(add8)(struct Z80 *z80, uint8_t a, uint8_t b)
{
	uint8_t r = a+b;
	uint8_t h = ((a&0xF)+(b&0xF))&0x10;
	uint8_t c = (r < a ? 0x01 : 0x00);
	uint8_t z = (r == 0 ? 0x40 : 0x00);
	// Sa == Sb && Sa != Sr
	uint8_t v = (((a^b)&0x80) == 0 && ((a^r)&0x80) != 0) ? 0x04 : 0x00;
	z80->gpr[RF] = (r&0xA8) | h | c | v | z;
	return r;
}

static uint8_t Z80NAME(adc8)(struct Z80 *z80, uint8_t a, uint8_t b)
{
	uint8_t rc = (z80->gpr[RF]&0x01);
	uint8_t r = a+b+rc;
	uint8_t h = ((a&0xF)+(b&0xF)+rc)&0x10;
	uint8_t c = (r < a || (rc == 1 && r == a) ? 0x01 : 0x00);
	uint8_t z = (r == 0 ? 0x40 : 0x00);
	// Sa == Sb && Sa != Sr
	uint8_t v = (((a^b)&0x80) == 0 && ((a^r)&0x80) != 0) ? 0x04 : 0x00;
	z80->gpr[RF] = (r&0xA8) | h | c | v | z;
	return r;
}

static uint8_t Z80NAME(sub8)(struct Z80 *z80, uint8_t a, uint8_t b)
{
	uint8_t r = a-b;
	uint8_t h = ((a&0xF)-(b&0xF))&0x10;
	uint8_t c = (r > a ? 0x01 : 0x00);
	uint8_t z = (r == 0 ? 0x40 : 0x00);
	// Sa != Sb && Sa != Sr
	uint8_t v = (((a^b)&0x80) != 0 && ((a^r)&0x80) != 0) ? 0x04 : 0x00;
	z80->gpr[RF] = (r&0xA8) | h | c | v | z | 0x02;
	return r;
}

static uint8_t Z80NAME(sbc8)(struct Z80 *z80, uint8_t a, uint8_t b)
{
	uint8_t rc = (z80->gpr[RF]&0x01);
	uint8_t r = a-b-rc;
	uint8_t h = ((a&0xF)-(b&0xF)-rc)&0x10;
	uint8_t c = (r > a || (rc == 1 && r == a) ? 0x01 : 0x00);
	uint8_t z = (r == 0 ? 0x40 : 0x00);
	// Sa != Sb && Sa != Sr
	uint8_t v = (((a^b)&0x80) != 0 && ((a^r)&0x80) != 0) ? 0x04 : 0x00;
	z80->gpr[RF] = (r&0xA8) | h | c | v | z | 0x02;
	return r;
}

static uint8_t Z80NAME(and8)(struct Z80 *z80, uint8_t a, uint8_t b)
{
	uint8_t r = a&b;
	uint8_t z = (r == 0 ? 0x40 : 0x00);
	uint8_t p = Z80NAME(parity)(r);
	z80->gpr[RF] = (r&0xA8) | p | z | 0x10;
	return r;
}

static uint8_t Z80NAME(xor8)(struct Z80 *z80, uint8_t a, uint8_t b)
{
	uint8_t r = a^b;
	uint8_t z = (r == 0 ? 0x40 : 0x00);
	uint8_t p = Z80NAME(parity)(r);
	z80->gpr[RF] = (r&0xA8) | p | z | 0x00;
	return r;
}

static uint8_t Z80NAME(or8)(struct Z80 *z80, uint8_t a, uint8_t b)
{
	uint8_t r = a|b;
	uint8_t z = (r == 0 ? 0x40 : 0x00);
	uint8_t p = Z80NAME(parity)(r);
	z80->gpr[RF] = (r&0xA8) | p | z | 0x00;
	return r;
}

static uint16_t Z80NAME(add16)(struct Z80 *z80, uint16_t a, uint16_t b)
{
	z80->wz[0] = (uint8_t)(a>>8);
	uint16_t r = a+b;
	uint8_t h = (((a&0xFFF)+(b&0xFFF))&0x1000)>>8;
	uint8_t c = (r < a ? 0x01 : 0x00);
	z80->gpr[RF] = (z80->gpr[RF]&0xC4)
		| ((r>>8)&0x28) | h | c;
	return r;
}

static uint16_t Z80NAME(adc16)(struct Z80 *z80, uint16_t a, uint16_t b)
{
	uint16_t rc = (uint16_t)(z80->gpr[RF]&0x01);
	uint16_t r = a+b+rc;
	uint8_t h = (((a&0xFFF)+(b&0xFFF)+rc)&0x1000)>>8;
	uint8_t c = (r < a || (rc == 1 && r == a) ? 0x01 : 0x00);
	uint8_t z = (r == 0 ? 0x40 : 0x00);
	// Sa == Sb && Sa != Sr
	uint8_t v = (((a^b)&0x8000) == 0 && ((a^r)&0x8000) != 0) ? 0x04 : 0x00;
	z80->gpr[RF] = ((r>>8)&0xA8) | h | c | v | z;
	return r;
}

static uint16_t Z80NAME(sbc16)(struct Z80 *z80, uint16_t a, uint16_t b)
{
	uint16_t rc = (uint16_t)(z80->gpr[RF]&0x01);
	uint16_t r = a-b-rc;
	uint8_t h = (((a&0xFFF)-(b&0xFFF)-rc)&0x1000)>>8;
	uint8_t c = (r > a || (rc == 1 && r == a) ? 0x01 : 0x00);
	uint8_t z = (r == 0 ? 0x40 : 0x00);
	// Sa != Sb && Sa != Sr
	uint8_t v = (((a^b)&0x8000) != 0 && ((a^r)&0x8000) != 0) ? 0x04 : 0x00;
	z80->gpr[RF] = ((r>>8)&0xA8) | h | c | v | z | 0x02;
	return r;
}

static void Z80NAME(op_jr_cond)(struct Z80 *z80, Z80_STATE_PARAMS, bool cond)
{
	uint16_t offs = (uint16_t)(int16_t)(int8_t)Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
	z80->wz[0] = (uint8_t)(offs>>8);
	if(cond) {
		z80->pc += offs;
		Z80_ADD_CYCLES(z80, 5);
	}
}

static void Z80NAME(op_jp_cond)(struct Z80 *z80, Z80_STATE_PARAMS, bool cond)
{
	uint16_t pcl = (uint16_t)Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
	uint16_t pch = (uint16_t)Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
	if(cond) {
		z80->pc = pcl+(pch<<8);
	}
}

static void Z80NAME(op_ret)(struct Z80 *z80, Z80_STATE_PARAMS)
{
	uint8_t pcl = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, z80->sp++);
	Z80_ADD_CYCLES(z80, 3);
	uint8_t pch = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, z80->sp++);
	Z80_ADD_CYCLES(z80, 3);
	//printf("RET %04X [%04X]\n", z80->pc, z80->sp);
	z80->pc = Z80NAME(pair)(pch, pcl);
	//printf("->  %04X\n", z80->pc);
}

static void Z80NAME(op_ret_cond)(struct Z80 *z80, Z80_STATE_PARAMS, bool cond)
{
	Z80_ADD_CYCLES(z80, 1);
	if(cond) {
		Z80NAME(op_ret)(z80, Z80_STATE_ARGS);
	}
}

static void Z80NAME(op_call_cond)(struct Z80 *z80, Z80_STATE_PARAMS, bool cond)
{
	uint16_t pcl = (uint16_t)Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
	uint16_t pch = (uint16_t)Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
	if(cond) {
		Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, --z80->sp, (uint8_t)(z80->pc>>8));
		Z80_ADD_CYCLES(z80, 4);
		Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, --z80->sp, (uint8_t)(z80->pc>>0));
		Z80_ADD_CYCLES(z80, 3);
		z80->pc = pcl+(pch<<8);
	}
}

static void Z80NAME(op_rst)(struct Z80 *z80, Z80_STATE_PARAMS, uint16_t addr)
{
	Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, --z80->sp, (uint8_t)(z80->pc>>8));
	Z80_ADD_CYCLES(z80, 4);
	Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, --z80->sp, (uint8_t)(z80->pc>>0));
	Z80_ADD_CYCLES(z80, 3);
	z80->pc = addr;
}

void Z80NAME(irq)(struct Z80 *z80, Z80_STATE_PARAMS, uint8_t dat)
{
	if(z80->iff1 == 0) {
		return;
	}

	/*
	printf("IRQ %d %d\n"
		, (int32_t)(z80->H.timestamp%684)
		, (int32_t)((z80->H.timestamp%(684*SCANLINES))/684)
		);
	*/

	// TODO: fetch op using dat
	//printf("%d\n", z80->im);
	//assert(z80->im == 1);

	if(z80->im == 2) {
		// IM2
		z80->iff1 = 0;
		z80->iff2 = 0;
		Z80_ADD_CYCLES(z80, 7);
		z80->r = (z80->r&0x80) + ((z80->r+1)&0x7F); // TODO: confirm
		Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, --z80->sp, (uint8_t)(z80->pc>>8));
		Z80_ADD_CYCLES(z80, 3);
		Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, --z80->sp, (uint8_t)(z80->pc>>0));
		Z80_ADD_CYCLES(z80, 3);
		uint16_t saddr = ((uint16_t)z80->i)<<8;
		saddr += 0xFF;
		z80->pc = 0xFF&Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, saddr++);
		Z80_ADD_CYCLES(z80, 3);
		z80->pc |= ((uint16_t)(0xFF&Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, saddr++)))<<8;
		Z80_ADD_CYCLES(z80, 3);
		z80->halted = false;

	} else {
		// XXX: assuming 0xFF is on the bus for IM 0 (which it IS on a SMS2)
		z80->iff1 = 0;
		z80->iff2 = 0;
		Z80_ADD_CYCLES(z80, 7);
		z80->r = (z80->r&0x80) + ((z80->r+1)&0x7F); // TODO: confirm
		Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, --z80->sp, (uint8_t)(z80->pc>>8));
		Z80_ADD_CYCLES(z80, 3);
		Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, --z80->sp, (uint8_t)(z80->pc>>0));
		Z80_ADD_CYCLES(z80, 3);
		z80->pc = 0x0038;
		z80->halted = false;
	}
}

void Z80NAME(nmi)(struct Z80 *z80, Z80_STATE_PARAMS)
{
	z80->iff1 = 0;
	Z80_ADD_CYCLES(z80, 5);
	z80->r = (z80->r&0x80) + ((z80->r+1)&0x7F); // TODO: confirm
	Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, --z80->sp, (uint8_t)(z80->pc>>8));
	Z80_ADD_CYCLES(z80, 3);
	Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, --z80->sp, (uint8_t)(z80->pc>>0));
	Z80_ADD_CYCLES(z80, 3);
	z80->pc = 0x0066;
	z80->halted = false;
}

void Z80NAME(run)(struct Z80 *z80, Z80_STATE_PARAMS, uint64_t timestamp)
{
	// Don't jump back into the past
	if(!TIME_IN_ORDER(z80->H.timestamp, timestamp)) {
		return;
	}

	// If halted, don't waste time fetching ops
	if(z80->halted) {
		if(z80->iff1 != 0 && Z80_INT_CHECK) {
			Z80NAME(irq)(z80, Z80_STATE_ARGS, 0xFF);
		} else {
			while(TIME_IN_ORDER(z80->H.timestamp, timestamp)) {
				Z80_ADD_CYCLES(z80, 4);
				z80->r = (z80->r&0x80) + ((z80->r+1)&0x7F);
			}
			//z80->H.timestamp = timestamp;
			return;
		}
	}

	// Run ops
	uint64_t lstamp = z80->H.timestamp;
	z80->H.timestamp_end = timestamp;
	while(TIME_IN_ORDER(z80->H.timestamp, z80->H.timestamp_end)) {
		//if(false && z80->pc != 0x215A) {
		if(false) {
			printf("%020lld: %04X: %02X: A=%02X SP=%04X\n"
				, (unsigned long long)((z80->H.timestamp-lstamp)/(uint64_t)3)
				, z80->pc, z80->gpr[RF], z80->gpr[RA], z80->sp);
		}

		// Check for IRQ
		if(z80->noni == 0 && z80->iff1 != 0 && Z80_INT_CHECK) {
			Z80NAME(irq)(z80, Z80_STATE_ARGS, 0xFF);
		}
		z80->noni = 0;

		lstamp = z80->H.timestamp;
		// Fetch
		int ix = -1;
		uint8_t op;

		for(;;) {
			op = Z80NAME(fetch_op_m1)(z80, Z80_STATE_ARGS);

			if((op|0x20) == 0xFD) {
				ix = (op&0x20)>>5;
			} else {
				break;
			}
		}

		if(op == 0xED) {
			op = Z80NAME(fetch_op_m1)(z80, Z80_STATE_ARGS);
			//printf("*ED %04X %02X %04X\n", z80->pc-2, op, z80->sp);
			// Decode
			switch(op) {
				//
				// X=1
				//

				// Z=0
				case 0x40: // IN B, (C)
					z80->gpr[RB] = Z80NAME(io_read)(Z80_STATE_ARGS, z80->H.timestamp,
						Z80NAME(pair_pbe)(&z80->gpr[RB]));
					z80->gpr[RF] = (z80->gpr[RF]&0x01)
						| (z80->gpr[RB]&0xA8)
						| Z80NAME(parity)(z80->gpr[RB])
						| (z80->gpr[RB] == 0 ? 0x40 : 0x00);
					Z80_ADD_CYCLES(z80, 4);
					break;
				case 0x48: // IN C, (C)
					z80->gpr[RC] = Z80NAME(io_read)(Z80_STATE_ARGS, z80->H.timestamp,
						Z80NAME(pair_pbe)(&z80->gpr[RB]));
					z80->gpr[RF] = (z80->gpr[RF]&0x01)
						| (z80->gpr[RC]&0xA8)
						| Z80NAME(parity)(z80->gpr[RC])
						| (z80->gpr[RC] == 0 ? 0x40 : 0x00);
					Z80_ADD_CYCLES(z80, 4);
					break;
				case 0x50: // IN D, (C)
					z80->gpr[RD] = Z80NAME(io_read)(Z80_STATE_ARGS, z80->H.timestamp,
						Z80NAME(pair_pbe)(&z80->gpr[RB]));
					z80->gpr[RF] = (z80->gpr[RF]&0x01)
						| (z80->gpr[RD]&0xA8)
						| Z80NAME(parity)(z80->gpr[RD])
						| (z80->gpr[RD] == 0 ? 0x40 : 0x00);
					Z80_ADD_CYCLES(z80, 4);
					break;
				case 0x58: // IN E, (C)
					z80->gpr[RE] = Z80NAME(io_read)(Z80_STATE_ARGS, z80->H.timestamp,
						Z80NAME(pair_pbe)(&z80->gpr[RB]));
					z80->gpr[RF] = (z80->gpr[RF]&0x01)
						| (z80->gpr[RE]&0xA8)
						| Z80NAME(parity)(z80->gpr[RE])
						| (z80->gpr[RE] == 0 ? 0x40 : 0x00);
					Z80_ADD_CYCLES(z80, 4);
					break;
				case 0x60: // IN H, (C)
					z80->gpr[RH] = Z80NAME(io_read)(Z80_STATE_ARGS, z80->H.timestamp,
						Z80NAME(pair_pbe)(&z80->gpr[RB]));
					z80->gpr[RF] = (z80->gpr[RF]&0x01)
						| (z80->gpr[RH]&0xA8)
						| Z80NAME(parity)(z80->gpr[RH])
						| (z80->gpr[RH] == 0 ? 0x40 : 0x00);
					Z80_ADD_CYCLES(z80, 4);
					break;
				case 0x68: // IN L, (C)
					z80->gpr[RL] = Z80NAME(io_read)(Z80_STATE_ARGS, z80->H.timestamp,
						Z80NAME(pair_pbe)(&z80->gpr[RB]));
					z80->gpr[RF] = (z80->gpr[RF]&0x01)
						| (z80->gpr[RL]&0xA8)
						| Z80NAME(parity)(z80->gpr[RL])
						| (z80->gpr[RL] == 0 ? 0x40 : 0x00);
					Z80_ADD_CYCLES(z80, 4);
					break;
				case 0x70: { // IN (C)
					uint8_t tmp = Z80NAME(io_read)(Z80_STATE_ARGS, z80->H.timestamp,
						Z80NAME(pair_pbe)(&z80->gpr[RB]));
					z80->gpr[RF] = (z80->gpr[RF]&0x01)
						| (tmp&0xA8)
						| Z80NAME(parity)(tmp)
						| (tmp == 0 ? 0x40 : 0x00);
					Z80_ADD_CYCLES(z80, 4);
				} break;
				case 0x78: // IN A, (C)
					z80->gpr[RA] = Z80NAME(io_read)(Z80_STATE_ARGS, z80->H.timestamp,
						Z80NAME(pair_pbe)(&z80->gpr[RB]));
					z80->gpr[RF] = (z80->gpr[RF]&0x01)
						| (z80->gpr[RA]&0xA8)
						| Z80NAME(parity)(z80->gpr[RA])
						| (z80->gpr[RA] == 0 ? 0x40 : 0x00);
					Z80_ADD_CYCLES(z80, 4);
					break;

				// Z=1
				case 0x41: // OUT (C), B
					Z80NAME(io_write)(Z80_STATE_ARGS, z80->H.timestamp,
						Z80NAME(pair_pbe)(&z80->gpr[RB]),
						z80->gpr[RB]);
					Z80_ADD_CYCLES(z80, 4);
					break;
				case 0x49: // OUT (C), C
					Z80NAME(io_write)(Z80_STATE_ARGS, z80->H.timestamp,
						Z80NAME(pair_pbe)(&z80->gpr[RB]),
						z80->gpr[RC]);
					Z80_ADD_CYCLES(z80, 4);
					break;
				case 0x51: // OUT (C), D
					Z80NAME(io_write)(Z80_STATE_ARGS, z80->H.timestamp,
						Z80NAME(pair_pbe)(&z80->gpr[RB]),
						z80->gpr[RD]);
					Z80_ADD_CYCLES(z80, 4);
					break;
				case 0x59: // OUT (C), E
					Z80NAME(io_write)(Z80_STATE_ARGS, z80->H.timestamp,
						Z80NAME(pair_pbe)(&z80->gpr[RB]),
						z80->gpr[RE]);
					Z80_ADD_CYCLES(z80, 4);
					break;
				case 0x61: // OUT (C), H
					Z80NAME(io_write)(Z80_STATE_ARGS, z80->H.timestamp,
						Z80NAME(pair_pbe)(&z80->gpr[RB]),
						z80->gpr[RH]);
					Z80_ADD_CYCLES(z80, 4);
					break;
				case 0x69: // OUT (C), L
					Z80NAME(io_write)(Z80_STATE_ARGS, z80->H.timestamp,
						Z80NAME(pair_pbe)(&z80->gpr[RB]),
						z80->gpr[RL]);
					Z80_ADD_CYCLES(z80, 4);
					break;
				case 0x71: // OUT (C), 0
					Z80NAME(io_write)(Z80_STATE_ARGS, z80->H.timestamp,
						Z80NAME(pair_pbe)(&z80->gpr[RB]),
						0);
					Z80_ADD_CYCLES(z80, 4);
					break;
				case 0x79: // OUT (C), A
					Z80NAME(io_write)(Z80_STATE_ARGS, z80->H.timestamp,
						Z80NAME(pair_pbe)(&z80->gpr[RB]),
						z80->gpr[RA]);
					Z80_ADD_CYCLES(z80, 4);
					break;

				// Z=2
				case 0x42: { // SBC HL, BC
					uint16_t hl = Z80NAME(pair_pbe)(&z80->gpr[RH]);
					uint16_t q = Z80NAME(pair_pbe)(&z80->gpr[RB]);
					hl = Z80NAME(sbc16)(z80, hl, q);
					z80->gpr[RH] = (uint8_t)(hl>>8);
					z80->gpr[RL] = (uint8_t)(hl>>0);
					Z80_ADD_CYCLES(z80, 7);
				} break;
				case 0x52: { // SBC HL, DE
					uint16_t hl = Z80NAME(pair_pbe)(&z80->gpr[RH]);
					uint16_t q = Z80NAME(pair_pbe)(&z80->gpr[RD]);
					hl = Z80NAME(sbc16)(z80, hl, q);
					z80->gpr[RH] = (uint8_t)(hl>>8);
					z80->gpr[RL] = (uint8_t)(hl>>0);
					Z80_ADD_CYCLES(z80, 7);
				} break;
				case 0x62: { // SBC HL, HL
					uint16_t hl = Z80NAME(pair_pbe)(&z80->gpr[RH]);
					uint16_t q = Z80NAME(pair_pbe)(&z80->gpr[RH]);
					hl = Z80NAME(sbc16)(z80, hl, q);
					z80->gpr[RH] = (uint8_t)(hl>>8);
					z80->gpr[RL] = (uint8_t)(hl>>0);
					Z80_ADD_CYCLES(z80, 7);
				} break;
				case 0x72: { // SBC HL, SP
					uint16_t hl = Z80NAME(pair_pbe)(&z80->gpr[RH]);
					uint16_t q = z80->sp;
					hl = Z80NAME(sbc16)(z80, hl, q);
					z80->gpr[RH] = (uint8_t)(hl>>8);
					z80->gpr[RL] = (uint8_t)(hl>>0);
					Z80_ADD_CYCLES(z80, 7);
				} break;

				case 0x4A: { // ADC HL, BC
					uint16_t hl = Z80NAME(pair_pbe)(&z80->gpr[RH]);
					uint16_t q = Z80NAME(pair_pbe)(&z80->gpr[RB]);
					hl = Z80NAME(adc16)(z80, hl, q);
					z80->gpr[RH] = (uint8_t)(hl>>8);
					z80->gpr[RL] = (uint8_t)(hl>>0);
					Z80_ADD_CYCLES(z80, 7);
				} break;
				case 0x5A: { // ADC HL, DE
					uint16_t hl = Z80NAME(pair_pbe)(&z80->gpr[RH]);
					uint16_t q = Z80NAME(pair_pbe)(&z80->gpr[RD]);
					hl = Z80NAME(adc16)(z80, hl, q);
					z80->gpr[RH] = (uint8_t)(hl>>8);
					z80->gpr[RL] = (uint8_t)(hl>>0);
					Z80_ADD_CYCLES(z80, 7);
				} break;
				case 0x6A: { // ADC HL, HL
					uint16_t hl = Z80NAME(pair_pbe)(&z80->gpr[RH]);
					uint16_t q = Z80NAME(pair_pbe)(&z80->gpr[RH]);
					hl = Z80NAME(adc16)(z80, hl, q);
					z80->gpr[RH] = (uint8_t)(hl>>8);
					z80->gpr[RL] = (uint8_t)(hl>>0);
					Z80_ADD_CYCLES(z80, 7);
				} break;
				case 0x7A: { // ADC HL, SP
					uint16_t hl = Z80NAME(pair_pbe)(&z80->gpr[RH]);
					uint16_t q = z80->sp;
					hl = Z80NAME(adc16)(z80, hl, q);
					z80->gpr[RH] = (uint8_t)(hl>>8);
					z80->gpr[RL] = (uint8_t)(hl>>0);
					Z80_ADD_CYCLES(z80, 7);
				} break;

				// Z=3
				case 0x43: { // LD (nn), BC
					uint8_t nl = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
					uint8_t nh = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
					uint16_t addr = Z80NAME(pair)(nh, nl);
					Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp,
						addr, z80->gpr[RC]);
					Z80_ADD_CYCLES(z80, 3);
					addr++;
					Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp,
						addr, z80->gpr[RB]);
					Z80_ADD_CYCLES(z80, 3);
				} break;
				case 0x53: { // LD (nn), DE
					uint8_t nl = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
					uint8_t nh = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
					uint16_t addr = Z80NAME(pair)(nh, nl);
					Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp,
						addr, z80->gpr[RE]);
					Z80_ADD_CYCLES(z80, 3);
					addr++;
					Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp,
						addr, z80->gpr[RD]);
					Z80_ADD_CYCLES(z80, 3);
				} break;
				case 0x63: { // LD (nn), HL (alt)
					uint8_t nl = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
					uint8_t nh = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
					uint16_t addr = Z80NAME(pair)(nh, nl);
					Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp,
						addr, z80->gpr[RL]);
					Z80_ADD_CYCLES(z80, 3);
					addr++;
					Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp,
						addr, z80->gpr[RH]);
					Z80_ADD_CYCLES(z80, 3);
				} break;
				case 0x73: { // LD (nn), SP
					uint8_t nl = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
					uint8_t nh = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
					uint16_t addr = Z80NAME(pair)(nh, nl);
					Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp,
						addr,
						(uint8_t)(z80->sp>>0));
					Z80_ADD_CYCLES(z80, 3);
					addr++;
					Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp,
						addr,
						(uint8_t)(z80->sp>>8));
					Z80_ADD_CYCLES(z80, 3);
				} break;

				case 0x4B: { // LD BC, (nn)
					uint8_t nl = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
					uint8_t nh = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
					uint16_t addr = Z80NAME(pair)(nh, nl);
					z80->gpr[RC] = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, addr);
					Z80_ADD_CYCLES(z80, 3);
					addr++;
					z80->gpr[RB] = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, addr);
					Z80_ADD_CYCLES(z80, 3);
				} break;
				case 0x5B: { // LD DE, (nn)
					uint8_t nl = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
					uint8_t nh = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
					uint16_t addr = Z80NAME(pair)(nh, nl);
					z80->gpr[RE] = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, addr);
					Z80_ADD_CYCLES(z80, 3);
					addr++;
					z80->gpr[RD] = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, addr);
					Z80_ADD_CYCLES(z80, 3);
				} break;
				case 0x6B: { // LD HL, (nn) (alt)
					uint8_t nl = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
					uint8_t nh = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
					uint16_t addr = Z80NAME(pair)(nh, nl);
					z80->gpr[RL] = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, addr);
					Z80_ADD_CYCLES(z80, 3);
					addr++;
					z80->gpr[RH] = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, addr);
					Z80_ADD_CYCLES(z80, 3);
				} break;
				case 0x7B: { // LD SP, (nn)
					uint8_t nl = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
					uint8_t nh = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
					uint16_t addr = Z80NAME(pair)(nh, nl);
					uint8_t spl = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, addr);
					Z80_ADD_CYCLES(z80, 3);
					addr++;
					uint8_t sph = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, addr);
					Z80_ADD_CYCLES(z80, 3);
					z80->sp = Z80NAME(pair)(sph, spl);
				} break;

				// Z=4: NEG
				case 0x44: case 0x4C:
				case 0x54: case 0x5C:
				case 0x64: case 0x6C:
				case 0x74: case 0x7C:
					z80->gpr[RA] = Z80NAME(sub8)(z80, 0, z80->gpr[RA]);
					break;

				// Z=5: RETI/RETN
				case 0x45: case 0x4D:
				case 0x55: case 0x5D:
				case 0x65: case 0x6D:
				case 0x75: case 0x7D:
					z80->iff1 = z80->iff2;
					Z80NAME(op_ret)(z80, Z80_STATE_ARGS);
					break;

				// Z=6: IM x
				case 0x46: z80->im = 0; break;
				case 0x4E: z80->im = 0; break;
				case 0x56: z80->im = 1; break;
				case 0x5E: z80->im = 2; break;
				case 0x66: z80->im = 0; break;
				case 0x6E: z80->im = 0; break;
				case 0x76: z80->im = 1; break;
				case 0x7E: z80->im = 2; break;

				// Z=7
				case 0x47: // LD I, A
					z80->i = z80->gpr[RA];
					Z80_ADD_CYCLES(z80, 1);
					break;
				case 0x4F: // LD R, A
					z80->r = z80->gpr[RA];
					Z80_ADD_CYCLES(z80, 1);
					break;
				case 0x57: // LD A, I
					z80->gpr[RA] = z80->i;
					z80->gpr[RF] = (z80->gpr[RF]&0x01)
						| (z80->gpr[RA]&0xA8)
						| (z80->gpr[RA] == 0 ? 0x40 : 0x00)
						| (z80->iff2 ? 0x04 : 0x00);
					Z80_ADD_CYCLES(z80, 1);
					break;
				case 0x5F: // LD A, R
					z80->gpr[RA] = z80->r;
					z80->gpr[RF] = (z80->gpr[RF]&0x01)
						| (z80->gpr[RA]&0xA8)
						| (z80->gpr[RA] == 0 ? 0x40 : 0x00)
						| (z80->iff2 ? 0x04 : 0x00);
					Z80_ADD_CYCLES(z80, 1);
					break;

				case 0x67: { // RRD
					uint16_t addr = Z80NAME(pair_pbe)(&z80->gpr[RH]);
					uint16_t val = 0xFF&(uint16_t)Z80NAME(mem_read)(Z80_STATE_ARGS,
						z80->H.timestamp, addr);
					Z80_ADD_CYCLES(z80, 3);
					uint8_t na = (val&0x0F);
					val = (val>>4);
					val |= ((z80->gpr[RA]<<4)&0xF0);
					z80->gpr[RA] &= 0xF0;
					z80->gpr[RA] |= na;
					Z80_ADD_CYCLES(z80, 4);
					Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, addr, (uint8_t)(val));
					Z80_ADD_CYCLES(z80, 3);
					z80->gpr[RF] = (z80->gpr[RF]&0x01)
						| (z80->gpr[RA]&0xA8)
						| (z80->gpr[RA] == 0 ? 0x40 : 0x00)
						| Z80NAME(parity)(z80->gpr[RA]);
				} break;
				case 0x6F: { // RLD
					uint16_t addr = Z80NAME(pair_pbe)(&z80->gpr[RH]);
					uint16_t val = 0xFF&(uint16_t)Z80NAME(mem_read)(Z80_STATE_ARGS,
						z80->H.timestamp, addr);
					Z80_ADD_CYCLES(z80, 3);
					val = (val<<4);
					val |= ((z80->gpr[RA])&0x0F);
					z80->gpr[RA] &= 0xF0;
					z80->gpr[RA] |= (val>>8)&0x0F;
					Z80_ADD_CYCLES(z80, 4);
					Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, addr, (uint8_t)(val));
					Z80_ADD_CYCLES(z80, 3);
					z80->gpr[RF] = (z80->gpr[RF]&0x01)
						| (z80->gpr[RA]&0xA8)
						| (z80->gpr[RA] == 0 ? 0x40 : 0x00)
						| Z80NAME(parity)(z80->gpr[RA]);
				} break;

				case 0x77: case 0x7F:
					// NOPs
					break;

				//
				// X=2
				//

				// Z=0
				case 0xA0: { // LDI
					uint16_t dr = Z80NAME(pair_pbe)(&z80->gpr[RD]);
					uint16_t sr = Z80NAME(pair_pbe)(&z80->gpr[RH]);
					uint8_t dat = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, sr);
					Z80_ADD_CYCLES(z80, 3);
					Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, dr, dat);
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
					uint16_t dr = Z80NAME(pair_pbe)(&z80->gpr[RD]);
					uint16_t sr = Z80NAME(pair_pbe)(&z80->gpr[RH]);
					uint8_t dat = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, sr);
					Z80_ADD_CYCLES(z80, 3);
					Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, dr, dat);
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
					z80->wz[0] = (uint8_t)(z80->pc>>8);
					Z80_ADD_CYCLES(z80, 5);
				} break;

				case 0xA8: { // LDD
					uint16_t dr = Z80NAME(pair_pbe)(&z80->gpr[RD]);
					uint16_t sr = Z80NAME(pair_pbe)(&z80->gpr[RH]);
					uint8_t dat = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, sr);
					Z80_ADD_CYCLES(z80, 3);
					Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, dr, dat);
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
					uint16_t dr = Z80NAME(pair_pbe)(&z80->gpr[RD]);
					uint16_t sr = Z80NAME(pair_pbe)(&z80->gpr[RH]);
					uint8_t dat = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, sr);
					Z80_ADD_CYCLES(z80, 3);
					Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, dr, dat);
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
					z80->wz[0] = (uint8_t)(z80->pc>>8);
					Z80_ADD_CYCLES(z80, 5);
				} break;

				//

				case 0xA1: { // CPI
					uint16_t sr = Z80NAME(pair_pbe)(&z80->gpr[RH]);
					uint8_t dat = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, sr);
					Z80_ADD_CYCLES(z80, 3);
					uint8_t c = z80->gpr[RF]&0x01;
					Z80NAME(sub8)(z80, z80->gpr[RA], dat);
					Z80_ADD_CYCLES(z80, 3+2);
					z80->gpr[RF] = (z80->gpr[RF]&0xD0)
						| 0x02
						| c;

					uint8_t f31 = z80->gpr[RA]-dat-((z80->gpr[RF]&0x10)>>4);
					z80->gpr[RF] |= (f31&0x08)|((f31<<4)&0x20);
					if((++z80->gpr[RL]) == 0) { z80->gpr[RH]++; }
					if((z80->gpr[RC]--) == 0) {
						z80->gpr[RB]--;
					} else if(z80->gpr[RC] == 0 && z80->gpr[RB] == 0) {
						break;
					}
					z80->gpr[RF] |= 0x04;
				} break;
				case 0xB1: { // CPIR
					uint16_t sr = Z80NAME(pair_pbe)(&z80->gpr[RH]);
					uint8_t dat = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, sr);
					Z80_ADD_CYCLES(z80, 3);
					uint8_t c = z80->gpr[RF]&0x01;
					Z80NAME(sub8)(z80, z80->gpr[RA], dat);
					Z80_ADD_CYCLES(z80, 3+2);
					z80->gpr[RF] = (z80->gpr[RF]&0xD0)
						| 0x02
						| c;

					uint8_t f31 = z80->gpr[RA]-dat-((z80->gpr[RF]&0x10)>>4);
					z80->gpr[RF] |= (f31&0x08)|((f31<<4)&0x20);
					if((++z80->gpr[RL]) == 0) { z80->gpr[RH]++; }
					if((z80->gpr[RC]--) == 0) {
						z80->gpr[RB]--;
					} else if(z80->gpr[RC] == 0 && z80->gpr[RB] == 0) {
						break;
					}
					z80->gpr[RF] |= 0x04;
					if(z80->gpr[RA] != dat) {
						z80->pc -= 2;
						z80->wz[0] = (uint8_t)(z80->pc>>8);
						Z80_ADD_CYCLES(z80, 5);
					}
				} break;

				case 0xA9: { // CPD
					uint16_t sr = Z80NAME(pair_pbe)(&z80->gpr[RH]);
					uint8_t dat = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, sr);
					Z80_ADD_CYCLES(z80, 3);
					uint8_t c = z80->gpr[RF]&0x01;
					Z80NAME(sub8)(z80, z80->gpr[RA], dat);
					Z80_ADD_CYCLES(z80, 3+2);
					z80->gpr[RF] = (z80->gpr[RF]&0xD0)
						| 0x02
						| c;

					uint8_t f31 = z80->gpr[RA]-dat-((z80->gpr[RF]&0x10)>>4);
					z80->gpr[RF] |= (f31&0x08)|((f31<<4)&0x20);
					if((z80->gpr[RL]--) == 0) { z80->gpr[RH]--; }
					if((z80->gpr[RC]--) == 0) {
						z80->gpr[RB]--;
					} else if(z80->gpr[RC] == 0 && z80->gpr[RB] == 0) {
						break;
					}
					z80->gpr[RF] |= 0x04;
				} break;
				case 0xB9: { // CPDR
					uint16_t sr = Z80NAME(pair_pbe)(&z80->gpr[RH]);
					uint8_t dat = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, sr);
					Z80_ADD_CYCLES(z80, 3);
					uint8_t c = z80->gpr[RF]&0x01;
					Z80NAME(sub8)(z80, z80->gpr[RA], dat);
					Z80_ADD_CYCLES(z80, 3+2);
					z80->gpr[RF] = (z80->gpr[RF]&0xD0)
						| 0x02
						| c;

					uint8_t f31 = z80->gpr[RA]-dat-((z80->gpr[RF]&0x10)>>4);
					z80->gpr[RF] |= (f31&0x08)|((f31<<4)&0x20);
					if((z80->gpr[RL]--) == 0) { z80->gpr[RH]--; }
					if((z80->gpr[RC]--) == 0) {
						z80->gpr[RB]--;
					} else if(z80->gpr[RC] == 0 && z80->gpr[RB] == 0) {
						break;
					}
					z80->gpr[RF] |= 0x04;
					if(z80->gpr[RA] != dat) {
						z80->pc -= 2;
						z80->wz[0] = (uint8_t)(z80->pc>>8);
						Z80_ADD_CYCLES(z80, 5);
					}
				} break;

				//
				// HERE BE DRAGONS
				// NO REALLY, THE FLAG AFFECTION MAKES LESS THAN NO SENSE
				//

				case 0xA2: { // INI
					uint16_t sr = Z80NAME(pair_pbe)(&z80->gpr[RB]);
					uint16_t dr = Z80NAME(pair_pbe)(&z80->gpr[RH]);
					uint8_t dat = Z80NAME(io_read)(Z80_STATE_ARGS, z80->H.timestamp, sr);
					Z80_ADD_CYCLES(z80, 4);
					Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, dr, dat);
					Z80_ADD_CYCLES(z80, 4);
					z80->gpr[RB]--;

					uint8_t magic1 = (z80->gpr[RC]+1)&0xFF;
					uint8_t magic2 = (magic1+dat);
					z80->gpr[RF] = (z80->gpr[RB]&0xA8)
						| (magic2 < dat ? 0x11 : 0x00)
						| (z80->gpr[RB] == 0 ? 0x40 : 0x00)
						| Z80NAME(parity)(z80->gpr[RB]^(magic2&7))
						| ((dat>>6)&0x02);

					if((++z80->gpr[RL]) == 0) { z80->gpr[RH]++; }
				} break;
				case 0xB2: { // INIR
					uint16_t sr = Z80NAME(pair_pbe)(&z80->gpr[RB]);
					uint16_t dr = Z80NAME(pair_pbe)(&z80->gpr[RH]);
					uint8_t dat = Z80NAME(io_read)(Z80_STATE_ARGS, z80->H.timestamp, sr);
					Z80_ADD_CYCLES(z80, 4);
					Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, dr, dat);
					Z80_ADD_CYCLES(z80, 4);
					z80->gpr[RB]--;

					uint8_t magic1 = (z80->gpr[RC]+1)&0xFF;
					uint8_t magic2 = (magic1+dat);
					z80->gpr[RF] = (z80->gpr[RB]&0xA8)
						| (magic2 < dat ? 0x11 : 0x00)
						| (z80->gpr[RB] == 0 ? 0x40 : 0x00)
						| Z80NAME(parity)(z80->gpr[RB]^(magic2&7))
						| ((dat>>6)&0x02);

					if((++z80->gpr[RL]) == 0) { z80->gpr[RH]++; }
					if(z80->gpr[RB] == 0x00) {
						break;
					}
					z80->pc -= 2;
					z80->wz[0] = (uint8_t)(z80->pc>>8);
					Z80_ADD_CYCLES(z80, 5);
				} break;

				case 0xAA: { // IND
					uint16_t sr = Z80NAME(pair_pbe)(&z80->gpr[RB]);
					uint16_t dr = Z80NAME(pair_pbe)(&z80->gpr[RH]);
					uint8_t dat = Z80NAME(io_read)(Z80_STATE_ARGS, z80->H.timestamp, sr);
					Z80_ADD_CYCLES(z80, 4);
					Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, dr, dat);
					Z80_ADD_CYCLES(z80, 4);
					z80->gpr[RB]--;

					uint8_t magic1 = (z80->gpr[RC]-1)&0xFF;
					uint8_t magic2 = (magic1+dat);
					z80->gpr[RF] = (z80->gpr[RB]&0xA8)
						| (magic2 < dat ? 0x11 : 0x00)
						| (z80->gpr[RB] == 0 ? 0x40 : 0x00)
						| Z80NAME(parity)(z80->gpr[RB]^(magic2&7))
						| ((dat>>6)&0x02);

					if((z80->gpr[RL]--) == 0) { z80->gpr[RH]--; }
				} break;
				case 0xBA: { // INDR
					uint16_t sr = Z80NAME(pair_pbe)(&z80->gpr[RB]);
					uint16_t dr = Z80NAME(pair_pbe)(&z80->gpr[RH]);
					uint8_t dat = Z80NAME(io_read)(Z80_STATE_ARGS, z80->H.timestamp, sr);
					Z80_ADD_CYCLES(z80, 4);
					Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, dr, dat);
					Z80_ADD_CYCLES(z80, 4);
					z80->gpr[RB]--;

					uint8_t magic1 = (z80->gpr[RC]-1)&0xFF;
					uint8_t magic2 = (magic1+dat);
					z80->gpr[RF] = (z80->gpr[RB]&0xA8)
						| (magic2 < dat ? 0x11 : 0x00)
						| (z80->gpr[RB] == 0 ? 0x40 : 0x00)
						| Z80NAME(parity)(z80->gpr[RB]^(magic2&7))
						| ((dat>>6)&0x02);

					if((z80->gpr[RL]--) == 0) { z80->gpr[RH]--; }
					if(z80->gpr[RB] == 0x00) {
						break;
					}
					z80->pc -= 2;
					z80->wz[0] = (uint8_t)(z80->pc>>8);
					Z80_ADD_CYCLES(z80, 5);
				} break;

				//

				case 0xA3: { // OUTI
					uint16_t sr = Z80NAME(pair_pbe)(&z80->gpr[RH]);
					uint16_t dr = Z80NAME(pair_pbe)(&z80->gpr[RB]);
					uint8_t dat = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, sr);
					//if(z80->gpr[RC] == 0xBF) printf("OUTI %02X %02X %04X\n", z80->gpr[RB], z80->gpr[RC], sr);
					Z80_ADD_CYCLES(z80, 4);
					Z80NAME(io_write)(Z80_STATE_ARGS, z80->H.timestamp, dr, dat);
					Z80_ADD_CYCLES(z80, 4);
					z80->gpr[RB]--;

					uint8_t magic1 = z80->gpr[RL];
					uint8_t magic2 = (magic1+dat);
					z80->gpr[RF] = (z80->gpr[RB]&0xA8)
						| (magic2 < dat ? 0x11 : 0x00)
						| (z80->gpr[RB] == 0 ? 0x40 : 0x00)
						| Z80NAME(parity)(z80->gpr[RB]^(magic2&7))
						| ((dat>>6)&0x02);

					if((++z80->gpr[RL]) == 0) { z80->gpr[RH]++; }
				} break;
				case 0xB3: { // OTIR
					uint16_t sr = Z80NAME(pair_pbe)(&z80->gpr[RH]);
					uint16_t dr = Z80NAME(pair_pbe)(&z80->gpr[RB]);
					uint8_t dat = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, sr);
					//printf("OTIR %02X %02X %04X\n", z80->gpr[RB], z80->gpr[RC], sr);
					Z80_ADD_CYCLES(z80, 4);
					Z80NAME(io_write)(Z80_STATE_ARGS, z80->H.timestamp, dr, dat);
					Z80_ADD_CYCLES(z80, 4);
					z80->gpr[RB]--;

					uint8_t magic1 = z80->gpr[RL];
					uint8_t magic2 = (magic1+dat);
					z80->gpr[RF] = (z80->gpr[RB]&0xA8)
						| (magic2 < dat ? 0x11 : 0x00)
						| (z80->gpr[RB] == 0 ? 0x40 : 0x00)
						| Z80NAME(parity)(z80->gpr[RB]^(magic2&7))
						| ((dat>>6)&0x02);

					if((++z80->gpr[RL]) == 0) { z80->gpr[RH]++; }
					if(z80->gpr[RB] == 0x00) {
						break;
					}
					z80->pc -= 2;
					z80->wz[0] = (uint8_t)(z80->pc>>8);
					Z80_ADD_CYCLES(z80, 5);
				} break;

				case 0xAB: { // OUTD
					uint16_t sr = Z80NAME(pair_pbe)(&z80->gpr[RH]);
					uint16_t dr = Z80NAME(pair_pbe)(&z80->gpr[RB]);
					uint8_t dat = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, sr);
					Z80_ADD_CYCLES(z80, 4);
					Z80NAME(io_write)(Z80_STATE_ARGS, z80->H.timestamp, dr, dat);
					Z80_ADD_CYCLES(z80, 4);
					z80->gpr[RB]--;

					uint8_t magic1 = z80->gpr[RL];
					uint8_t magic2 = (magic1+dat);
					z80->gpr[RF] = (z80->gpr[RB]&0xA8)
						| (magic2 < dat ? 0x11 : 0x00)
						| (z80->gpr[RB] == 0 ? 0x40 : 0x00)
						| Z80NAME(parity)(z80->gpr[RB]^(magic2&7))
						| ((dat>>6)&0x02);

					if((z80->gpr[RL]--) == 0) { z80->gpr[RH]--; }
				} break;
				case 0xBB: { // OTDR
					uint16_t sr = Z80NAME(pair_pbe)(&z80->gpr[RH]);
					uint16_t dr = Z80NAME(pair_pbe)(&z80->gpr[RB]);
					uint8_t dat = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, sr);
					Z80_ADD_CYCLES(z80, 4);
					Z80NAME(io_write)(Z80_STATE_ARGS, z80->H.timestamp, dr, dat);
					Z80_ADD_CYCLES(z80, 4);
					z80->gpr[RB]--;

					uint8_t magic1 = z80->gpr[RL];
					uint8_t magic2 = (magic1+dat);
					z80->gpr[RF] = (z80->gpr[RB]&0xA8)
						| (magic2 < dat ? 0x11 : 0x00)
						| (z80->gpr[RB] == 0 ? 0x40 : 0x00)
						| Z80NAME(parity)(z80->gpr[RB]^(magic2&7))
						| ((dat>>6)&0x02);

					if((z80->gpr[RL]--) == 0) { z80->gpr[RH]--; }
					if(z80->gpr[RB] == 0x00) {
						break;
					}
					z80->pc -= 2;
					z80->wz[0] = (uint8_t)(z80->pc>>8);
					Z80_ADD_CYCLES(z80, 5);
				} break;

				default:
					//fprintf(stderr, "OP: ED %02X\n", op);
					//fflush(stderr); abort();
					// NOP
					break;
			} continue;
		}

		if(op == 0xCB) {
			if(false && ix >= 0) {
				// TODO!
				fprintf(stderr, "OP: %02X CB %02X\n", (ix<<5)|0xDD, op);
				fflush(stderr); abort();
				break;
			}

			uint8_t val;
			uint8_t bval;

			// Read Iz if necessary
			// FIXME: timing sucks here
			if(ix >= 0) {
				uint16_t addr = Z80NAME(pair_pbe)(z80->idx[ix&1]);
				addr += (uint16_t)(int16_t)(int8_t)Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
				z80->wz[0] = (uint8_t)(addr>>8);
				z80->wz[1] = (uint8_t)(addr>>0);
				Z80_ADD_CYCLES(z80, 1);
				val = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, addr);
				Z80_ADD_CYCLES(z80, 4);
			}

			// Fetch
			op = Z80NAME(fetch_op_m1)(z80, Z80_STATE_ARGS);
			//printf("*CB %04X %2d %02X %04X\n", z80->pc, ix, op, z80->sp);
			//int oy = (op>>3)&7;
			int oz = op&7;
			int ox = (op>>6);

			// Read if we haven't read Iz
			if(ix >= 0) {
				// Do nothing to val
				// But set bval to something suitable
				bval = z80->wz[0];

			} else if(oz == 6) {
				bval = z80->wz[0];
				val = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp,
					Z80NAME(pair_pbe)(&z80->gpr[RH]));
				Z80_ADD_CYCLES(z80, 4);

			} else {
				bval = val = z80->gpr[oz];
			}


			// ALU
			switch(op&~7) {
				//
				// X=0
				//
				case 0x00: // RLC
					z80->gpr[RF] = ((val>>7)&0x01);
					val = ((val<<1)|((val>>7)&1));
					z80->gpr[RF] |= (val&0xA8) | Z80NAME(parity)(val);
					z80->gpr[RF] |= (val == 0 ? 0x40 : 0x00);
					break;
				case 0x08: // RRC
					z80->gpr[RF] = (val&0x01);
					val = ((val<<7)|((val>>1)&0x7F));
					z80->gpr[RF] |= (val&0xA8) | Z80NAME(parity)(val);
					z80->gpr[RF] |= (val == 0 ? 0x40 : 0x00);
					break;

				case 0x10: { // RL
					uint8_t c = z80->gpr[RF]&0x01;
					z80->gpr[RF] = ((val>>7)&0x01);
					val = (val<<1)|c;
					z80->gpr[RF] |= (val&0xA8) | Z80NAME(parity)(val);
					z80->gpr[RF] |= (val == 0 ? 0x40 : 0x00);
				} break;
				case 0x18: { // RR
					uint8_t c = z80->gpr[RF]&0x01;
					z80->gpr[RF] = (val&0x01);
					val = ((c<<7)|((val>>1)&0x7F));
					z80->gpr[RF] |= (val&0xA8) | Z80NAME(parity)(val);
					z80->gpr[RF] |= (val == 0 ? 0x40 : 0x00);
				} break;

				case 0x20: // SLL
					z80->gpr[RF] = ((val>>7)&0x01);
					val = (val<<1);
					z80->gpr[RF] |= (val&0xA8) | Z80NAME(parity)(val);
					z80->gpr[RF] |= (val == 0 ? 0x40 : 0x00);
					break;
				case 0x28: // SRA
					z80->gpr[RF] = (val&0x01);
					val = (val>>1)|(val&0x80);
					z80->gpr[RF] |= (val&0xA8) | Z80NAME(parity)(val);
					z80->gpr[RF] |= (val == 0 ? 0x40 : 0x00);
					break;
				case 0x30: // SLA
					z80->gpr[RF] = ((val>>7)&0x01);
					val = (val<<1)|1;
					z80->gpr[RF] |= (val&0xA8) | Z80NAME(parity)(val);
					z80->gpr[RF] |= (val == 0 ? 0x40 : 0x00);
					break;
				case 0x38: // SRL
					z80->gpr[RF] = (val&0x01);
					val = (val>>1)&0x7F;
					z80->gpr[RF] |= (val&0xA8) | Z80NAME(parity)(val);
					z80->gpr[RF] |= (val == 0 ? 0x40 : 0x00);
					break;

				//
				// X=1
				//
				case 0x40: { // BIT 0, r
					uint8_t sf = z80->gpr[RF]&0x01;
					Z80NAME(and8)(z80, val, 0x01);
					z80->gpr[RF] &= ~0x29;
					z80->gpr[RF] |= sf | (bval&0x28);
				} break;
				case 0x48: { // BIT 1, r
					uint8_t sf = z80->gpr[RF]&0x01;
					Z80NAME(and8)(z80, val, 0x02);
					z80->gpr[RF] &= ~0x29;
					z80->gpr[RF] |= sf | (bval&0x28);
				} break;
				case 0x50: { // BIT 2, r
					uint8_t sf = z80->gpr[RF]&0x01;
					Z80NAME(and8)(z80, val, 0x04);
					z80->gpr[RF] &= ~0x29;
					z80->gpr[RF] |= sf | (bval&0x28);
				} break;
				case 0x58: { // BIT 3, r
					uint8_t sf = z80->gpr[RF]&0x01;
					Z80NAME(and8)(z80, val, 0x08);
					z80->gpr[RF] &= ~0x29;
					z80->gpr[RF] |= sf | (bval&0x28);
				} break;
				case 0x60: { // BIT 4, r
					uint8_t sf = z80->gpr[RF]&0x01;
					Z80NAME(and8)(z80, val, 0x10);
					z80->gpr[RF] &= ~0x29;
					z80->gpr[RF] |= sf | (bval&0x28);
				} break;
				case 0x68: { // BIT 5, r
					uint8_t sf = z80->gpr[RF]&0x01;
					Z80NAME(and8)(z80, val, 0x20);
					z80->gpr[RF] &= ~0x29;
					z80->gpr[RF] |= sf | (bval&0x28);
				} break;
				case 0x70: { // BIT 6, r
					uint8_t sf = z80->gpr[RF]&0x01;
					Z80NAME(and8)(z80, val, 0x40);
					z80->gpr[RF] &= ~0x29;
					z80->gpr[RF] |= sf | (bval&0x28);
				} break;
				case 0x78: { // BIT 7, r
					uint8_t sf = z80->gpr[RF]&0x01;
					Z80NAME(and8)(z80, val, 0x80);
					z80->gpr[RF] &= ~0x29;
					z80->gpr[RF] |= sf | (bval&0x28);
				} break;

				//
				// X=2
				//
				case 0x80: val &= ~0x01; break; // RES 0, r
				case 0x88: val &= ~0x02; break; // RES 1, r
				case 0x90: val &= ~0x04; break; // RES 2, r
				case 0x98: val &= ~0x08; break; // RES 3, r
				case 0xA0: val &= ~0x10; break; // RES 4, r
				case 0xA8: val &= ~0x20; break; // RES 5, r
				case 0xB0: val &= ~0x40; break; // RES 6, r
				case 0xB8: val &= ~0x80; break; // RES 7, r

				//
				// X=3
				//
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

			} else if(oz == 6) {
				if(ix < 0) {
					Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp,
						Z80NAME(pair_pbe)(&z80->gpr[RH]),
						val);
					Z80_ADD_CYCLES(z80, 3);
				}

			} else {
				z80->gpr[oz] = val;
			}

			// Write to (Iz+d) if we use DD/FD
			if(ix >= 0) {
				uint16_t addr = Z80NAME(pair_pbe)(&z80->wz[0]);
				Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp,
					addr,
					val);
				Z80_ADD_CYCLES(z80, 3);
			}

			//printf("*CB DONE %04X\n", z80->pc);

			continue;
		}

		//printf("%04X %02X %04X\n", z80->pc-1, op, z80->sp);

		// X=1 decode (LD y, z)
		if((op>>6) == 1) {

			if(op == 0x76) {
				// HALT
				if(TIME_IN_ORDER(z80->H.timestamp, z80->H.timestamp_end)) {
					z80->H.timestamp = z80->H.timestamp_end;
				}
				z80->halted = true;
				z80->noni = 0;
				if(z80->iff1 != 0 && Z80_INT_CHECK) {
					Z80NAME(irq)(z80, Z80_STATE_ARGS, 0xFF);
					continue;
				} else {
					while(TIME_IN_ORDER(z80->H.timestamp, timestamp)) {
						Z80_ADD_CYCLES(z80, 4);
					}
					return;
				}
			}

			int oy = (op>>3)&7;
			int oz = op&7;

			uint8_t val;

			// Read
			if((oz&~1)==4 && ix >= 0 && oy != 6) {
				val = z80->idx[ix&1][oz&1];

			} else if(oz == 6) {
				if(ix >= 0) {
					uint16_t addr = Z80NAME(fetch_ix_d)(z80, Z80_STATE_ARGS, ix);
					val = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, addr);
					Z80_ADD_CYCLES(z80, 3);
				} else {
					val = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp,
						Z80NAME(pair_pbe)(&z80->gpr[RH]));
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
					uint16_t addr = Z80NAME(fetch_ix_d)(z80, Z80_STATE_ARGS, ix);
					Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp,
						addr,
						val);
					Z80_ADD_CYCLES(z80, 3);
				} else {
					Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp,
						Z80NAME(pair_pbe)(&z80->gpr[RH]),
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
					uint16_t addr = Z80NAME(fetch_ix_d)(z80, Z80_STATE_ARGS, ix);
					val = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, addr);
					Z80_ADD_CYCLES(z80, 3);
				} else {
					val = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp,
						Z80NAME(pair_pbe)(&z80->gpr[RH]));
					Z80_ADD_CYCLES(z80, 3);
				}

			} else {
				val = z80->gpr[oz];
			}

			// ALU
			switch(oy) {
				case 0: { // ADD A, r
					z80->gpr[RA] = Z80NAME(add8)(z80, z80->gpr[RA], val);
				} break;
				case 1: { // ADC A, r
					z80->gpr[RA] = Z80NAME(adc8)(z80, z80->gpr[RA], val);
				} break;
				case 2: { // SUB r
					z80->gpr[RA] = Z80NAME(sub8)(z80, z80->gpr[RA], val);
				} break;
				case 3: { // SBC A, r
					z80->gpr[RA] = Z80NAME(sbc8)(z80, z80->gpr[RA], val);
				} break;
				case 4: { // AND r
					z80->gpr[RA] = Z80NAME(and8)(z80, z80->gpr[RA], val);
				} break;
				case 5: { // XOR r
					z80->gpr[RA] = Z80NAME(xor8)(z80, z80->gpr[RA], val);
				} break;
				case 6: { // OR r
					z80->gpr[RA] = Z80NAME(or8)(z80, z80->gpr[RA], val);
				} break;
				case 7: { // CP r
					Z80NAME(sub8)(z80, z80->gpr[RA], val);
					z80->gpr[RF] = (z80->gpr[RF]&~0x28) | (val&0x28);
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
			case 0x08: { // EX AF, AF'
				uint8_t t;
				t = z80->gpr[RA]; z80->gpr[RA] = z80->shadow[RA]; z80->shadow[RA] = t;
				t = z80->gpr[RF]; z80->gpr[RF] = z80->shadow[RF]; z80->shadow[RF] = t;
			} break;
			case 0x10: // DJNZ d
				z80->gpr[RB]--;
				Z80_ADD_CYCLES(z80, 1);
				Z80NAME(op_jr_cond)(z80, Z80_STATE_ARGS, z80->gpr[RB] != 0);
				break;
			case 0x18: // JR d
				Z80NAME(op_jr_cond)(z80, Z80_STATE_ARGS, true);
				break;
			case 0x20: // JR NZ, d
				Z80NAME(op_jr_cond)(z80, Z80_STATE_ARGS, (z80->gpr[RF]&0x40) == 0);
				break;
			case 0x28: // JR Z, d
				Z80NAME(op_jr_cond)(z80, Z80_STATE_ARGS, (z80->gpr[RF]&0x40) != 0);
				break;
			case 0x30: // JR NC, d
				Z80NAME(op_jr_cond)(z80, Z80_STATE_ARGS, (z80->gpr[RF]&0x01) == 0);
				break;
			case 0x38: // JR C, d
				Z80NAME(op_jr_cond)(z80, Z80_STATE_ARGS, (z80->gpr[RF]&0x01) != 0);
				break;

			// Z=1
			case 0x01: // LD BC, nn
				z80->gpr[RC] = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
				z80->gpr[RB] = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
				break;
			case 0x11: // LD DE, nn
				z80->gpr[RE] = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
				z80->gpr[RD] = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
				break;
			case 0x21: if(ix >= 0) {
					// LD Iz, nn
					z80->idx[ix&1][1] = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
					z80->idx[ix&1][0] = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
				} else {
					// LD HL, nn
					z80->gpr[RL] = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
					z80->gpr[RH] = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
				} break;
			case 0x31: { // LD SP, nn
				uint16_t spl = (uint16_t)Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
				uint16_t sph = (uint16_t)Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
				z80->sp = Z80NAME(pair)(sph, spl);
			} break;

			case 0x09: if(ix >= 0) {
					// ADD Iz, BC
					uint16_t hl = Z80NAME(pair_pbe)(z80->idx[ix&1]);
					uint16_t q = Z80NAME(pair_pbe)(&z80->gpr[RB]);
					hl = Z80NAME(add16)(z80, hl, q);
					z80->idx[ix&1][0] = (uint8_t)(hl>>8);
					z80->idx[ix&1][1] = (uint8_t)(hl>>0);
					Z80_ADD_CYCLES(z80, 7);
				} else {
					// ADD HL, BC
					uint16_t hl = Z80NAME(pair_pbe)(&z80->gpr[RH]);
					uint16_t q = Z80NAME(pair_pbe)(&z80->gpr[RB]);
					hl = Z80NAME(add16)(z80, hl, q);
					z80->gpr[RH] = (uint8_t)(hl>>8);
					z80->gpr[RL] = (uint8_t)(hl>>0);
					Z80_ADD_CYCLES(z80, 7);
				} break;
			case 0x19: if(ix >= 0) {
					// ADD Iz, DE
					uint16_t hl = Z80NAME(pair_pbe)(z80->idx[ix&1]);
					uint16_t q = Z80NAME(pair_pbe)(&z80->gpr[RD]);
					hl = Z80NAME(add16)(z80, hl, q);
					z80->idx[ix&1][0] = (uint8_t)(hl>>8);
					z80->idx[ix&1][1] = (uint8_t)(hl>>0);
					Z80_ADD_CYCLES(z80, 7);
				} else {
					// ADD HL, DE
					uint16_t hl = Z80NAME(pair_pbe)(&z80->gpr[RH]);
					uint16_t q = Z80NAME(pair_pbe)(&z80->gpr[RD]);
					hl = Z80NAME(add16)(z80, hl, q);
					z80->gpr[RH] = (uint8_t)(hl>>8);
					z80->gpr[RL] = (uint8_t)(hl>>0);
					Z80_ADD_CYCLES(z80, 7);
				} break;
			case 0x29: if(ix >= 0) {
					// ADD Iz, Iz
					uint16_t hl = Z80NAME(pair_pbe)(z80->idx[ix&1]);
					uint16_t q = hl;
					hl = Z80NAME(add16)(z80, hl, q);
					z80->idx[ix&1][0] = (uint8_t)(hl>>8);
					z80->idx[ix&1][1] = (uint8_t)(hl>>0);
					Z80_ADD_CYCLES(z80, 7);
				} else {
					// ADD HL, HL
					uint16_t hl = Z80NAME(pair_pbe)(&z80->gpr[RH]);
					uint16_t q = hl;
					hl = Z80NAME(add16)(z80, hl, q);
					z80->gpr[RH] = (uint8_t)(hl>>8);
					z80->gpr[RL] = (uint8_t)(hl>>0);
					Z80_ADD_CYCLES(z80, 7);
				} break;
			case 0x39: if(ix >= 0) {
					// ADD Iz, SP
					uint16_t hl = Z80NAME(pair_pbe)(z80->idx[ix&1]);
					uint16_t q = z80->sp;
					hl = Z80NAME(add16)(z80, hl, q);
					z80->idx[ix&1][0] = (uint8_t)(hl>>8);
					z80->idx[ix&1][1] = (uint8_t)(hl>>0);
					Z80_ADD_CYCLES(z80, 7);
				} else {
					// ADD HL, SP
					uint16_t hl = Z80NAME(pair_pbe)(&z80->gpr[RH]);
					uint16_t q = z80->sp;
					hl = Z80NAME(add16)(z80, hl, q);
					z80->gpr[RH] = (uint8_t)(hl>>8);
					z80->gpr[RL] = (uint8_t)(hl>>0);
					Z80_ADD_CYCLES(z80, 7);
				} break;

			// Z=2
			case 0x02: // LD (BC), A
				Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp,
					Z80NAME(pair_pbe)(&z80->gpr[RB]),
					z80->gpr[RA]);
				Z80_ADD_CYCLES(z80, 3);
				break;
			case 0x12: // LD (DE), A
				Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp,
					Z80NAME(pair_pbe)(&z80->gpr[RD]),
					z80->gpr[RA]);
				Z80_ADD_CYCLES(z80, 3);
				break;
			case 0x0A: // LD A, (BC)
				z80->gpr[RA] = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp,
					Z80NAME(pair_pbe)(&z80->gpr[RB]));
				Z80_ADD_CYCLES(z80, 3);
				break;
			case 0x1A: // LD A, (DE)
				z80->gpr[RA] = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp,
					Z80NAME(pair_pbe)(&z80->gpr[RD]));
				Z80_ADD_CYCLES(z80, 3);
				break;

			case 0x22:
				if(ix >= 0) {
					// LD Iz, (nn)
					uint8_t nl = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
					uint8_t nh = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
					uint16_t addr = Z80NAME(pair)(nh, nl);
					Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, addr, z80->idx[ix&1][1]);
					Z80_ADD_CYCLES(z80, 3);
					addr++;
					Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, addr, z80->idx[ix&1][0]);
					Z80_ADD_CYCLES(z80, 3);
				} else {
					// LD (nn), HL
					uint8_t nl = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
					uint8_t nh = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
					uint16_t addr = Z80NAME(pair)(nh, nl);
					Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, addr, z80->gpr[RL]);
					Z80_ADD_CYCLES(z80, 3);
					addr++;
					Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, addr, z80->gpr[RH]);
					Z80_ADD_CYCLES(z80, 3);
				} break;
			case 0x32: { // LD (nn), A
				uint8_t nl = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
				uint8_t nh = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
				uint16_t addr = Z80NAME(pair)(nh, nl);
				Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, addr, z80->gpr[RA]);
				Z80_ADD_CYCLES(z80, 3);
			} break;
			case 0x2A:
				if(ix >= 0) {
					// LD Iz, (nn)
					uint8_t nl = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
					uint8_t nh = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
					uint16_t addr = Z80NAME(pair)(nh, nl);
					z80->idx[ix&1][1] = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, addr);
					Z80_ADD_CYCLES(z80, 3);
					addr++;
					z80->idx[ix&1][0] = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, addr);
					Z80_ADD_CYCLES(z80, 3);
				} else {
					// LD HL, (nn)
					uint8_t nl = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
					uint8_t nh = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
					uint16_t addr = Z80NAME(pair)(nh, nl);
					z80->gpr[RL] = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, addr);
					Z80_ADD_CYCLES(z80, 3);
					addr++;
					z80->gpr[RH] = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, addr);
					Z80_ADD_CYCLES(z80, 3);
				} break;
			case 0x3A: { // LD A, (nn)
				uint8_t nl = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
				uint8_t nh = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
				uint16_t addr = Z80NAME(pair)(nh, nl);
				z80->gpr[RA] = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, addr);
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
				z80->gpr[RB] = Z80NAME(inc8)(z80, z80->gpr[RB]);
				break;
			case 0x0C: // INC C
				z80->gpr[RC] = Z80NAME(inc8)(z80, z80->gpr[RC]);
				break;
			case 0x14: // INC D
				z80->gpr[RD] = Z80NAME(inc8)(z80, z80->gpr[RD]);
				break;
			case 0x1C: // INC E
				z80->gpr[RE] = Z80NAME(inc8)(z80, z80->gpr[RE]);
				break;
			case 0x24: if(ix >= 0) {
					// INC IzH
					z80->idx[ix&1][0] = Z80NAME(inc8)(z80, z80->idx[ix&1][0]);
				} else {
					// INC H
					z80->gpr[RH] = Z80NAME(inc8)(z80, z80->gpr[RH]);
				} break;
			case 0x2C: if(ix >= 0) {
					// INC IzL
					z80->idx[ix&1][1] = Z80NAME(inc8)(z80, z80->idx[ix&1][1]);
				} else {
					// INC L
					z80->gpr[RL] = Z80NAME(inc8)(z80, z80->gpr[RL]);
				} break;
			case 0x34: if(ix >= 0) {
					// INC (Iz+d)
					uint16_t addr = Z80NAME(fetch_ix_d)(z80, Z80_STATE_ARGS, ix);
					uint8_t val = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, addr);
					Z80_ADD_CYCLES(z80, 4);
					val = Z80NAME(inc8)(z80, val);
					Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, addr, val);
					Z80_ADD_CYCLES(z80, 3);
				} else {
					// INC (HL)
					uint16_t addr = Z80NAME(pair_pbe)(&z80->gpr[RH]);
					uint8_t val = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, addr);
					Z80_ADD_CYCLES(z80, 4);
					val = Z80NAME(inc8)(z80, val);
					Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, addr, val);
					Z80_ADD_CYCLES(z80, 3);
				} break;
			case 0x3C: // INC A
				z80->gpr[RA] = Z80NAME(inc8)(z80, z80->gpr[RA]);
				break;

			// Z=5
			case 0x05: // DEC B
				z80->gpr[RB] = Z80NAME(dec8)(z80, z80->gpr[RB]);
				break;
			case 0x0D: // DEC C
				z80->gpr[RC] = Z80NAME(dec8)(z80, z80->gpr[RC]);
				break;
			case 0x15: // DEC D
				z80->gpr[RD] = Z80NAME(dec8)(z80, z80->gpr[RD]);
				break;
			case 0x1D: // DEC E
				z80->gpr[RE] = Z80NAME(dec8)(z80, z80->gpr[RE]);
				break;
			case 0x25: if(ix >= 0) {
					// DEC IzH
					z80->idx[ix&1][0] = Z80NAME(dec8)(z80, z80->idx[ix&1][0]);
				} else {
					// DEC H
					z80->gpr[RH] = Z80NAME(dec8)(z80, z80->gpr[RH]);
				} break;
			case 0x2D: if(ix >= 0) {
					// DEC IzL
					z80->idx[ix&1][1] = Z80NAME(dec8)(z80, z80->idx[ix&1][1]);
				} else {
					// DEC L
					z80->gpr[RL] = Z80NAME(dec8)(z80, z80->gpr[RL]);
				} break;
			case 0x35: if(ix >= 0) {
					// DEC (Iz+d)
					uint16_t addr = Z80NAME(fetch_ix_d)(z80, Z80_STATE_ARGS, ix);
					uint8_t val = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, addr);
					Z80_ADD_CYCLES(z80, 4);
					val = Z80NAME(dec8)(z80, val);
					Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, addr, val);
					Z80_ADD_CYCLES(z80, 3);
				} else {
					// DEC (HL)
					uint16_t addr = Z80NAME(pair_pbe)(&z80->gpr[RH]);
					uint8_t val = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, addr);
					Z80_ADD_CYCLES(z80, 4);
					val = Z80NAME(dec8)(z80, val);
					Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, addr, val);
					Z80_ADD_CYCLES(z80, 3);
				} break;
			case 0x3D: // DEC A
				z80->gpr[RA] = Z80NAME(dec8)(z80, z80->gpr[RA]);
				break;

			// Z=6
			case 0x06: // LD B, n
				z80->gpr[RB] = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
				break;
			case 0x0E: // LD C, n
				z80->gpr[RC] = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
				break;
			case 0x16: // LD D, n
				z80->gpr[RD] = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
				break;
			case 0x1E: // LD E, n
				z80->gpr[RE] = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
				break;
			case 0x26: if(ix >= 0) {
					// LD IxH, n
					z80->idx[ix&1][0] = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
				} else {
					// LD H, n
					z80->gpr[RH] = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
				} break;
			case 0x2E: if(ix >= 0) {
					// LD IxL, n
					z80->idx[ix&1][1] = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
				} else {
					// LD L, n
					z80->gpr[RL] = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
				} break;
			case 0x36: if(ix >= 0) {
					// LD (Iz+d), n
					uint16_t addr = Z80NAME(fetch_ix_d)(z80, Z80_STATE_ARGS, ix);
					Z80_ADD_CYCLES(z80, -5);
					uint8_t dat = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
					Z80_ADD_CYCLES(z80, 2);
					Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, addr, dat);
					Z80_ADD_CYCLES(z80, 3);
			
				} else {
					// LD (HL), n
					Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp,
						Z80NAME(pair_pbe)(&z80->gpr[RH]),
						Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS));
					Z80_ADD_CYCLES(z80, 3);
				} break;
			case 0x3E: // LD A, n
				z80->gpr[RA] = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
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

			case 0x27: { // DAA
				// HALP
				uint8_t a = z80->gpr[RA];
				uint8_t ah = a>>4;
				uint8_t al = a&15;
				uint8_t c = z80->gpr[RF]&0x01;
				uint8_t n = z80->gpr[RF]&0x02;
				uint8_t h = z80->gpr[RF]&0x10;
				uint8_t nf = c | n;

				// Get diff + C'
				uint8_t diff = 0;
				if(c != 0) {
					diff |= 0x60;
					if(h != 0 || al >= 0xA) {
						diff |= 0x06;
					}
				} else if(al >= 0xA) {
					diff |= 0x06;
					if(ah >= 0x9) {
						diff |= 0x60;
						nf |= 0x01;
					}
				} else {
					if(h != 0) {
						diff |= 0x06;
					}
					if(ah >= 0xA) {
						diff |= 0x60;
						nf |= 0x01;
					}
				}

				// Get H' + Calc result
				if(n == 0) {
					if(al >= 0xA) {
						nf |= 0x10;
					}
					a += diff;

				} else {
					if(h != 0 && al <= 0x5) {
						nf |= 0x10;
					}
					a -= diff;
				}

				// Calc flags
				nf |= (a&0xA8);
				nf |= Z80NAME(parity)(a);
				nf |= (a == 0 ? 0x40 : 0x00);
				z80->gpr[RF] = nf;
				z80->gpr[RA] = a;
			} break;

			case 0x2F: // CPL
				z80->gpr[RA] ^= 0xFF;
				z80->gpr[RF] = (z80->gpr[RF]&0xC5)
					| (z80->gpr[RA]&0x28)
					| 0x12;
				break;

			case 0x37: // SCF
				z80->gpr[RF] = (z80->gpr[RF]&0xC4)
					| (z80->gpr[RA]&0x28)
					| 0x01;
				break;
			case 0x3F: // CCF
				z80->gpr[RF] = ((z80->gpr[RF]&0xC5)
					| ((z80->gpr[RF]&0x01)<<4)
					| (z80->gpr[RA]&0x28)) ^ 0x01;
				break;

			//
			// X=3
			//

			// Z=0
			case 0xC0: // RET NZ
				Z80NAME(op_ret_cond)(z80, Z80_STATE_ARGS, (z80->gpr[RF]&0x40) == 0);
				break;
			case 0xC8: // RET Z
				Z80NAME(op_ret_cond)(z80, Z80_STATE_ARGS, (z80->gpr[RF]&0x40) != 0);
				break;
			case 0xD0: // RET NC
				Z80NAME(op_ret_cond)(z80, Z80_STATE_ARGS, (z80->gpr[RF]&0x01) == 0);
				break;
			case 0xD8: // RET C
				Z80NAME(op_ret_cond)(z80, Z80_STATE_ARGS, (z80->gpr[RF]&0x01) != 0);
				break;
			case 0xE0: // RET PO
				Z80NAME(op_ret_cond)(z80, Z80_STATE_ARGS, (z80->gpr[RF]&0x04) == 0);
				break;
			case 0xE8: // RET PE
				Z80NAME(op_ret_cond)(z80, Z80_STATE_ARGS, (z80->gpr[RF]&0x04) != 0);
				break;
			case 0xF0: // RET P
				Z80NAME(op_ret_cond)(z80, Z80_STATE_ARGS, (z80->gpr[RF]&0x80) == 0);
				break;
			case 0xF8: // RET M
				Z80NAME(op_ret_cond)(z80, Z80_STATE_ARGS, (z80->gpr[RF]&0x80) != 0);
				break;

			// Z=1
			case 0xC1: // POP BC
				z80->gpr[RC] = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, z80->sp++);
				Z80_ADD_CYCLES(z80, 3);
				z80->gpr[RB] = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, z80->sp++);
				Z80_ADD_CYCLES(z80, 3);
				break;
			case 0xD1: // POP DE
				z80->gpr[RE] = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, z80->sp++);
				Z80_ADD_CYCLES(z80, 3);
				z80->gpr[RD] = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, z80->sp++);
				Z80_ADD_CYCLES(z80, 3);
				break;
			case 0xE1: if(ix >= 0) {
					// POP Iz
					z80->idx[ix&1][1] = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, z80->sp++);
					Z80_ADD_CYCLES(z80, 3);
					z80->idx[ix&1][0] = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, z80->sp++);
					Z80_ADD_CYCLES(z80, 3);
				} else {
					// POP HL
					z80->gpr[RL] = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, z80->sp++);
					Z80_ADD_CYCLES(z80, 3);
					z80->gpr[RH] = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, z80->sp++);
					Z80_ADD_CYCLES(z80, 3);
				} break;
			case 0xF1: // POP AF
				z80->gpr[RF] = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, z80->sp++);
				Z80_ADD_CYCLES(z80, 3);
				z80->gpr[RA] = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, z80->sp++);
				Z80_ADD_CYCLES(z80, 3);
				break;

			case 0xC9: // RET
				Z80NAME(op_ret)(z80, Z80_STATE_ARGS);
				break;
			case 0xD9: { // EXX
				uint8_t t;
				t = z80->gpr[RB]; z80->gpr[RB] = z80->shadow[RB]; z80->shadow[RB] = t;
				t = z80->gpr[RC]; z80->gpr[RC] = z80->shadow[RC]; z80->shadow[RC] = t;
				t = z80->gpr[RD]; z80->gpr[RD] = z80->shadow[RD]; z80->shadow[RD] = t;
				t = z80->gpr[RE]; z80->gpr[RE] = z80->shadow[RE]; z80->shadow[RE] = t;
				t = z80->gpr[RH]; z80->gpr[RH] = z80->shadow[RH]; z80->shadow[RH] = t;
				t = z80->gpr[RL]; z80->gpr[RL] = z80->shadow[RL]; z80->shadow[RL] = t;

				// also cover WZ
				t = z80->wz[0];
				z80->wz[0] = z80->wz[2];
				z80->wz[2] = t;
				t = z80->wz[1];
				z80->wz[1] = z80->wz[3];
				z80->wz[3] = t;
			} break;

			case 0xE9: if(ix >= 0) {
					// JP (Iz)
					z80->pc = Z80NAME(pair_pbe)(z80->idx[ix&1]);
				} else {
					// JP (HL)
					z80->pc = Z80NAME(pair_pbe)(&z80->gpr[RH]);
				} break;
			case 0xF9: if(ix >= 0) {
					// LD SP, Iz
					z80->sp = Z80NAME(pair_pbe)(z80->idx[ix&1]);
					Z80_ADD_CYCLES(z80, 2);
				} else {
					// LD SP, HL
					z80->sp = Z80NAME(pair_pbe)(&z80->gpr[RH]);
					Z80_ADD_CYCLES(z80, 2);
				} break;

			// Z=2
			case 0xC2: // JP NZ, d
				Z80NAME(op_jp_cond)(z80, Z80_STATE_ARGS, (z80->gpr[RF]&0x40) == 0);
				break;
			case 0xCA: // JP Z, d
				Z80NAME(op_jp_cond)(z80, Z80_STATE_ARGS, (z80->gpr[RF]&0x40) != 0);
				break;
			case 0xD2: // JP NC, d
				Z80NAME(op_jp_cond)(z80, Z80_STATE_ARGS, (z80->gpr[RF]&0x01) == 0);
				break;
			case 0xDA: // JP C, d
				Z80NAME(op_jp_cond)(z80, Z80_STATE_ARGS, (z80->gpr[RF]&0x01) != 0);
				break;
			case 0xE2: // JP PO, d
				Z80NAME(op_jp_cond)(z80, Z80_STATE_ARGS, (z80->gpr[RF]&0x04) == 0);
				break;
			case 0xEA: // JP PE, d
				Z80NAME(op_jp_cond)(z80, Z80_STATE_ARGS, (z80->gpr[RF]&0x04) != 0);
				break;
			case 0xF2: // JP P, d
				Z80NAME(op_jp_cond)(z80, Z80_STATE_ARGS, (z80->gpr[RF]&0x80) == 0);
				break;
			case 0xFA: // JP M, d
				Z80NAME(op_jp_cond)(z80, Z80_STATE_ARGS, (z80->gpr[RF]&0x80) != 0);
				break;

			// Z=3
			case 0xC3: // JP nn
				Z80NAME(op_jp_cond)(z80, Z80_STATE_ARGS, true);
				break;

			case 0xD3: { // OUT (n), A
				uint16_t port = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
				port &= 0x00FF;
				port |= (port << 8);
				//printf("IO WRITE %04X %02X [%04X]\n", port, z80->gpr[RA], z80->pc-2);
				Z80NAME(io_write)(Z80_STATE_ARGS, z80->H.timestamp, port, z80->gpr[RA]);
				Z80_ADD_CYCLES(z80, 4);
			} break;
			case 0xDB: { // IN A, (n)
				uint16_t port = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
				port &= 0x00FF;
				port |= (port << 8);
				z80->gpr[RA] = Z80NAME(io_read)(Z80_STATE_ARGS, z80->H.timestamp, port);
				//printf("IN %04X %02X [%04X]\n", port, z80->gpr[RA], z80->pc-2);
				z80->gpr[RF] = (z80->gpr[RF]&0x01)
					| (z80->gpr[RA]&0xA8)
					| (z80->gpr[RA] == 0 ? 0x40 : 0x00)
					| Z80NAME(parity)(z80->gpr[RA]);
				Z80_ADD_CYCLES(z80, 4);
			} break;

			case 0xE3: if(ix >= 0) {
					// EX (SP), Iz
					// XXX: actual timing pattern is unknown
					uint8_t tl = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, z80->sp+0);
					Z80_ADD_CYCLES(z80, 4);
					uint8_t th = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, z80->sp+1);
					Z80_ADD_CYCLES(z80, 4);
					Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, z80->sp+0, z80->idx[ix&1][1]);
					Z80_ADD_CYCLES(z80, 4);
					Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, z80->sp+1, z80->idx[ix&1][0]);
					Z80_ADD_CYCLES(z80, 3);
					z80->idx[ix&1][1] = tl;
					z80->idx[ix&1][0] = th;

				} else {
					// EX (SP), HL
					// XXX: actual timing pattern is unknown
					uint8_t tl = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, z80->sp+0);
					Z80_ADD_CYCLES(z80, 4);
					uint8_t th = Z80NAME(mem_read)(Z80_STATE_ARGS, z80->H.timestamp, z80->sp+1);
					Z80_ADD_CYCLES(z80, 4);
					Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, z80->sp+0, z80->gpr[RL]);
					Z80_ADD_CYCLES(z80, 4);
					Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, z80->sp+1, z80->gpr[RH]);
					Z80_ADD_CYCLES(z80, 3);
					z80->gpr[RL] = tl;
					z80->gpr[RH] = th;
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
				z80->noni = 1;
				break;

			// Z=4
			case 0xC4: // CALL NZ, d
				Z80NAME(op_call_cond)(z80, Z80_STATE_ARGS, (z80->gpr[RF]&0x40) == 0);
				break;
			case 0xCC: // CALL Z, d
				Z80NAME(op_call_cond)(z80, Z80_STATE_ARGS, (z80->gpr[RF]&0x40) != 0);
				break;
			case 0xD4: // CALL NC, d
				Z80NAME(op_call_cond)(z80, Z80_STATE_ARGS, (z80->gpr[RF]&0x01) == 0);
				break;
			case 0xDC: // CALL C, d
				Z80NAME(op_call_cond)(z80, Z80_STATE_ARGS, (z80->gpr[RF]&0x01) != 0);
				break;
			case 0xE4: // CALL PO, d
				Z80NAME(op_call_cond)(z80, Z80_STATE_ARGS, (z80->gpr[RF]&0x04) == 0);
				break;
			case 0xEC: // CALL PE, d
				Z80NAME(op_call_cond)(z80, Z80_STATE_ARGS, (z80->gpr[RF]&0x04) != 0);
				break;
			case 0xF4: // CALL P, d
				Z80NAME(op_call_cond)(z80, Z80_STATE_ARGS, (z80->gpr[RF]&0x80) == 0);
				break;
			case 0xFC: // CALL M, d
				Z80NAME(op_call_cond)(z80, Z80_STATE_ARGS, (z80->gpr[RF]&0x80) != 0);
				break;

			// Z=5
			case 0xC5: // PUSH BC
				Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, --z80->sp, z80->gpr[RB]);
				Z80_ADD_CYCLES(z80, 4);
				Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, --z80->sp, z80->gpr[RC]);
				Z80_ADD_CYCLES(z80, 3);
				break;
			case 0xD5: // PUSH DE
				Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, --z80->sp, z80->gpr[RD]);
				Z80_ADD_CYCLES(z80, 4);
				Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, --z80->sp, z80->gpr[RE]);
				Z80_ADD_CYCLES(z80, 3);
				break;
			case 0xE5: if(ix >= 0) {
					// PUSH Iz
					Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, --z80->sp, z80->idx[ix&1][0]);
					Z80_ADD_CYCLES(z80, 4);
					Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, --z80->sp, z80->idx[ix&1][1]);
					Z80_ADD_CYCLES(z80, 3);
				} else {
					// PUSH HL
					Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, --z80->sp, z80->gpr[RH]);
					Z80_ADD_CYCLES(z80, 4);
					Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, --z80->sp, z80->gpr[RL]);
					Z80_ADD_CYCLES(z80, 3);
				} break;
			case 0xF5: // PUSH AF
				Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, --z80->sp, z80->gpr[RA]);
				Z80_ADD_CYCLES(z80, 4);
				Z80NAME(mem_write)(Z80_STATE_ARGS, z80->H.timestamp, --z80->sp, z80->gpr[RF]);
				Z80_ADD_CYCLES(z80, 3);
				break;

			case 0xCD: // CALL nn
				Z80NAME(op_call_cond)(z80, Z80_STATE_ARGS, true);
				break;

			// Z=6
			case 0xC6: { // ADD A, n
				uint8_t val = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
				z80->gpr[RA] = Z80NAME(add8)(z80, z80->gpr[RA], val);
			} break;
			case 0xCE: { // ADC A, n
				uint8_t val = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
				z80->gpr[RA] = Z80NAME(adc8)(z80, z80->gpr[RA], val);
			} break;
			case 0xD6: { // SUB n
				uint8_t val = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
				z80->gpr[RA] = Z80NAME(sub8)(z80, z80->gpr[RA], val);
			} break;
			case 0xDE: { // SBC A, n
				uint8_t val = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
				z80->gpr[RA] = Z80NAME(sbc8)(z80, z80->gpr[RA], val);
			} break;
			case 0xE6: { // AND n
				uint8_t val = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
				z80->gpr[RA] = Z80NAME(and8)(z80, z80->gpr[RA], val);
			} break;
			case 0xEE: { // XOR n
				uint8_t val = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
				z80->gpr[RA] = Z80NAME(xor8)(z80, z80->gpr[RA], val);
			} break;
			case 0xF6: { // OR n
				uint8_t val = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
				z80->gpr[RA] = Z80NAME(or8)(z80, z80->gpr[RA], val);
			} break;
			case 0xFE: { // CP n
				uint8_t val = Z80NAME(fetch_op_x)(z80, Z80_STATE_ARGS);
				Z80NAME(sub8)(z80, z80->gpr[RA], val);
				z80->gpr[RF] = (z80->gpr[RF]&~0x28) | (val&0x28);
			} break;

			// Z=7: RST
			case 0xC7: // RST $00
				Z80NAME(op_rst)(z80, Z80_STATE_ARGS, 0x0000);
				break;
			case 0xCF: // RST $08
				Z80NAME(op_rst)(z80, Z80_STATE_ARGS, 0x0008);
				break;
			case 0xD7: // RST $10
				Z80NAME(op_rst)(z80, Z80_STATE_ARGS, 0x0010);
				break;
			case 0xDF: // RST $18
				Z80NAME(op_rst)(z80, Z80_STATE_ARGS, 0x0018);
				break;
			case 0xE7: // RST $20
				Z80NAME(op_rst)(z80, Z80_STATE_ARGS, 0x0020);
				break;
			case 0xEF: // RST $28
				Z80NAME(op_rst)(z80, Z80_STATE_ARGS, 0x0028);
				break;
			case 0xF7: // RST $30
				Z80NAME(op_rst)(z80, Z80_STATE_ARGS, 0x0030);
				break;
			case 0xFF: // RST $38
				Z80NAME(op_rst)(z80, Z80_STATE_ARGS, 0x0038);
				break;

			default:
				// TODO!
				fprintf(stderr, "OP: %02X [%04X]\n", op, z80->pc-1);
				fflush(stderr); abort();
				break;
		}
	}
}

void Z80NAME(init)(struct EmuGlobal *H, struct Z80 *z80)
{
	*z80 = (struct Z80){ .H={.timestamp=0,}, };
	Z80NAME(reset)(z80);
}

