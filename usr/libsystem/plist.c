/* =============================================================================
 * Chimera Operating System — Apple XML Property List (plist) Parser & DOM Engine
 * usr/libsystem/plist.c
 * ============================================================================= */

#include <plist.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

#ifndef NULL
#define NULL ((void *)0)
#endif

/* ── XML Lexer & Tokenizer Helpers ────────────────────────────────────────── */

typedef struct {
    const char *src;
    size_t len;
    size_t pos;
} plist_parser_t;

static void skip_whitespace_and_comments(plist_parser_t *p) {
    while (p->pos < p->len) {
        char c = p->src[p->pos];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            p->pos++;
            continue;
        }
        // Skip XML comments <!-- ... -->
        if (p->pos + 4 <= p->len && strncmp(p->src + p->pos, "<!--", 4) == 0) {
            p->pos += 4;
            while (p->pos + 3 <= p->len && strncmp(p->src + p->pos, "-->", 3) != 0) {
                p->pos++;
            }
            if (p->pos + 3 <= p->len) {
                p->pos += 3;
            }
            continue;
        }
        // Skip DOCTYPE and <?xml ... ?>
        if (p->pos + 2 <= p->len && (strncmp(p->src + p->pos, "<?", 2) == 0 || strncmp(p->src + p->pos, "<!", 2) == 0)) {
            while (p->pos < p->len && p->src[p->pos] != '>') {
                p->pos++;
            }
            if (p->pos < p->len && p->src[p->pos] == '>') {
                p->pos++;
            }
            continue;
        }
        break;
    }
}

static char *decode_xml_entities(const char *src, size_t len) {
    char *out = (char *)malloc(len + 1);
    if (!out) return NULL;
    size_t out_idx = 0;
    for (size_t i = 0; i < len; i++) {
        if (src[i] == '&') {
            if (i + 4 < len && strncmp(src + i, "&amp;", 5) == 0) {
                out[out_idx++] = '&';
                i += 4;
            } else if (i + 3 < len && strncmp(src + i, "&lt;", 4) == 0) {
                out[out_idx++] = '<';
                i += 3;
            } else if (i + 3 < len && strncmp(src + i, "&gt;", 4) == 0) {
                out[out_idx++] = '>';
                i += 3;
            } else if (i + 5 < len && strncmp(src + i, "&quot;", 6) == 0) {
                out[out_idx++] = '"';
                i += 5;
            } else if (i + 5 < len && strncmp(src + i, "&apos;", 6) == 0) {
                out[out_idx++] = '\'';
                i += 5;
            } else {
                out[out_idx++] = src[i];
            }
        } else {
            out[out_idx++] = src[i];
        }
    }
    out[out_idx] = '\0';
    return out;
}

static plist_t *plist_node_alloc(plist_type_t type) {
    plist_t *n = (plist_t *)malloc(sizeof(plist_t));
    if (!n) return NULL;
    memset(n, 0, sizeof(plist_t));
    n->type = type;
    return n;
}

static plist_t *parse_plist_value(plist_parser_t *p);

static plist_t *parse_plist_dict(plist_parser_t *p) {
    plist_t *dict = plist_node_alloc(PLIST_TYPE_DICT);
    if (!dict) return NULL;

    struct plist_dict_entry **tail = &dict->u.dict.head;

    while (p->pos < p->len) {
        skip_whitespace_and_comments(p);
        if (p->pos >= p->len) break;

        // check for </dict>
        if (p->pos + 7 <= p->len && strncmp(p->src + p->pos, "</dict>", 7) == 0) {
            p->pos += 7;
            return dict;
        }

        // expect <key>
        if (p->pos + 5 <= p->len && strncmp(p->src + p->pos, "<key>", 5) == 0) {
            p->pos += 5;
            const char *kstart = p->src + p->pos;
            while (p->pos + 6 <= p->len && strncmp(p->src + p->pos, "</key>", 6) != 0) {
                p->pos++;
            }
            size_t klen = (p->src + p->pos) - kstart;
            char *key = decode_xml_entities(kstart, klen);
            if (p->pos + 6 <= p->len) p->pos += 6; // skip </key>

            skip_whitespace_and_comments(p);
            plist_t *val = parse_plist_value(p);

            struct plist_dict_entry *entry = (struct plist_dict_entry *)malloc(sizeof(struct plist_dict_entry));
            if (entry) {
                entry->key = key;
                entry->value = val;
                entry->next = NULL;
                *tail = entry;
                tail = &entry->next;
                dict->u.dict.count++;
            }
        } else {
            p->pos++;
        }
    }
    return dict;
}

static plist_t *parse_plist_array(plist_parser_t *p) {
    plist_t *arr = plist_node_alloc(PLIST_TYPE_ARRAY);
    if (!arr) return NULL;

    struct plist_array_entry **tail = &arr->u.array.head;

    while (p->pos < p->len) {
        skip_whitespace_and_comments(p);
        if (p->pos >= p->len) break;

        // check for </array>
        if (p->pos + 8 <= p->len && strncmp(p->src + p->pos, "</array>", 8) == 0) {
            p->pos += 8;
            return arr;
        }

        plist_t *val = parse_plist_value(p);
        if (val) {
            struct plist_array_entry *entry = (struct plist_array_entry *)malloc(sizeof(struct plist_array_entry));
            if (entry) {
                entry->value = val;
                entry->next = NULL;
                *tail = entry;
                tail = &entry->next;
                arr->u.array.count++;
            }
        } else {
            p->pos++;
        }
    }
    return arr;
}

static plist_t *parse_plist_value(plist_parser_t *p) {
    skip_whitespace_and_comments(p);
    if (p->pos >= p->len) return NULL;

    // <dict> or <dict/>
    if (p->pos + 7 <= p->len && strncmp(p->src + p->pos, "<dict/>", 7) == 0) {
        p->pos += 7;
        return plist_node_alloc(PLIST_TYPE_DICT);
    }
    if (p->pos + 6 <= p->len && strncmp(p->src + p->pos, "<dict>", 6) == 0) {
        p->pos += 6;
        return parse_plist_dict(p);
    }

    // <array> or <array/>
    if (p->pos + 8 <= p->len && strncmp(p->src + p->pos, "<array/>", 8) == 0) {
        p->pos += 8;
        return plist_node_alloc(PLIST_TYPE_ARRAY);
    }
    if (p->pos + 7 <= p->len && strncmp(p->src + p->pos, "<array>", 7) == 0) {
        p->pos += 7;
        return parse_plist_array(p);
    }

    // <string> or <string/>
    if (p->pos + 9 <= p->len && strncmp(p->src + p->pos, "<string/>", 9) == 0) {
        p->pos += 9;
        plist_t *n = plist_node_alloc(PLIST_TYPE_STRING);
        if (n) n->u.str_val = strdup("");
        return n;
    }
    if (p->pos + 8 <= p->len && strncmp(p->src + p->pos, "<string>", 8) == 0) {
        p->pos += 8;
        const char *start = p->src + p->pos;
        while (p->pos + 9 <= p->len && strncmp(p->src + p->pos, "</string>", 9) != 0) {
            p->pos++;
        }
        size_t len = (p->src + p->pos) - start;
        plist_t *n = plist_node_alloc(PLIST_TYPE_STRING);
        if (n) n->u.str_val = decode_xml_entities(start, len);
        if (p->pos + 9 <= p->len) p->pos += 9;
        return n;
    }

    // <integer>
    if (p->pos + 9 <= p->len && strncmp(p->src + p->pos, "<integer>", 9) == 0) {
        p->pos += 9;
        const char *start = p->src + p->pos;
        while (p->pos + 10 <= p->len && strncmp(p->src + p->pos, "</integer>", 10) != 0) {
            p->pos++;
        }
        size_t len = (p->src + p->pos) - start;
        char *str = decode_xml_entities(start, len);
        plist_t *n = plist_node_alloc(PLIST_TYPE_INTEGER);
        if (n && str) n->u.int_val = strtoll(str, NULL, 10);
        free(str);
        if (p->pos + 10 <= p->len) p->pos += 10;
        return n;
    }

    // <real>
    if (p->pos + 6 <= p->len && strncmp(p->src + p->pos, "<real>", 6) == 0) {
        p->pos += 6;
        const char *start = p->src + p->pos;
        while (p->pos + 7 <= p->len && strncmp(p->src + p->pos, "</real>", 7) != 0) {
            p->pos++;
        }
        size_t len = (p->src + p->pos) - start;
        char *str = decode_xml_entities(start, len);
        plist_t *n = plist_node_alloc(PLIST_TYPE_REAL);
        if (n && str) n->u.real_val = strtod(str, NULL);
        free(str);
        if (p->pos + 7 <= p->len) p->pos += 7;
        return n;
    }

    // <true/> and <false/>
    if (p->pos + 7 <= p->len && strncmp(p->src + p->pos, "<true/>", 7) == 0) {
        p->pos += 7;
        plist_t *n = plist_node_alloc(PLIST_TYPE_BOOLEAN);
        if (n) n->u.bool_val = 1;
        return n;
    }
    if (p->pos + 8 <= p->len && strncmp(p->src + p->pos, "<false/>", 8) == 0) {
        p->pos += 8;
        plist_t *n = plist_node_alloc(PLIST_TYPE_BOOLEAN);
        if (n) n->u.bool_val = 0;
        return n;
    }

    // <date>
    if (p->pos + 6 <= p->len && strncmp(p->src + p->pos, "<date>", 6) == 0) {
        p->pos += 6;
        const char *start = p->src + p->pos;
        while (p->pos + 7 <= p->len && strncmp(p->src + p->pos, "</date>", 7) != 0) {
            p->pos++;
        }
        size_t len = (p->src + p->pos) - start;
        plist_t *n = plist_node_alloc(PLIST_TYPE_DATE);
        if (n) n->u.str_val = decode_xml_entities(start, len);
        if (p->pos + 7 <= p->len) p->pos += 7;
        return n;
    }

    // <data>
    if (p->pos + 6 <= p->len && strncmp(p->src + p->pos, "<data>", 6) == 0) {
        p->pos += 6;
        const char *start = p->src + p->pos;
        while (p->pos + 7 <= p->len && strncmp(p->src + p->pos, "</data>", 7) != 0) {
            p->pos++;
        }
        size_t len = (p->src + p->pos) - start;
        plist_t *n = plist_node_alloc(PLIST_TYPE_DATA);
        if (n) n->u.str_val = decode_xml_entities(start, len);
        if (p->pos + 7 <= p->len) p->pos += 7;
        return n;
    }

    return NULL;
}

/* ── Public API Implementation ────────────────────────────────────────────── */

plist_t *plist_from_xml(const char *xml_str, size_t len) {
    if (!xml_str || len == 0) return NULL;

    plist_parser_t parser;
    parser.src = xml_str;
    parser.len = len;
    parser.pos = 0;

    skip_whitespace_and_comments(&parser);

    // Skip <plist ...>
    while (parser.pos < parser.len) {
        if (parser.pos + 6 <= parser.len && strncmp(parser.src + parser.pos, "<plist", 6) == 0) {
            while (parser.pos < parser.len && parser.src[parser.pos] != '>') {
                parser.pos++;
            }
            if (parser.pos < parser.len) parser.pos++; // skip '>'
            break;
        }
        parser.pos++;
    }

    return parse_plist_value(&parser);
}

plist_t *plist_read_file(const char *path) {
    if (!path) return NULL;
    int fd = open(path, O_RDONLY, 0);
    if (fd < 0) return NULL;

    off_t sz = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    if (sz <= 0 || sz > 10 * 1024 * 1024) {
        close(fd);
        return NULL;
    }

    char *buf = (char *)malloc(sz + 1);
    if (!buf) {
        close(fd);
        return NULL;
    }

    ssize_t rd = read(fd, buf, sz);
    close(fd);
    if (rd != sz) {
        free(buf);
        return NULL;
    }
    buf[sz] = '\0';

    plist_t *res = plist_from_xml(buf, sz);
    free(buf);
    return res;
}

plist_type_t plist_get_type(const plist_t *node) {
    return node ? node->type : PLIST_TYPE_NULL;
}

const char *plist_get_string_val(const plist_t *node) {
    if (!node || node->type != PLIST_TYPE_STRING) return NULL;
    return node->u.str_val;
}

int64_t plist_get_int_val(const plist_t *node) {
    if (!node || node->type != PLIST_TYPE_INTEGER) return 0;
    return node->u.int_val;
}

int plist_get_bool_val(const plist_t *node) {
    if (!node || node->type != PLIST_TYPE_BOOLEAN) return 0;
    return node->u.bool_val;
}

plist_t *plist_dict_get(const plist_t *dict, const char *key) {
    if (!dict || dict->type != PLIST_TYPE_DICT || !key) return NULL;
    struct plist_dict_entry *curr = dict->u.dict.head;
    while (curr) {
        if (curr->key && strcmp(curr->key, key) == 0) {
            return curr->value;
        }
        curr = curr->next;
    }
    return NULL;
}

const char *plist_dict_get_string(const plist_t *dict, const char *key) {
    plist_t *val = plist_dict_get(dict, key);
    return plist_get_string_val(val);
}

int64_t plist_dict_get_int(const plist_t *dict, const char *key, int64_t default_val) {
    plist_t *val = plist_dict_get(dict, key);
    if (!val || val->type != PLIST_TYPE_INTEGER) return default_val;
    return val->u.int_val;
}

int plist_dict_get_bool(const plist_t *dict, const char *key, int default_val) {
    plist_t *val = plist_dict_get(dict, key);
    if (!val || val->type != PLIST_TYPE_BOOLEAN) return default_val;
    return val->u.bool_val;
}

size_t plist_array_get_size(const plist_t *arr) {
    if (!arr || arr->type != PLIST_TYPE_ARRAY) return 0;
    return arr->u.array.count;
}

plist_t *plist_array_get(const plist_t *arr, size_t index) {
    if (!arr || arr->type != PLIST_TYPE_ARRAY) return NULL;
    struct plist_array_entry *curr = arr->u.array.head;
    size_t i = 0;
    while (curr) {
        if (i == index) return curr->value;
        i++;
        curr = curr->next;
    }
    return NULL;
}

void plist_free(plist_t *node) {
    if (!node) return;
    switch (node->type) {
        case PLIST_TYPE_STRING:
        case PLIST_TYPE_DATE:
        case PLIST_TYPE_DATA:
            if (node->u.str_val) free(node->u.str_val);
            break;
        case PLIST_TYPE_DICT: {
            struct plist_dict_entry *curr = node->u.dict.head;
            while (curr) {
                struct plist_dict_entry *next = curr->next;
                if (curr->key) free(curr->key);
                if (curr->value) plist_free(curr->value);
                free(curr);
                curr = next;
            }
            break;
        }
        case PLIST_TYPE_ARRAY: {
            struct plist_array_entry *curr = node->u.array.head;
            while (curr) {
                struct plist_array_entry *next = curr->next;
                if (curr->value) plist_free(curr->value);
                free(curr);
                curr = next;
            }
            break;
        }
        default:
            break;
    }
    free(node);
}

static void print_indent(int indent) {
    for (int i = 0; i < indent; i++) printf("  ");
}

void plist_dump(const plist_t *node, int indent) {
    if (!node) {
        printf("(null)\n");
        return;
    }
    switch (node->type) {
        case PLIST_TYPE_STRING:
            printf("\"%s\"\n", node->u.str_val ? node->u.str_val : "");
            break;
        case PLIST_TYPE_INTEGER:
            printf("%lld\n", (long long)node->u.int_val);
            break;
        case PLIST_TYPE_REAL:
            printf("%f\n", node->u.real_val);
            break;
        case PLIST_TYPE_BOOLEAN:
            printf("%s\n", node->u.bool_val ? "true" : "false");
            break;
        case PLIST_TYPE_DATE:
            printf("Date: %s\n", node->u.str_val ? node->u.str_val : "");
            break;
        case PLIST_TYPE_DATA:
            printf("Data (%zu bytes)\n", node->u.str_val ? strlen(node->u.str_val) : 0);
            break;
        case PLIST_TYPE_DICT: {
            printf("{\n");
            struct plist_dict_entry *curr = node->u.dict.head;
            while (curr) {
                print_indent(indent + 1);
                printf("\"%s\" = ", curr->key ? curr->key : "");
                plist_dump(curr->value, indent + 1);
                curr = curr->next;
            }
            print_indent(indent);
            printf("}\n");
            break;
        }
        case PLIST_TYPE_ARRAY: {
            printf("(\n");
            struct plist_array_entry *curr = node->u.array.head;
            while (curr) {
                print_indent(indent + 1);
                plist_dump(curr->value, indent + 1);
                curr = curr->next;
            }
            print_indent(indent);
            printf(")\n");
            break;
        }
        default:
            printf("(unknown)\n");
            break;
    }
}

/* ── Mutation & Creation API ────────────────────────────────────────────── */

plist_t *plist_create_string(const char *str) {
    plist_t *n = plist_node_alloc(PLIST_TYPE_STRING);
    if (n && str) n->u.str_val = strdup(str);
    return n;
}

plist_t *plist_create_int(int64_t val) {
    plist_t *n = plist_node_alloc(PLIST_TYPE_INTEGER);
    if (n) n->u.int_val = val;
    return n;
}

plist_t *plist_create_bool(int val) {
    plist_t *n = plist_node_alloc(PLIST_TYPE_BOOLEAN);
    if (n) n->u.bool_val = val ? 1 : 0;
    return n;
}

plist_t *plist_create_dict(void) {
    return plist_node_alloc(PLIST_TYPE_DICT);
}

plist_t *plist_create_array(void) {
    return plist_node_alloc(PLIST_TYPE_ARRAY);
}

int plist_dict_set(plist_t *dict, const char *key, plist_t *val) {
    if (!dict || dict->type != PLIST_TYPE_DICT || !key) return -1;
    struct plist_dict_entry *curr = dict->u.dict.head;
    while (curr) {
        if (curr->key && strcmp(curr->key, key) == 0) {
            if (curr->value) plist_free(curr->value);
            curr->value = val;
            return 0;
        }
        curr = curr->next;
    }
    struct plist_dict_entry *entry = (struct plist_dict_entry *)malloc(sizeof(struct plist_dict_entry));
    if (!entry) return -1;
    entry->key = strdup(key);
    entry->value = val;
    entry->next = dict->u.dict.head;
    dict->u.dict.head = entry;
    dict->u.dict.count++;
    return 0;
}

int plist_dict_set_string(plist_t *dict, const char *key, const char *val) {
    return plist_dict_set(dict, key, plist_create_string(val));
}

int plist_dict_set_int(plist_t *dict, const char *key, int64_t val) {
    return plist_dict_set(dict, key, plist_create_int(val));
}

int plist_dict_set_bool(plist_t *dict, const char *key, int val) {
    return plist_dict_set(dict, key, plist_create_bool(val));
}

int plist_dict_remove(plist_t *dict, const char *key) {
    if (!dict || dict->type != PLIST_TYPE_DICT || !key) return -1;
    struct plist_dict_entry **curr = &dict->u.dict.head;
    while (*curr) {
        if ((*curr)->key && strcmp((*curr)->key, key) == 0) {
            struct plist_dict_entry *to_free = *curr;
            *curr = to_free->next;
            if (to_free->key) free(to_free->key);
            if (to_free->value) plist_free(to_free->value);
            free(to_free);
            dict->u.dict.count--;
            return 0;
        }
        curr = &(*curr)->next;
    }
    return -1;
}

static void buf_append(char **buf, size_t *cap, size_t *len, const char *str) {
    if (!str) return;
    size_t slen = strlen(str);
    if (*len + slen + 1 >= *cap) {
        *cap = (*cap + slen + 1024) * 2;
        *buf = (char *)realloc(*buf, *cap);
    }
    if (*buf) {
        memcpy(*buf + *len, str, slen);
        *len += slen;
        (*buf)[*len] = '\0';
    }
}

static void buf_append_indent(char **buf, size_t *cap, size_t *len, int indent) {
    for (int i = 0; i < indent; i++) buf_append(buf, cap, len, "  ");
}

static void serialize_xml_node(const plist_t *node, char **buf, size_t *cap, size_t *len, int indent) {
    if (!node) return;
    char tmp[128];
    switch (node->type) {
        case PLIST_TYPE_STRING:
            buf_append(buf, cap, len, "<string>");
            buf_append(buf, cap, len, node->u.str_val ? node->u.str_val : "");
            buf_append(buf, cap, len, "</string>\n");
            break;
        case PLIST_TYPE_INTEGER:
            sprintf(tmp, "<integer>%lld</integer>\n", (long long)node->u.int_val);
            buf_append(buf, cap, len, tmp);
            break;
        case PLIST_TYPE_REAL:
            sprintf(tmp, "<real>%f</real>\n", node->u.real_val);
            buf_append(buf, cap, len, tmp);
            break;
        case PLIST_TYPE_BOOLEAN:
            buf_append(buf, cap, len, node->u.bool_val ? "<true/>\n" : "<false/>\n");
            break;
        case PLIST_TYPE_DATE:
            buf_append(buf, cap, len, "<date>");
            buf_append(buf, cap, len, node->u.str_val ? node->u.str_val : "");
            buf_append(buf, cap, len, "</date>\n");
            break;
        case PLIST_TYPE_DATA:
            buf_append(buf, cap, len, "<data>");
            buf_append(buf, cap, len, node->u.str_val ? node->u.str_val : "");
            buf_append(buf, cap, len, "</data>\n");
            break;
        case PLIST_TYPE_DICT:
            buf_append(buf, cap, len, "<dict>\n");
            for (struct plist_dict_entry *curr = node->u.dict.head; curr; curr = curr->next) {
                buf_append_indent(buf, cap, len, indent + 1);
                buf_append(buf, cap, len, "<key>");
                buf_append(buf, cap, len, curr->key ? curr->key : "");
                buf_append(buf, cap, len, "</key>\n");
                buf_append_indent(buf, cap, len, indent + 1);
                serialize_xml_node(curr->value, buf, cap, len, indent + 1);
            }
            buf_append_indent(buf, cap, len, indent);
            buf_append(buf, cap, len, "</dict>\n");
            break;
        case PLIST_TYPE_ARRAY:
            buf_append(buf, cap, len, "<array>\n");
            for (struct plist_array_entry *curr = node->u.array.head; curr; curr = curr->next) {
                buf_append_indent(buf, cap, len, indent + 1);
                serialize_xml_node(curr->value, buf, cap, len, indent + 1);
            }
            buf_append_indent(buf, cap, len, indent);
            buf_append(buf, cap, len, "</array>\n");
            break;
        default:
            break;
    }
}

char *plist_to_xml(const plist_t *node, size_t *out_len) {
    if (!node) return NULL;
    size_t cap = 2048;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;
    buf[0] = '\0';

    buf_append(&buf, &cap, &len, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    buf_append(&buf, &cap, &len, "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n");
    buf_append(&buf, &cap, &len, "<plist version=\"1.0\">\n");
    serialize_xml_node(node, &buf, &cap, &len, 0);
    buf_append(&buf, &cap, &len, "</plist>\n");

    if (out_len) *out_len = len;
    return buf;
}

int plist_write_file(const plist_t *node, const char *path) {
    if (!node || !path) return -1;
    size_t xlen = 0;
    char *xml = plist_to_xml(node, &xlen);
    if (!xml) return -1;

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        free(xml);
        return -1;
    }

    ssize_t written = write(fd, xml, xlen);
    close(fd);
    free(xml);
    return (written == (ssize_t)xlen) ? 0 : -1;
}
