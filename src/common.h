#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>

#include <unistd.h>

#include <SDL.h>

#define TIME_IN_ORDER(t0, t1) (((t0) - (t1)) > ((t1) - (t0)))

#define Z80_ADD_CYCLES(z80, v) (z80)->timestamp += ((v)*3)
#define VDP_ADD_CYCLES(vdp, v) (vdp)->timestamp += ((v)*2)

#define RB 0
#define RC 1
#define RD 2
#define RE 3
#define RH 4
#define RL 5
#define RF 6
#define RA 7

struct Z80
{
	// CPU state
	uint8_t gpr[8];
	uint8_t shadow[8];
	uint8_t idx[2][2];
	uint8_t wz[4]; // internal register + shadow; required for some flag bit 3/5 stuff
	uint8_t i,r,iff1,iff2;
	uint16_t sp;
	uint16_t pc;
	uint8_t halted, im, noni;

	// Tracking state
	uint64_t timestamp;
	uint64_t timestamp_end;
};

struct VDP
{
	// VDP state
	
	// Tracking state
	uint64_t timestamp;
	uint64_t timestamp_end;
};

struct PSG
{
	// PSG state
	
	// Tracking state
	uint64_t timestamp;
	uint64_t timestamp_end;
};

struct SMS
{
	uint8_t vram[16384];
	uint8_t ram[8192];
	uint8_t cram[32];
	struct Z80 z80;
	struct VDP vdp;
	struct PSG psg;
	uint8_t paging[4];

	uint64_t timestamp;
	uint64_t timestamp_end;
};

extern uint8_t sms_rom[512*1024];
extern bool sms_rom_is_banked;

// sms.c
uint8_t sms_input_fetch(struct SMS *sms, uint64_t timestamp, int port);
void sms_init(struct SMS *sms);
void sms_copy(struct SMS *dest, struct SMS *src);
void sms_run(struct SMS *sms, uint64_t timestamp);

// z80.c
void z80_reset(struct Z80 *z80);
void z80_init(struct Z80 *z80);
void z80_run(struct Z80 *z80, struct SMS *sms, uint64_t timestamp);

