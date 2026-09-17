/* DimonVirtualCPU-64 Assembler (dimon-as)
 * RISC-V style 64-bit ISA with fixed 32-bit little-endian words.
 * All messages, mnemonics and identifiers are in English.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdint.h>
#include <inttypes.h>

#include "dimon64.h"

#define MAX_LINES 32768
#define MAX_LABELS 8192
#define MAX_LINE_LEN 2048
#define MAX_RELOCS 32768

typedef struct { char name[96]; uint64_t addr; } Label;
static Label labels[MAX_LABELS];
static int nlabels = 0;

typedef struct { char text[MAX_LINE_LEN]; int lineno; } Line;
static Line lines[MAX_LINES];
static int nlines = 0;

static int errors = 0;
static uint32_t relocations[MAX_RELOCS];
static uint32_t nrelocations = 0;
static int emit_dexe = 0;
static const char *dexe_name = NULL;

static void err(int lineno, const char *msg) {
    fprintf(stderr, "ASM error [line %d]: %s\n", lineno, msg);
    errors++;
}

/* ---------- helpers ---------- */
static void trim(char *s) {
    char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
}

static void strupper(char *s) {
    for (; *s; s++) *s = (char)toupper((unsigned char)*s);
}

/* Strip comment ';' or '#' or '//' outside quotes */
static void strip_comment(char *s) {
    int in_s = 0, in_d = 0;
    for (char *p = s; *p; p++) {
        if (*p == '\'' && !in_d) in_s = !in_s;
        else if (*p == '"' && !in_s) in_d = !in_d;
        else if (!in_s && !in_d) {
            if (*p == ';' || *p == '#') { *p = 0; break; }
            if (*p == '/' && *(p + 1) == '/') { *p = 0; break; }
        }
    }
}

/* Parse 64-bit number: dec, 0xHEX, 0bBIN, 0oOCT, 'c', char escapes. */
static int parse_number64(const char *s, int64_t *out) {
    char b[256];
    strncpy(b, s, sizeof(b) - 1); b[sizeof(b) - 1] = 0;
    trim(b);
    if (!b[0]) return 0;
    if (b[0] == '\'' && strlen(b) >= 3 && b[strlen(b) - 1] == '\'') {
        if (b[1] == '\\') {
            if (b[2] == 'n') { *out = 10; return 1; }
            if (b[2] == 't') { *out = 9; return 1; }
            if (b[2] == 'r') { *out = 13; return 1; }
            if (b[2] == '0') { *out = 0; return 1; }
            if (b[2] == '\\') { *out = '\\'; return 1; }
            if (b[2] == '\'') { *out = '\''; return 1; }
            *out = (unsigned char)b[2];
            return 1;
        }
        *out = (unsigned char)b[1];
        return 1;
    }
    int neg = 0;
    const char *p = b;
    if (*p == '-') { neg = 1; p++; }
    else if (*p == '+') p++;
    /* skip underscores for readability */
    char clean[256]; int ci = 0;
    for (const char *q = p; *q && ci < 250; q++) {
        if (*q != '_') clean[ci++] = *q;
    }
    clean[ci] = 0;
    long long v = 0;
    if (clean[0] == '0' && (clean[1] == 'x' || clean[1] == 'X')) {
        char *e; v = strtoll(clean, &e, 16);
        if (*e) return 0;
    } else if (clean[0] == '0' && (clean[1] == 'b' || clean[1] == 'B')) {
        v = 0;
        const char *q = clean + 2;
        if (!*q) return 0;
        while (*q == '0' || *q == '1') { v = v * 2 + (*q - '0'); q++; }
        if (*q) return 0;
    } else if (clean[0] == '0' && (clean[1] == 'o' || clean[1] == 'O')) {
        char *e; v = strtoll(clean + 2, &e, 8);
        if (*e) return 0;
    } else {
        char *e; v = strtoll(clean, &e, 0);
        if (*e) return 0;
    }
    if (neg) v = -v;
    *out = (int64_t)v;
    return 1;
}

static int valid_label(const char *s) {
    if (!s[0] || !(isalpha((unsigned char)s[0]) || s[0] == '_' || s[0] == '.')) return 0;
    for (const char *p = s + 1; *p; p++)
        if (!(isalnum((unsigned char)*p) || *p == '_' || *p == '.' || *p == '$')) return 0;
    return 1;
}

/* Register parsing: R0-R31, x0-x31, ABI names */
static int parse_reg(const char *s, int *out) {
    char b[64];
    strncpy(b, s, sizeof(b) - 1); b[sizeof(b) - 1] = 0;
    trim(b);
    if (!b[0]) return 0;
    /* R0..R31 / x0..x31 (case-insensitive for prefix) */
    if ((b[0] == 'R' || b[0] == 'r' || b[0] == 'X' || b[0] == 'x') && isdigit((unsigned char)b[1])) {
        char *e = NULL;
        long v = strtol(b + 1, &e, 10);
        if (e && *e == 0 && v >= 0 && v <= 31) { *out = (int)v; return 1; }
        return 0;
    }
    char low[64];
    strncpy(low, b, sizeof(low) - 1); low[sizeof(low) - 1] = 0;
    for (char *p = low; *p; p++) *p = (char)tolower((unsigned char)*p);
    struct { const char *n; int r; } tab[] = {
        {"zero", 0}, {"ra", 1}, {"sp", 2}, {"gp", 3}, {"tp", 4},
        {"t0", 5}, {"t1", 6}, {"t2", 7},
        {"s0", 8}, {"fp", 8}, {"s1", 9},
        {"a0", 10}, {"a1", 11}, {"a2", 12}, {"a3", 13},
        {"a4", 14}, {"a5", 15}, {"a6", 16}, {"a7", 17},
        {"s2", 18}, {"s3", 19}, {"s4", 20}, {"s5", 21},
        {"s6", 22}, {"s7", 23}, {"s8", 24}, {"s9", 25},
        {"s10", 26}, {"s11", 27},
        {"t3", 28}, {"t4", 29}, {"t5", 30}, {"t6", 31},
        {NULL, -1}
    };
    for (int i = 0; tab[i].n; i++) {
        if (!strcmp(low, tab[i].n)) { *out = tab[i].r; return 1; }
    }
    return 0;
}

static Label *find_label(const char *name) {
    for (int i = 0; i < nlabels; i++)
        if (!strcmp(labels[i].name, name)) return &labels[i];
    return NULL;
}

static void add_label(const char *name, uint64_t addr, int lineno) {
    char tmp[96];
    strncpy(tmp, name, sizeof(tmp) - 1); tmp[sizeof(tmp) - 1] = 0;
    /* labels are case-sensitive; strip trailing colon if present */
    size_t L = strlen(tmp);
    if (L && tmp[L - 1] == ':') tmp[L - 1] = 0;
    if (!valid_label(tmp)) return;
    if (find_label(tmp)) {
        char m[160]; snprintf(m, sizeof(m), "duplicate label '%s'", tmp);
        err(lineno, m);
        return;
    }
    if (nlabels >= MAX_LABELS) { err(lineno, "too many labels"); return; }
    snprintf(labels[nlabels].name, sizeof(labels[nlabels].name), "%s", tmp);
    labels[nlabels].addr = addr;
    nlabels++;
}

/* Split line into label, mnemonic, rest */
static void split_line(char *line, char *lab, char *mnem, char *rest) {
    lab[0] = 0; mnem[0] = 0; rest[0] = 0;
    trim(line);
    if (!line[0]) return;
    /* label with colon */
    char *colon = strchr(line, ':');
    if (colon) {
        size_t n = (size_t)(colon - line);
        char tmp[128];
        if (n < sizeof(tmp)) {
            strncpy(tmp, line, n); tmp[n] = 0; trim(tmp);
            /* label must be single token (no spaces) */
            if (tmp[0] && !strchr(tmp, ' ') && !strchr(tmp, '\t') && valid_label(tmp)) {
                strcpy(lab, tmp);
                memmove(line, colon + 1, strlen(colon + 1) + 1);
                trim(line);
            }
        }
    }
    if (!line[0]) return;
    /* directives starting with '.' have no label split issue; parse mnemonic */
    char *p = line;
    while (*p && !isspace((unsigned char)*p)) p++;
    size_t n = (size_t)(p - line);
    if (n >= 127) n = 127;
    strncpy(mnem, line, n); mnem[n] = 0;
    if (*p) strcpy(rest, p + 1);
    trim(rest);
    /* bare label without colon? e.g. "loop ADD ..." is not supported; require colon. */
}

/* Split operands by comma, respecting parens and quotes */
static int split_operands(char *rest, char ops[4][512]) {
    if (!rest[0]) return 0;
    int n = 0, ci = 0, depth = 0, in_s = 0, in_d = 0;
    char cur[512]; ci = 0;
    for (char *p = rest; ; p++) {
        char c = *p;
        int end = (c == 0);
        if (c == '\'' && !in_d) in_s = !in_s;
        else if (c == '"' && !in_s) in_d = !in_d;
        else if (!in_s && !in_d) {
            if (c == '(') depth++;
            else if (c == ')') depth--;
        }
        if ((c == ',' && !in_s && !in_d && depth == 0) || end) {
            cur[ci] = 0; trim(cur);
            if (n < 4) { strcpy(ops[n++], cur); }
            ci = 0;
            if (end) break;
        } else {
            if (ci < 500) cur[ci++] = c;
        }
    }
    return n;
}

/* Resolve symbol expression: number | label | label+offset | label-offset */
static int resolve_expr(const char *s, uint64_t pc, int64_t *out_val, int lineno) {
    char b[512];
    strncpy(b, s, sizeof(b) - 1); b[sizeof(b) - 1] = 0;
    trim(b);
    if (!b[0]) return 0;
    int64_t num = 0;
    if (parse_number64(b, &num)) { *out_val = num; return 1; }
    /* look for + or - separating label and offset (skip leading sign) */
    char *plus = NULL, *minus = NULL;
    /* find last + / - outside quotes */
    int in_s = 0, in_d = 0;
    for (char *p = b + 1; *p; p++) {
        if (*p == '\'') in_s = !in_s;
        else if (*p == '"') in_d = !in_d;
        else if (!in_s && !in_d) {
            if (*p == '+') plus = p;
            else if (*p == '-') minus = p;
        }
    }
    char *sep = NULL;
    if (plus && minus) sep = (plus > minus) ? plus : minus;
    else sep = plus ? plus : minus;
    /* Heuristic: if string contains '(' it is a memory operand, not expr */
    if (strchr(b, '(')) sep = NULL;
    if (sep) {
        char op = *sep;
        *sep = 0;
        char left[256], right[256];
        strncpy(left, b, sizeof(left) - 1); left[sizeof(left) - 1] = 0;
        strncpy(right, sep + 1, sizeof(right) - 1); right[sizeof(right) - 1] = 0;
        trim(left); trim(right);
        int64_t off = 0;
        if (!parse_number64(right, &off)) {
            if (lineno > 0) {
                char m[300]; snprintf(m, sizeof(m), "invalid offset in expression '%s'", s);
                err(lineno, m);
            }
            return 0;
        }
        Label *l = find_label(left);
        if (!l) {
            /* maybe numeric left? */
            int64_t lv = 0;
            if (parse_number64(left, &lv)) {
                *out_val = (op == '+') ? (lv + off) : (lv - off);
                return 1;
            }
            if (lineno > 0) {
                char m[300]; snprintf(m, sizeof(m), "unknown label '%s'", left);
                err(lineno, m);
            }
            return 0;
        }
        *out_val = (op == '+') ? ((int64_t)l->addr + off) : ((int64_t)l->addr - off);
        return 1;
    }
    /* plain label? also handle '.' (current pc)? */
    if (!strcmp(b, ".")) { *out_val = (int64_t)pc; return 1; }
    Label *l = find_label(b);
    if (l) { *out_val = (int64_t)l->addr; return 1; }
    /* PC-relative? no */
    if (lineno > 0) {
        char m[300]; snprintf(m, sizeof(m), "unknown symbol '%.100s'", b);
        err(lineno, m);
    }
    return 0;
}

/* Parse mem operand: offset(rs1) or (rs1) */
static int parse_mem(const char *s, int *rs1_out, int64_t *off_out, uint64_t pc, int lineno, int need_label_ok) {
    char b[512];
    strncpy(b, s, sizeof(b) - 1); b[sizeof(b) - 1] = 0;
    trim(b);
    char *lp = strchr(b, '(');
    char *rp = strchr(b, ')');
    if (!lp || !rp || rp < lp) return 0;
    *lp = 0;
    *rp = 0;
    char offs[256], regs[128];
    strncpy(offs, b, sizeof(offs) - 1); offs[sizeof(offs) - 1] = 0;
    strncpy(regs, lp + 1, sizeof(regs) - 1); regs[sizeof(regs) - 1] = 0;
    trim(offs); trim(regs);
    int rs1 = 0;
    if (!parse_reg(regs, &rs1)) return 0;
    int64_t off = 0;
    if (!offs[0]) off = 0;
    else if (parse_number64(offs, &off)) { }
    else {
        if (!need_label_ok) return 0;
        if (!resolve_expr(offs, pc, &off, lineno)) return 0;
    }
    *rs1_out = rs1;
    *off_out = off;
    return 1;
}

/* ---------- mnemonic classification ---------- */
static int is_rtype(const char *m) {
    return (!strcmp(m, "ADD") || !strcmp(m, "SUB") || !strcmp(m, "AND") ||
            !strcmp(m, "OR") || !strcmp(m, "XOR") || !strcmp(m, "SLL") ||
            !strcmp(m, "SRL") || !strcmp(m, "SRA") || !strcmp(m, "SLT") ||
            !strcmp(m, "SLTU") || !strcmp(m, "MUL") || !strcmp(m, "DIV") ||
            !strcmp(m, "DIVU") || !strcmp(m, "REM"));
}
static int is_itype_imm(const char *m) {
    return (!strcmp(m, "ADDI") || !strcmp(m, "ANDI") || !strcmp(m, "ORI") ||
            !strcmp(m, "XORI") || !strcmp(m, "SLTI") || !strcmp(m, "SLTIU") ||
            !strcmp(m, "SLLI") || !strcmp(m, "SRLI") || !strcmp(m, "SRAI"));
}
static int is_load(const char *m) {
    return (!strcmp(m, "LB") || !strcmp(m, "LH") || !strcmp(m, "LW") ||
            !strcmp(m, "LD") || !strcmp(m, "LBU") || !strcmp(m, "LHU") ||
            !strcmp(m, "LWU"));
}
static int is_store(const char *m) {
    return (!strcmp(m, "SB") || !strcmp(m, "SH") || !strcmp(m, "SW") ||
            !strcmp(m, "SD"));
}
static int is_branch(const char *m) {
    return (!strcmp(m, "BEQ") || !strcmp(m, "BNE") || !strcmp(m, "BLT") ||
            !strcmp(m, "BGE") || !strcmp(m, "BLTU") || !strcmp(m, "BGEU") ||
            !strcmp(m, "BLE") || !strcmp(m, "BGT") || !strcmp(m, "BLEU") || !strcmp(m, "BGTU"));
}
static int is_directive(const char *m) {
    return (m[0] == '.' || !strcmp(m, "DB") || !strcmp(m, "DW") ||
            !strcmp(m, "DS") || !strcmp(m, "RESB") || !strcmp(m, "RESW"));
}

/* LI size helper: must exactly match emit_li_seq below */
static int64_t li_hi_part(int64_t v) {
    int64_t t = v + 0x800;
    int64_t hi;
    if (t >= 0) hi = t >> 12;
    else hi = -((-t + 4095) / 4096);
    int64_t lo = v - hi * 4096;
    while (lo < -2048) { hi--; lo += 4096; }
    while (lo > 2047) { hi++; lo -= 4096; }
    return hi;
}
static int li_size_for(int64_t v) {
    if (v >= -2048 && v <= 2047) return 1;
    if (v >= INT32_MIN && v <= INT32_MAX) return 2;
    return li_size_for(li_hi_part(v)) + 2;
}

/* Instruction size in bytes (pass1). pc = current address. */
static int instr_size(const char *mnem_raw, const char *rest_raw, uint64_t pc, int lineno) {
    char m[64];
    strncpy(m, mnem_raw, sizeof(m) - 1); m[sizeof(m) - 1] = 0; strupper(m);
    char rest[1024];
    strncpy(rest, rest_raw, sizeof(rest) - 1); rest[sizeof(rest) - 1] = 0;

    if (!m[0]) return 0;
    /* directives */
    if (!strcmp(m, ".ORG") || !strcmp(m, "ORG")) return 0;
    if (!strcmp(m, ".TEXT") || !strcmp(m, ".DATA") || !strcmp(m, ".GLOBL") ||
        !strcmp(m, ".GLOBAL") || !strcmp(m, ".OPTION") || !strcmp(m, ".OPTION")) return 0;
    if (!strcmp(m, ".EQU") || !strcmp(m, ".SET")) return 0;
    if (!strcmp(m, ".ALIGN") || !strcmp(m, ".BALIGN") || !strcmp(m, ".P2ALIGN")) {
        int64_t v = 4;
        if (rest[0]) {
            if (!parse_number64(rest, &v)) { err(lineno, ".align requires a number"); return -1; }
            if (!strcmp(m, ".ALIGN") || !strcmp(m, ".P2ALIGN")) {
                /* RISC-V .align N means 2^N bytes; but also accept byte count.
                   Heuristic: if v <= 12 treat as power-of-two. */
                if (v >= 0 && v <= 12) v = (int64_t)(1ULL << v);
            }
            if (v <= 0) v = 4;
        }
        uint64_t np = (pc + (uint64_t)v - 1) & ~((uint64_t)v - 1);
        /* only valid if v is power of two; else align to v */
        if (v & (v - 1)) np = ((pc + (uint64_t)v - 1) / (uint64_t)v) * (uint64_t)v;
        return (int)(np - pc);
    }
    if (!strcmp(m, ".SPACE") || !strcmp(m, ".ZERO") || !strcmp(m, ".SKIP") ||
        !strcmp(m, "DS") || !strcmp(m, "RESB")) {
        int64_t v = 0;
        /* allow "N, fill"? take first number */
        char tmp[512]; strcpy(tmp, rest);
        char *c = strchr(tmp, ','); if (c) *c = 0;
        if (!parse_number64(tmp, &v) || v < 0) { err(lineno, ".space requires a non-negative size"); return -1; }
        return (int)v;
    }
    if (!strcmp(m, "RESW")) {
        int64_t v = 0;
        if (!parse_number64(rest, &v) || v < 0) { err(lineno, "RESW requires a number"); return -1; }
        return (int)(v * 4);
    }
    if (!strcmp(m, ".BYTE") || !strcmp(m, "DB")) {
        if (!rest[0]) { err(lineno, ".byte requires operands"); return -1; }
        /* count items: strings count as strlen, others 1 */
        int count = 0;
        int in_s = 0, in_d = 0;
        char cur[1024]; int ci = 0;
        char tmp[2048]; snprintf(tmp, sizeof(tmp), "%s,", rest);
        for (char *p = tmp; ; p++) {
            char c = *p; int end = (c == 0);
            if (c == '\'' && !in_d) in_s = !in_s;
            else if (c == '"' && !in_s) in_d = !in_d;
            if ((c == ',' && !in_s && !in_d) || end) {
                cur[ci] = 0; trim(cur);
                if (cur[0] == '"' && strlen(cur) >= 2 && cur[strlen(cur) - 1] == '"') {
                    /* parse escapes length */
                    int sl = 0;
                    for (size_t i = 1; i < strlen(cur) - 1; i++) {
                        if (cur[i] == '\\') { i++; }
                        sl++;
                    }
                    count += sl;
                } else if (cur[0]) count += 1;
                ci = 0;
                if (end) break;
            } else { if (ci < 1000) cur[ci++] = c; }
        }
        return count;
    }
    if (!strcmp(m, ".HALF") || !strcmp(m, ".SHORT")) {
        if (!rest[0]) return 0;
        int n = 1;
        for (const char *p = rest; *p; p++) if (*p == ',') n++;
        return n * 2;
    }
    if (!strcmp(m, ".WORD") || !strcmp(m, "DW")) {
        if (!rest[0]) return 0;
        int n = 1;
        int in_s = 0, in_d = 0;
        for (const char *p = rest; *p; p++) {
            if (*p == '\'') in_s = !in_s;
            else if (*p == '"') in_d = !in_d;
            else if (*p == ',' && !in_s && !in_d) n++;
        }
        return n * 4;
    }
    if (!strcmp(m, ".DWORD") || !strcmp(m, ".QUAD")) {
        if (!rest[0]) return 0;
        int n = 1;
        for (const char *p = rest; *p; p++) if (*p == ',') n++;
        return n * 8;
    }
    if (!strcmp(m, ".STRING") || !strcmp(m, ".ASCIZ") || !strcmp(m, ".ASCII")) {
        /* "str", "str2" ... */
        int total = 0;
        const char *p = rest;
        while (*p) {
            while (*p && isspace((unsigned char)*p)) p++;
            if (*p != '"') {
                /* allow single-quoted char? */
                if (*p == 0) break;
                err(lineno, ".string requires quoted strings");
                return -1;
            }
            p++;
            int sl = 0;
            while (*p && *p != '"') {
                if (*p == '\\') { p++; if (*p) p++; }
                else p++;
                sl++;
            }
            if (*p == '"') p++;
            total += sl;
            while (*p && isspace((unsigned char)*p)) p++;
            if (*p == ',') { p++; continue; }
            else break;
        }
        if (!strcmp(m, ".ASCII")) return total;
        return total + 1; /* NUL (one per directive; for multiple strings, NUL only at end? use per-directive) */
    }

    /* real instructions: align pc to 4 first? padding handled by caller.
       Here return size assuming pc already aligned. */
    if (!strcmp(m, "NOP") || !strcmp(m, "MV") || !strcmp(m, "J") ||
        !strcmp(m, "JR") || !strcmp(m, "RET") || !strcmp(m, "CALL") ||
        !strcmp(m, "TAIL") || !strcmp(m, "HLT") || !strcmp(m, "HALT") ||
        !strcmp(m, "ECALL") || !strcmp(m, "EBREAK") || !strcmp(m, "IRET") ||
        !strcmp(m, "SRET") || !strcmp(m, "MRET") || !strcmp(m, "INT") ||
        !strcmp(m, "NEG") || !strcmp(m, "NOT") || !strcmp(m, "SEQZ") ||
        !strcmp(m, "SNEZ")) {
        if (!strcmp(m, "LI") || !strcmp(m, "LA")) { /* handled below */ }
        else if (!strcmp(m, "MV") || !strcmp(m, "NOT") || !strcmp(m, "NEG") ||
                 !strcmp(m, "SEQZ") || !strcmp(m, "SNEZ")) return 4;
        else if (!strcmp(m, "J") || !strcmp(m, "JR") || !strcmp(m, "RET") ||
                 !strcmp(m, "CALL") || !strcmp(m, "TAIL") || !strcmp(m, "HLT") ||
                 !strcmp(m, "HALT") || !strcmp(m, "NOP") || !strcmp(m, "ECALL") ||
                 !strcmp(m, "EBREAK") || !strcmp(m, "IRET") || !strcmp(m, "SRET") ||
                 !strcmp(m, "MRET") || !strcmp(m, "INT")) return 4;
    }
    if (!strcmp(m, "LI")) {
        char ops[4][512]; char tmp[1024]; strcpy(tmp, rest);
        if (split_operands(tmp, ops) != 2) { err(lineno, "LI requires 2 operands"); return -1; }
        int64_t v = 0;
        char immb[512]; strcpy(immb, ops[1]); trim(immb);
        if (parse_number64(immb, &v)) return li_size_for(v) * 4;
        /* label: assume worst case 8 bytes (LUI+ADDI) for 32-bit addresses,
           but if 64-bit label? addresses < 64MB so 8 is enough. */
        return 8;
    }
    if (!strcmp(m, "LA")) return 8;
    if (is_rtype(m) || is_itype_imm(m) || is_load(m) || is_store(m) ||
        is_branch(m) || !strcmp(m, "LUI") || !strcmp(m, "AUIPC") ||
        !strcmp(m, "JAL") || !strcmp(m, "JALR")) {
        return 4;
    }
    if (!strcmp(m, "PUSH") || !strcmp(m, "POP") || !strcmp(m, "MOV") ||
        !strcmp(m, "CMP") || !strcmp(m, "JMP") || !strcmp(m, "JZ") ||
        !strcmp(m, "JNZ") || !strcmp(m, "CALL16") || !strcmp(m, "IN") ||
        !strcmp(m, "OUT") || !strcmp(m, "LDB") || !strcmp(m, "STB") ||
        !strcmp(m, "INC") || !strcmp(m, "DEC") || !strcmp(m, "MUL16")) {
        char msg[128]; snprintf(msg, sizeof(msg),
            "legacy 16-bit mnemonic '%s' is not supported in 64-bit mode", m);
        err(lineno, msg);
        return -1;
    }
    char msg[128]; snprintf(msg, sizeof(msg), "unknown mnemonic '%s'", mnem_raw);
    err(lineno, msg);
    return -1;
}

/* ---------- emission ---------- */
static uint8_t *img = NULL;
static uint64_t img_cap = 0;
static uint64_t cur = 0, himark = 0;

static void ensure_cap(uint64_t need) {
    if (need <= img_cap) return;
    uint64_t ncap = img_cap ? img_cap * 2 : 65536;
    while (ncap < need) ncap *= 2;
    uint8_t *n = (uint8_t *)realloc(img, (size_t)ncap);
    if (!n) { fprintf(stderr, "Out of memory\n"); exit(1); }
    memset(n + img_cap, 0, (size_t)(ncap - img_cap));
    img = n;
    img_cap = ncap;
}

static void emit8(uint8_t v) {
    ensure_cap(cur + 1);
    img[cur++] = v;
    if (cur > himark) himark = cur;
}
static void emit32le(uint32_t w) {
    emit8((uint8_t)(w & 0xFF));
    emit8((uint8_t)((w >> 8) & 0xFF));
    emit8((uint8_t)((w >> 16) & 0xFF));
    emit8((uint8_t)((w >> 24) & 0xFF));
}
static void emit64le(uint64_t w) {
    for (int i = 0; i < 8; i++) emit8((uint8_t)((w >> (8 * i)) & 0xFF));
}
static void emit16le(uint16_t w) { emit8((uint8_t)(w & 0xFF)); emit8((uint8_t)((w >> 8) & 0xFF)); }

static void align_cur(uint64_t a) {
    if (a == 0) return;
    while ((cur % a) != 0) emit8(0);
}

/* forward */
static void emit_li_seq(int rd, int64_t v);

static void emit_r(int rd, int rs1, int rs2, uint8_t f3, uint8_t f7) {
    uint32_t w = dimon64_encode_r((uint8_t)rd, f3, (uint8_t)rs1, (uint8_t)rs2, f7,
                                  DIMON64_OPCODE_OP);
    emit32le(w);
}
static void emit_i_alu(int rd, int rs1, int64_t imm, uint8_t f3) {
    uint32_t w = dimon64_encode_i((uint8_t)rd, f3, (uint8_t)rs1, (int32_t)imm,
                                  DIMON64_OPCODE_OP_IMM);
    emit32le(w);
}
static void emit_shift_imm(int rd, int rs1, int shamt, uint8_t f3, uint8_t f7imm) {
    int32_t imm = (int32_t)(((f7imm & 0x7F) << 5) | (shamt & 0x3F));
    /* for RV64, bit 30 selects SRAI; imm[11:5] holds funct */
    uint32_t w = dimon64_encode_i((uint8_t)rd, f3, (uint8_t)rs1, imm,
                                  DIMON64_OPCODE_OP_IMM);
    emit32le(w);
}

static void emit_li_seq(int rd, int64_t v) {
    if (v >= -2048 && v <= 2047) {
        emit_i_alu(rd, 0, v, DIMON64_F3_ADDI);
        return;
    }
    if (v >= INT32_MIN && v <= INT32_MAX) {
        int64_t hi_s = li_hi_part(v);
        int64_t lo = v - hi_s * 4096;
        int32_t hi20 = (int32_t)(hi_s & 0xFFFFF);
        uint32_t w = dimon64_encode_u((uint8_t)rd, hi20, DIMON64_OPCODE_LUI);
        emit32le(w);
        emit_i_alu(rd, rd, lo, DIMON64_F3_ADDI);
        return;
    }
    /* 64-bit recursive */
    int64_t hi = li_hi_part(v);
    int64_t lo = v - hi * 4096;
    emit_li_seq(rd, hi);
    emit_shift_imm(rd, rd, 12, DIMON64_F3_SLLI, 0x00);
    emit_i_alu(rd, rd, lo, DIMON64_F3_ADDI);
}

static int r_f3(const char *m) {
    if (!strcmp(m, "ADD") || !strcmp(m, "SUB") || !strcmp(m, "MUL")) return 0x0;
    if (!strcmp(m, "SLL")) return 0x1;
    if (!strcmp(m, "SLT")) return 0x2;
    if (!strcmp(m, "SLTU")) return 0x3;
    if (!strcmp(m, "XOR") || !strcmp(m, "DIV")) return 0x4;
    if (!strcmp(m, "SRL") || !strcmp(m, "SRA") || !strcmp(m, "DIVU")) return 0x5;
    if (!strcmp(m, "OR") || !strcmp(m, "REM")) return 0x6;
    if (!strcmp(m, "AND")) return 0x7;
    return 0;
}
static int r_f7(const char *m) {
    if (!strcmp(m, "SUB") || !strcmp(m, "SRA")) return DIMON64_F7_ALT;
    if (!strcmp(m, "MUL") || !strcmp(m, "DIV") || !strcmp(m, "DIVU") ||
        !strcmp(m, "REM")) return DIMON64_F7_MEXT;
    return DIMON64_F7_BASE;
}
static int i_f3(const char *m) {
    if (!strcmp(m, "ADDI")) return 0x0;
    if (!strcmp(m, "SLLI")) return 0x1;
    if (!strcmp(m, "SLTI")) return 0x2;
    if (!strcmp(m, "SLTIU")) return 0x3;
    if (!strcmp(m, "XORI")) return 0x4;
    if (!strcmp(m, "SRLI") || !strcmp(m, "SRAI")) return 0x5;
    if (!strcmp(m, "ORI")) return 0x6;
    if (!strcmp(m, "ANDI")) return 0x7;
    return 0;
}
static int load_f3(const char *m) {
    if (!strcmp(m, "LB")) return DIMON64_F3_LB;
    if (!strcmp(m, "LH")) return DIMON64_F3_LH;
    if (!strcmp(m, "LW")) return DIMON64_F3_LW;
    if (!strcmp(m, "LD")) return DIMON64_F3_LD;
    if (!strcmp(m, "LBU")) return DIMON64_F3_LBU;
    if (!strcmp(m, "LHU")) return DIMON64_F3_LHU;
    if (!strcmp(m, "LWU")) return DIMON64_F3_LWU;
    return 0;
}
static int store_f3(const char *m) {
    if (!strcmp(m, "SB")) return DIMON64_F3_SB;
    if (!strcmp(m, "SH")) return DIMON64_F3_SH;
    if (!strcmp(m, "SW")) return DIMON64_F3_SW;
    return DIMON64_F3_SD;
}
static int branch_f3(const char *m) {
    if (!strcmp(m, "BEQ")) return DIMON64_F3_BEQ;
    if (!strcmp(m, "BNE")) return DIMON64_F3_BNE;
    if (!strcmp(m, "BLT") || !strcmp(m, "BGT")) return DIMON64_F3_BLT;
    if (!strcmp(m, "BGE") || !strcmp(m, "BLE")) return DIMON64_F3_BGE;
    if (!strcmp(m, "BLTU") || !strcmp(m, "BGTU")) return DIMON64_F3_BLTU;
    return DIMON64_F3_BGEU;
}

/* Unescape string content into buffer, returns length */
static int unescape(const char *src, size_t slen, char *dst, size_t cap) {
    size_t di = 0;
    for (size_t i = 0; i < slen && di + 1 < cap; i++) {
        if (src[i] == '\\' && i + 1 < slen) {
            i++;
            switch (src[i]) {
                case 'n': dst[di++] = '\n'; break;
                case 't': dst[di++] = '\t'; break;
                case 'r': dst[di++] = '\r'; break;
                case '0': dst[di++] = '\0'; break;
                case '"': dst[di++] = '"'; break;
                case '\'': dst[di++] = '\''; break;
                case '\\': dst[di++] = '\\'; break;
                default: dst[di++] = src[i]; break;
            }
        } else {
            dst[di++] = src[i];
        }
    }
    return (int)di;
}

static void pass2_emit(const char *mnem_raw, const char *rest_raw, uint64_t pc, int lineno) {
    char m[64];
    strncpy(m, mnem_raw, sizeof(m) - 1); m[sizeof(m) - 1] = 0; strupper(m);
    char rest[2048];
    strncpy(rest, rest_raw, sizeof(rest) - 1); rest[sizeof(rest) - 1] = 0;
    if (!m[0]) return;

    if (!strcmp(m, ".ORG") || !strcmp(m, "ORG")) {
        int64_t v = 0;
        if (parse_number64(rest, &v)) {
            if (v < 0) { err(lineno, ".org address must be non-negative"); return; }
            if ((uint64_t)v < cur) {
                /* allow backward org only if within emitted image (fill)? */
            }
            ensure_cap((uint64_t)v);
            while (cur < (uint64_t)v) emit8(0);
            if ((uint64_t)v > himark) himark = (uint64_t)v;
        } else {
            int64_t ev = 0;
            if (resolve_expr(rest, pc, &ev, lineno)) {
                ensure_cap((uint64_t)ev);
                while (cur < (uint64_t)ev) emit8(0);
            } else err(lineno, ".org requires an address");
        }
        return;
    }
    if (!strcmp(m, ".TEXT") || !strcmp(m, ".DATA")) return;
    if (!strcmp(m, ".GLOBL") || !strcmp(m, ".GLOBAL") || !strcmp(m, ".OPTION")) return;
    if (!strcmp(m, ".EQU") || !strcmp(m, ".SET")) {
        /* .equ name, value : define label as absolute value */
        char ops[4][512]; char tmp[1024]; strcpy(tmp, rest);
        if (split_operands(tmp, ops) != 2) { err(lineno, ".equ requires name and value"); return; }
        trim(ops[0]); trim(ops[1]);
        int64_t v = 0;
        if (parse_number64(ops[1], &v)) {
            add_label(ops[0], (uint64_t)v, lineno);
        } else {
            int64_t ev = 0;
            if (resolve_expr(ops[1], pc, &ev, 0)) add_label(ops[0], (uint64_t)ev, lineno);
            else err(lineno, ".equ invalid value");
        }
        return;
    }
    if (!strcmp(m, ".ALIGN") || !strcmp(m, ".BALIGN") || !strcmp(m, ".P2ALIGN")) {
        int64_t v = 4;
        if (rest[0]) {
            char tmp[512]; strcpy(tmp, rest);
            char *c = strchr(tmp, ','); if (c) *c = 0;
            trim(tmp);
            if (!parse_number64(tmp, &v)) { err(lineno, ".align requires a number"); return; }
            if (!strcmp(m, ".ALIGN") || !strcmp(m, ".P2ALIGN")) {
                if (v >= 0 && v <= 12) v = (int64_t)(1ULL << v);
            }
        }
        if (v <= 0) v = 4;
        uint64_t a = (uint64_t)v;
        uint64_t np;
        if ((a & (a - 1)) == 0) np = (cur + a - 1) & ~(a - 1);
        else np = ((cur + a - 1) / a) * a;
        while (cur < np) emit8(0);
        return;
    }
    if (!strcmp(m, ".SPACE") || !strcmp(m, ".ZERO") || !strcmp(m, ".SKIP") ||
        !strcmp(m, "DS") || !strcmp(m, "RESB")) {
        char tmp[512]; strcpy(tmp, rest);
        char *c = strchr(tmp, ','); int fill = 0;
        if (c) { *c = 0; parse_number64(c + 1, &(int64_t){0}); fill = 0; }
        trim(tmp);
        int64_t v = 0;
        if (!parse_number64(tmp, &v)) { err(lineno, ".space requires a size"); return; }
        for (int64_t i = 0; i < v; i++) emit8((uint8_t)fill);
        return;
    }
    if (!strcmp(m, "RESW")) {
        int64_t v = 0;
        if (!parse_number64(rest, &v)) return;
        for (int64_t i = 0; i < v * 4; i++) emit8(0);
        return;
    }
    if (!strcmp(m, ".BYTE") || !strcmp(m, "DB")) {
        int in_s = 0, in_d = 0;
        char curtok[1024]; int ci = 0;
        char tmp[4096]; snprintf(tmp, sizeof(tmp), "%s,", rest);
        for (char *p = tmp; ; p++) {
            char c = *p; int end = (c == 0);
            if (c == '\'' && !in_d) in_s = !in_s;
            else if (c == '"' && !in_s) in_d = !in_d;
            if ((c == ',' && !in_s && !in_d) || end) {
                curtok[ci] = 0; trim(curtok);
                if (curtok[0] == '"' && strlen(curtok) >= 2 && curtok[strlen(curtok) - 1] == '"') {
                    char ub[1024];
                    int L = unescape(curtok + 1, strlen(curtok) - 2, ub, sizeof(ub));
                    for (int i = 0; i < L; i++) emit8((uint8_t)ub[i]);
                } else if (curtok[0]) {
                    int64_t v = 0;
                    if (parse_number64(curtok, &v)) emit8((uint8_t)(v & 0xFF));
                    else {
                        int64_t ev = 0;
                        if (resolve_expr(curtok, pc, &ev, lineno)) emit8((uint8_t)(ev & 0xFF));
                        else err(lineno, "invalid .byte item");
                    }
                }
                ci = 0;
                if (end) break;
            } else { if (ci < 1000) curtok[ci++] = c; }
        }
        return;
    }
    if (!strcmp(m, ".HALF") || !strcmp(m, ".SHORT")) {
        char tmp[2048]; strcpy(tmp, rest);
        /* manual split by comma */
        char *tok = strtok(tmp, ",");
        while (tok) {
            trim(tok);
            if (tok[0]) {
                int64_t v = 0;
                if (parse_number64(tok, &v)) emit16le((uint16_t)(v & 0xFFFF));
                else {
                    int64_t ev = 0;
                    if (resolve_expr(tok, pc, &ev, lineno)) emit16le((uint16_t)(ev & 0xFFFF));
                    else err(lineno, "invalid .half item");
                }
            }
            tok = strtok(NULL, ",");
        }
        return;
    }
    if (!strcmp(m, ".WORD") || !strcmp(m, "DW")) {
        int in_s = 0, in_d = 0;
        char curtok[1024]; int ci = 0;
        char tmp[4096]; snprintf(tmp, sizeof(tmp), "%s,", rest);
        for (char *p = tmp; ; p++) {
            char c = *p; int end = (c == 0);
            if (c == '\'' && !in_d) in_s = !in_s;
            else if (c == '"' && !in_s) in_d = !in_d;
            if ((c == ',' && !in_s && !in_d) || end) {
                curtok[ci] = 0; trim(curtok);
                if (curtok[0]) {
                    int64_t v = 0;
                    if (parse_number64(curtok, &v)) {
                        uint32_t w = (uint32_t)(v & 0xFFFFFFFF);
                        emit8((uint8_t)(w & 0xFF)); emit8((uint8_t)((w >> 8) & 0xFF));
                        emit8((uint8_t)((w >> 16) & 0xFF)); emit8((uint8_t)((w >> 24) & 0xFF));
                    } else {
                        int64_t ev = 0;
                        if (resolve_expr(curtok, pc, &ev, lineno)) {
                            uint32_t w = (uint32_t)(ev & 0xFFFFFFFF);
                            emit8((uint8_t)(w & 0xFF)); emit8((uint8_t)((w >> 8) & 0xFF));
                            emit8((uint8_t)((w >> 16) & 0xFF)); emit8((uint8_t)((w >> 24) & 0xFF));
                        } else err(lineno, "invalid .word item");
                    }
                }
                ci = 0;
                if (end) break;
            } else { if (ci < 1000) curtok[ci++] = c; }
        }
        return;
    }
    if (!strcmp(m, ".DWORD") || !strcmp(m, ".QUAD")) {
        char tmp[4096]; snprintf(tmp, sizeof(tmp), "%s,", rest);
        int in_s = 0, in_d = 0;
        char curtok[1024]; int ci = 0;
        for (char *p = tmp; ; p++) {
            char c = *p; int end = (c == 0);
            if (c == '\'' && !in_d) in_s = !in_s;
            else if (c == '"' && !in_s) in_d = !in_d;
            if ((c == ',' && !in_s && !in_d) || end) {
                curtok[ci] = 0; trim(curtok);
                if (curtok[0]) {
                    int64_t v = 0;
                    if (parse_number64(curtok, &v)) emit64le((uint64_t)v);
                    else {
                        int64_t ev = 0;
                        if (resolve_expr(curtok, pc, &ev, lineno)) emit64le((uint64_t)ev);
                        else err(lineno, "invalid .quad item");
                    }
                }
                ci = 0;
                if (end) break;
            } else { if (ci < 1000) curtok[ci++] = c; }
        }
        return;
    }
    if (!strcmp(m, ".STRING") || !strcmp(m, ".ASCIZ") || !strcmp(m, ".ASCII")) {
        const char *p = rest;
        int first = 1;
        while (*p) {
            while (*p && isspace((unsigned char)*p)) p++;
            if (*p != '"') break;
            p++;
            char raw[2048]; int ri = 0;
            while (*p && *p != '"') {
                if (*p == '\\' && *(p + 1)) { raw[ri++] = *p++; raw[ri++] = *p++; }
                else raw[ri++] = *p++;
                if (ri > 2000) break;
            }
            if (*p == '"') p++;
            char ub[2048];
            int L = unescape(raw, (size_t)ri, ub, sizeof(ub));
            for (int i = 0; i < L; i++) emit8((uint8_t)ub[i]);
            first = 0;
            while (*p && isspace((unsigned char)*p)) p++;
            if (*p == ',') { p++; continue; }
            else break;
        }
        if (first) { err(lineno, ".string requires a quoted string"); return; }
        if (strcmp(m, ".ASCII") != 0) emit8(0);
        return;
    }

    /* instructions: ensure 4-byte alignment */
    align_cur(4);

    /* pseudos */
    if (!strcmp(m, "NOP")) { emit32le(0x00000013u); return; }
    if (!strcmp(m, "HLT") || !strcmp(m, "HALT")) {
        emit32le(dimon64_encode_i(0, 0, 0, DIMON64_SYS_EBREAK, DIMON64_OPCODE_SYSTEM));
        return;
    }
    if (!strcmp(m, "ECALL")) {
        emit32le(dimon64_encode_i(0, 0, 0, DIMON64_SYS_ECALL, DIMON64_OPCODE_SYSTEM));
        return;
    }
    if (!strcmp(m, "EBREAK")) {
        emit32le(dimon64_encode_i(0, 0, 0, DIMON64_SYS_EBREAK, DIMON64_OPCODE_SYSTEM));
        return;
    }
    if (!strcmp(m, "IRET") || !strcmp(m, "SRET") || !strcmp(m, "MRET")) {
        emit32le(dimon64_encode_i(0, 0, 0, DIMON64_SYS_IRET, DIMON64_OPCODE_SYSTEM));
        return;
    }
    if (!strcmp(m, "INT")) {
        int64_t v = 0;
        char ops[4][512]; char tmp[1024]; strcpy(tmp, rest);
        if (split_operands(tmp, ops) != 1) { err(lineno, "INT requires 1 operand"); return; }
        trim(ops[0]);
        if (!parse_number64(ops[0], &v)) {
            int64_t ev = 0;
            if (!resolve_expr(ops[0], pc, &ev, lineno)) return;
            v = ev;
        }
        if (v < 0 || v > 0xFFF) { err(lineno, "INT immediate range 0..4095"); return; }
        emit32le(dimon64_encode_i(0, 0, 0, (int32_t)v, DIMON64_OPCODE_SYSTEM));
        return;
    }
    if (!strcmp(m, "MV") || !strcmp(m, "NOT") || !strcmp(m, "NEG") ||
        !strcmp(m, "SEQZ") || !strcmp(m, "SNEZ")) {
        char ops[4][512]; char tmp[1024]; strcpy(tmp, rest);
        if (split_operands(tmp, ops) != 2 && strcmp(m, "MV") != 0) {
            /* SEQZ/SNEZ have 2 ops too */
            if (split_operands(tmp, ops) != 2) { err(lineno, "pseudo requires 2 operands"); return; }
        }
        if (!strcmp(m, "MV")) {
            if (split_operands(tmp, ops) != 2) { err(lineno, "MV requires 2 operands"); return; }
            int rd = 0, rs = 0;
            if (!parse_reg(ops[0], &rd) || !parse_reg(ops[1], &rs)) { err(lineno, "invalid register"); return; }
            emit_i_alu(rd, rs, 0, DIMON64_F3_ADDI);
            return;
        }
        if (!strcmp(m, "NOT")) {
            int rd = 0, rs = 0;
            if (!parse_reg(ops[0], &rd) || !parse_reg(ops[1], &rs)) { err(lineno, "invalid register"); return; }
            emit_i_alu(rd, rs, -1, DIMON64_F3_XORI);
            return;
        }
        if (!strcmp(m, "NEG")) {
            int rd = 0, rs = 0;
            if (!parse_reg(ops[0], &rd) || !parse_reg(ops[1], &rs)) { err(lineno, "invalid register"); return; }
            emit_r(rd, 0, rs, 0x0, DIMON64_F7_ALT); /* SUB rd, x0, rs */
            return;
        }
        if (!strcmp(m, "SEQZ")) {
            int rd = 0, rs = 0;
            if (!parse_reg(ops[0], &rd) || !parse_reg(ops[1], &rs)) { err(lineno, "invalid register"); return; }
            /* SLTIU rd, rs, 1 */
            emit_i_alu(rd, rs, 1, DIMON64_F3_SLTIU);
            return;
        }
        if (!strcmp(m, "SNEZ")) {
            int rd = 0, rs = 0;
            if (!parse_reg(ops[0], &rd) || !parse_reg(ops[1], &rs)) { err(lineno, "invalid register"); return; }
            /* SLTU rd, x0, rs */
            emit_r(rd, 0, rs, 0x3, DIMON64_F7_BASE);
            return;
        }
    }
    if (!strcmp(m, "LI")) {
        char ops[4][512]; char tmp[1024]; strcpy(tmp, rest);
        if (split_operands(tmp, ops) != 2) { err(lineno, "LI requires 2 operands"); return; }
        int rd = 0;
        if (!parse_reg(ops[0], &rd)) { err(lineno, "LI: invalid destination register"); return; }
        trim(ops[1]);
        int64_t v = 0;
        if (parse_number64(ops[1], &v)) { emit_li_seq(rd, v); return; }
        int64_t ev = 0;
        if (resolve_expr(ops[1], pc, &ev, lineno)) {
            /* label address: force 2-word LUI+ADDI */
            if (emit_dexe) {
                if (nrelocations >= MAX_RELOCS) { err(lineno, "too many DEXE relocations"); return; }
                relocations[nrelocations++] = (uint32_t)cur;
            }
            int32_t hi = (int32_t)(((ev + 0x800) >> 12) & 0xFFFFF);
            int32_t lo = (int32_t)(ev - ((int64_t)hi << 12));
            emit32le(dimon64_encode_u((uint8_t)rd, hi, DIMON64_OPCODE_LUI));
            emit_i_alu(rd, rd, lo, DIMON64_F3_ADDI);
            return;
        }
        return;
    }
    if (!strcmp(m, "LA")) {
        char ops[4][512]; char tmp[1024]; strcpy(tmp, rest);
        if (split_operands(tmp, ops) != 2) { err(lineno, "LA requires 2 operands"); return; }
        int rd = 0;
        if (!parse_reg(ops[0], &rd)) { err(lineno, "LA: invalid register"); return; }
        int64_t ev = 0;
        if (!resolve_expr(ops[1], pc, &ev, lineno)) return;
        if (emit_dexe) {
            if (nrelocations >= MAX_RELOCS) { err(lineno, "too many DEXE relocations"); return; }
            relocations[nrelocations++] = (uint32_t)cur;
        }
        int32_t hi = (int32_t)(((ev + 0x800) >> 12) & 0xFFFFF);
        int32_t lo = (int32_t)(ev - ((int64_t)hi << 12));
        emit32le(dimon64_encode_u((uint8_t)rd, hi, DIMON64_OPCODE_LUI));
        emit_i_alu(rd, rd, lo, DIMON64_F3_ADDI);
        return;
    }
    if (!strcmp(m, "J") || !strcmp(m, "JR") || !strcmp(m, "RET") ||
        !strcmp(m, "CALL") || !strcmp(m, "TAIL")) {
        if (!strcmp(m, "RET")) {
            emit32le(dimon64_encode_i(0, 0, 1, 0, DIMON64_OPCODE_JALR));
            return;
        }
        if (!strcmp(m, "JR")) {
            char ops[4][512]; char tmp[1024]; strcpy(tmp, rest);
            if (split_operands(tmp, ops) != 1) { err(lineno, "JR requires 1 operand"); return; }
            int rs = 0;
            if (!parse_reg(ops[0], &rs)) { err(lineno, "invalid register"); return; }
            emit32le(dimon64_encode_i(0, 0, (uint8_t)rs, 0, DIMON64_OPCODE_JALR));
            return;
        }
        /* J label / CALL label / TAIL label */
        char ops[4][512]; char tmp[1024]; strcpy(tmp, rest);
        if (split_operands(tmp, ops) != 1) { err(lineno, "jump requires 1 operand"); return; }
        int64_t target = 0;
        if (!resolve_expr(ops[0], pc, &target, lineno)) return;
        int64_t off = target - (int64_t)pc;
        if ((off & 1) || off < -(1 << 20) || off > ((1 << 20) - 2)) {
            char msg[160]; snprintf(msg, sizeof(msg), "jump target out of range (+/-1MB): '%.80s'", ops[0]);
            err(lineno, msg); return;
        }
        int rd = (!strcmp(m, "CALL")) ? 1 : 0;
        emit32le(dimon64_encode_j((uint8_t)rd, (int32_t)off, DIMON64_OPCODE_JAL));
        return;
    }

    /* real R-type */
    if (is_rtype(m)) {
        char ops[4][512]; char tmp[1024]; strcpy(tmp, rest);
        if (split_operands(tmp, ops) != 3) { err(lineno, "R-type requires 3 operands (rd, rs1, rs2)"); return; }
        int rd = 0, rs1 = 0, rs2 = 0;
        if (!parse_reg(ops[0], &rd) || !parse_reg(ops[1], &rs1) || !parse_reg(ops[2], &rs2)) {
            err(lineno, "invalid register in R-type"); return;
        }
        emit_r(rd, rs1, rs2, (uint8_t)r_f3(m), (uint8_t)r_f7(m));
        return;
    }
    if (is_itype_imm(m)) {
        char ops[4][512]; char tmp[1024]; strcpy(tmp, rest);
        if (split_operands(tmp, ops) != 3) { err(lineno, "I-type requires 3 operands (rd, rs1, imm)"); return; }
        int rd = 0, rs1 = 0;
        if (!parse_reg(ops[0], &rd) || !parse_reg(ops[1], &rs1)) { err(lineno, "invalid register"); return; }
        if (!strcmp(m, "SLLI") || !strcmp(m, "SRLI") || !strcmp(m, "SRAI")) {
            int64_t sh = 0;
            if (!parse_number64(ops[2], &sh) || sh < 0 || sh > 63) { err(lineno, "shift amount range 0..63"); return; }
            uint8_t f7 = (!strcmp(m, "SRAI")) ? 0x20 : 0x00;
            emit_shift_imm(rd, rs1, (int)sh, (uint8_t)i_f3(m), f7);
            return;
        }
        int64_t imm = 0;
        trim(ops[2]);
        if (parse_number64(ops[2], &imm)) { }
        else if (!resolve_expr(ops[2], pc, &imm, lineno)) return;
        if (imm < -2048 || imm > 2047) { err(lineno, "immediate range -2048..2047"); return; }
        emit_i_alu(rd, rs1, imm, (uint8_t)i_f3(m));
        return;
    }
    if (is_load(m)) {
        char ops[4][512]; char tmp[1024]; strcpy(tmp, rest);
        if (split_operands(tmp, ops) != 2) { err(lineno, "load requires 2 operands (rd, offset(rs1))"); return; }
        int rd = 0, rs1 = 0; int64_t off = 0;
        if (!parse_reg(ops[0], &rd)) { err(lineno, "invalid destination register"); return; }
        if (!parse_mem(ops[1], &rs1, &off, pc, lineno, 0)) { err(lineno, "invalid memory operand (use offset(rs1))"); return; }
        if (off < -2048 || off > 2047) { err(lineno, "load offset range -2048..2047"); return; }
        emit32le(dimon64_encode_i((uint8_t)rd, (uint8_t)load_f3(m), (uint8_t)rs1,
                                  (int32_t)off, DIMON64_OPCODE_LOAD));
        return;
    }
    if (is_store(m)) {
        char ops[4][512]; char tmp[1024]; strcpy(tmp, rest);
        if (split_operands(tmp, ops) != 2) { err(lineno, "store requires 2 operands (rs2, offset(rs1))"); return; }
        int rs2 = 0, rs1 = 0; int64_t off = 0;
        if (!parse_reg(ops[0], &rs2)) { err(lineno, "invalid source register"); return; }
        if (!parse_mem(ops[1], &rs1, &off, pc, lineno, 0)) { err(lineno, "invalid memory operand (use offset(rs1))"); return; }
        if (off < -2048 || off > 2047) { err(lineno, "store offset range -2048..2047"); return; }
        emit32le(dimon64_encode_s((uint8_t)store_f3(m), (uint8_t)rs1, (uint8_t)rs2,
                                  (int32_t)off, DIMON64_OPCODE_STORE));
        return;
    }
    if (is_branch(m)) {
        char ops[4][512]; char tmp[1024]; strcpy(tmp, rest);
        if (split_operands(tmp, ops) != 3) { err(lineno, "branch requires 3 operands (rs1, rs2, label)"); return; }
        int rs1 = 0, rs2 = 0;
        if (!parse_reg(ops[0], &rs1) || !parse_reg(ops[1], &rs2)) { err(lineno, "invalid register"); return; }
        if (!strcmp(m, "BLE") || !strcmp(m, "BGT") || !strcmp(m, "BLEU") || !strcmp(m, "BGTU")) {
            int tmp_r = rs1;
            rs1 = rs2;
            rs2 = tmp_r;
        }
        int64_t target = 0;
        if (!resolve_expr(ops[2], pc, &target, lineno)) return;
        int64_t off = target - (int64_t)pc;
        if ((off & 1) || off < -4096 || off > 4094) {
            err(lineno, "branch target out of range (+/-4KB)");
            return;
        }
        emit32le(dimon64_encode_b((uint8_t)branch_f3(m), (uint8_t)rs1, (uint8_t)rs2,
                                  (int32_t)off, DIMON64_OPCODE_BRANCH));
        return;
    }
    if (!strcmp(m, "LUI") || !strcmp(m, "AUIPC")) {
        char ops[4][512]; char tmp[1024]; strcpy(tmp, rest);
        if (split_operands(tmp, ops) != 2) { err(lineno, "U-type requires 2 operands (rd, imm)"); return; }
        int rd = 0;
        if (!parse_reg(ops[0], &rd)) { err(lineno, "invalid register"); return; }
        int64_t imm = 0;
        trim(ops[1]);
        if (parse_number64(ops[1], &imm)) { }
        else if (!resolve_expr(ops[1], pc, &imm, lineno)) return;
        /* accept 20-bit or full address upper */
        int32_t imm20;
        if (imm >= -524288 && imm <= 524287) imm20 = (int32_t)(imm & 0xFFFFF);
        else if (imm >= 0 && imm <= 0xFFFFF) imm20 = (int32_t)imm;
        else {
            /* if full address, take upper 20 bits (for manual use) */
            imm20 = (int32_t)(((imm + 0x800) >> 12) & 0xFFFFF);
        }
        uint8_t opc = (!strcmp(m, "LUI")) ? DIMON64_OPCODE_LUI : DIMON64_OPCODE_AUIPC;
        emit32le(dimon64_encode_u((uint8_t)rd, imm20, opc));
        return;
    }
    if (!strcmp(m, "JAL")) {
        char ops[4][512]; char tmp[1024]; strcpy(tmp, rest);
        int n = split_operands(tmp, ops);
        int rd = 1; char *targ = NULL;
        if (n == 1) { rd = 1; targ = ops[0]; }
        else if (n == 2) {
            if (!parse_reg(ops[0], &rd)) { err(lineno, "invalid register"); return; }
            targ = ops[1];
        } else { err(lineno, "JAL requires 1 or 2 operands"); return; }
        int64_t target = 0;
        if (!resolve_expr(targ, pc, &target, lineno)) return;
        int64_t off = target - (int64_t)pc;
        if ((off & 1) || off < -(1 << 20) || off > ((1 << 20) - 2)) {
            err(lineno, "JAL target out of range (+/-1MB)"); return;
        }
        emit32le(dimon64_encode_j((uint8_t)rd, (int32_t)off, DIMON64_OPCODE_JAL));
        return;
    }
    if (!strcmp(m, "JALR")) {
        char ops[4][512]; char tmp[1024]; strcpy(tmp, rest);
        int n = split_operands(tmp, ops);
        if (n == 2) {
            /* JALR rd, offset(rs1) */
            int rd = 0, rs1 = 0; int64_t off = 0;
            if (!parse_reg(ops[0], &rd)) { err(lineno, "invalid register"); return; }
            if (!parse_mem(ops[1], &rs1, &off, pc, lineno, 0)) { err(lineno, "invalid JALR operand"); return; }
            if (off < -2048 || off > 2047) { err(lineno, "JALR offset range -2048..2047"); return; }
            emit32le(dimon64_encode_i((uint8_t)rd, 0, (uint8_t)rs1, (int32_t)off,
                                      DIMON64_OPCODE_JALR));
            return;
        } else if (n == 3) {
            int rd = 0, rs1 = 0;
            if (!parse_reg(ops[0], &rd) || !parse_reg(ops[1], &rs1)) { err(lineno, "invalid register"); return; }
            int64_t off = 0;
            if (!parse_number64(ops[2], &off)) { err(lineno, "invalid offset"); return; }
            if (off < -2048 || off > 2047) { err(lineno, "JALR offset range -2048..2047"); return; }
            emit32le(dimon64_encode_i((uint8_t)rd, 0, (uint8_t)rs1, (int32_t)off,
                                      DIMON64_OPCODE_JALR));
            return;
        }
        err(lineno, "JALR requires (rd, offset(rs1))");
        return;
    }
    {
        char msg[128]; snprintf(msg, sizeof(msg), "unknown mnemonic '%s'", mnem_raw);
        err(lineno, msg);
    }
}

static int load_source(const char *path, int depth) {
    if (depth > 16) {
        fprintf(stderr, "Error: include depth exceeded: %s\n", path);
        return -1;
    }
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); return -1; }
    char basedir[512] = "";
    const char *ls = strrchr(path, '/');
    if (ls) {
        size_t dl = (size_t)(ls - path);
        if (dl < sizeof(basedir)) { strncpy(basedir, path, dl); basedir[dl] = 0; }
    }
    char linebuf[MAX_LINE_LEN];
    while (fgets(linebuf, sizeof(linebuf), f)) {
        char check[MAX_LINE_LEN];
        strncpy(check, linebuf, sizeof(check) - 1); check[sizeof(check) - 1] = 0;
        strip_comment(check);
        trim(check);
        if ((!strncasecmp(check, ".include", 8) && (isspace((unsigned char)check[8]) || check[8] == '"')) ||
            (!strncasecmp(check, "include", 7) && (isspace((unsigned char)check[7]) || check[7] == '"')) ||
            (!strncasecmp(check, "%include", 8) && (isspace((unsigned char)check[8]) || check[8] == '"'))) {
            char *p = check;
            while (*p && !isspace((unsigned char)*p) && *p != '"') p++;
            while (*p && isspace((unsigned char)*p)) p++;
            char inc[MAX_LINE_LEN] = "";
            if (*p == '"' || *p == '\'') {
                char q = *p++;
                char *eq = strchr(p, q);
                if (eq) { size_t L = (size_t)(eq - p); if (L < sizeof(inc)) { strncpy(inc, p, L); inc[L] = 0; } }
            } else { snprintf(inc, sizeof(inc), "%s", p); trim(inc); }
            if (!inc[0]) { fprintf(stderr, "Include syntax error in %s\n", path); fclose(f); return -1; }
            char sub[1024];
            if (inc[0] == '/' || !basedir[0]) snprintf(sub, sizeof(sub), "%s", inc);
            else snprintf(sub, sizeof(sub), "%s/%s", basedir, inc);
            if (load_source(sub, depth + 1) != 0) {
                if (basedir[0] && load_source(inc, depth + 1) == 0) { }
                else { fclose(f); return -1; }
            }
            continue;
        }
        if (nlines >= MAX_LINES) { fprintf(stderr, "Too many lines (max %d)\n", MAX_LINES); fclose(f); return -1; }
        snprintf(lines[nlines].text, sizeof(lines[nlines].text), "%s", linebuf);
        lines[nlines].lineno = nlines + 1;
        nlines++;
    }
    fclose(f);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s input.asm [-o output.bin] [--dexe NAME]\n", argv[0]);
        return 1;
    }
    const char *inpath = argv[1];
    const char *outpath = "a.bin";
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) outpath = argv[++i];
        else if (!strcmp(argv[i], "--dexe") && i + 1 < argc) { emit_dexe = 1; dexe_name = argv[++i]; }
        else if (argv[i][0] != '-') outpath = argv[i];
    }
    if (load_source(inpath, 0) != 0) return 1;

    /* PASS 1: labels and sizes */
    uint64_t addr = 0;
    /* per-line address cache for pass2 */
    static uint64_t line_addr[MAX_LINES];
    for (int i = 0; i < nlines; i++) {
        char tmp[MAX_LINE_LEN]; strcpy(tmp, lines[i].text);
        strip_comment(tmp);
        char lab[128], mnem[128], rest[1024];
        split_line(tmp, lab, mnem, rest);
        /* .org handling */
        if (mnem[0]) {
            char up[128]; strcpy(up, mnem); strupper(up);
            if (!strcmp(up, ".ORG") || !strcmp(up, "ORG")) {
                int64_t v = 0;
                if (!parse_number64(rest, &v)) {
                    /* allow label expr in pass1? resolve if possible else 0 */
                    v = (int64_t)addr;
                }
                if (v < 0) { err(lines[i].lineno, ".org negative"); continue; }
                addr = (uint64_t)v;
                line_addr[i] = addr;
                continue;
            }
        }
        if (lab[0]) add_label(lab, addr, lines[i].lineno);
        if (!mnem[0]) { line_addr[i] = addr; continue; }
        char up[128]; strcpy(up, mnem); strupper(up);
        if (!strcmp(up, ".EQU") || !strcmp(up, ".SET")) {
            /* .equ defines absolute symbol; pass1: try parse number */
            char ops[4][512]; char t2[1024]; strcpy(t2, rest);
            if (split_operands(t2, ops) == 2) {
                trim(ops[0]); trim(ops[1]);
                int64_t v = 0;
                if (parse_number64(ops[1], &v)) add_label(ops[0], (uint64_t)v, lines[i].lineno);
                /* else defer to pass2 */
            }
            line_addr[i] = addr;
            continue;
        }
        /* align current addr to 4 before instructions (not directives) */
        if (!is_directive(up)) {
            if ((addr % 4) != 0) addr = (addr + 3) & ~3ULL;
        }
        /* .align directive may advance addr */
        if (!strcmp(up, ".ALIGN") || !strcmp(up, ".BALIGN") || !strcmp(up, ".P2ALIGN")) {
            line_addr[i] = addr;
            int sz = instr_size(mnem, rest, addr, lines[i].lineno);
            if (sz < 0) continue;
            addr += (uint64_t)sz;
            continue;
        }
        line_addr[i] = addr;
        int sz = instr_size(mnem, rest, addr, lines[i].lineno);
        if (sz < 0) continue;
        addr += (uint64_t)sz;
        if (addr >= DIMON64_MEM_SIZE) { err(lines[i].lineno, "program exceeds 64MB memory"); break; }
    }
    if (errors) { fprintf(stderr, "Aborted: %d errors (pass1)\n", errors); return 1; }

    /* PASS 2 */
    img = NULL; img_cap = 0; cur = 0; himark = 0; errors = 0;
    ensure_cap(4096);
    for (int i = 0; i < nlines; i++) {
        char tmp[MAX_LINE_LEN]; strcpy(tmp, lines[i].text);
        strip_comment(tmp);
        char lab[128], mnem[128], rest[1024];
        split_line(tmp, lab, mnem, rest);
        if (!mnem[0]) continue;
        char up[128]; strcpy(up, mnem); strupper(up);
        if (!strcmp(up, ".EQU") || !strcmp(up, ".SET")) {
            /* define if not already (label expr) */
            char ops[4][512]; char t2[1024]; strcpy(t2, rest);
            if (split_operands(t2, ops) == 2) {
                trim(ops[0]); trim(ops[1]);
                if (!find_label(ops[0])) {
                    int64_t ev = 0;
                    if (resolve_expr(ops[1], line_addr[i], &ev, 0))
                        add_label(ops[0], (uint64_t)ev, lines[i].lineno);
                }
            }
            continue;
        }
        /* seek to line address */
        uint64_t want = line_addr[i];
        char upchk[128]; strcpy(upchk, mnem); strupper(upchk);
        int is_org = (!strcmp(upchk, ".ORG") || !strcmp(upchk, "ORG"));
        if (!is_org) {
            if (!is_directive(upchk)) {
                /* instructions were aligned in pass1 */
                align_cur(4);
            }
            if (cur != want) {
                /* pass1/pseudo size mismatch can cause drift for LI with labels.
                   If cur < want, pad; if cur > want, labels overlap -> error. */
                if (cur < want) {
                    while (cur < want) emit8(0);
                } else {
                    char msg[160];
                    snprintf(msg, sizeof(msg), "address drift at '%s' (pass1/pass2 size mismatch)", mnem);
                    err(lines[i].lineno, msg);
                    /* resync */
                    want = cur;
                }
            }
        }
        pass2_emit(mnem, rest, cur, lines[i].lineno);
    }
    if (errors) { fprintf(stderr, "Aborted: %d errors (pass2)\n", errors); free(img); return 1; }

    FILE *o = fopen(outpath, "wb");
    if (!o) { perror("fopen out"); free(img); return 1; }
    if (himark == 0) himark = 4;
    if (emit_dexe) {
        Dimon64ExecHeader h;
        memset(&h, 0, sizeof(h)); memcpy(h.magic, DIMON64_EXEC_MAGIC, 8);
        h.version = DIMON64_EXEC_VERSION; h.header_size = (uint32_t)sizeof(h);
        h.image_size = (uint32_t)himark; h.bss_size = 0;
        h.entry_offset = 0; h.memory_size = DIMON64_APP_SLOT_SIZE;
        h.relocation_count = nrelocations;
        snprintf(h.name, sizeof(h.name), "%s", dexe_name ? dexe_name : "app");
        if (fwrite(&h, 1, sizeof(h), o) != sizeof(h) ||
            fwrite(img, 1, (size_t)himark, o) != (size_t)himark ||
            (nrelocations && fwrite(relocations, sizeof(uint32_t), nrelocations, o) != nrelocations)) {
            perror("fwrite"); fclose(o); free(img); return 1;
        }
    } else if (fwrite(img, 1, (size_t)himark, o) != (size_t)himark) {
        perror("fwrite"); fclose(o); free(img); return 1;
    }
    fclose(o);
    printf("OK: %s -> %s (%" PRIu64 " image bytes%s)\n", inpath, outpath, himark,
           emit_dexe ? ", DEXE64" : "");
    if (nlabels) {
        printf("Labels (%d):\n", nlabels);
        for (int i = 0; i < nlabels; i++)
            printf("  %-20s = 0x%08" PRIX64 "\n", labels[i].name, labels[i].addr);
    }
    free(img);
    return 0;
}
