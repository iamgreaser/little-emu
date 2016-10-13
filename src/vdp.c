#include "common.h"

uint8_t frame_data[SCANLINES][342];

static void vdp_do_reg_write(struct VDP *vdp, struct SMS *sms, uint64_t timestamp)
{
	uint8_t reg = (uint8_t)((vdp->ctrl_addr>>8)&0x0F);
	uint8_t val = (uint8_t)(vdp->ctrl_addr);
	printf("REG %X = %02X\n", reg, val);
	vdp->regs[reg] = val;
}

void vdp_init(struct VDP *vdp)
{
	*vdp = (struct VDP){ .timestamp=0 };
	vdp->ctrl_addr = 0xBF00;
	vdp->ctrl_latch = 0;
	vdp->read_buf = 0x00;
}

void vdp_run(struct VDP *vdp, struct SMS *sms, uint64_t timestamp)
{
	if(!TIME_IN_ORDER(vdp->timestamp, timestamp)) {
		return;
	}

	// Fetch vcounters/hcounters
	uint32_t vctr_beg = ((vdp->timestamp/(684ULL))%((unsigned long long)SCANLINES));
	uint32_t hctr_beg = (vdp->timestamp%(684ULL));
	uint32_t vctr_end = ((timestamp/(684ULL))%((unsigned long long)SCANLINES));
	uint32_t hctr_end = (timestamp%(684ULL));
	hctr_beg >>= 1;
	hctr_end >>= 1;

	// Fetch pointers
	uint8_t *sp_attrs = &sms->vram[(vdp->regs[0x05]&0x7E)<< 7];
	uint8_t *bg_names = &sms->vram[(vdp->regs[0x02]&0x0E)<<10];
	uint8_t *sp_tiles = &sms->vram[(vdp->regs[0x06]&0x04)<<11];
	uint8_t *bg_tiles = &sms->vram[0];

	// Draw screen section
	int vctr = vctr_beg;
	int lborder = ((vdp->regs[0x00]&0x00) != 0 ? 8 : 0);
	int bcol = sms->cram[(vdp->regs[0x07]&0x0F)|0x10];
	int scx = vdp->regs[0x08];
	int scy = vdp->regs[0x09];

	for(;;) {
		int hbeg = (vctr == vctr_beg ? hctr_beg : 0);
		int hend = (vctr == vctr_end ? hctr_end : 684);
		int y = vctr - 67;

		if(y < 0 || y >= 192 || (vdp->regs[0x01]&0x40)==0) {
			if(y < -54 || y >= 192+48) {
				for(int hctr = hbeg; hctr < hend; hctr++) {
					frame_data[vctr][hctr] = 0x00;
				}
			} else {
				for(int hctr = hbeg; hctr < hend; hctr++) {
					frame_data[vctr][hctr] = bcol;
				}
			}
		} else {
			int py = (y+scy)%(28*8);

			for(int hctr = hbeg; hctr < hend; hctr++) {
				int x = hctr - 47;
				if(x >= lborder && x < 256) {
					// TODO get colour PROPERLY
					// TODO sprites
					int px = (x+scx)&0xFF;

					// Read name table for tile
					uint8_t *np = &bg_names[2*((py>>3)*32+(px>>3))];
					uint16_t nl = (uint16_t)(np[0]);
					uint16_t nh = (uint16_t)(np[1]);
					uint16_t n = nl|(nh<<8);
					uint16_t tile = n&0x1FF;
					uint8_t *tp = &bg_tiles[tile*4*8];
					uint8_t pal = (nh<<1)&0x10;
					uint8_t prio = (nh>>4)&0xFF;
					uint8_t xflip = ((nh>>1)&1)*7;
					uint8_t yflip = ((nh>>2)&1)*7;
					int spx = (px^xflip^7)&7;
					int spy = ((py^yflip)&7)<<2;

					// Read tile
					uint8_t t0 = tp[spy+0];
					uint8_t t1 = tp[spy+1];
					uint8_t t2 = tp[spy+2];
					uint8_t t3 = tp[spy+3];

					// Planar to chunky
					uint8_t v = 0
						| (((t0>>spx)&1)<<0)
						| (((t1>>spx)&1)<<1)
						| (((t2>>spx)&1)<<2)
						| (((t3>>spx)&1)<<3)
						| 0;

					// Write
					v = (v == 0 ? 0 : v|pal);
					frame_data[vctr][hctr] = sms->cram[v&0x1F];
					//frame_data[vctr][hctr] = v&0x1F;
				} else {
					frame_data[vctr][hctr] = bcol;
				}
			}
		}

		// Next line (if it exists)
		if(vctr == vctr_end) { break; }
		vctr++;
		if(vctr == SCANLINES) { vctr = 0; }
		assert(vctr < SCANLINES);
	}

	//printf("%03d.%03d -> %03d.%03d\n" , vctr_beg, hctr_beg , vctr_end, hctr_end);

	// Update timestamp
	vdp->timestamp = timestamp;
}

uint8_t vdp_read_ctrl(struct VDP *vdp, struct SMS *sms, uint64_t timestamp)
{
	vdp_run(vdp, sms, timestamp);
	vdp->ctrl_latch = 0;
	sms->z80.in_irq = 0;
	uint8_t ret = vdp->status;
	vdp->status = 0x1F;
	return ret;
}

uint8_t vdp_read_data(struct VDP *vdp, struct SMS *sms, uint64_t timestamp)
{
	vdp_run(vdp, sms, timestamp);
	vdp->ctrl_latch = 0;
	uint8_t ret = vdp->read_buf;
	vdp->read_buf = sms->vram[vdp->ctrl_addr&0x3FFF];
	vdp->ctrl_addr = ((vdp->ctrl_addr+1)&0x3FFF)
		| (vdp->ctrl_addr&0xC000);
	return ret;
}

void vdp_write_ctrl(struct VDP *vdp, struct SMS *sms, uint64_t timestamp, uint8_t val)
{
	vdp_run(vdp, sms, timestamp);
	if(vdp->ctrl_latch == 0) {
		vdp->ctrl_addr &= ~0x00FF;
		vdp->ctrl_addr |= ((uint16_t)val)<<0;
		vdp->ctrl_latch = 1;
	} else {
		vdp->ctrl_addr &= ~0xFF00;
		vdp->ctrl_addr |= ((uint16_t)val)<<8;
		printf("VDP CTRL %04X\n", vdp->ctrl_addr);

		switch(vdp->ctrl_addr>>14) {
			case 0: // Read
				vdp->read_buf = sms->vram[vdp->ctrl_addr&0x3FFF];
				vdp->ctrl_addr = ((vdp->ctrl_addr+1)&0x3FFF)
					| (vdp->ctrl_addr&0xC000);
				break;
			case 1: // Write
				break;
			case 2: // Register
				vdp_do_reg_write(vdp, sms, timestamp);
				break;
			case 3: // CRAM
				break;
		}

		vdp->ctrl_latch = 0;
	}
	//printf("VDP CTRL %02X\n", val);
}

void vdp_write_data(struct VDP *vdp, struct SMS *sms, uint64_t timestamp, uint8_t val)
{
	vdp_run(vdp, sms, timestamp);
	vdp->ctrl_latch = 0;
	if(vdp->ctrl_addr >= 0xC000) {
		// CRAM
		sms->cram[vdp->ctrl_addr&0x001F] = val;
		printf("CRAM %02X %02X\n", vdp->ctrl_addr&0x1F, val);
	} else {
		// VRAM
		sms->vram[vdp->ctrl_addr&0x3FFF] = val;
	}

	vdp->ctrl_addr = ((vdp->ctrl_addr+1)&0x3FFF)
		| (vdp->ctrl_addr&0xC000);
}
