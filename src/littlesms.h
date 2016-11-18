#include <stdint.h>

#ifndef USE_NTSC
#define USE_NTSC 0
#endif

#define TIME_IN_ORDER(t0, t1) (((t0) - (t1)) > ((t1) - (t0)))

#define PSG_OUT_BUF_LEN (1<<24)

#define RB 0
#define RC 1
#define RD 2
#define RE 3
#define RH 4
#define RL 5
#define RF 6
#define RA 7

#if (USE_NTSC) != 0
#define SCANLINES 262
#define FRAME_START_Y 43
#define FRAME_BORDER_TOP 27
#define FRAME_BORDER_BOTTOM 24
#define FRAME_WAIT (1000000/60)
#else
#define SCANLINES 313
#define FRAME_START_Y 70
#define FRAME_BORDER_TOP 54
#define FRAME_BORDER_BOTTOM 48
#define FRAME_WAIT (1000000/50)
#endif

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
} __attribute__((__packed__));

struct VDP
{
	// VDP state
	uint8_t sprd[8][4];
	uint8_t regs[16];
	uint8_t sprx[8];
	uint16_t ctrl_addr;
	uint8_t ctrl_latch;
	uint8_t read_buf;
	uint8_t status;
	uint8_t status_latches;

	uint8_t line_counter;
	uint8_t scx, scy;

	uint8_t irq_out;
	uint8_t irq_mask;

	
	// Tracking state
	uint64_t timestamp;
	uint64_t timestamp_end;
} __attribute__((__packed__));

struct PSG
{
	// PSG state
	uint16_t period[4];
	uint32_t poffs[4];
	uint16_t vol[4];
	uint16_t onstate[4];
	uint16_t lfsr_offs;
	uint8_t lcmd;
	uint8_t lnoise;
	
	// Tracking state
	uint64_t timestamp;
	uint64_t timestamp_end;
} __attribute__((__packed__));

struct SMS
{
	uint8_t vram[16384];
	uint8_t ram[8192];
	uint8_t cram[32];
	struct Z80 z80;
	struct VDP vdp;
	struct PSG psg;
	uint8_t paging[4];
	uint8_t joy[2];
	uint8_t memcfg;
	uint8_t iocfg;
	uint8_t hlatch;

	bool no_draw;
	uint64_t timestamp;
	uint64_t timestamp_end;
} __attribute__((__packed__));

struct SMSGlobal
{
	struct SMS current;

	// SMS
	uint8_t rom[4*1024*1024];
	uint64_t twait;
	bool rom_is_banked;

	// VDP
	uint8_t frame_data[SCANLINES][342];

	// PSG
	int32_t outhpf_charge;
} __attribute__((__packed__));


// psg.c
void psg_pop_16bit_mono(int16_t *buf, size_t len);
void psg_run(struct PSG *psg, struct SMSGlobal *G, struct SMS *sms, uint64_t timestamp);
void psg_init(struct SMSGlobal *G, struct PSG *psg);
void psg_write(struct PSG *psg, struct SMSGlobal *G, struct SMS *sms, uint64_t timestamp, uint8_t val);

// sms.c
void sms_init(struct SMSGlobal *G, struct SMS *sms);
void sms_copy(struct SMS *dest, struct SMS *src);
void sms_run(struct SMSGlobal *G, struct SMS *sms, uint64_t timestamp);
void sms_run_frame(struct SMSGlobal *G, struct SMS *sms);
extern void (*sms_hook_poll_input)(struct SMSGlobal *G, struct SMS *sms, int controller, uint64_t timestamp);

// vdp.c
uint8_t vdp_read_ctrl(struct VDP *vdp, struct SMSGlobal *G, struct SMS *sms, uint64_t timestamp);
uint8_t vdp_read_data(struct VDP *vdp, struct SMSGlobal *G, struct SMS *sms, uint64_t timestamp);
void vdp_write_ctrl(struct VDP *vdp, struct SMSGlobal *G, struct SMS *sms, uint64_t timestamp, uint8_t val);
void vdp_write_data(struct VDP *vdp, struct SMSGlobal *G, struct SMS *sms, uint64_t timestamp, uint8_t val);
void vdp_run(struct VDP *vdp, struct SMSGlobal *G, struct SMS *sms, uint64_t timestamp);
void vdp_init(struct SMSGlobal *G, struct VDP *vdp);
void vdp_estimate_line_irq(struct VDP *vdp, struct SMSGlobal *G, struct SMS *sms, uint64_t timestamp);

// z80.c
void sms_z80_reset(struct Z80 *z80);
void sms_z80_init(struct SMSGlobal *G, struct Z80 *z80);
void sms_z80_irq(struct Z80 *z80, struct SMSGlobal *G, struct SMS *sms, uint8_t dat);
void sms_z80_nmi(struct Z80 *z80, struct SMSGlobal *G, struct SMS *sms);
void sms_z80_run(struct Z80 *z80, struct SMSGlobal *G, struct SMS *sms, uint64_t timestamp);

// main.c
extern struct SMSGlobal Gsms;
uint64_t time_now(void);


