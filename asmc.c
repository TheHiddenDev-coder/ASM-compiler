#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>

#define MAX_LINE     512
#define MAX_TOKEN    256
#define MAX_VARS     256
#define MAX_TOKENS   64

/* ── Data types ─────────────────────────────────────────────────── */
typedef enum {
    TY_BOOL,
    TY_STR,
    TY_FLOAT32,
    TY_FLOAT64,
    /* unsigned ints: uint4 uint8 uint16 uint32 uint64 uint128 */
    TY_UINT4, TY_UINT8, TY_UINT16, TY_UINT32, TY_UINT64, TY_UINT128,
    /* signed ints: int4 int8 int16 int32 int64 int128 */
    TY_INT4,  TY_INT8,  TY_INT16,  TY_INT32,  TY_INT64,  TY_INT128,
    TY_UNKNOWN
} VarType;

typedef struct {
    char    name[MAX_TOKEN];
    VarType type;
} Var;

static Var   vars[MAX_VARS];
static int   var_count = 0;
static int   line_num  = 0;
static FILE *out;

/* ── Helpers ─────────────────────────────────────────────────────── */
static void die(const char *msg) {
    fprintf(stderr, "Error (line %d): %s\n", line_num, msg);
    exit(1);
}

static VarType parse_type(const char *t) {
    if (!strcmp(t,"bool"))    return TY_BOOL;
    if (!strcmp(t,"str"))     return TY_STR;
    if (!strcmp(t,"float32")) return TY_FLOAT32;
    if (!strcmp(t,"float64")) return TY_FLOAT64;
    if (!strcmp(t,"uint4"))   return TY_UINT4;
    if (!strcmp(t,"uint8"))   return TY_UINT8;
    if (!strcmp(t,"uint16"))  return TY_UINT16;
    if (!strcmp(t,"uint32"))  return TY_UINT32;
    if (!strcmp(t,"uint64"))  return TY_UINT64;
    if (!strcmp(t,"uint128")) return TY_UINT128;
    if (!strcmp(t,"int4"))    return TY_INT4;
    if (!strcmp(t,"int8"))    return TY_INT8;
    if (!strcmp(t,"int16"))   return TY_INT16;
    if (!strcmp(t,"int32"))   return TY_INT32;
    if (!strcmp(t,"int64"))   return TY_INT64;
    if (!strcmp(t,"int128"))  return TY_INT128;
    return TY_UNKNOWN;
}

static const char *ctype(VarType t) {
    switch (t) {
        case TY_BOOL:    return "int";
        case TY_STR:     return "char*";
        case TY_FLOAT32: return "float";
        case TY_FLOAT64: return "double";
        case TY_UINT4:   return "uint8_t";   /* no 4-bit in C, use next up */
        case TY_UINT8:   return "uint8_t";
        case TY_UINT16:  return "uint16_t";
        case TY_UINT32:  return "uint32_t";
        case TY_UINT64:  return "uint64_t";
        case TY_UINT128: return "unsigned __int128";
        case TY_INT4:    return "int8_t";
        case TY_INT8:    return "int8_t";
        case TY_INT16:   return "int16_t";
        case TY_INT32:   return "int32_t";
        case TY_INT64:   return "int64_t";
        case TY_INT128:  return "__int128";
        default:         return "void*";
    }
}

/* printf / scanf format specifier for a type */
static const char *fmt_print(VarType t) {
    switch (t) {
        case TY_BOOL:    return "%d";
        case TY_STR:     return "%s";
        case TY_FLOAT32: return "%f";
        case TY_FLOAT64: return "%lf";
        case TY_UINT4:
        case TY_UINT8:   return "%hhu";
        case TY_UINT16:  return "%hu";
        case TY_UINT32:  return "%u";
        case TY_UINT64:  return "%" PRIu64;
        case TY_INT4:
        case TY_INT8:    return "%hhd";
        case TY_INT16:   return "%hd";
        case TY_INT32:   return "%d";
        case TY_INT64:   return "%" PRId64;
        /* 128-bit: no standard specifier; we cast */
        default:         return "%lld";
    }
}
static const char *fmt_scan(VarType t) {
    /* scanf needs different specifiers for some types */
    switch (t) {
        case TY_FLOAT32: return "%f";
        case TY_FLOAT64: return "%lf";
        case TY_STR:     return "%255s";  /* bounded */
        default:         return fmt_print(t);
    }
}

static Var *find_var(const char *name) {
    for (int i = 0; i < var_count; i++)
        if (!strcmp(vars[i].name, name))
            return &vars[i];
    return NULL;
}

static int is_type_kw(const char *t) { return parse_type(t) != TY_UNKNOWN; }

/* ── Tokeniser (respects quoted strings) ────────────────────────── */
static int tokenise(char *line, char tokens[][MAX_TOKEN]) {
    int n = 0;
    char *p = line;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (!*p || *p == ';' || *p == '\n' || *p == '\r') break;

        if (*p == '"') {
            /* quoted string – keep quotes */
            char *dst = tokens[n++];
            *dst++ = *p++;
            while (*p && *p != '"') {
                if (*p == '\\' && *(p+1)) *dst++ = *p++;
                *dst++ = *p++;
            }
            if (*p == '"') *dst++ = *p++;
            *dst = '\0';
        } else {
            char *dst = tokens[n++];
            while (*p && *p != ' ' && *p != '\t' && *p != ',' &&
                   *p != ';' && *p != '\n' && *p != '\r')
                *dst++ = *p++;
            *dst = '\0';
        }
        if (n >= MAX_TOKENS) break;
    }
    return n;
}

/* ── Code generation ─────────────────────────────────────────────── */
static void compile_file(FILE *in) {
    /* Emit C header */
    fprintf(out,
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <stdint.h>\n"
        "#include <string.h>\n\n"
        "int main(void) {\n"
    );

    char   raw[MAX_LINE];
    char   tokens[MAX_TOKENS][MAX_TOKEN];

    /* ── First pass: collect variable declarations for forward-decl ── */
    /* We do a single-pass and emit declarations inline (valid in C99+) */

    rewind(in);
    line_num = 0;

    while (fgets(raw, sizeof raw, in)) {
        line_num++;
        char line[MAX_LINE];
        strcpy(line, raw);

        /* strip leading whitespace for comment detection */
        char *trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
        if (*trimmed == ';' || *trimmed == '\n' || *trimmed == '\r' || !*trimmed)
            continue;

        int n = tokenise(line, tokens);
        if (n == 0) continue;

        /* ── Variable declaration: <type> <name> ── */
        if (is_type_kw(tokens[0])) {
            if (n < 2) die("type keyword without variable name");
            VarType ty = parse_type(tokens[0]);
            const char *name = tokens[1];

            if (find_var(name)) die("variable already declared");
            strcpy(vars[var_count].name, name);
            vars[var_count].type = ty;
            var_count++;

            if (ty == TY_STR) {
                fprintf(out, "    char %s[256] = \"\";\n", name);
            } else if (ty == TY_BOOL) {
                fprintf(out, "    int %s = 0;\n", name);
            } else {
                fprintf(out, "    %s %s = 0;\n", ctype(ty), name);
            }
            continue;
        }

        /* ── mov <var>, <value> ── */
        if (!strcmp(tokens[0], "mov")) {
            if (n < 3) die("mov requires 2 operands");
            Var *v = find_var(tokens[1]);
            if (!v) die("mov: undeclared variable");

            if (v->type == TY_STR) {
                /* value may be quoted string or another var */
                if (tokens[2][0] == '"') {
                    /* strip outer quotes */
                    char tmp[MAX_TOKEN];
                    strncpy(tmp, tokens[2]+1, MAX_TOKEN-1);
                    int len = strlen(tmp);
                    if (len > 0 && tmp[len-1] == '"') tmp[len-1] = '\0';
                    fprintf(out, "    strncpy(%s, \"%s\", 255);\n", v->name, tmp);
                } else {
                    fprintf(out, "    strncpy(%s, %s, 255);\n", v->name, tokens[2]);
                }
            } else if (v->type == TY_BOOL) {
                /* accept true/false/0/1 */
                const char *val = tokens[2];
                if (!strcmp(val,"true"))       fprintf(out, "    %s = 1;\n", v->name);
                else if (!strcmp(val,"false")) fprintf(out, "    %s = 0;\n", v->name);
                else                           fprintf(out, "    %s = (%s)(%s);\n", v->name, ctype(v->type), val);
            } else {
                fprintf(out, "    %s = (%s)(%s);\n", v->name, ctype(v->type), tokens[2]);
            }
            continue;
        }

        /* ── Arithmetic: add/sub/mul/div <var>, <operand> ── */
        if (!strcmp(tokens[0],"add") || !strcmp(tokens[0],"sub") ||
            !strcmp(tokens[0],"mul") || !strcmp(tokens[0],"div")) {

            if (n < 3) die("arithmetic op requires 2 operands");
            Var *v = find_var(tokens[1]);
            if (!v) die("arithmetic: undeclared variable");
            if (v->type == TY_STR || v->type == TY_BOOL)
                die("arithmetic not supported on str/bool");

            char op = tokens[0][0] == 'a' ? '+' :
                      tokens[0][0] == 's' ? '-' :
                      tokens[0][0] == 'm' ? '*' : '/';

            fprintf(out, "    %s %c= (%s)(%s);\n",
                    v->name, op, ctype(v->type), tokens[2]);
            continue;
        }

        /* ── mod <var>, <operand> ── */
        if (!strcmp(tokens[0], "mod")) {
            if (n < 3) die("mod requires 2 operands");
            Var *v = find_var(tokens[1]);
            if (!v) die("mod: undeclared variable");
            if (v->type == TY_FLOAT32 || v->type == TY_FLOAT64)
                die("mod not supported on floats (use fmod)");
            fprintf(out, "    %s %%= (%s)(%s);\n",
                    v->name, ctype(v->type), tokens[2]);
            continue;
        }

        /* ── log <value> ── */
        if (!strcmp(tokens[0], "log")) {
            if (n < 2) die("log requires an argument");
            const char *arg = tokens[1];
            if (arg[0] == '"') {
                /* string literal – strip outer quotes, re-escape */
                char tmp[MAX_TOKEN];
                strncpy(tmp, arg+1, MAX_TOKEN-1);
                int len = strlen(tmp);
                if (len > 0 && tmp[len-1] == '"') tmp[len-1] = '\0';
                fprintf(out, "    printf(\"%%s\", \"%s\");\n", tmp);
            } else {
                Var *v = find_var(arg);
                if (!v) {
                    /* bare literal (number) */
                    fprintf(out, "    printf(\"%s\", %s);\n", "%s", arg);
                } else if (v->type == TY_STR) {
                    fprintf(out, "    printf(\"%%s\", %s);\n", v->name);
                } else if (v->type == TY_BOOL) {
                    fprintf(out, "    printf(\"%%s\", %s ? \"true\" : \"false\");\n", v->name);
                } else if (v->type == TY_UINT128) {
                    /* no standard format; print via helper */
                    fprintf(out,
                        "    { unsigned __int128 _tmp = %s; if(_tmp==0){printf(\"0\");}else{"
                        "char _buf[40]; int _i=39; _buf[_i]='\\0';"
                        "while(_tmp>0){_buf[--_i]='0'+(_tmp%%10);_tmp/=10;}"
                        "printf(\"%%s\",_buf+_i);} }\n", v->name);
                } else if (v->type == TY_INT128) {
                    fprintf(out,
                        "    { __int128 _tmp = %s; if(_tmp==0){printf(\"0\");}else{"
                        "char _buf[41]; int _i=40; _buf[_i]='\\0'; int _neg=(_tmp<0);"
                        "if(_neg)_tmp=-_tmp;"
                        "while(_tmp>0){_buf[--_i]='0'+(_tmp%%10);_tmp/=10;}"
                        "if(_neg)_buf[--_i]='-';"
                        "printf(\"%%s\",_buf+_i);} }\n", v->name);
                } else {
                    fprintf(out, "    printf(\"%s\", %s);\n", fmt_print(v->type), v->name);
                }
            }
            continue;
        }

        /* ── logln <value>  (log + newline) ── */
        if (!strcmp(tokens[0], "logln")) {
            if (n < 2) {
                fprintf(out, "    printf(\"\\n\");\n");
            } else {
                const char *arg = tokens[1];
                if (arg[0] == '"') {
                    char tmp[MAX_TOKEN];
                    strncpy(tmp, arg+1, MAX_TOKEN-1);
                    int len = strlen(tmp);
                    if (len > 0 && tmp[len-1] == '"') tmp[len-1] = '\0';
                    fprintf(out, "    printf(\"%%s\\n\", \"%s\");\n", tmp);
                } else {
                    Var *v = find_var(arg);
                    if (!v) {
                        fprintf(out, "    printf(\"%s\\n\", %s);\n", "%s", arg);
                    } else if (v->type == TY_STR) {
                        fprintf(out, "    printf(\"%%s\\n\", %s);\n", v->name);
                    } else if (v->type == TY_BOOL) {
                        fprintf(out, "    printf(\"%%s\\n\", %s ? \"true\" : \"false\");\n", v->name);
                    } else {
                        fprintf(out, "    printf(\"%s\\n\", %s);\n", fmt_print(v->type), v->name);
                    }
                }
            }
            continue;
        }

        /* ── read <var> ── */
        if (!strcmp(tokens[0], "read")) {
            if (n < 2) die("read requires a variable name");
            Var *v = find_var(tokens[1]);
            if (!v) die("read: undeclared variable");

            if (v->type == TY_STR) {
                fprintf(out, "    scanf(\"%s\", %s);\n", fmt_scan(TY_STR), v->name);
            } else if (v->type == TY_BOOL) {
                fprintf(out, "    { int _b; scanf(\"%%d\", &_b); %s = !!_b; }\n", v->name);
            } else if (v->type == TY_UINT128 || v->type == TY_INT128) {
                /* read as string, parse manually */
                fprintf(out,
                    "    { char _buf[64]; scanf(\"%%63s\", _buf);\n"
                    "      %s _acc = 0; char *_p = _buf;\n"
                    "      int _neg = (*_p == '-'); if(_neg) _p++;\n"
                    "      while(*_p) _acc = _acc*10 + (*_p++ - '0');\n"
                    "      %s = _neg ? -_acc : _acc; }\n",
                    ctype(v->type), v->name);
            } else {
                fprintf(out, "    scanf(\"%s\", &%s);\n", fmt_scan(v->type), v->name);
            }
            continue;
        }

        /* ── Unknown instruction ── */
        char errbuf[MAX_LINE];
        snprintf(errbuf, sizeof errbuf, "unknown instruction '%s'", tokens[0]);
        die(errbuf);
    }

    fprintf(out, "    return 0;\n}\n");
}

/* ── Entry point ─────────────────────────────────────────────────── */
int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <input.asm> <output.c>\n", argv[0]);
        return 1;
    }

    FILE *in = fopen(argv[1], "r");
    if (!in) { perror("open input"); return 1; }

    out = fopen(argv[2], "w");
    if (!out) { perror("open output"); fclose(in); return 1; }

    compile_file(in);

    fclose(in);
    fclose(out);
    printf("Compiled '%s' -> '%s'\n", argv[1], argv[2]);
    return 0;
}