#include <sys/mman.h>
#include <assert.h>
#include <math.h>
#include <locale.h>
#include <time.h>
#include "minigbs.h"

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "Some of the bitfield / casting used in here assumes little endian :("
#endif

struct Config cfg;
uint8_t* mem;

static uint8_t* banks[32];
static int next_counter;
static int boldness[16*3];
static struct GBSHeader h;

void bank_switch(int which){
	if(cfg.debug_mode) printf("Bank switching to %d.\n", which);

	// allowing bank switch to 0 seems to break some games
	if(which > 0 && which < 32 && banks[which]){
		memcpy(mem + 0x4000, banks[which], 0x4000);
		if(cfg.debug_mode) puts("Bank switch success.");
	}
}

static inline void mem_write(uint16_t addr, uint8_t val){
	if(addr >= 0x2000 && addr < 0x4000){
		bank_switch(val);
	} else if(addr >= 0xFF10 && addr <= 0xFF40){
		if(!cfg.subdued && mem[addr] != val){
			boldness[addr - 0xFF10] = (int)audio_rate >> 3;
		}
		audio_write(addr, val);
		next_counter = 0;
	} else if(addr < 0x8000){
		if(cfg.debug_mode){
			printf("rom write?: [%4x] <- [%2x]\n", addr, val);
		}
	} else if(addr == 0xFF06 || addr == 0xFF07){
		mem[addr] = val;
		audio_update_rate();
	} else {
		if(cfg.debug_mode){
			switch(addr){
				case 0xFF04: printf("DIV write: %2x\n", val); break;
				case 0xFF05: printf("TIMA write: %2x\n", val); break;
				case 0xFF0F: printf("IF write: %2x\n", val); break;
				case 0xFF41: printf("STAT: %2x\n", val); break;
				case 0xFF46: printf("DMA: %2x\n", val); break;
				case 0xFFFF: printf("IE: %2x\n", val); break;
			}
		}
		mem[addr] = val;
	}
}

static inline uint8_t mem_read(uint16_t addr){
	uint8_t val = mem[addr];

	static uint8_t ortab[] = {
		0x80, 0x3f, 0x00, 0xff, 0xbf,
		0xff, 0x3f, 0x00, 0xff, 0xbf,
		0x7f, 0xff, 0x9f, 0xff, 0xbf,
		0xff, 0xff, 0x00, 0x00, 0xbf,
		0x00, 0x00, 0x70
	};
	
	if(addr >= 0xFF10 && addr <= 0xFF26){
		val |= ortab[addr - 0xFF10];
	}

	if(cfg.debug_mode){
		switch(addr){
			case 0xFF10 ... 0xFF40: printf("Audio read: [%4x] = [%2x]\n", addr, val); break;
			case 0xFF04: printf("DIV read: %2x\n", val); break;
			case 0xFF05: printf("TIMA read: %2x\n", val); break;
			case 0xFF06: printf("TMA read: %2x\n", val); break;
			case 0xFF07: printf("TAC read: %2x\n", val); break;
			case 0xFF0F: printf("IF read: %2x\n", val); break;
			case 0xFF41: printf("STAT read: %2x\n", val); break;
			case 0xFF46: printf("DMA read: %2x\n", val); break;
			case 0xFFFF: printf("IE read: %2x\n", val); break;
		}
	}
	return val;
}

bool cpu_step(void){

	uint8_t op = mem[regs.pc];
	size_t x = op >> 6;
	size_t y = (op >> 3) & 7;
	size_t z = op & 7;

	bool is_ret = false;
	unsigned cycles = 0;

	if(cfg.debug_mode){
		printf("[PC:%4x] [SP:%4x] [AF:%4x] [BC:%4x] [DE:%4x] [HL:%4x] [%2x] ",
		       regs.pc, regs.sp, regs.af, regs.bc, regs.de, regs.hl, op);
		debug_dump(mem + regs.pc);
	}

#define OP(x) &&op_##x
#define ALUY (void*)(1)

	static const void* xmap[4] = {
		[1] = OP(mov8),
		[2] = ALUY
	};

	static const void* zmap[4][8] = {
		[0] = {
			[2] = OP(ldsta16),
			[3] = OP(incdec16),
			[4] = OP(inc8),
			[5] = OP(dec8),
			[6] = OP(ld8)
		},
		[3] = {
			[6] = ALUY,
			[7] = OP(rst)
		},
	};

	static const void* ymap[4][8][8] = {
		[0] = {
			[0] = { OP(nop) , OP(stsp) , OP(stop), OP(jr)   , OP(jrcc), OP(jrcc) , OP(jrcc), OP(jrcc)  },
			[1] = { OP(ld16), OP(addhl), OP(ld16), OP(addhl), OP(ld16), OP(addhl), OP(ld16), OP(addhl) },
			[7] = { OP(rlca), OP(rrca) , OP(rla) , OP(rra)  , OP(daa) , OP(cpl)  , OP(scf) , OP(ccf)   },
		},
		[3] = {
			[0] = { OP(retcc) , OP(retcc) , OP(retcc) , OP(retcc) , OP(sth)  , OP(addsp), OP(ldh)  , OP(ldsp)  },
			[1] = { OP(pop)   , OP(ret)   , OP(pop)   , OP(reti)  , OP(pop)  , OP(jphl) , OP(pop)  , OP(sphl)  },
			[2] = { OP(jpcc)  , OP(jpcc)  , OP(jpcc)  , OP(jpcc)  , OP(stha) , OP(st16) , OP(ldha) , OP(lda16) },
			[3] = { OP(jp)    , OP(cb)    , OP(undef) , OP(undef) , OP(undef), OP(undef), OP(di)   , OP(ei)    },
			[4] = { OP(callcc), OP(callcc), OP(callcc), OP(callcc), OP(undef), OP(undef), OP(undef), OP(undef) },
			[5] = { OP(push)  , OP(call)  , OP(push)  , OP(undef) , OP(push) , OP(undef), OP(push) , OP(undef) },
		}
	};

	static const void* alu[8] = {
		OP(add), OP(adc), OP(sub), OP(sbc), OP(and), OP(xor), OP(or), OP(cp)
	};

	static const struct {
		uint8_t shift;
		uint8_t want;
	} cc[] = {
		{ 7, 0 }, // NZ
		{ 7, 1 }, // Z
		{ 4, 0 }, // NC
		{ 4, 1 }, // C
	};

	// TODO: clean this mess up
	uint8_t*           r[] = { &regs.b, &regs.c, &regs.d, &regs.e, &regs.h, &regs.l, mem + regs.hl, &regs.a };
	static uint16_t*  rr[] = { &regs.bc, &regs.de, &regs.hl, &regs.hl };
	static void*     rot[] = { &&op_rlc, &&op_rrc, &&op_rl, &&op_rr, &&op_sla, &&op_sra, &&op_swap, &&op_srl };
	static uint16_t* rp2[] = { &regs.bc, &regs.de, &regs.hl, &regs.af };

	uint8_t alu_val;

#define R_READ(i)     ({ uint8_t v; if(i == 6){ v = mem_read(regs.hl); } else { v = *r[i]; } v; })
#define R_WRITE(i, v) ({ if(i == 6){ mem_write(regs.hl, v); } else { *r[i] = v; }; *r[i]; })

	if(xmap[x] > ALUY){
		goto *xmap[x];
	} else if(xmap[x] == ALUY){
		alu_val = R_READ(z);
		goto *alu[y];
	} else if(zmap[x][z] > ALUY){
		goto *zmap[x][z];
	} else if(zmap[x][z] == ALUY){
		alu_val = mem_read(++regs.pc);
		goto *alu[y];
	} else {
		goto *ymap[x][z][y];
	}

#undef ALUY
#undef OP

#define OP(name, len, cy, code) op_##name: { code; cycles += cy; regs.pc += len; goto end; }
#define CHECKCC(n) (((regs.flags.all >> cc[n].shift) & 1) == cc[n].want)

#define SS(p) (((uint16_t*)&regs.bc)[p])
#define DD(p) (((uint16_t*)&regs.bc)+(p))
#define NN    ((((uint16_t)mem_read(regs.pc+2)) << 8) | mem_read(regs.pc+1))

	OP(mov8, 1, 4, {
		if(z == 6 && y == 6){
			puts("HALT?");
		} else {
			if(z == 6 || y == 6){ cycles += 4; }
			R_WRITE(y, R_READ(z));
		}
	});

	OP(ldsta16, 1, 8, {
		size_t p = y >> 1;

		if(y & 1){
			regs.a = mem_read(*rr[p]);
		} else {
			mem_write(*rr[p], regs.a);
		}

		if(p == 2) regs.hl++;
		else if(p == 3) regs.hl--;
	});

	OP(incdec16, 1, 8, {
		if(y & 1){
			--*DD(y >> 1);
		} else {
			++*DD(y >> 1);
		}
	});

	OP(inc8, 1, 4, {
		regs.flags.h = (R_READ(y) & 0xF) == 9;
		R_WRITE(y, R_READ(y) + 1);
		regs.flags.z = !R_READ(y);
		regs.flags.n = 0;
	});

	OP(dec8, 1, 4, {
		regs.flags.h = (R_READ(y) & 0xF) == 0;
		R_WRITE(y, R_READ(y) - 1);
		regs.flags.z = !R_READ(y);
		regs.flags.n = 1;
	});

	OP(ld8, 2, 8, {
		R_WRITE(y, mem_read(regs.pc + 1));
		if(y == 6) cycles += 4;
	});

	OP(nop, 1, 4, {
		// skip
	});

	OP(stsp, 3, 20, {
		mem_write(NN+1, regs.sp >> 8);
		mem_write(NN  , regs.sp & 0xFF);
	});

	OP(stop, 2, 4, {
		// skip
	});

	OP(jr, 2, 12, {
		regs.pc += (int8_t)mem_read(regs.pc+1);
	});

	OP(jrcc, 2, 8, {
		if(CHECKCC(y - 4)){
			regs.pc += (int8_t)mem_read(regs.pc+1);
			cycles += 4;
		}
	});

	OP(ld16, 3, 8, {
		*DD(y >> 1) = NN;
	});

	OP(addhl, 1, 8, {
		uint16_t ss = SS(y >> 1);
		regs.flags.h = (((ss&0x0FFF) + (regs.hl&0x0FFF)) & 0x1000) == 0x1000;
		regs.flags.c = __builtin_add_overflow(regs.hl, ss, &regs.hl);
		regs.flags.n = 0;
	});

	OP(rlca, 1, 4, {
		regs.flags.c = regs.a >> 7;
		regs.a = (regs.a << 1) | regs.flags.c;
		regs.flags.z = regs.flags.n = regs.flags.h = 0;
	});

	OP(rrca, 1, 4, {
		regs.flags.c = regs.a & 1;
		regs.a = (regs.a >> 1) | regs.flags.c << 7;
		regs.flags.z = regs.flags.n = regs.flags.h = 0;
	});

	OP(rla, 1, 4, {
		size_t newc = regs.a >> 7;
		regs.a = (regs.a << 1) | regs.flags.c;
		regs.flags.c = newc;
		regs.flags.z = regs.flags.n = regs.flags.h = 0;
	});

	OP(rra, 1, 4, {
		size_t newc = regs.a & 1;
		regs.a = (regs.a >> 1) | regs.flags.c << 7;
		regs.flags.c = newc;
		regs.flags.z = regs.flags.n = regs.flags.h = 0;
	});

	OP(daa, 1, 4, {
		size_t up = regs.a >> 4;
		size_t dn = regs.a & 0xF;
		size_t newc = 0;

		if(dn >= 10 || regs.flags.h){
			if(regs.flags.n){
				newc |= __builtin_sub_overflow(regs.a, 0x06, &regs.a);
			} else {
				newc |= __builtin_add_overflow(regs.a, 0x06, &regs.a);
			}
		}

		if(up >= 10 || regs.flags.c){
			if(regs.flags.n){
				newc |= __builtin_sub_overflow(regs.a, 0x60, &regs.a);
			} else {
				newc |= __builtin_add_overflow(regs.a, 0x60, &regs.a);
			}
		}

		regs.flags.c = newc;
		regs.flags.h = 0;
		regs.flags.z = !regs.a;
	});

	OP(cpl, 1, 4, {
		regs.a = ~regs.a;
		regs.flags.h = 1;
		regs.flags.n = 1;
	});

	OP(scf, 1, 4, {
		regs.flags.c = 1;
		regs.flags.h = 0;
		regs.flags.n = 0;
	});

	OP(ccf, 1, 4, {
		regs.flags.c = !regs.flags.c;
		regs.flags.h = 0;
		regs.flags.n = 0;
	});

	OP(retcc, 1, 8, {
		if(CHECKCC(y)){
			regs.pc = ((mem_read(regs.sp+1) << 8) | mem_read(regs.sp)) - 1;
			regs.sp += 2;
			cycles += 12;
			is_ret = true;
		}
	});

	OP(sth, 2, 12, {
		mem_write(0xFF00 + mem_read(regs.pc+1), regs.a);
	});

	OP(addsp, 2, 16, {
		regs.flags.h = (((regs.sp&0x0FFF) + (mem_read(regs.pc+1)&0x0F)) & 0x1000) == 0x1000;
		regs.flags.c = __builtin_add_overflow(regs.sp, (int8_t)mem_read(regs.pc+1), (int16_t*)&regs.sp);
		regs.flags.z = regs.flags.n = 0;
	});

	OP(ldh, 2, 12, {
		regs.a = mem_read(0xFF00 + mem_read(regs.pc+1));
	});

	OP(ldsp, 2, 12, {
		regs.hl = regs.sp + (char)mem[regs.pc+1];
		regs.flags.h = regs.flags.n = regs.flags.z = regs.flags.c = 0; // XXX: probably wrong
	});

	OP(pop, 1, 12, {
		*rp2[y >> 1] = (mem_read(regs.sp+1) << 8) | mem_read(regs.sp);
		regs.sp += 2;
	});

	OP(ret, 0, 16, {
		regs.pc = (mem_read(regs.sp+1) << 8 | mem_read(regs.sp));
		regs.sp += 2;
		is_ret = true;
	});

	OP(reti, 0, 16, {
		regs.pc = mem_read(regs.sp+1) << 8 | mem_read(regs.sp);
		regs.sp += 2;
		// XXX: interrupts not implemented
		is_ret = true;
	});

	OP(jphl, 0, 4, {
		regs.pc = regs.hl;
	});

	OP(sphl, 1, 8, {
		regs.sp = regs.hl;
	});

	OP(jpcc, 3, 12, {
		if(CHECKCC(y)){
			regs.pc = NN - 3;
			cycles += 4;
		}
	});

	OP(stha, 1, 8, {
		mem_write(0xFF00 + regs.c, regs.a);
	});

	OP(st16, 3, 16, {
		mem_write(NN, regs.a);
	});

	OP(ldha, 1, 8, {
		regs.a = mem_read(0xFF00 + regs.c);
	});

	OP(lda16, 3, 16, {
		regs.a = mem_read(NN);
	});

	OP(jp, 0, 16, {
		regs.pc = NN;
	});

	OP(cb, 0, 0, {
		op = mem_read(++regs.pc);
		x = (op >> 6);
		y = (op >> 3) & 7;
		z = op & 7;

		cycles += (z == 6) ? 16 : 8;
		++regs.pc;

		if(x == 0){
			goto *rot[y];
		} else if(x == 1){ // BIT
			regs.flags.z = !(R_READ(z) & (1 << y));
			regs.flags.n = 0;
			regs.flags.h = 1;
		} else if(x == 2){ // RES
			R_WRITE(z, R_READ(z) & ~(1 << y));
		} else { // SET
			R_WRITE(z, R_READ(z) | (1 << y));
		}
	});

	OP(undef, 1, 4, {
		// skip
	});

	OP(di, 1, 4, {
		// XXX: interrupts not implemented
	});

	OP(ei, 1, 4, {
		// XXX: interrupts not implemented
	});

	OP(callcc, 3, 12, {
		if(CHECKCC(y)){
			mem_write(regs.sp-1, (regs.pc + 3) >> 8);
			mem_write(regs.sp-2, (regs.pc + 3) & 0xFF);
			regs.sp -= 2;
			regs.pc = NN - 3;
			cycles += 12;
		}
	});

	OP(push, 1, 16, {
		mem_write(regs.sp-2, *rp2[y >> 1] & 0xFF);
		mem_write(regs.sp-1, *rp2[y >> 1] >> 8);
		regs.sp -= 2;
	});

	OP(call, 0, 24, {
		mem_write(regs.sp-1, (regs.pc + 3) >> 8);
		mem_write(regs.sp-2, (regs.pc + 3) & 0xFF);
		regs.sp -= 2;
		regs.pc = NN;
	});

	OP(rst, 0, 16, {
		mem_write(regs.sp-1, (regs.pc + 1) >> 8);
		mem_write(regs.sp-2, (regs.pc + 1) & 0xFF);
		regs.pc = h.load_addr + (y*8);
		regs.sp -= 2;
	});

	OP(add, 1, 4, {
		regs.flags.h = (((regs.a&0x0F) + (alu_val&0x0F)) & 0x10) == 0x10;
		regs.flags.c = __builtin_add_overflow(regs.a, alu_val, &regs.a);
		regs.flags.z = regs.a == 0;
		regs.flags.n = 0;
	});

	OP(adc, 1, 4, {
		regs.flags.h = (((regs.a&0x0F) + (alu_val&0x0F) + regs.flags.c) & 0x10) == 0x10;
		uint8_t tmp;
		regs.flags.c = __builtin_add_overflow(regs.a, regs.flags.c, &tmp) | __builtin_add_overflow(tmp, alu_val, &regs.a);
		regs.flags.z = regs.a == 0;
		regs.flags.n = 0;
	});

	OP(sub, 1, 4, {
		regs.flags.h = (regs.a&0x0F) < (alu_val&0x0F);
		regs.flags.c = __builtin_sub_overflow(regs.a, alu_val, &regs.a);
		regs.flags.z = regs.a == 0;
		regs.flags.n = 1;
	});

	OP(sbc, 1, 4, {
		regs.flags.h = (regs.a&0x0F) < (alu_val&0x0F) || (regs.a&0x0F) < regs.flags.c;
		uint8_t tmp;
		regs.flags.c = __builtin_sub_overflow(regs.a, regs.flags.c, &tmp) | __builtin_sub_overflow(tmp, alu_val, &regs.a);
		regs.flags.z = regs.a == 0;
		regs.flags.n = 1;
	});

	OP(and, 1, 4, {
		regs.flags.h = 1;
		regs.flags.n = regs.flags.c = 0;
		regs.a &= alu_val;
		regs.flags.z = !regs.a;
	});

	OP(xor, 1, 4, {
		regs.flags.h = regs.flags.n = regs.flags.c = 0;
		regs.a ^= alu_val;
		regs.flags.z = !regs.a;
	});

	OP(or, 1, 4, {
		regs.flags.h = regs.flags.n = regs.flags.c = 0;
		regs.a |= alu_val;
		regs.flags.z = !regs.a;
	});

	OP(cp, 1, 4, {
		uint8_t tmp;
		regs.flags.h = (regs.a&0x0F) < (alu_val&0x0F);
		regs.flags.c = __builtin_sub_overflow(regs.a, alu_val, &tmp);
		regs.flags.z = tmp == 0;
		regs.flags.n = 1;
	});

	OP(rlc, 0, 0, {
		regs.flags.c = R_READ(z) >> 7;
		R_WRITE(z, (R_READ(z) << 1) | regs.flags.c);
		regs.flags.z = !R_READ(z);
		regs.flags.n = regs.flags.h = 0;
	});

	OP(rrc, 0, 0, {
		regs.flags.c = R_READ(z) & 1;
		R_WRITE(z, (R_READ(z) >> 1) | regs.flags.c << 7);
		regs.flags.z = !R_READ(z);
		regs.flags.n = regs.flags.h = 0;
	});

	OP(rl, 0, 0, {
		size_t newc = R_READ(z) >> 7;
		R_WRITE(z, (R_READ(z) << 1) | regs.flags.c);
		regs.flags.c = newc;
		regs.flags.z = !R_READ(z);
		regs.flags.n = regs.flags.h = 0;
	});

	OP(rr, 0, 0, {
		size_t newc = R_READ(z) & 1;
		R_WRITE(z, (R_READ(z) >> 1) | regs.flags.c << 7);
		regs.flags.c = newc;
		regs.flags.z = !R_READ(z);
		regs.flags.n = regs.flags.h = 0;
	});

	OP(sla, 0, 0, {
		regs.flags.c = R_READ(z) >> 7;
		R_WRITE(z, R_READ(z) << 1);
		regs.flags.z = !R_READ(z);
		regs.flags.n = regs.flags.h = 0;
	});

	OP(sra, 0, 0, {
		regs.flags.c = R_READ(z) & 1; // ????
		R_WRITE(z, ((int8_t)R_READ(z)) >> 1);
		regs.flags.z = !R_READ(z);
		regs.flags.n = regs.flags.h = 0;
	});

	OP(swap, 0, 0, {
		uint8_t tmp = ((R_READ(z) & 0xF) << 4) | (R_READ(z) >> 4);
		R_WRITE(z, tmp);
		regs.flags.z = !R_READ(z);
		regs.flags.n = regs.flags.h = regs.flags.c = 0;
	});

	OP(srl, 0, 0, {
		regs.flags.c = R_READ(z) & 1;
		R_WRITE(z, R_READ(z) >> 1);
		regs.flags.z = !R_READ(z);
		regs.flags.n = regs.flags.h = 0;
	});

end:;
	return is_ret;
}

void usage(const char* argv0, FILE* out){
	fprintf(out,
	        "Usage: %s [-dhmqs] file [song index]\n\n"
			"  -h, Output this info to stdout.\n\n"
			"  -d, Debug mode   : Dump a cpu trace to stdout, implies -q.\n"
			"  -m, Mono mode    : Disable colors.\n"
			"  -q, Quiet mode   : Disable UI.\n"
			"  -s, Subdued mode : Don't flash/embolden changed registers.\n\n",
			argv0);
}

static int msg_timer;
static char msg[128];

void set_msg(const char* fmt, ...){
	va_list va;
	va_start(va, fmt);

	vsnprintf(msg, sizeof(msg), fmt, va);

	msg_timer = 20;
	va_end(va);
}

uint64_t get_time(void){
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (ts.tv_sec * 1000000UL) + (ts.tv_nsec / 1000UL);
}

void ui_draw_info(void){
	char buf[256] = {};
	int len = 0;

	move(0, 0);
	attron(A_BOLD);

	len = snprintf(buf, countof(buf), "[%d/%d]", cfg.song_no, h.song_count - 1);
	mvaddstr(0, cfg.win_w-len, buf);
	len = 0;
	*buf = 0;

	if(*h.title && strncmp(h.title, "<?>", 4) != 0){
		len += snprintf(buf + len, countof(buf) - len, "%.32s", h.title);
	}
	if(*h.copyright && strncmp(h.copyright, "<?>", 4) != 0){
		len += snprintf(buf + len, countof(buf) - len, "  ©%.32s", h.copyright);
	}
	mvaddstr(1, (cfg.win_w-len)/2, buf);
	*buf = 0;

	if(*h.author && strncmp(h.author, "<?>", 4) != 0){
		len = snprintf(buf, countof(buf), "by %.32s ", h.author);
	}
	mvaddstr(2, (cfg.win_w-len)/2, buf);
}

void ui_draw_regs(void){
	static const int color_map[3][16] = {
		{ 1, 1, 1, 1, 1, 5, 2, 2, 2, 2, 3, 3, 3, 3, 3, 5 },
		{ 4, 4, 4, 4, 6, 6, 6, 5, 5, 5, 5, 5, 5, 5, 5, 5 },
		{ 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3 },
	};

	int x = (cfg.win_w-47)/2;
	int y = (cfg.win_h-6)/6+4;

	move(y, x);
	attron(A_BOLD);
	for(int i = 0; i < 0x10; ++i) printw("  %1X", i);
	attroff(A_BOLD);

	for(int i = 0; i < 3; ++i){
		move(y+i+1, x-4);
		attron(A_BOLD);
		printw("FF%d ", i+1);
		attroff(A_BOLD);
		for(int j = 0; j < 0x10; ++j){
			if(boldness[i*16+j]){
				attron(A_BOLD);
			}
			attron(COLOR_PAIR(color_map[i][j]));
			printw(" %02x", mem[0xFF10 + (i*0x10) + j]);
			attroff(COLOR_PAIR(color_map[i][j]));
			if(boldness[i*16+j]){
				attroff(A_BOLD);
				--boldness[i*16+j];
			}
		}
	}
}

void change_speed(float amt){
	audio_speed_modifier = MAX(0.1f, MIN(2.0f, audio_speed_modifier + amt));
	set_msg("Speed: %d%%\n", (int)roundf(100.0f * audio_speed_modifier));
	audio_update_rate();
}

int main(int argc, char** argv){
	setlocale(LC_ALL, "");
	char* prog = argv[0];

	int opt;
	while((opt = getopt(argc, argv, "dhmqs")) != -1){
		switch(opt){
			case 'd':
				cfg.hide_ui = cfg.debug_mode = true;
				break;
			case 'h':
				usage(prog, stdout);
				return 0;
			case 'm':
				cfg.monochrome = true;
				break;
			case 'q':
				cfg.hide_ui = true;
				break;
			case 's':
				cfg.subdued = true;
				break;
			default:
				usage(prog, stderr);
				return 1;
		}
	}

	if(optind >= argc){
		fprintf(stderr, "Missing file argument.\n\n");
		usage(argv[0], stderr);
		return 1;
	}

	argc -= (optind-1);
	argv += (optind-1);

	FILE* f = fopen(argv[1], "r");
	if(!f){
		fprintf(stderr, "Error opening file '%s': %m\n", argv[1]);
		return 1;
	}

	if(fread(&h, sizeof(h), 1, f) != 1){
		return 1;
	}

	if(strncmp(h.id, "GBS", 3) != 0){
		fprintf(stderr, "That doesn't look like a GBS file.\n");
		return 1;
	}

	if(h.version != 1){
		fprintf(stderr, "This GBS file is version %d, I can only handle version 1 :(\n", h.version);
		return 1;
	}

	cfg.song_no = argc > 2 ? atoi(argv[2]) : MAX(0, h.start_song - 1);
	if(cfg.song_no >= h.song_count){
		fprintf(stderr, "The file says it has %d tracks, index %d is out of range.\n", h.song_count, cfg.song_no);
		return 1;
	}

	if(cfg.debug_mode){
		printf("id   : %.3s\n", h.id);
		printf("ver  : %d\n", h.version);
		printf("count: %d\n", h.song_count);
		printf("start: %d\n", h.start_song);
		printf("load : %x\n", h.load_addr);
		printf("init : %x\n", h.init_addr);
		printf("play : %x\n", h.play_addr);
		printf("sp   : %x\n", h.sp);
		printf("tma  : %d\n", h.tma);
		printf("tac  : %d\n", h.tac);
		printf("title: %.32s\n", h.title);
		printf("authr: %.32s\n", h.author);
		printf("copyr: %.32s\n", h.copyright);
	}

	mem = mmap(NULL, 0x12000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	assert(mem != MAP_FAILED);
	mem += 0x1000;

	mprotect(mem - 0x1000 , 0x1000, PROT_NONE);
	mprotect(mem + 0x10000, 0x1000, PROT_NONE);

	fseek(f, 0, SEEK_END);
	long fsz = ftell(f) - 0x70;
	fseek(f, 0x70, SEEK_SET);

	if(cfg.debug_mode){
		puts("rom banks:");
	}

	int bno = h.load_addr / 0x4000;
	int off = h.load_addr % 0x4000;

	while(1){
		uint8_t* page = mmap(NULL, 0x4000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		assert(page != MAP_FAILED);
		banks[bno] = page;

		size_t n = fread(page + off, 1, 0x4000 - off, f);
		if(cfg.debug_mode){
			printf("Bank %d: %zu\n", bno, n);
		}

		if(feof(f)){
			break;
		} else if(ferror(f)){
			printf("Error reading file: %m\n");
			break;
		}

		off = 0;
		if(++bno >= 32){
			puts("Too many banks...");
			exit(1);
		}
	}
	fclose(f);

	static const uint8_t regs_init[] = {
		0x80, 0xBF, 0xF3, 0xFF, 0x3F, 0xFF, 0x3F, 0x00,
		0xFF, 0x3F, 0x7F, 0xFF, 0x9F, 0xFF, 0x3F, 0xFF,
		0xFF, 0x00, 0x00, 0x3F, 0x77, 0xF3, 0xF1,
	};

	static const uint8_t wave_init[] = {
		0xac, 0xdd, 0xda, 0x48,
		0x36, 0x02, 0xcf, 0x16,
		0x2c, 0x04, 0xe5, 0x2c,
		0xac, 0xdd, 0xda, 0x48
	};

	if(!cfg.hide_ui){
		initscr();
		noecho();
		cbreak();
		timeout(0);
		curs_set(0);
		nodelay(stdscr, TRUE);
		keypad(stdscr, TRUE);
		set_escdelay(0);

		if(!cfg.monochrome){
			start_color();

			if(can_change_color() && COLORS > 8){
				init_color(COLOR_BLACK, 91, 91, 102);
			}

			init_pair(1, COLOR_CYAN   , COLOR_BLACK);
			init_pair(2, COLOR_MAGENTA, COLOR_BLACK);
			init_pair(3, COLOR_RED    , COLOR_BLACK);
			init_pair(4, COLOR_YELLOW , COLOR_BLACK);
			init_pair(5, COLOR_BLACK  , COLOR_BLACK);
			init_pair(6, COLOR_GREEN  , COLOR_BLACK);
			init_pair(7, COLOR_WHITE  , COLOR_BLACK);

			bkgd(COLOR_PAIR(7));
		}
		
		getmaxyx(stdscr, cfg.win_h, cfg.win_w);
	}

	mem[0xff06] = h.tma;
	mem[0xff07] = h.tac;

	cfg.volume = 1.0f;
	audio_init();

	// hack to avoid ALSA warnings breaking the UI
	fclose(stderr);

	bool paused;
restart:

	audio_reset();
	clear();
	if(banks[0]) memcpy(mem, banks[0], 0x4000);
	if(banks[1]) memcpy(mem + 0x4000, banks[1], 0x4000);

	memset(&regs, 0, sizeof(regs));
	memset(mem + 0x8000, 0, 0x8000);

	memcpy(mem, mem + h.load_addr, 0x62);

	regs.sp = h.sp - 2;
	regs.pc = h.init_addr;
	regs.a = cfg.song_no;

	mem[0xffff] = 1; // IE
	mem[0xff06] = h.tma;
	mem[0xff07] = h.tac;

	for(int i = 0; i < 23; ++i){
		mem_write(0xFF10 + i, regs_init[i]);
	}
	memcpy(mem + 0xff30, wave_init, 16);

	paused = false;
	audio_pause(false);

	uint64_t prev_output_time = 0, prev_ui_time = 0;
	bool redraw_note_freq = false;

	while(1){

		bool redraw_ui = !cfg.hide_ui && (get_time() - prev_ui_time) > 16667UL;
		if(redraw_ui){
			redraw_note_freq = true;
		}

		if(paused){
			usleep(20000);
		} else if(cpu_step() && regs.sp == h.sp){
			regs.pc = h.play_addr;
			regs.sp -= 2;

			if(next_counter > 90){
				cfg.song_no = (cfg.song_no + 1) % h.song_count;
				set_msg("Next\n");
				goto restart;
			}

			if(cfg.debug_mode){
				puts("-------------------------");
			}

			audio_output(redraw_note_freq);
			redraw_note_freq = false;

			if(!cfg.hide_ui){
				ui_draw_info();
				ui_draw_regs();
				refresh();
			}

			uint64_t diff = get_time() - prev_output_time;
			int64_t slp = (1000000UL / audio_rate) - (diff + 1000);
			if(slp > 5000) usleep(slp);
			prev_output_time = get_time();
		}

		if(!redraw_ui) continue;
		prev_ui_time = get_time();

		if(msg_timer > 0){
			int x, y;
			getmaxyx(stdscr, y, x);
			move(y-1, 0);

			if(--msg_timer == 0){
				clrtoeol();
			} else {
				if(msg_timer > 15) attron(A_BOLD);
				printw("%s\n", msg);
				if(msg_timer > 15) attroff(A_BOLD);
			}
		}

		int key;
		switch((key = getch())){

			case 27: // Escape
				if(getch() != -1) break;
			case 'q':
				goto end;

			case '1':
			case '2':
			case '3':
			case '4': {
				bool muted = audio_mute(key - '0', -1);
				set_msg("Channel %c %smuted\n", key, muted ? "" : "un");
			} break;

			case 'n':
			case KEY_RIGHT:
				cfg.song_no = (cfg.song_no + 1) % h.song_count;
				set_msg("Next\n");
				goto restart;

			case 'p':
			case KEY_LEFT:
				cfg.song_no = (h.song_count + cfg.song_no - 1) % h.song_count;
				set_msg("Previous\n");
				goto restart;

			case 'r':
			case KEY_UP:
				set_msg("Replay\n");
				goto restart;

			case ' ':
			case KEY_DOWN:
				paused = !paused;
				audio_pause(paused);
				set_msg("%s\n", paused ? "Paused" : "Resumed");
				break;

			case KEY_RESIZE:
				getmaxyx(stdscr, cfg.win_h, cfg.win_w);
				clear();
				break;

			case '=':
			case '+':
				cfg.volume = MIN(1.0f, cfg.volume + 0.05f);
				set_msg("Volume: %d%%\n", (int)roundf(100.0f * cfg.volume));
				break;

			case '-':
			case '_':
				cfg.volume = MAX(0.0f, cfg.volume - 0.05f);
				set_msg("Volume: %d%%\n", (int)roundf(100.0f * cfg.volume));
				break;

			case '[':
				change_speed(-0.05);
				break;

			case ']':
				change_speed(0.05);
				break;

			case KEY_BACKSPACE:
				change_speed(1.0f - audio_speed_modifier);
				break;

			case 12: // CTRL-L
				clear();
				break;

			default:
				break;
		}
	}

end:
	if(!cfg.hide_ui){
		endwin();
	}

	return 0;
}
