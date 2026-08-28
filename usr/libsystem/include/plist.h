/* =============================================================================
 * Chimera Operating System — Apple XML Property List (plist) Parser & DOM Engine
 * usr/libsystem/include/plist.h
 * ============================================================================= */

#ifndef _PLIST_H_
#define _PLIST_H_

#include <sys/types.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PLIST_TYPE_NULL = 0,
    PLIST_TYPE_BOOLEAN,
    PLIST_TYPE_INTEGER,
    PLIST_TYPE_REAL,
    PLIST_TYPE_STRING,
    PLIST_TYPE_DATA,
    PLIST_TYPE_DATE,
    PLIST_TYPE_ARRAY,
    PLIST_TYPE_DICT
} plist_type_t;

typedef struct plist_node plist_t;

struct plist_dict_entry {
    char *key;
    plist_t *value;
    struct plist_dict_entry *next;
};

struct plist_array_entry {
    plist_t *value;
    struct plist_array_entry *next;
};

struct plist_node {
    plist_type_t type;
    union {
        int bool_val;
        int64_t int_val;
        double real_val;
        char *str_val;
        struct {
            struct plist_dict_entry *head;
            size_t count;
        } dict;
        struct {
            struct plist_array_entry *head;
            size_t count;
        } array;
    } u;
};

/* Parsing */
plist_t *plist_read_file(const char *path);
plist_t *plist_from_xml(const char *xml_str, size_t len);

/* Node inspection */
plist_type_t plist_get_type(const plist_t *node);
const char *plist_get_string_val(const plist_t *node);
int64_t plist_get_int_val(const plist_t *node);
int plist_get_bool_val(const plist_t *node);

/* Dictionary helpers */
plist_t *plist_dict_get(const plist_t *dict, const char *key);
const char *plist_dict_get_string(const plist_t *dict, const char *key);
int64_t plist_dict_get_int(const plist_t *dict, const char *key, int64_t default_val);
int plist_dict_get_bool(const plist_t *dict, const char *key, int default_val);

/* Array helpers */
size_t plist_array_get_size(const plist_t *arr);
plist_t *plist_array_get(const plist_t *arr, size_t index);

/* Creation & Mutation */
plist_t *plist_create_string(const char *str);
plist_t *plist_create_int(int64_t val);
plist_t *plist_create_bool(int val);
plist_t *plist_create_dict(void);
plist_t *plist_create_array(void);
int plist_dict_set(plist_t *dict, const char *key, plist_t *val);
int plist_dict_set_string(plist_t *dict, const char *key, const char *val);
int plist_dict_set_int(plist_t *dict, const char *key, int64_t val);
int plist_dict_set_bool(plist_t *dict, const char *key, int val);
int plist_dict_remove(plist_t *dict, const char *key);

/* Serialization & File Output */
char *plist_to_xml(const plist_t *node, size_t *out_len);
int plist_write_file(const plist_t *node, const char *path);

/* Memory management and formatting */
void plist_free(plist_t *node);
void plist_dump(const plist_t *node, int indent);

#ifdef __cplusplus
}
#endif

#endif /* _PLIST_H_ */
