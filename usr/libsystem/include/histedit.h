#ifndef _HISTEDIT_H
#define _HISTEDIT_H

// stub histedit to satisfy myhistedit.h types
typedef void History;
typedef void EditLine;
typedef struct { int num; const char *str; } HistEvent;

#define H_ENTER  1
#define H_APPEND 2
#define H_FIRST  3
#define H_LAST   4
#define H_NEXT   5
#define H_PREV   6
#define H_NEXT_EVENT 7
#define H_PREV_EVENT 8
#define H_PREV_STR   9
#define H_SETSIZE    10

#define EL_PROMPT      1
#define EL_TERMINAL    2
#define EL_EDITOR      3
#define EL_HIST        4
#define EL_PROMPT_ESC  5

static inline History *history_init(void) { return (void*)0; }
static inline void history_end(History *h) { (void)h; }
static inline int history(History *h, HistEvent *ev, int op, ...) { (void)h; (void)ev; (void)op; return 0; }

static inline EditLine *el_init(const char *prog, void *fin, void *fout, void *ferr) { (void)prog; (void)fin; (void)fout; (void)ferr; return (void*)0; }
static inline void el_end(EditLine *e) { (void)e; }
static inline const char *el_gets(EditLine *e, int *cnt) { (void)e; *cnt = 0; return (void*)0; }
static inline int el_set(EditLine *e, int op, ...) { (void)e; (void)op; return 0; }
static inline int el_source(EditLine *e, const char *file) { (void)e; (void)file; return 0; }

#endif
