#include <stdint.h>
#include <stdio.h>
#include "minigbs.h"

static const char* opcodes[] = {
	[0x00] = "nop",
	[0x01] = "ld bc, $%2hhx",
	[0x02] = "ld [bc], a",
	[0x03] = "inc bc",
	[0x04] = "inc b",
	[0x05] = "dec b",
	[0x06] = "ld b, $%2hhx",
	[0x07] = "rlca",
	[0x08] = "ld [$%4hx], sp",
	[0x09] = "add hl, bc",
	[0x0a] = "ld a, [bc]",
	[0x0b] = "dec bc",
	[0x0c] = "inc c",
	[0x0d] = "dec c",
	[0x0e] = "ld c, $%2hhx",
	[0x0f] = "rrca",

	[0x10] = "stop 0",
	[0x11] = "ld de, $%4hx",
	[0x12] = "ld [de], a",
	[0x13] = "inc de",
	[0x14] = "inc d",
	[0x15] = "dec d",
	[0x16] = "ld d, $%2hhx",
	[0x17] = "rla",
	[0x18] = "jr %+hhd",
	[0x19] = "add hl, de",
	[0x1a] = "ld a, [de]",
	[0x1b] = "dec de",
	[0x1c] = "inc e",
	[0x1d] = "dec e",
	[0x1e] = "ld e, $%2hhx",
	[0x1f] = "rra",

	[0x20] = "jr nz, %+hhd",
	[0x21] = "ld hl, $%4hx",
	[0x22] = "ld [hl+], a",
	[0x23] = "inc hl",
	[0x24] = "inc h",
	[0x25] = "dec h",
	[0x26] = "ld h, $%02hhx",
	[0x27] = "daa",
	[0x28] = "jr z, %+hhd",
	[0x29] = "add hl, hl",
	[0x2a] = "ld a, [hl+]",
	[0x2b] = "dec hl",
	[0x2c] = "inc l",
	[0x2d] = "dec l",
	[0x2e] = "ld l, $%02hhx",
	[0x2f] = "cpl",

	[0x30] = "jr nc, %+hhd",
	[0x31] = "ld sp, $%4hx",
	[0x32] = "ld [hl-], a",
	[0x33] = "inc sp",
	[0x34] = "inc [hl]",
	[0x35] = "dec [hl]",
	[0x36] = "ld [hl], $%02hhx",
	[0x37] = "scf",
	[0x38] = "jr c, %+hhd",
	[0x39] = "add hl, sp",
	[0x3a] = "ld a, [hl-]",
	[0x3b] = "dec sp",
	[0x3c] = "inc a",
	[0x3d] = "dec a",
	[0x3e] = "ld a, $%02hhx",
	[0x3f] = "ccf",

	[0x40] = "ld b, b",
	[0x41] = "ld b, c",
	[0x42] = "ld b, d",
	[0x43] = "ld b, e",
	[0x44] = "ld b, h",
	[0x45] = "ld b, l",
	[0x46] = "ld b, [hl]",
	[0x47] = "ld b, a",
	[0x48] = "ld c, b",
	[0x49] = "ld c, c",
	[0x4a] = "ld c, d",
	[0x4b] = "ld c, e",
	[0x4c] = "ld c, h",
	[0x4d] = "ld c, l",
	[0x4e] = "ld c, [hl]",
	[0x4f] = "ld c, a",

	[0x50] = "ld d, b",
	[0x51] = "ld d, c",
	[0x52] = "ld d, d",
	[0x53] = "ld d, e",
	[0x54] = "ld d, h",
	[0x55] = "ld d, l",
	[0x56] = "ld d, [hl]",
	[0x57] = "ld d, a",
	[0x58] = "ld e, b",
	[0x59] = "ld e, c",
	[0x5a] = "ld e, d",
	[0x5b] = "ld e, e",
	[0x5c] = "ld e, h",
	[0x5d] = "ld e, l",
	[0x5e] = "ld e, [hl]",
	[0x5f] = "ld e, a",

	[0x60] = "ld h, b",
	[0x61] = "ld h, c",
	[0x62] = "ld h, d",
	[0x63] = "ld h, e",
	[0x64] = "ld h, h",
	[0x65] = "ld h, l",
	[0x66] = "ld h, [hl]",
	[0x67] = "ld h, a",
	[0x68] = "ld l, b",
	[0x69] = "ld l, c",
	[0x6a] = "ld l, d",
	[0x6b] = "ld l, e",
	[0x6c] = "ld l, h",
	[0x6d] = "ld l, l",
	[0x6e] = "ld l, [hl]",
	[0x6f] = "ld l, a",

	[0x70] = "ld [hl], b",
	[0x71] = "ld [hl], c",
	[0x72] = "ld [hl], d",
	[0x73] = "ld [hl], e",
	[0x74] = "ld [hl], h",
	[0x75] = "ld [hl], l",
	[0x76] = "halt",
	[0x77] = "ld [hl], a",
	[0x78] = "ld a, b",
	[0x79] = "ld a, c",
	[0x7a] = "ld a, d",
	[0x7b] = "ld a, e",
	[0x7c] = "ld a, h",
	[0x7d] = "ld a, l",
	[0x7e] = "ld a, [hl]",
	[0x7f] = "ld a, a",

	[0x80] = "add a, b",
	[0x81] = "add a, c",
	[0x82] = "add a, d",
	[0x83] = "add a, e",
	[0x84] = "add a, h",
	[0x85] = "add a, l",
	[0x86] = "add a, [hl]",
	[0x87] = "add a, a",
	[0x88] = "adc a, b",
	[0x89] = "adc a, c",
	[0x8a] = "adc a, d",
	[0x8b] = "adc a, e",
	[0x8c] = "adc a, h",
	[0x8d] = "adc a, l",
	[0x8e] = "adc a, [hl]",
	[0x8f] = "adc a, a",

	[0x90] = "sub a, b",
	[0x91] = "sub a, c",
	[0x92] = "sub a, d",
	[0x93] = "sub a, e",
	[0x94] = "sub a, h",
	[0x95] = "sub a, l",
	[0x96] = "sub a, [hl]",
	[0x97] = "sub a, a",
	[0x98] = "sbc a, b",
	[0x99] = "sbc a, c",
	[0x9a] = "sbc a, d",
	[0x9b] = "sbc a, e",
	[0x9c] = "sbc a, h",
	[0x9d] = "sbc a, l",
	[0x9e] = "sbc a, [hl]",
	[0x9f] = "sbc a, a",

	[0xa0] = "and a, b",
	[0xa1] = "and a, c",
	[0xa2] = "and a, d",
	[0xa3] = "and a, e",
	[0xa4] = "and a, h",
	[0xa5] = "and a, l",
	[0xa6] = "and a, [hl]",
	[0xa7] = "and a, a",
	[0xa8] = "xor a, b",
	[0xa9] = "xor a, c",
	[0xaa] = "xor a, d",
	[0xab] = "xor a, e",
	[0xac] = "xor a, h",
	[0xad] = "xor a, l",
	[0xae] = "xor a, [hl]",
	[0xaf] = "xor a, a",

	[0xb0] = "or a, b",
	[0xb1] = "or a, c",
	[0xb2] = "or a, d",
	[0xb3] = "or a, e",
	[0xb4] = "or a, h",
	[0xb5] = "or a, l",
	[0xb6] = "or a, [hl]",
	[0xb7] = "or a, a",
	[0xb8] = "cp a, b",
	[0xb9] = "cp a, c",
	[0xba] = "cp a, d",
	[0xbb] = "cp a, e",
	[0xbc] = "cp a, h",
	[0xbd] = "cp a, l",
	[0xbe] = "cp a, [hl]",
	[0xbf] = "cp a, a",

	[0xc0] = "ret nz",
	[0xc1] = "pop bc",
	[0xc2] = "jp nz, $%04hx",
	[0xc3] = "jp $%04hx",
	[0xc4] = "call nz, $%04hx",
	[0xc5] = "push bc",
	[0xc6] = "add a, $%02hhx",
	[0xc7] = "rst 00h",
	[0xc8] = "ret z",
	[0xc9] = "ret",
	[0xca] = "jp z, $%04hx",
	[0xcb] = "*",
	[0xcc] = "call z, $%04hx",
	[0xcd] = "call $%04hx",
	[0xce] = "adc a, $%02hhx",
	[0xcf] = "rst 08h",

	[0xd0] = "ret nc",
	[0xd1] = "pop de",
	[0xd2] = "jp nc, $%04hx",
	[0xd3] = "???",
	[0xd4] = "call nc, $%04hx",
	[0xd5] = "push de",
	[0xd6] = "sub a, $%02hhx",
	[0xd7] = "rst 10h",
	[0xd8] = "ret c",
	[0xd9] = "reti",
	[0xda] = "jp c, $%04hx",
	[0xdb] = "???",
	[0xdc] = "call c, $%04hx",
	[0xdd] = "???",
	[0xde] = "sbc a, $%02hhx",
	[0xdf] = "rst 18h",

	[0xe0] = "ldh [$ff%02hhx], a",
	[0xe1] = "pop hl",
	[0xe2] = "ld [$ff+c], a",
	[0xe3] = "???",
	[0xe4] = "???",
	[0xe5] = "push hl",
	[0xe6] = "and a, $%02hhx",
	[0xe7] = "rst 20h",
	[0xe8] = "add sp, %+hhd",
	[0xe9] = "jp [hl]",
	[0xea] = "ld [$%04x], a",
	[0xeb] = "???",
	[0xec] = "???",
	[0xed] = "???",
	[0xee] = "xor a, $%02hhx",
	[0xef] = "rst 28h",

	[0xf0] = "ldh a, [$ff%02hhx]",
	[0xf1] = "pop af",
	[0xf2] = "ld a, [$ff+c]",
	[0xf3] = "di",
	[0xf4] = "???",
	[0xf5] = "push af",
	[0xf6] = "or a, $%02hhx",
	[0xf7] = "rst 30h",
	[0xf8] = "ld hl,sp%+hhd",
	[0xf9] = "ld sp, hl",
	[0xfa] = "ld a, [$%04hx]",
	[0xfb] = "ei",
	[0xfc] = "???",
	[0xfd] = "???",
	[0xfe] = "cp a, $%02hhx",
	[0xff] = "rst 38h",
};

static const char* cb_ops[] = {
	"rlc", "rrc", "rl", "rr", "sla", "sra", "swap", "srl", "bit", "res", "set",
};

static const char* cb_regnames[] = {
	"b", "c", "d", "e", "h", "l", "[hl]", "a"
};

static struct regs prev_regs;
static uint8_t     prev_op;
static uint32_t    prev_len;

enum {
	DBG_COLOUR_PLAIN,
	DBG_COLOUR_ACTIVE,
	DBG_COLOUR_CHANGED,
	DBG_COLOUR_JUMP_TAKEN,
	DBG_COLOUR_JUMP_UNTAKEN,
};

static int colours[] = {
	[DBG_COLOUR_PLAIN]        = 00,
	[DBG_COLOUR_ACTIVE]       = 33,
	[DBG_COLOUR_CHANGED]      = 36,
	[DBG_COLOUR_JUMP_TAKEN]   = 32,
	[DBG_COLOUR_JUMP_UNTAKEN] = 31,
};

enum {
	DBG_REG_A  = (1 << 0),
	DBG_REG_F  = (1 << 1),
	DBG_REG_B  = (1 << 2),
	DBG_REG_C  = (1 << 3),
	DBG_REG_D  = (1 << 4),
	DBG_REG_E  = (1 << 5),
	DBG_REG_H  = (1 << 6),
	DBG_REG_L  = (1 << 7),
	DBG_REG_SP = (1 << 8),
};

// FIXME: it is pretty silly to do a lot of this with string parsing...

static bool debug_is_jump(uint8_t op){
	const char* str = opcodes[op];
	return strncmp(str, "call", 4) == 0
		|| strncmp(str, "jr", 2) == 0
		|| strncmp(str, "jp", 2) == 0
		|| strncmp(str, "rst", 3) == 0
		|| strncmp(str, "ret", 3) == 0;
}

static void debug_get_regs(const uint8_t* op, uint32_t* mask_out, uint32_t* len_out){
	const char* str = opcodes[*op];
	uint32_t mask = 0;

	if(mask_out){
		if(*op == 0xcb){
			size_t z = op[1] & 7;
			char* tmp = alloca(strlen(cb_regnames[z])+2);
			*tmp = ' ';
			strcpy(tmp+1, cb_regnames[z]);
			str = tmp;
		}

		if(strstr(str, " a")) mask |= DBG_REG_A;
		if(strstr(str, " b")) mask |= DBG_REG_B;

		// hack to avoid jumps on carry
		if(*op != 0x38 && *op != 0xd8 && *op != 0xda && *op != 0xdc && strstr(str, " c")) mask |= DBG_REG_C;

		if(strstr(str, " d")) mask |= DBG_REG_D;
		if(strstr(str, " e")) mask |= DBG_REG_E;
		if(strstr(str, " h")) mask |= DBG_REG_H;
		if(strstr(str, " l")) mask |= DBG_REG_L;

		if(strstr(str, "af")) mask |= (DBG_REG_A | DBG_REG_F);
		if(strstr(str, "bc")) mask |= (DBG_REG_B | DBG_REG_C);
		if(strstr(str, "hl")) mask |= (DBG_REG_H | DBG_REG_L);

		if(strstr(str, "sp")) mask |= (DBG_REG_SP);

		char* p;
		if((p = strstr(str, "de")) && p[2] != 'c') mask |= (DBG_REG_D | DBG_REG_E);

		switch(*op){
			case 0x07: // rlca
			case 0x0f: // rrca
			case 0x17: // rla
			case 0x1f: // rra
			case 0x27: // daa
			case 0x2f: // cpl
				mask |= DBG_REG_A;
				break;
			case 0x37: // scf
			case 0x3f: // ccf

				// might as well highlight flags for conditional jumps too
			case 0x20:
			case 0x28:
			case 0x30:
			case 0x38:
			case 0xc0:
			case 0xc2:
			case 0xc4:
			case 0xc8:
			case 0xca:
			case 0xcc:
			case 0xd0:
			case 0xd2:
			case 0xd4:
			case 0xd8:
			case 0xda:
			case 0xdc:
				mask |= DBG_REG_F;
				break;
		}

		*mask_out = mask;
	}

	char* p = strchr(str, '%');
	if(p){
		while(strchr("%+0", *p)){
			++p;
		}

		*len_out = (*p == '4') ? 3 : 2;
	} else if(*op == 0xcb){
		*len_out = 2;
	} else {
		*len_out = 1;
	}
}

static struct reg_info {
	const char* name;
	uint32_t mask;
} reg_by_offset[] = {
	[offsetof(struct regs, a)]    = { "A",  DBG_REG_A  },
	[offsetof(struct regs, b)]    = { "B",  DBG_REG_B  },
	[offsetof(struct regs, c)]    = { "C",  DBG_REG_C  },
	[offsetof(struct regs, d)]    = { "D",  DBG_REG_D  },
	[offsetof(struct regs, e)]    = { "E",  DBG_REG_E  },
	[offsetof(struct regs, a)-1]  = { "F",  DBG_REG_F  },
	[offsetof(struct regs, h)]    = { "H",  DBG_REG_H  },
	[offsetof(struct regs, l)]    = { "L",  DBG_REG_L  },
	[offsetof(struct regs, sp)]   = { "SP", DBG_REG_SP },
};

static void debug_print_colour_reg_16(uint32_t mask, void* regs, off_t offset){
	uint16_t a = ((uint16_t*)regs)[offset/2];
	uint16_t b = ((uint16_t*)&prev_regs)[offset/2];

	int name_colour
		= (mask & reg_by_offset[offset].mask)
		? colours[DBG_COLOUR_ACTIVE]
		: colours[DBG_COLOUR_PLAIN]
		;

	int val_colour
		= a == b
		? name_colour
		: colours[DBG_COLOUR_CHANGED]
		;

	printf("\e[%dm%s\e[0m:\e[%dm%04x\e[0m ",
		   name_colour,
		   reg_by_offset[offset].name,
		   val_colour,
		   a);
}

static void debug_print_colour_reg(uint32_t mask, void* regs, off_t offset){
	int val_colours[2];
	int name_colours[2];

	for(int i = 0; i < 2; ++i){
		uint8_t a = ((uint8_t*)regs)[offset+i];
		uint8_t b = ((uint8_t*)&prev_regs)[offset+i];

		name_colours[i]
			= (mask & reg_by_offset[offset+i].mask)
			? colours[DBG_COLOUR_ACTIVE]
			: colours[DBG_COLOUR_PLAIN]
			;

		val_colours[i]
			= a == b
			? name_colours[i]
			: colours[DBG_COLOUR_CHANGED]
			;
	}

	printf(
		"\e[%dm%s\e[%dm%s\e[0m:\e[%dm%02x\e[%dm%02x\e[0m ",
		name_colours[1],
		reg_by_offset[offset+1].name,
		name_colours[0],
		reg_by_offset[offset].name,
		val_colours[1],
		((uint8_t*)regs)[offset+1],
		val_colours[0],
		((uint8_t*)regs)[offset]
	);
}

void debug_dump(uint8_t* op, struct regs* regs){
	if(!cfg.debug_mode)
		return;

	uint32_t mask, len;
	debug_get_regs(op, &mask, &len);

	// opcode & operand bytes
	char op_str[10];
	{
		memset(op_str, ' ', sizeof(op_str)-1);
		op_str[9] = 0;

		char* p = op_str;
		for(uint32_t i = 0; i < len; ++i){
			sprintf(p, "%02hhx", op[i]);
			p[2] = ' ';
			p += 3;
		}
	}

	if(cfg.debug_mode >= 2){
		if(debug_is_jump(prev_op)){
			int c = (regs->pc != prev_regs.pc + prev_len)
				? colours[DBG_COLOUR_JUMP_TAKEN]
				: colours[DBG_COLOUR_JUMP_UNTAKEN]
				;

			printf("\e[%dm%04x\e[0m: ", c, regs->pc);
		} else {
			printf("%04x: ", regs->pc);
		}

		printf("%s| ", op_str);

		debug_print_colour_reg_16 (mask, regs, offsetof(struct regs, sp));
		debug_print_colour_reg    (mask, regs, offsetof(struct regs, af));
		debug_print_colour_reg    (mask, regs, offsetof(struct regs, bc));
		debug_print_colour_reg    (mask, regs, offsetof(struct regs, de));
		debug_print_colour_reg    (mask, regs, offsetof(struct regs, hl));

		printf("| ");
	} else {
		printf("%04x: %s| SP:%04x AF:%04x BC:%04x DE:%04x HL:%04x | ",
			   regs->pc, op_str, regs->sp, regs->af, regs->bc, regs->de, regs->hl);
	}

	char mnemomic[15];

	if(*op == 0xCB){
		size_t x = (op[1] >> 6);
		size_t y = (op[1] >> 3) & 7;
		size_t z = op[1] & 7;

		if(x == 0){
			snprintf(mnemomic, sizeof(mnemomic), "%s %s", cb_ops[y], cb_regnames[z]);
		} else {
			snprintf(mnemomic, sizeof(mnemomic), "%s %zu, %s", cb_ops[x + 7], y, cb_regnames[z]);
		}
	} else {
		snprintf(mnemomic, sizeof(mnemomic), opcodes[*op], *(uint16_t*)(op+1));
	}

	char* p = strchr(mnemomic, '[');
	int32_t addr = -1;

	if(p){
		switch(p[1]){
			case 'h': addr = regs->hl; break;
			case 'b': addr = regs->bc; break;
			case 'd': addr = regs->de; break;
			case '%': addr = *(uint16_t*)(op+1); break;
			case '$': {
				int lo = *op & 0xf;
				if(lo == 0){
					addr = 0xff00 + op[1];
				} else if(lo == 2){
					addr = 0xff00 + regs->c;
				} else {
					addr = *(uint16_t*)(op+1);
				}
				break;
			}
		}
	}

	if(cfg.debug_mode == 2){
		const char* colour = debug_is_jump(*op) ? "\e[1;34m" : "";

		if(addr != -1){
			printf("%s%-14s\e[0m | [%02x]\n", colour, mnemomic, mem[addr]);
		} else {
			printf("%s%-14s\e[0m |\n", colour, mnemomic);
		}
	} else {
		if(addr != -1){
			printf("%-14s | [%02x]\n", mnemomic, mem[addr]);
		} else {
			printf("%-14s |\n", mnemomic);
		}
	}

	prev_regs = *regs;
	prev_op = *op;
	prev_len = len;
}

void debug_separator(void){
	if(!cfg.debug_mode)
		return;

	puts("---------------+-----------------------------------------+----------------+-----");

	// XXX: not obvious that the function will do this...
	prev_op = 0;
	prev_len = 0;
}

void debug_msg(const char* fmt, ...){
	if(!cfg.debug_mode)
		return;

	va_list va;
	va_start(va, fmt);

	char msg[39];
	vsnprintf(msg, sizeof(msg), fmt, va);

	if(cfg.debug_mode == 2){
		printf("     \e[0;35m* * *     \e[0m+ \e[0;35m%-39s\e[0m +                +\n", msg);
	} else {
		printf("     * * *     + %-39s +                +\n", msg);
	}

	va_end(va);
}
