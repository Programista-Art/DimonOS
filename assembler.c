/* Dimon-16 Assembler (dimon-as)
   Syntax:
     LABEL:  MOV R0, 123  ; comment
     .org 0x100
     DB "Hi", 0
     DW 123, LABEL
     DS 64
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdint.h>

#include "dimon16.h"

#define MAX_LINES 32768
#define MAX_LABELS 4096
#define MAX_LINE_LEN 1024

typedef struct { char name[64]; uint16_t addr; } Label;
static Label labels[MAX_LABELS];
static int nlabels = 0;

typedef struct { char text[MAX_LINE_LEN]; int lineno; } Line;
static Line lines[MAX_LINES];
static int nlines = 0;

static int errors = 0;

static void err(int lineno, const char *msg) {
    fprintf(stderr, "ASM error [line %d]: %s\n", lineno, msg);
    errors++;
}

/* ---------- String helper functions ---------- */
static void trim(char *s) {
    /* Leading whitespace */
    char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    /* Trailing whitespace */
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n-1])) s[--n] = 0;
}

static void strupper(char *s) {
    for (; *s; s++) *s = (char)toupper((unsigned char)*s);
}

/* Strip comment ';' outside of quoted strings ('...' and "...") */
static void strip_comment(char *s) {
    int in_s = 0, in_d = 0;
    for (char *p = s; *p; p++) {
        if (*p == '\'' && !in_d) in_s = !in_s;
        else if (*p == '"' && !in_s) in_d = !in_d;
        else if (*p == ';' && !in_s && !in_d) { *p = 0; break; }
    }
}

/* Parse number: dec, 0xHEX, 0bBIN, 'c', -number. Returns 1 on success. */
static int parse_number(const char *s, int *out) {
    char b[128];
    strncpy(b, s, sizeof(b)-1); b[sizeof(b)-1] = 0;
    trim(b);
    if (!b[0]) return 0;
    /* 'c' character literal */
    if (b[0] == '\'' && strlen(b) >= 3 && b[strlen(b)-1] == '\'') {
        if (b[1] == '\\' && b[2] == 'n') { *out = 10; return 1; }
        *out = (unsigned char)b[1];
        return 1;
    }
    int neg = 0;
    const char *p = b;
    if (*p == '-') { neg = 1; p++; }
    else if (*p == '+') p++;
    long v = 0;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        char *e; v = strtol(p, &e, 16);
        if (*e) return 0;
    } else if (p[0] == '0' && (p[1] == 'b' || p[1] == 'B')) {
        v = 0;
        const char *q = p + 2;
        if (!*q) return 0;
        while (*q == '0' || *q == '1') { v = v * 2 + (*q - '0'); q++; }
        if (*q) return 0;
    } else {
        char *e; v = strtol(p, &e, 10);
        if (*e) return 0;
    }
    if (neg) v = -v;
    *out = (int)v;
    return 1;
}

static int is_reg(const char *s, int *r) {
    char b[32]; strncpy(b, s, sizeof(b)-1); b[sizeof(b)-1]=0;
    trim(b);
    if ((b[0]=='R'||b[0]=='r') && b[1]>='0' && b[1]<='7' && b[2]==0) {
        *r = b[1]-'0'; return 1;
    }
    return 0;
}

static Label *find_label(const char *name) {
    for (int i = 0; i < nlabels; i++)
        if (strcmp(labels[i].name, name) == 0) return &labels[i];
    return NULL;
}

static void add_label(const char *name, uint16_t addr, int lineno) {
    if (find_label(name)) {
        char m[128]; snprintf(m, sizeof(m), "duplicate label '%s'", name);
        err(lineno, m);
        return;
    }
    if (nlabels >= MAX_LABELS) { err(lineno, "too many labels"); return; }
    snprintf(labels[nlabels].name, sizeof(labels[nlabels].name), "%s", name);
    labels[nlabels].addr = addr;
    nlabels++;
}

/* Check if string is a valid label name */
static int valid_label(const char *s) {
    if (!s[0] || !(isalpha((unsigned char)s[0]) || s[0]=='_')) return 0;
    for (const char *p = s+1; *p; p++)
        if (!(isalnum((unsigned char)*p) || *p=='_')) return 0;
    return 1;
}

/* Split line into: [label] [mnemonic] [rest of operands] */
static void split_line(char *line, char *lab, char *mnem, char *rest) {
    lab[0]=0; mnem[0]=0; rest[0]=0;
    trim(line);
    if (!line[0]) return;
    char *colon = strchr(line, ':');
    if (colon) {
        size_t n = (size_t)(colon - line);
        char tmp[128];
        if (n < sizeof(tmp)) {
            strncpy(tmp, line, n); tmp[n]=0; trim(tmp);
            if (valid_label(tmp)) {
                strcpy(lab, tmp);
                memmove(line, colon+1, strlen(colon+1)+1);
                trim(line);
            }
        }
    }
    if (!line[0]) return;
    char *p = line;
    while (*p && !isspace((unsigned char)*p)) p++;
    size_t n = (size_t)(p - line);
    strncpy(mnem, line, n < 127 ? n : 127);
    mnem[n < 127 ? n : 127] = 0;
    if (*p) strcpy(rest, p+1);
    trim(rest);
}

/* Split comma-separated operands */
static int split_operands(char *rest, char ops[3][256]) {
    int n = 0;
    char cur[256]; int ci = 0;
    int in_s = 0, in_d = 0;
    for (char *p = rest; ; p++) {
        char c = *p;
        int end = (c == 0);
        if (c == '\'' && !in_d) in_s = !in_s;
        else if (c == '"' && !in_s) in_d = !in_d;
        if ((c == ',' && !in_s && !in_d) || end) {
            cur[ci] = 0;
            trim(cur);
            if (cur[0] || n > 0) {
                if (n < 3) strcpy(ops[n++], cur);
            }
            ci = 0;
            if (end) break;
        } else {
            if (ci < 250) cur[ci++] = c;
        }
    }
    if (rest[0]==0) return 0;
    return n;
}

typedef enum { OT_NONE, OT_REG, OT_IMM, OT_MEM, OT_REGIND, OT_LABEL } OpType;
typedef struct { OpType t; int reg; int imm; char lab[64]; } Op;

static Op classify(const char *s) {
    Op o; memset(&o, 0, sizeof(o));
    char b[256]; strncpy(b, s, sizeof(b)-1); b[sizeof(b)-1]=0; trim(b);
    int r, v;
    if (!b[0]) { o.t = OT_NONE; return o; }
    if (is_reg(b, &r)) { o.t = OT_REG; o.reg = r; return o; }
    if (b[0]=='[') {
        size_t n = strlen(b);
        if (n>=2 && b[n-1]==']') {
            char inner[256]; strncpy(inner, b+1, n-2); inner[n-2]=0; trim(inner);
            if (is_reg(inner, &r)) { o.t = OT_REGIND; o.reg = r; return o; }
            if (parse_number(inner, &v)) { o.t = OT_MEM; o.imm = v & 0xFFFF; return o; }
            if (valid_label(inner)) { o.t = OT_MEM; strcpy(o.lab, inner); return o; }
        }
        o.t = OT_NONE; return o;
    }
    if (parse_number(b, &v)) { o.t = OT_IMM; o.imm = v & 0xFFFF; return o; }
    if (valid_label(b)) { o.t = OT_LABEL; strcpy(o.lab, b); return o; }
    o.t = OT_NONE; return o;
}

static int mode_of(Op *o) {
    switch (o->t) {
        case OT_REG: return MODE_REG;
        case OT_IMM:
        case OT_LABEL: return MODE_IMM;
        case OT_MEM: return MODE_MEM;
        case OT_REGIND: return MODE_REGIND;
        default: return -1;
    }
}
static int oplen_of(Op *o) {
    switch (o->t) {
        case OT_REG:
        case OT_REGIND: return 1;
        case OT_IMM:
        case OT_LABEL:
        case OT_MEM: return 2;
        default: return 0;
    }
}
static int is_mem_label_op(Op *o) {
    return o->t == OT_MEM && o->lab[0] != 0;
}

static uint8_t opcode_of(const char *m) {
    char b[32]; strncpy(b, m, sizeof(b)-1); b[sizeof(b)-1]=0; strupper(b);
    if (!strcmp(b,"HLT")) return OP_HLT;
    if (!strcmp(b,"NOP")) return OP_NOP;
    if (!strcmp(b,"MOV")) return OP_MOV;
    if (!strcmp(b,"ADD")) return OP_ADD;
    if (!strcmp(b,"SUB")) return OP_SUB;
    if (!strcmp(b,"MUL")) return OP_MUL;
    if (!strcmp(b,"DIV")) return OP_DIV;
    if (!strcmp(b,"AND")) return OP_AND;
    if (!strcmp(b,"OR")) return OP_OR;
    if (!strcmp(b,"XOR")) return OP_XOR;
    if (!strcmp(b,"CMP")) return OP_CMP;
    if (!strcmp(b,"NOT")) return OP_NOT;
    if (!strcmp(b,"SHL")) return OP_SHL;
    if (!strcmp(b,"SHR")) return OP_SHR;
    if (!strcmp(b,"INC")) return OP_INC;
    if (!strcmp(b,"DEC")) return OP_DEC;
    if (!strcmp(b,"PUSH")) return OP_PUSH;
    if (!strcmp(b,"POP")) return OP_POP;
    if (!strcmp(b,"JMP")) return OP_JMP;
    if (!strcmp(b,"JZ")) return OP_JZ;
    if (!strcmp(b,"JNZ")) return OP_JNZ;
    if (!strcmp(b,"JC")) return OP_JC;
    if (!strcmp(b,"JNC")) return OP_JNC;
    if (!strcmp(b,"CALL")) return OP_CALL;
    if (!strcmp(b,"RET")) return OP_RET;
    if (!strcmp(b,"IN")) return OP_IN;
    if (!strcmp(b,"OUT")) return OP_OUT;
    if (!strcmp(b,"INT")) return OP_INT;
    if (!strcmp(b,"IRET")) return OP_IRET;
    if (!strcmp(b,"LDB")) return OP_LDB;
    if (!strcmp(b,"STB")) return OP_STB;
    return 0xFF;
}

static int is_two_op(const char *m) {
    char b[32]; strncpy(b, m, sizeof(b)-1); b[sizeof(b)-1]=0; strupper(b);
    return (!strcmp(b,"MOV")||!strcmp(b,"ADD")||!strcmp(b,"SUB")||
            !strcmp(b,"MUL")||!strcmp(b,"DIV")||!strcmp(b,"AND")||
            !strcmp(b,"OR")||!strcmp(b,"XOR")||!strcmp(b,"CMP")||
            !strcmp(b,"SHL")||!strcmp(b,"SHR")||
            !strcmp(b,"LDB")||!strcmp(b,"STB"));
}
static int is_one_op(const char *m) {
    char b[32]; strncpy(b, m, sizeof(b)-1); b[sizeof(b)-1]=0; strupper(b);
    return (!strcmp(b,"NOT")||!strcmp(b,"INC")||!strcmp(b,"DEC")||
            !strcmp(b,"PUSH")||!strcmp(b,"POP")||!strcmp(b,"JMP")||
            !strcmp(b,"JZ")||!strcmp(b,"JNZ")||!strcmp(b,"JC")||
            !strcmp(b,"JNC")||!strcmp(b,"CALL"));
}
static int is_zero_op(const char *m) {
    char b[32]; strncpy(b, m, sizeof(b)-1); b[sizeof(b)-1]=0; strupper(b);
    return (!strcmp(b,"HLT")||!strcmp(b,"NOP")||!strcmp(b,"RET")||!strcmp(b,"IRET"));
}

/* Calculate DB directive size in bytes */
static int db_size(const char *rest) {
    int count = 0;
    char cur[256]; int ci = 0;
    int in_s = 0, in_d = 0;
    char tmp[1024]; strncpy(tmp, rest, sizeof(tmp)-1); tmp[sizeof(tmp)-1]=0;
    size_t L = strlen(tmp);
    tmp[L+1]=0; tmp[L]=',';
    for (char *p = tmp; ; p++) {
        char c = *p;
        int end = (c==0);
        if (c=='\'' && !in_d) in_s=!in_s;
        else if (c=='"' && !in_s) in_d=!in_d;
        if ((c==',' && !in_s && !in_d) || end) {
            cur[ci]=0; trim(cur);
            if (cur[0]=='"' && cur[strlen(cur)-1]=='"') {
                count += (int)strlen(cur)-2;
            } else if (cur[0]) count += 1;
            ci=0;
            if (end) break;
        } else {
            if (ci<250) cur[ci++]=c;
        }
    }
    return count;
}

static int dw_count(const char *rest) {
    if (!rest[0]) return 0;
    int n=1, in_s=0, in_d=0;
    for (const char *p=rest; *p; p++) {
        if (*p=='\'' && !in_d) in_s=!in_s;
        else if (*p=='"' && !in_s) in_d=!in_d;
        else if (*p==',' && !in_s && !in_d) n++;
    }
    return n;
}

/* ============ PASS 1: Labels and Sizes ============ */
static int instr_size(const char *mnem, const char *rest, int lineno) {
    char b[32]; strncpy(b, mnem, sizeof(b)-1); b[sizeof(b)-1]=0; strupper(b);
    if (is_zero_op(b)) {
        if (rest[0]) { err(lineno, "instruction takes no operands"); return -1; }
        return 1;
    }
    if (!strcmp(b,"INT")) {
        int v; char ops[3][256];
        char tmp[512]; strcpy(tmp, rest);
        if (split_operands(tmp, ops)!=1) { err(lineno,"INT requires 1 operand"); return -1; }
        if (!parse_number(ops[0],&v) || v<0 || v>255) { err(lineno,"INT range 0..255"); return -1; }
        return 2;
    }
    if (!strcmp(b,"IN")) {
        char ops[3][256]; char tmp[512]; strcpy(tmp, rest);
        if (split_operands(tmp,ops)!=2) { err(lineno,"IN R, port"); return -1; }
        int r,vv;
        if (!is_reg(ops[0],&r)) { err(lineno,"IN: first argument must be register"); return -1; }
        if (!parse_number(ops[1],&vv)||vv<0||vv>255) { err(lineno,"IN: port range 0..255"); return -1; }
        return 3;
    }
    if (!strcmp(b,"OUT")) {
        char ops[3][256]; char tmp[512]; strcpy(tmp, rest);
        if (split_operands(tmp,ops)!=2) { err(lineno,"OUT port, R"); return -1; }
        int r,vv;
        if (!parse_number(ops[0],&vv)||vv<0||vv>255) { err(lineno,"OUT: port range 0..255"); return -1; }
        if (!is_reg(ops[1],&r)) { err(lineno,"OUT: second argument must be register"); return -1; }
        return 3;
    }
    if (is_two_op(b)) {
        char ops[3][256]; char tmp[512]; strcpy(tmp, rest);
        if (split_operands(tmp,ops)!=2) { err(lineno,"2 operands required"); return -1; }
        Op d=classify(ops[0]), s=classify(ops[1]);
        if (d.t==OT_NONE){err(lineno,"invalid DST");return -1;}
        if (s.t==OT_NONE){err(lineno,"invalid SRC");return -1;}
        if (d.t==OT_IMM||d.t==OT_LABEL){err(lineno,"DST cannot be immediate value");return -1;}
        if (!strcmp(b,"LDB")) {
            if (d.t!=OT_REG){err(lineno,"LDB requires register as DST");return -1;}
            return 2 + (s.t == OT_IMM ? 1 : oplen_of(&s));
        }
        return 2 + oplen_of(&d) + oplen_of(&s);
    }
    if (is_one_op(b)) {
        char ops[3][256]; char tmp[512]; strcpy(tmp, rest);
        if (split_operands(tmp,ops)!=1) { err(lineno,"1 operand required"); return -1; }
        Op o=classify(ops[0]);
        if (o.t==OT_NONE){err(lineno,"invalid operand");return -1;}
        if ((!strcmp(b,"POP")||!strcmp(b,"NOT")||!strcmp(b,"INC")||!strcmp(b,"DEC"))
            && (o.t==OT_IMM||o.t==OT_LABEL)){err(lineno,"target cannot be immediate value");return -1;}
        return 2 + oplen_of(&o);
    }
    /* Directives */
    if (!strcmp(b,"DB")) return db_size(rest);
    if (!strcmp(b,"DW")) return 2*dw_count(rest);
    if (!strcmp(b,"DS")||!strcmp(b,"RESB")||!strcmp(b,"RESW")) {
        int v; if(!parse_number(rest,&v)||v<0){err(lineno,"DS requires a number");return -1;}
        if (!strcmp(b,"RESW")) v*=2;
        return v;
    }
    if (!strcmp(b,".ORG")||!strcmp(b,"ORG")) return 0;
    char m[96]; snprintf(m,sizeof(m),"unknown mnemonic '%s'",mnem);
    err(lineno,m);
    return -1;
}

/* ============ PASS 2: Emission ============ */
static uint8_t *img = NULL;
static uint16_t cur = 0, himark = 0;

static void emit8(uint8_t v) { img[cur]=(uint8_t)v; cur=(uint16_t)(cur+1); if(cur>himark)himark=cur; }
static void emit16(uint16_t v){ emit8(v&0xFF); emit8((v>>8)&0xFF); }

static int resolve_val(Op *o, int lineno, uint16_t *out) {
    if (o->t==OT_IMM){*out=(uint16_t)o->imm;return 1;}
    if (o->t==OT_LABEL){
        Label *l=find_label(o->lab);
        if(!l){char m[128];snprintf(m,sizeof(m),"unknown label '%s'",o->lab);err(lineno,m);return 0;}
        *out=l->addr;return 1;
    }
    if (o->t==OT_MEM){
        if(o->lab[0]){Label *l=find_label(o->lab);
            if(!l){char m[128];snprintf(m,sizeof(m),"unknown label '%s'",o->lab);err(lineno,m);return 0;}
            *out=l->addr;return 1;}
        *out=(uint16_t)o->imm;return 1;
    }
    return 0;
}

static void emit_operand(Op *o, int lineno) {
    if (o->t==OT_REG||o->t==OT_REGIND) emit8((uint8_t)o->reg);
    else {
        uint16_t v=0;
        if(resolve_val(o,lineno,&v)) emit16(v);
        else emit16(0);
    }
}

static void pass2_emit(const char *mnem, const char *rest, int lineno) {
    char b[32]; strncpy(b, mnem, sizeof(b)-1); b[sizeof(b)-1]=0; strupper(b);
    if (!b[0]) return;
    if (!strcmp(b,".ORG")||!strcmp(b,"ORG")) {
        int v; if(parse_number(rest,&v)) cur=(uint16_t)(v&0xFFFF);
        return;
    }
    if (!strcmp(b,"DB")) {
        char curtok[256]; int ci=0, in_s=0, in_d=0;
        char tmp[2048]; snprintf(tmp,sizeof(tmp),"%s,",rest);
        for(char *p=tmp;;p++){
            char c=*p; int end=(c==0);
            if(c=='\''&&!in_d)in_s=!in_s;
            else if(c=='"'&&!in_s)in_d=!in_d;
            if((c==','&&!in_s&&!in_d)||end){
                curtok[ci]=0; trim(curtok);
                if(curtok[0]=='"'&&curtok[strlen(curtok)-1]=='"'){
                    for(size_t i=1;i<strlen(curtok)-1;i++) emit8((uint8_t)curtok[i]);
                } else if(curtok[0]){
                    int v; if(parse_number(curtok,&v)) emit8((uint8_t)(v&0xFF));
                    else err(lineno,"invalid DB item");
                }
                ci=0;
                if(end)break;
            } else { if(ci<250)curtok[ci++]=c; }
        }
        return;
    }
    if (!strcmp(b,"DW")) {
        char curtok[256]; int ci=0, in_s=0, in_d=0;
        char tmp[2048]; snprintf(tmp,sizeof(tmp),"%s,",rest);
        for(char *p=tmp;;p++){
            char c=*p; int end=(c==0);
            if(c=='\''&&!in_d)in_s=!in_s;
            else if(c=='"'&&!in_s)in_d=!in_d;
            if((c==','&&!in_s&&!in_d)||end){
                curtok[ci]=0; trim(curtok);
                if(curtok[0]){
                    int v;
                    if(parse_number(curtok,&v)) emit16((uint16_t)(v&0xFFFF));
                    else if(valid_label(curtok)){
                        Label *l=find_label(curtok);
                        if(l) emit16(l->addr);
                        else { char m[300]; snprintf(m,sizeof(m),"unknown label '%s'",curtok); err(lineno,m); emit16(0); }
                    } else err(lineno,"invalid DW item");
                }
                ci=0;
                if(end)break;
            } else { if(ci<250)curtok[ci++]=c; }
        }
        return;
    }
    if (!strcmp(b,"DS")||!strcmp(b,"RESB")||!strcmp(b,"RESW")) {
        int v; if(!parse_number(rest,&v)) return;
        if(!strcmp(b,"RESW")) v*=2;
        for(int i=0;i<v;i++) emit8(0);
        return;
    }

    uint8_t op = opcode_of(b);
    if (op == 0xFF) { err(lineno,"unknown instruction"); return; }

    if (is_zero_op(b)) {
        emit8(op);
        return;
    }
    if (!strcmp(b,"INT")) {
        char ops[3][256]; char tmp[512]; strcpy(tmp, rest);
        split_operands(tmp, ops);
        int v=0; parse_number(ops[0], &v);
        emit8(op);
        emit8((uint8_t)(v&0xFF));
        return;
    }
    if (!strcmp(b,"IN")) {
        char ops[3][256]; char tmp[512]; strcpy(tmp, rest);
        split_operands(tmp, ops);
        int r=0, p=0; is_reg(ops[0],&r); parse_number(ops[1],&p);
        emit8(op);
        emit8((uint8_t)r);
        emit8((uint8_t)(p&0xFF));
        return;
    }
    if (!strcmp(b,"OUT")) {
        char ops[3][256]; char tmp[512]; strcpy(tmp, rest);
        split_operands(tmp, ops);
        int r=0, p=0; parse_number(ops[0],&p); is_reg(ops[1],&r);
        emit8(op);
        emit8((uint8_t)(p&0xFF));
        emit8((uint8_t)r);
        return;
    }
    if (is_two_op(b)) {
        char ops[3][256]; char tmp[512]; strcpy(tmp, rest);
        split_operands(tmp, ops);
        Op d=classify(ops[0]), s=classify(ops[1]);
        uint8_t modes = 0;
        if (!strcmp(b,"LDB")) {
            modes = (uint8_t)(((d.reg & 0x0F) << 4) | (mode_of(&s) & 0x0F));
            emit8(op);
            emit8(modes);
            if (s.t == OT_IMM) {
                emit8((uint8_t)(s.imm & 0xFF));
            } else {
                emit_operand(&s, lineno);
            }
            return;
        }
        modes = (uint8_t)(((mode_of(&d)&0x0F)<<4) | (mode_of(&s)&0x0F));
        emit8(op);
        emit8(modes);
        emit_operand(&d, lineno);
        emit_operand(&s, lineno);
        return;
    }
    if (is_one_op(b)) {
        char ops[3][256]; char tmp[512]; strcpy(tmp, rest);
        split_operands(tmp, ops);
        Op o=classify(ops[0]);
        emit8(op);
        emit8((uint8_t)(mode_of(&o)&0x0F));
        emit_operand(&o, lineno);
        return;
    }
}

static int load_source(const char *path, int depth) {
    if (depth > 16) {
        fprintf(stderr, "Error: include depth exceeded (nested too deep): %s\n", path);
        return -1;
    }
    FILE *f = fopen(path, "r");
    if (!f) {
        perror(path);
        return -1;
    }

    char basedir[512] = "";
    const char *last_slash = strrchr(path, '/');
    if (last_slash) {
        size_t dirlen = (size_t)(last_slash - path);
        if (dirlen < sizeof(basedir)) {
            strncpy(basedir, path, dirlen);
            basedir[dirlen] = '\0';
        }
    }

    char linebuf[MAX_LINE_LEN];
    int line_in_file = 0;
    while (fgets(linebuf, sizeof(linebuf), f)) {
        line_in_file++;
        char check[MAX_LINE_LEN];
        strncpy(check, linebuf, sizeof(check)-1);
        check[sizeof(check)-1] = 0;
        strip_comment(check);
        trim(check);

        if ((!strncasecmp(check, ".include", 8) && (isspace((unsigned char)check[8]) || check[8]=='"')) ||
            (!strncasecmp(check, "include", 7) && (isspace((unsigned char)check[7]) || check[7]=='"')) ||
            (!strncasecmp(check, "%include", 8) && (isspace((unsigned char)check[8]) || check[8]=='"'))) {
            char *p = check;
            while (*p && !isspace((unsigned char)*p) && *p != '"') p++;
            while (*p && isspace((unsigned char)*p)) p++;
            char inc_name[MAX_LINE_LEN] = "";
            if (*p == '"' || *p == '\'') {
                char quote = *p++;
                char *endq = strchr(p, quote);
                if (endq) {
                    size_t len = (size_t)(endq - p);
                    if (len < sizeof(inc_name)) {
                        strncpy(inc_name, p, len);
                        inc_name[len] = 0;
                    }
                }
            } else {
                snprintf(inc_name, sizeof(inc_name), "%s", p);
                trim(inc_name);
            }

            if (!inc_name[0]) {
                fprintf(stderr, "Include syntax error at line %d of %s\n", line_in_file, path);
                fclose(f);
                return -1;
            }

            char subpath[1024];
            if (inc_name[0] == '/' || basedir[0] == '\0') {
                snprintf(subpath, sizeof(subpath), "%s", inc_name);
            } else {
                snprintf(subpath, sizeof(subpath), "%s/%s", basedir, inc_name);
            }

            if (load_source(subpath, depth + 1) != 0) {
                if (basedir[0] && load_source(inc_name, depth + 1) == 0) {
                    /* ok */
                } else {
                    fclose(f);
                    return -1;
                }
            }
            continue;
        }

        if (nlines >= MAX_LINES) {
            fprintf(stderr, "Too many lines in program (max %d)\n", MAX_LINES);
            fclose(f);
            return -1;
        }
        snprintf(lines[nlines].text, sizeof(lines[nlines].text), "%s", linebuf);
        lines[nlines].lineno = nlines + 1;
        nlines++;
    }
    fclose(f);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,"Usage: %s input.asm [-o output.bin]\n", argv[0]);
        return 1;
    }
    const char *inpath = argv[1];
    const char *outpath = "a.bin";
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i],"-o") && i+1<argc) outpath = argv[++i];
        else if (argv[i][0] != '-') outpath = argv[i];
    }
    if (load_source(inpath, 0) != 0) return 1;

    /* PASS 1 */
    uint16_t addr = 0;
    for(int i=0;i<nlines;i++){
        char tmp[MAX_LINE_LEN]; strcpy(tmp,lines[i].text);
        strip_comment(tmp);
        char lab[128],mnem[128],rest[512];
        split_line(tmp,lab,mnem,rest);
        if(lab[0]) add_label(lab,addr,lines[i].lineno);
        if(!mnem[0]) continue;
        char up[128]; strcpy(up,mnem); strupper(up);
        if(!strcmp(up,".ORG")||!strcmp(up,"ORG")){
            int v; if(!parse_number(rest,&v)){err(lines[i].lineno,".ORG requires address");continue;}
            addr=(uint16_t)(v&0xFFFF);
            continue;
        }
        int sz=instr_size(mnem,rest,lines[i].lineno);
        if(sz<0) continue;
        addr=(uint16_t)(addr+sz);
    }
    if(errors){fprintf(stderr,"Aborted: %d errors (pass1)\n",errors);return 1;}

    /* PASS 2 */
    img=calloc(MEM_SIZE,1);
    if(!img){perror("calloc");return 1;}
    cur=0; himark=0; errors=0;
    for(int i=0;i<nlines;i++){
        char tmp[MAX_LINE_LEN]; strcpy(tmp,lines[i].text);
        strip_comment(tmp);
        char lab[128],mnem[128],rest[512];
        split_line(tmp,lab,mnem,rest);
        if(!mnem[0]) continue;
        pass2_emit(mnem,rest,lines[i].lineno);
    }
    if(errors){fprintf(stderr,"Aborted: %d errors (pass2)\n",errors);free(img);return 1;}

    FILE *o=fopen(outpath,"wb");
    if(!o){perror("fopen out");free(img);return 1;}
    if(himark==0)himark=1;
    if(fwrite(img,1,himark,o)!=himark){perror("fwrite");fclose(o);free(img);return 1;}
    fclose(o);
    printf("OK: %s -> %s (%u bytes)\n", inpath, outpath, himark);
    if(nlabels){
        printf("Labels (%d):\n", nlabels);
        for(int i=0;i<nlabels;i++) printf("  %-16s = 0x%04X\n", labels[i].name, labels[i].addr);
    }
    free(img);
    (void)is_mem_label_op;
    return 0;
}
