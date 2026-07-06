#include<cstdio>
#include <regex.h>
#include <cstring>
#include <cstdlib>

#include "../../include/expr.h"
#include "../../include/cpu.h"
#include "../../include/memory.h"


enum TokenType {
    TK_NOTYPE = 0,  // 空白（匹配后忽略，不加入 token 序列）
    TK_NUM,         // 十进制整数，如 123
    TK_HEX,         // 十六进制整数，如 0xff
    TK_REG,         // 寄存器名，如 $ra $a0 $pc
    TK_PLUS,        // +
    TK_MINUS,       // -
    TK_MUL,         // *（同时也用作一元解引用，在 parse 阶段区分）
    TK_DIV,         // /
    TK_LPAREN,      // (
    TK_RPAREN,      // )
    TK_EQ,          // ==
    TK_NEQ,         // !=
    TK_AND,         // &&
    TK_DEREF,       // 一元 *（在 mark_deref 中标记，不是词法阶段的类型）
};

struct Rule {
    const char *pattern;
    TokenType   type;
};

static Rule rules[] = {
    {" +",               TK_NOTYPE},  // 空白
    {"0[xX][0-9a-fA-F]+", TK_HEX},  // 0x... 必须在 TK_NUM 之前！
    {"[0-9]+",           TK_NUM},
    {"\\$[a-z][a-z0-9]*", TK_REG},
    {"==",               TK_EQ},     // == 必须在单字符之前
    {"!=",               TK_NEQ},
    {"&&",               TK_AND},
    {"\\+",              TK_PLUS},
    {"-",                TK_MINUS},
    {"\\*",              TK_MUL},
    {"/",                TK_DIV},
    {"\\(",              TK_LPAREN},
    {"\\)",              TK_RPAREN},
};

const int NR_RULES = sizeof(rules) / sizeof(rules[0]);   // 正则匹配规则条数
static regex_t re[NR_RULES];  // 编译后的正则表达式，与 rules[] 一一对应

// 编译所有词法规则对应的正则表达式（只需执行一次，用 static bool 控制）
static void init_regex() {
    static bool re_compiled = false;
    if (re_compiled) return;

    for (int i = 0; i < NR_RULES; i++) {
        regcomp(&re[i], rules[i].pattern, REG_EXTENDED);
    }
    re_compiled = true;
}


struct Token{
    TokenType type;
    char str[64];
};

static Token tokens[256];  // 词法分析结果存放处
static int   nr_token;     // 当前 token 数量

static bool tokenize(const char *e) {
    init_regex();
    nr_token = 0;
    int pos = 0;
    int len = strlen(e);

    while (pos < len) {
        // 逐条尝试规则
        bool matched = false;
        for (int i = 0; i < NR_RULES; i++) {
            regmatch_t pmatch;
            if (regexec(&re[i], e + pos, 1, &pmatch, 0) == 0 && pmatch.rm_so == 0) {
                // 在 pos 位置匹配成功，匹配长度为 pmatch.rm_eo
                int mlen = pmatch.rm_eo;
                if (rules[i].type == TK_NOTYPE) {
                    pos += mlen;  // 空白直接跳过
                    matched = true;
                    break;
                }
                // 记录 Token：复制原始字符串，设置类型
                tokens[nr_token].type = rules[i].type;
                strncpy(tokens[nr_token].str,e + pos, mlen);
                tokens[nr_token].str[mlen] = '\0';
                nr_token++;
                pos += mlen;
                matched = true;
                break;
            }
        }
        if (!matched) {
            // 没有规则匹配：词法错误
            fprintf(stderr, "词法错误：无法识别字符 '%c' 位于位置 %d\n", e[pos], pos);
            return false;
        }
    }
    return true;
}

static void mark_deref() {
    for (int i = 0; i < nr_token; i++) {
        if (tokens[i].type == TK_MUL) {
            if (i == 0
                || (tokens[i-1].type != TK_RPAREN
                 && tokens[i-1].type != TK_NUM
                 && tokens[i-1].type != TK_HEX
                 && tokens[i-1].type != TK_REG)) {
                tokens[i].type = TK_DEREF;
            }
        }
    }
}

uint32_t expr_eval(const char *e, bool *success);

uint32_t parse_expr(int *pos, bool *ok);
uint32_t parse_eq(int *pos, bool *ok);
uint32_t parse_add(int *pos, bool *ok);
uint32_t parse_mul(int *pos, bool *ok);
uint32_t parse_unary(int *pos, bool *ok);
uint32_t parse_primary(int *pos, bool *ok);

uint32_t expr_eval(const char *e, bool *success) {
    *success = false;
    if (!tokenize(e)) return 0;
    mark_deref();

    int  pos = 0;
    bool ok  = true;
    uint32_t val = parse_expr(&pos, &ok);

    if (!ok || pos != nr_token) {
        if (ok) fprintf(stderr, "表达式未完全解析\n");
        return 0;
    }
    *success = true;
    return val;
}

uint32_t parse_expr(int *pos, bool *ok) {   // &&
    uint32_t val = parse_eq(pos, ok);
    if(!*ok) return 0;
    while(*pos < nr_token && tokens[*pos].type == TK_AND) {
        *pos += 1;
        uint32_t rhs = parse_eq(pos, ok);
        if(!*ok) return 0;
        val = (val && rhs) ? 1 : 0;
    }
    return val;
}

uint32_t parse_eq(int *pos, bool *ok) {     // == !=
    uint32_t val = parse_add(pos, ok);
    if(!*ok) return 0;
    while(*pos < nr_token && (tokens[*pos].type == TK_EQ || tokens[*pos].type == TK_NEQ)) {
        int op = tokens[*pos].type;
        (*pos) ++;
        uint32_t rhs = parse_add(pos, ok);
        if(!*ok) return 0;
        if(op == TK_EQ) val = (val == rhs) ? 1 : 0;
        else val = (val != rhs) ? 1 : 0;
    }
    return val;
}

uint32_t parse_add(int *pos, bool *ok) {
    uint32_t val = parse_mul(pos, ok);
    if(!*ok) return 0;
    while(*pos < nr_token && (tokens[*pos].type == TK_PLUS || tokens[*pos].type == TK_MINUS)) {
        int op = tokens[*pos].type;
        (*pos) ++;
        uint32_t rhs = parse_mul(pos, ok);
        if(!*ok) return 0;
        if(op == TK_PLUS) val = val + rhs;
        else val = val - rhs;
    }
    return val;
}

uint32_t parse_mul(int *pos, bool *ok) {  // * /
    uint32_t val = parse_unary(pos, ok);
    if(!*ok) return 0;
    while(*pos < nr_token && (tokens[*pos].type == TK_MUL || tokens[*pos].type == TK_DIV)) {
        int op = tokens[*pos].type;
        (*pos) ++;
        uint32_t rhs = parse_unary(pos, ok);
        if(!*ok) return 0;
        if(op == TK_MUL) val = val * rhs;
        else {
            if(rhs == 0) {
                printf("calculation error: divide zero\n");
                *ok = false;
                return 0;
            }
            val = val / rhs;
        }
    }
    return val;
}

uint32_t parse_unary(int *pos, bool *ok) {
    int op = tokens[*pos].type;
    uint32_t val = 0;
    if(op == TK_MINUS || op == TK_DEREF) {
        (*pos) ++;
        val = parse_unary(pos, ok);
        if(!*ok) return 0;
        if(op == TK_MINUS) val = -val;   // uint will get complement type
        else val = paddr_read(val, 4);   // 解引用按字（4字节）读取
    }
    else {
        val = parse_primary(pos, ok);
        if(!*ok) return 0;
    }
    return val;
}

uint32_t parse_primary(int *pos, bool *ok) {
    int tk_type = tokens[*pos].type;
    uint32_t val = 0;
    switch(tk_type) {
        case TK_NUM:
            val = strtoul(tokens[*pos].str, NULL, 10);
            (*pos) ++;
            break;
        case TK_HEX:
            val = strtoul(tokens[*pos].str + 2, NULL, 16);
            (*pos) ++;
            break;
        case TK_REG:
            if(strcmp(tokens[*pos].str+1, "pc") == 0) val = cpu.pc;
            else {
                int i = 0;
                while(strcmp(tokens[*pos].str + 1, GPR_NAMES[i]) && i < 32) i++;
                if(i == 32) {
                    printf("syntax error: invalid register name\n");
                    *ok = false;
                    return 0;
                }
                val = cpu.gpr[i];
            }
            (*pos) ++;
            break;
        case TK_LPAREN:
            (*pos) ++;
            val = parse_expr(pos,ok);
            if(!*ok) return 0;
            if(*pos >= nr_token || tokens[*pos].type != TK_RPAREN) {
                printf("syntax error: no right parenthesis\n");
                *ok = false;
                return 0;
            }
            (*pos) ++;
            break;
        default:
            printf("syntax error: unexpected token %s\n",tokens[*pos].str);
            *ok = false;
            return 0;
    }
    return val;
}