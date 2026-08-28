/*
 * Chimera OS Objective-C 2.0 Core Runtime Implementation
 * ABI compliant with Apple/ravynOS ObjC runtime
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <objc/objc.h>
#include <objc/runtime.h>

typedef unsigned long NSUInteger;
typedef double CGFloat;
typedef double NSTimeInterval;

struct method_2_t {
    const char *name;
    const char *types;
    void *imp;
};

struct method_list_2_t {
    uint32_t entsizeAndFlags;
    uint32_t count;
    struct method_2_t methods[1];
};

struct class_ro_2_t {
    uint32_t flags;
    uint32_t instanceStart;
    uint32_t instanceSize;
    uint32_t reserved;
    const uint8_t *ivarLayout;
    const char *name;
    const struct method_list_2_t *baseMethodList;
    const void *baseProtocols;
    const void *ivars;
    const uint8_t *weakIvarLayout;
    const void *baseProperties;
};

struct objc_class_2 {
    Class isa;
    Class superclass;
    void *cache;
    void *vtable;
    uintptr_t data_bits;
};

struct objc_super {
    id receiver;
    Class super_class;
};

id objc_alloc(Class cls);
id objc_alloc_init(Class cls);

static void *default_nil_stub(id self, SEL _cmd, ...) {
    (void)self; (void)_cmd;
    return NULL;
}

static id default_alloc(id self, SEL _cmd, ...) {
    (void)_cmd;
    return objc_alloc((Class)self);
}

static id default_init(id self, SEL _cmd, ...) {
    (void)_cmd;
    return self;
}

static id default_new(id self, SEL _cmd, ...) {
    (void)_cmd;
    return objc_alloc_init((Class)self);
}

static id default_self(id self, SEL _cmd, ...) {
    (void)_cmd;
    return self;
}

static void default_release(id self, SEL _cmd, ...) {
    (void)self; (void)_cmd;
}

static void default_dealloc(id self, SEL _cmd, ...) {
    (void)_cmd;
    if (self) free(self);
}

static Class default_class(id self, SEL _cmd, ...) {
    (void)_cmd;
    return object_getClass(self);
}

struct category_2_t {
    const char *name;
    Class cls;
    const struct method_list_2_t *instanceMethods;
    const struct method_list_2_t *classMethods;
    const void *protocols;
    const void *instanceProperties;
};

struct category_entry_map {
    Class cls;
    const struct method_list_2_t *instanceMethods;
    const struct method_list_2_t *classMethods;
};
static struct category_entry_map s_categories[1024];
static int s_category_count = 0;

static void *search_method_list(const struct method_list_2_t *mlist, const char *sel_name) {
    if (!mlist || !sel_name) return NULL;
    uint32_t entsize = mlist->entsizeAndFlags & 0xffff;
    if (entsize < sizeof(struct method_2_t)) entsize = sizeof(struct method_2_t);
    uint32_t count = mlist->count;
    
    for (uint32_t i = 0; i < count; i++) {
        const struct method_2_t *m = (const struct method_2_t *)((const uint8_t *)&mlist->methods[0] + i * entsize);
        if (m->name) {
            if (m->name == sel_name || strcmp(m->name, sel_name) == 0) {
                return m->imp;
            }
        }
    }
    return NULL;
}

static void *class_find_method_imp(Class cls, const char *sel_name) {
    if (!cls || !sel_name) return NULL;
    if ((uintptr_t)cls < 0x1000ULL || (uintptr_t)cls >= 0x800000000000ULL) return NULL;
    
    struct objc_class_2 *c = (struct objc_class_2 *)cls;
    if (!c->data_bits || c->data_bits < 0x1000ULL || c->data_bits >= 0x800000000000ULL) return NULL;
    
    struct class_ro_2_t *ro = (struct class_ro_2_t *)(c->data_bits & ~7ULL);
    if (!ro || (uintptr_t)ro < 0x1000ULL || (uintptr_t)ro >= 0x800000000000ULL) return NULL;
    if (ro->baseMethodList && (uintptr_t)ro->baseMethodList >= 0x1000ULL && (uintptr_t)ro->baseMethodList < 0x800000000000ULL) {
        void *imp = search_method_list(ro->baseMethodList, sel_name);
        if (imp) return imp;
    }
    
    // Check attached categories
    bool is_meta = (ro && (ro->flags & 1));
    for (int i = 0; i < s_category_count; i++) {
        if (is_meta) {
            if (s_categories[i].cls && (object_getClass((id)s_categories[i].cls) == cls || s_categories[i].cls == cls)) {
                void *imp = search_method_list(s_categories[i].classMethods, sel_name);
                if (imp) return imp;
            }
        } else {
            if (s_categories[i].cls == cls) {
                void *imp = search_method_list(s_categories[i].instanceMethods, sel_name);
                if (imp) return imp;
            }
        }
    }
    
    return NULL;
}

void *objc_msgSend_lookup(id self, const char *sel_name) {
    if (!self) return (void *)default_nil_stub;
    Class cls = object_getClass(self);
    if (!cls) return (void *)default_nil_stub;

    // 1. Walk class and superclass hierarchy
    for (Class c = cls; c != Nil; ) {
        void *imp = class_find_method_imp(c, sel_name);
        if (imp) return imp;

        struct objc_class_2 *oc = (struct objc_class_2 *)c;
        c = oc->superclass;
    }

    // 2. Built-in standard root methods
    if (sel_name) {
        if (strcmp(sel_name, "new") == 0) return (void *)default_new;
        if (strcmp(sel_name, "alloc") == 0 || strcmp(sel_name, "allocWithZone:") == 0) return (void *)default_alloc;
        if (strcmp(sel_name, "init") == 0) return (void *)default_init;
        if (strcmp(sel_name, "retain") == 0 || strcmp(sel_name, "autorelease") == 0 || strcmp(sel_name, "self") == 0) return (void *)default_self;
        if (strcmp(sel_name, "release") == 0) return (void *)default_release;
        if (strcmp(sel_name, "dealloc") == 0) return (void *)default_dealloc;
        if (strcmp(sel_name, "class") == 0) return (void *)default_class;
    }

    return (void *)default_nil_stub;
}

void *objc_msgSendSuper_lookup(struct objc_super *sup, const char *sel_name) {
    if (!sup || !sup->receiver) return (void *)default_nil_stub;
    Class cls = sup->super_class;
    if (!cls) return (void *)default_nil_stub;
    
    for (Class c = cls; c != Nil; ) {
        void *imp = class_find_method_imp(c, sel_name);
        if (imp) return imp;
        
        struct objc_class_2 *oc = (struct objc_class_2 *)c;
        c = oc->superclass;
    }
    
    if (sel_name) {
        if (strcmp(sel_name, "init") == 0) return (void *)default_init;
        if (strcmp(sel_name, "retain") == 0 || strcmp(sel_name, "autorelease") == 0 || strcmp(sel_name, "self") == 0) return (void *)default_self;
        if (strcmp(sel_name, "release") == 0) return (void *)default_release;
        if (strcmp(sel_name, "dealloc") == 0) return (void *)default_dealloc;
        if (strcmp(sel_name, "class") == 0) return (void *)default_class;
    }
    
    return (void *)default_nil_stub;
}

void *objc_msgSendSuper2_lookup(struct objc_super *sup, const char *sel_name) {
    if (!sup || !sup->receiver) return (void *)default_nil_stub;
    Class cls = sup->super_class;
    if (!cls) return (void *)default_nil_stub;
    
    struct objc_class_2 *curr = (struct objc_class_2 *)cls;
    Class start_class = curr->superclass;
    
    for (Class c = start_class; c != Nil; ) {
        void *imp = class_find_method_imp(c, sel_name);
        if (imp) return imp;
        
        struct objc_class_2 *oc = (struct objc_class_2 *)c;
        c = oc->superclass;
    }
    
    if (sel_name) {
        if (strcmp(sel_name, "init") == 0) return (void *)default_init;
        if (strcmp(sel_name, "retain") == 0 || strcmp(sel_name, "autorelease") == 0 || strcmp(sel_name, "self") == 0) return (void *)default_self;
        if (strcmp(sel_name, "release") == 0) return (void *)default_release;
        if (strcmp(sel_name, "dealloc") == 0) return (void *)default_dealloc;
        if (strcmp(sel_name, "class") == 0) return (void *)default_class;
    }
    
    return (void *)default_nil_stub;
}

Class object_getClass(id obj) {
    if (!obj || (uintptr_t)obj < 0x1000ULL || (uintptr_t)obj >= 0x800000000000ULL) return Nil;
    return *(Class *)obj;
}

const char *class_getName(Class cls) {
    if (!cls || (uintptr_t)cls < 0x1000ULL || (uintptr_t)cls >= 0x800000000000ULL) return "Nil";
    struct objc_class_2 *c = (struct objc_class_2 *)cls;
    if (c->data_bits && (uintptr_t)c->data_bits >= 0x1000ULL && (uintptr_t)c->data_bits < 0x800000000000ULL) {
        struct class_ro_2_t *ro = (struct class_ro_2_t *)(c->data_bits & ~7ULL);
        if (ro && (uintptr_t)ro >= 0x1000ULL && (uintptr_t)ro < 0x800000000000ULL && ro->name) return ro->name;
    }
    return "UnknownClass";
}

const char *object_getClassName(id obj) {
    if (!obj) return "nil";
    Class cls = object_getClass(obj);
    return cls ? class_getName(cls) : "nil";
}

struct class_entry_map {
    const char *name;
    Class cls;
};
static struct class_entry_map s_classes[1024];
static int s_class_count = 0;
static bool s_classes_initialized = false;

static void init_class_registry(void) {
    if (s_classes_initialized) return;
    s_classes_initialized = true;

    unsigned long size = 0;
    extern char *getsectdata(const char *segname, const char *sectname, unsigned long *size);
    Class *classes = (Class *)getsectdata("__DATA", "__objc_classlist", &size);
    if (classes && size > 0) {
        int count = (int)(size / sizeof(Class));
        for (int i = 0; i < count; i++) {
            Class cls = classes[i];
            if (cls) {
                const char *name = class_getName(cls);
                if (name && strcmp(name, "Nil") != 0 && strcmp(name, "UnknownClass") != 0) {
                    if (s_class_count < 1024) {
                        s_classes[s_class_count].name = name;
                        s_classes[s_class_count].cls = cls;
                        s_class_count++;
                    }
                }
            }
        }
    }

    unsigned long cat_size = 0;
    struct category_2_t **cats = (struct category_2_t **)getsectdata("__DATA", "__objc_catlist", &cat_size);
    if (cats && cat_size > 0) {
        int count = (int)(cat_size / sizeof(struct category_2_t *));
        for (int i = 0; i < count; i++) {
            struct category_2_t *cat = cats[i];
            if (cat && s_category_count < 1024) {
                s_categories[s_category_count].cls = cat->cls;
                s_categories[s_category_count].instanceMethods = cat->instanceMethods;
                s_categories[s_category_count].classMethods = cat->classMethods;
                s_category_count++;
            }
        }
    }
    printf("[init_class_registry PID %d] Registered %d classes, %d categories\n", getpid(), s_class_count, s_category_count);
    fflush(stdout);
}

Class objc_getClass(const char *name) {
    if (!name) return Nil;
    init_class_registry();
    for (int i = 0; i < s_class_count; i++) {
        if (s_classes[i].name && strcmp(s_classes[i].name, name) == 0) {
            return s_classes[i].cls;
        }
    }
    return Nil;
}

Class objc_lookUpClass(const char *name) {
    return objc_getClass(name);
}

const char *sel_getName(SEL sel) {
    if (!sel) return "<null selector>";
    return (const char *)sel;
}

SEL sel_registerName(const char *name) {
    return (SEL)name;
}

id objc_retain(id obj) {
    return obj;
}

void objc_release(id obj) {
    (void)obj;
}

id objc_autorelease(id obj) {
    return obj;
}

id objc_getProperty(id self, SEL _cmd, ptrdiff_t offset, BOOL atomic) {
    (void)_cmd; (void)atomic;
    if (!self) return nil;
    id *val = (id *)((uint8_t *)self + offset);
    return *val;
}

void objc_setProperty_atomic(id self, SEL _cmd, id newValue, ptrdiff_t offset) {
    (void)_cmd;
    if (!self) return;
    id *val = (id *)((uint8_t *)self + offset);
    *val = newValue;
}

void objc_setProperty_nonatomic(id self, SEL _cmd, id newValue, ptrdiff_t offset) {
    (void)_cmd;
    if (!self) return;
    id *val = (id *)((uint8_t *)self + offset);
    *val = newValue;
}

__attribute__((weak)) int objc_sync_enter(id obj) {
    (void)obj;
    return 0;
}

__attribute__((weak)) int objc_sync_exit(id obj) {
    (void)obj;
    return 0;
}

void objc_exception_throw(id exception) {
    const char *cls = exception ? object_getClassName(exception) : "nil";
    fprintf(stderr, "[XIU ObjC] Unhandled exception of class '%s' at %p (callers: %p %p %p %p %p %p)\n",
        cls,
        exception,
        __builtin_return_address(0),
        __builtin_return_address(1),
        __builtin_return_address(2),
        __builtin_return_address(3),
        __builtin_return_address(4),
        __builtin_return_address(5));
    abort();
}

void *objc_begin_catch(void *exc) {
    return exc;
}

void objc_end_catch(void) {
}

extern const char * const *NSProcessInfoArgv;

const char *objc_mainImageName(void) {
    if (NSProcessInfoArgv && NSProcessInfoArgv[0] && NSProcessInfoArgv[0][0]) {
        return NSProcessInfoArgv[0];
    }
    return "/System/Library/CoreServices/WindowServer";
}

id objc_alloc(Class cls) {
    if (!cls) return nil;
    Class metacls = object_getClass((id)cls);
    if (metacls) {
        for (Class c = metacls; c != Nil; ) {
            void *awz_imp = class_find_method_imp(c, "allocWithZone:");
            if (awz_imp) {
                return ((id (*)(id, SEL, void *))awz_imp)((id)cls, (SEL)"allocWithZone:", NULL);
            }
            void *alloc_imp = class_find_method_imp(c, "alloc");
            if (alloc_imp) {
                return ((id (*)(id, SEL))alloc_imp)((id)cls, (SEL)"alloc");
            }
            struct objc_class_2 *oc = (struct objc_class_2 *)c;
            c = oc->superclass;
        }
    }
    return class_createInstance(cls, 0);
}

id objc_alloc_init(Class cls) {
    if (!cls) return nil;
    id obj = objc_alloc(cls);
    if (obj) {
        extern id objc_msgSend(id, SEL, ...);
        obj = objc_msgSend(obj, (SEL)"init");
    }
    return obj;
}

id objc_allocWithZone(Class cls) {
    return objc_alloc(cls);
}

void *objc_autoreleasePoolPush(void) {
    return (void *)1;
}

void objc_autoreleasePoolPop(void *pool) {
    (void)pool;
}

void objc_copyStruct(void *dest, const void *src, ptrdiff_t size, BOOL atomic, BOOL hasStrong) {
    (void)atomic; (void)hasStrong;
    if (dest && src && size > 0) {
        memcpy(dest, src, size);
    }
}

const char **objc_copyImageNames(unsigned int *outCount) {
    const char *mainName = objc_mainImageName();
    const char **images = (const char **)malloc(sizeof(char *) * 2);
    if (!images) {
        if (outCount) *outCount = 0;
        return NULL;
    }
    images[0] = mainName;
    images[1] = NULL;
    if (outCount) *outCount = 1;
    return images;
}

int __objc_personality_v0(void) { return 0; }
void _Unwind_Resume(void *e) { (void)e; abort(); }

const char *class_getImageName(Class cls) {
    (void)cls;
    return objc_mainImageName();
}

__attribute__((weak)) uint32_t defaultEncoding = 4;

typedef struct _NSZone NSZone;


void OBJCInitializeProcess(void) {
}

void *O2FontCreateWithDataProvider_platform(void *provider) {
    (void)provider;
    return NULL;
}

void *O2FontCreateWithFontName_platform(const char *name) {
    (void)name;
    return NULL;
}

__attribute__((weak)) void O2FunctionEvaluate(void *f, float in, float *out) {
    (void)f; (void)in; (void)out;
}

__attribute__((weak)) void O2FunctionRelease(void *f) {
    (void)f;
}

__attribute__((weak)) void *O2FunctionRetain(void *f) {
    return f;
}

void O2PDFCharWidthsGetAdvances(void *cw, void *g, int count, void *adv) {
    (void)cw; (void)g; (void)count; (void)adv;
}

void *O2ShadingColorSpace(void *s) {
    (void)s;
    return NULL;
}

__attribute__((weak)) const void *NSStreamFileCurrentOffsetKey = "NSStreamFileCurrentOffsetKey";

__attribute__((weak)) void NSSelectSetShutdownForCurrentThread(void) {
}

void NSUnimplementedFunction(const char *func, const char *file, int line) {
    (void)func; (void)file; (void)line;
}

__attribute__((weak)) const void *NSParagraphStyleAttributeName = "NSParagraphStyleAttributeName";




void NSInvalidAbstractInvocation(void) {
    fprintf(stderr, "[XIU ObjC] Invalid abstract invocation\n");
    abort();
}

id class_createInstance(Class cls, size_t extraBytes) {
    if (!cls) return nil;
    size_t size = class_getInstanceSize(cls) + extraBytes;
    if (size < sizeof(void *)) size = sizeof(void *);
    id obj = (id)calloc(1, size);
    if (obj) {
        *(Class *)obj = cls;
    }
    return obj;
}

id class_createInstanceFromZone(Class cls, size_t extraBytes, void *zone) {
    (void)zone;
    return class_createInstance(cls, extraBytes);
}

__attribute__((weak)) id NSAllocateObject(Class aClass, size_t extraBytes, NSZone *zone) {
    (void)zone;
    return class_createInstance(aClass, extraBytes);
}

__attribute__((weak)) void NSDeallocateObject(id anObject) {
    if (anObject) {
        free(anObject);
    }
}

__attribute__((weak)) id NSCopyObject(id anObject, size_t extraBytes, NSZone *zone) {
    (void)zone;
    if (!anObject) return nil;
    Class aClass = object_getClass(anObject);
    size_t size = class_getInstanceSize(aClass) + extraBytes;
    id copy = (id)malloc(size);
    if (copy) {
        memcpy(copy, anObject, size);
    }
    return copy;
}

SEL sel_getUid(const char *name) {
    return (SEL)name;
}

size_t class_getInstanceSize(Class cls) {
    if (!cls) return 0;
    struct objc_class_2 *c = (struct objc_class_2 *)cls;
    if (c->data_bits) {
        struct class_ro_2_t *ro = (struct class_ro_2_t *)(c->data_bits & ~7ULL);
        if (ro && ro->instanceSize > 0) {
            return (size_t)ro->instanceSize;
        }
    }
    return 256;
}

__attribute__((weak)) BOOL NSDebugEnabled = NO;

__attribute__((weak)) const void *NSFontAttributeName = "NSFontAttributeName";
__attribute__((weak)) const void *NSForegroundColorAttributeName = "NSForegroundColorAttributeName";


__attribute__((weak)) BOOL NSCurrentLocaleIsMetric(void) {
    return YES;
}

__attribute__((weak)) void NSCooperativeThreadBlocking(void) {}
__attribute__((weak)) void NSCooperativeThreadWaiting(void) {}

const char *objc_ext_skip_type_specifier(const char *type, BOOL relaxed) {
    (void)relaxed;
    if (!type || !*type) return type;
    return type + 1;
}

unsigned objc_ext_sizeof_type(const char *type) {
    if (!type || !*type) return sizeof(void *);
    switch (*type) {
        case 'c': case 'C': case 'B': return sizeof(char);
        case 's': case 'S': return sizeof(short);
        case 'i': case 'I': return sizeof(int);
        case 'l': case 'L': return sizeof(long);
        case 'q': case 'Q': return sizeof(long long);
        case 'f': return sizeof(float);
        case 'd': return sizeof(double);
        default: return sizeof(void *);
    }
}

unsigned objc_ext_alignof_type(const char *type) {
    return objc_ext_sizeof_type(type);
}

__attribute__((weak)) id NSDictionaryFromStringsFormatString(id str) {
    (void)str;
    return nil;
}

double MIN(double a, double b) { return a < b ? a : b; }
double MAX(double a, double b) { return a > b ? a : b; }

void *CFRetain(void *cf) { return cf; }
void CFRelease(void *cf) { (void)cf; }

NSUInteger CFStringHashNSString(id s) {
    return (NSUInteger)s;
}

void *CGImageDestinationCreateWithData(void *data, void *type, size_t count, void *options) {
    (void)data; (void)type; (void)count; (void)options;
    return NULL;
}
void CGImageDestinationAddImage(void *dest, void *image, void *properties) {
    (void)dest; (void)image; (void)properties;
}
BOOL CGImageDestinationFinalize(void *dest) {
    (void)dest;
    return YES;
}

void *CGImageSourceCreateWithData(void *data, void *options) {
    (void)data; (void)options;
    return NULL;
}
size_t CGImageSourceGetCount(void *isrc) {
    (void)isrc;
    return 0;
}
void *CGImageSourceCreateImageAtIndex(void *isrc, size_t index, void *options) {
    (void)isrc; (void)index; (void)options;
    return NULL;
}
void *CGImageSourceCopyPropertiesAtIndex(void *isrc, size_t index, void *options) {
    (void)isrc; (void)index; (void)options;
    return NULL;
}

int CGSOrderedWindowNumbers(int cid, int *list, int count) {
    (void)cid; (void)list; (void)count;
    return 0;
}

void *CTFontCreateWithGraphicsFont(void *font, CGFloat size, void *matrix, void *attrs) {
    (void)font; (void)size; (void)matrix; (void)attrs;
    return NULL;
}
void *CTFontCreatePathForGlyph(void *font, uint16_t glyph, void *matrix) {
    (void)font; (void)glyph; (void)matrix;
    return NULL;
}

const char *NSPlatformExecutableDirectory(void) {
    return "bin";
}

const char *NSPlatformAlternateExecutableDirectory(void) {
    return "Applications";
}

const char *NSPlatformExecutableFileExtension(void) {
    return "";
}

const char *NSPlatformLoadableObjectFilePrefix(void) {
    return "lib";
}

const char *NSPlatformLoadableObjectFileExtension(void) {
    return ".so";
}


__attribute__((weak)) void NSPlatformSleepThreadForTimeInterval(NSTimeInterval interval) {
    if (interval > 0) {
        extern int usleep(unsigned int useconds);
        usleep((unsigned int)(interval * 1000000.0));
    }
}

double ABS(double x) { return x < 0 ? -x : x; }

/* CoreFoundation collection stubs — ponytail: bare minimum to link, no real CF yet */
typedef struct { int dummy; } _CFArray;
typedef struct { int dummy; } _CFDict;
typedef struct { int dummy; } _CFData;
typedef struct { uint32_t port; } _CFMachPort;

void *CFArrayCreateMutable(void *alloc, long capacity, void *cbs) {
    (void)alloc; (void)capacity; (void)cbs;
    return calloc(1, sizeof(_CFArray));
}
void CFArrayAppendValue(void *arr, const void *val) { (void)arr; (void)val; }
long CFArrayGetCount(void *arr) { (void)arr; return 0; }
const void *CFArrayGetValueAtIndex(void *arr, long idx) { (void)arr; (void)idx; return NULL; }

void *CFDictionaryCreateMutable(void *alloc, long capacity, void *kcbs, void *vcbs) {
    (void)alloc; (void)capacity; (void)kcbs; (void)vcbs;
    return calloc(1, sizeof(_CFDict));
}
const void *CFDictionaryGetValue(void *dict, const void *key) { (void)dict; (void)key; return NULL; }
void CFDictionarySetValue(void *dict, const void *key, const void *val) { (void)dict; (void)key; (void)val; }
void CFDictionaryRemoveValue(void *dict, const void *key) { (void)dict; (void)key; }
void CFDictionaryRemoveAllValues(void *dict) { (void)dict; }
long CFDictionaryGetCount(void *dict) { (void)dict; return 0; }
void CFDictionaryGetKeysAndValues(void *dict, const void **keys, const void **vals) { (void)dict; (void)keys; (void)vals; }

void *CFDataCreate(void *alloc, const uint8_t *bytes, long length) {
    (void)alloc;
    _CFData *d = calloc(1, sizeof(_CFData) + length);
    if (d && bytes) memcpy(d + 1, bytes, length);
    return d;
}
const uint8_t *CFDataGetBytePtr(void *data) {
    if (!data) return NULL;
    return (const uint8_t *)((_CFData *)data + 1);
}

void *CFMachPortCreateWithPort(void *alloc, uint32_t port, void *cb, void *ctx, void *shouldFree) {
    (void)alloc; (void)cb; (void)ctx; (void)shouldFree;
    _CFMachPort *p = calloc(1, sizeof(_CFMachPort));
    if (p) p->port = port;
    return p;
}
uint32_t CFMachPortGetPort(void *port) {
    if (!port) return 0;
    return ((_CFMachPort *)port)->port;
}

unsigned long CFStringConvertNSStringEncodingToEncoding(unsigned long nsenc) {
    return nsenc;
}
int CFStringEncodingBytesToUnicode(unsigned long enc, int flags, const uint8_t *bytes, long numBytes, long *usedByteLen, uint16_t *chars, long maxCharLen, long *usedCharLen) {
    (void)enc; (void)flags; (void)bytes; (void)numBytes; (void)usedByteLen; (void)chars; (void)maxCharLen; (void)usedCharLen;
    return -1;
}

Class class_getSuperclass(Class cls) {
    if (!cls) return NULL;
    struct objc_class_2 *c = (struct objc_class_2 *)cls;
    return c->superclass;
}

int objc_getClassList(Class *buffer, int bufferCount) {
    init_class_registry();
    if (!buffer || bufferCount <= 0) {
        return s_class_count;
    }
    int count = s_class_count < bufferCount ? s_class_count : bufferCount;
    for (int i = 0; i < count; i++) {
        buffer[i] = s_classes[i].cls;
    }
    return count;
}

__attribute__((weak)) const void *NSFontNameAttribute = "NSFontNameAttribute";
__attribute__((weak)) const void *NSFontSizeAttribute = "NSFontSizeAttribute";
__attribute__((weak)) const void *NSFontTraitsAttribute = "NSFontTraitsAttribute";
__attribute__((weak)) const void *NSFontVisibleNameAttribute = "NSFontVisibleNameAttribute";
__attribute__((weak)) const void *NSFontSymbolicTrait = "NSFontSymbolicTrait";
__attribute__((weak)) const void *NSFontWeightTrait = "NSFontWeightTrait";
__attribute__((weak)) const void *NSFontFixedAdvanceAttribute = "NSFontFixedAdvanceAttribute";
__attribute__((weak)) const void *NSFontFaceAttribute = "NSFontFaceAttribute";
__attribute__((weak)) const void *NSFontFamilyAttribute = "NSFontFamilyAttribute";
__attribute__((weak)) const void *NSFontCharacterSetAttribute = "NSFontCharacterSetAttribute";
__attribute__((weak)) const void *NSFontSlantTrait = "NSFontSlantTrait";

void *CTFontCreateWithName(void *name, CGFloat size, void *matrix) {
    (void)name; (void)size; (void)matrix;
    return NULL;
}
CGFloat CTFontGetAscent(void *font) { (void)font; return 12.0; }
CGFloat CTFontGetDescent(void *font) { (void)font; return 3.0; }
CGFloat CTFontGetLeading(void *font) { (void)font; return 1.0; }
CGFloat CTFontGetSize(void *font) { (void)font; return 12.0; }
CGFloat CTFontGetSlantAngle(void *font) { (void)font; return 0.0; }
CGFloat CTFontGetUnderlinePosition(void *font) { (void)font; return -1.0; }
CGFloat CTFontGetUnderlineThickness(void *font) { (void)font; return 1.0; }
CGFloat CTFontGetXHeight(void *font) { (void)font; return 8.0; }
void *CTFontCopyName(void *font, void *nameKey) { (void)font; (void)nameKey; return NULL; }
void *CTFontGetGlyphsForCharacters(void *font, void *chars, void *glyphs, long count) { (void)font; (void)chars; (void)glyphs; (void)count; return NULL; }
void CTFontGetAdvancesForGlyphs(void *font, int orient, void *glyphs, void *advances, long count) { (void)font; (void)orient; (void)glyphs; (void)advances; (void)count; }
void *CTFontCopyFullName(void *font) { (void)font; return NULL; }
void *CTFontCreateUIFontForLanguage(int uiType, CGFloat size, void *language) { (void)uiType; (void)size; (void)language; return NULL; }

typedef struct { CGFloat x, y, w, h; } _CGRect;
_CGRect CTFontGetBoundingBox(void *font) { (void)font; _CGRect r = {0,0,10,14}; return r; }
CGFloat CTFontGetCapHeight(void *font) { (void)font; return 10.0; }
long CTFontGetGlyphCount(void *font) { (void)font; return 256; }

int LSOpenCFURLRef(void *url, void *outLaunchedURL) {
    (void)url; (void)outLaunchedURL;
    return -1;
}

extern id objc_msgSend(id self, SEL op, ...);

BOOL LSIsNSBundle(void *cfurl) {
    if (!cfurl) return NO;
    SEL sel_pathExtension = sel_registerName("pathExtension");
    SEL sel_UTF8String = sel_registerName("UTF8String");
    if (!sel_pathExtension || !sel_UTF8String) return NO;
    id (*msgSend)(id, SEL, ...) = (id (*)(id, SEL, ...))objc_msgSend;
    id ext = msgSend((id)cfurl, sel_pathExtension);
    if (!ext) return NO;
    const char *extStr = (const char *)msgSend(ext, sel_UTF8String);
    if (extStr && strcmp(extStr, "app") == 0) return YES;
    return NO;
}

BOOL LSIsAppDir(void *cfurl) {
    if (!cfurl) return NO;
    SEL sel_pathExtension = sel_registerName("pathExtension");
    SEL sel_UTF8String = sel_registerName("UTF8String");
    if (!sel_pathExtension || !sel_UTF8String) return NO;
    id (*msgSend)(id, SEL, ...) = (id (*)(id, SEL, ...))objc_msgSend;
    id ext = msgSend((id)cfurl, sel_pathExtension);
    if (!ext) return NO;
    const char *extStr = (const char *)msgSend(ext, sel_UTF8String);
    if (extStr && strcasecmp(extStr, "appdir") == 0) return YES;
    return NO;
}

FILE *popen(const char *command, const char *type) {
    (void)command; (void)type;
    return NULL;
}

int pclose(FILE *stream) {
    (void)stream;
    return -1;
}

void *CFCopyDescription(void *cf) {
    if (!cf) return NULL;
    SEL sel = sel_registerName("description");
    id (*msgSend)(id, SEL) = (id (*)(id, SEL))objc_msgSend;
    return (void *)msgSend((id)cf, sel);
}

BOOL CFEqual(void *a, void *b) {
    if (a == b) return YES;
    if (!a || !b) return NO;
    SEL sel = sel_registerName("isEqual:");
    BOOL (*msgSend)(id, SEL, id) = (BOOL (*)(id, SEL, id))objc_msgSend;
    return msgSend((id)a, sel, (id)b);
}

NSUInteger CFHash(void *cf) {
    if (!cf) return 0;
    SEL sel = sel_registerName("hash");
    NSUInteger (*msgSend)(id, SEL) = (NSUInteger (*)(id, SEL))objc_msgSend;
    return msgSend((id)cf, sel);
}

int class_getVersion(Class cls) { (void)cls; return 0; }
void class_setVersion(Class cls, int version) { (void)cls; (void)version; }
void objc_exception_rethrow(void) { abort(); }
void objc_terminate(void) { abort(); }
id object_dispose(id obj) { if (obj) free(obj); return nil; }
void objc_setProperty_atomic_copy(id self, SEL _cmd, id newValue, ptrdiff_t offset) {
    (void)_cmd;
    if (!self) return;
    id *slot = (id *)((uint8_t *)self + offset);
    *slot = newValue;
}

bool NSZombieEnabled __attribute__((weak)) = false;
__attribute__((weak)) void NSRegisterZombie(id obj) { (void)obj; }

/* Exception typeinfo for @catch(id); the personality only compares pointers */
extern void *_objc_ehtype_vtable;
struct objc_ehtype_t { void **vtable; void *cls; };
struct objc_ehtype_t OBJC_EHTYPE_id __attribute__((used)) = { &_objc_ehtype_vtable, NULL };

/* XIU has no platform-suffixed resources; referenced by NSNibLoading */
const void * const NSPlatformResourceNameSuffix = NULL;

__attribute__((weak)) const void *kCFBundleNameKey = "kCFBundleNameKey";
__attribute__((weak)) const void *kCFStreamPropertySocketNativeHandle = "kCFStreamPropertySocketNativeHandle";

/* ---- runtime introspection used by Foundation (NSInvocation, KVC, ...) ---- */

static struct method_2_t *find_method_entry(Class cls, const char *sel_name) {
    if (!cls || !sel_name) return NULL;
    if ((uintptr_t)cls < 0x1000ULL || (uintptr_t)cls >= 0x800000000000ULL) return NULL;

    for (Class c = cls; c != Nil; ) {
        struct objc_class_2 *oc = (struct objc_class_2 *)c;
        if (oc->data_bits && (uintptr_t)oc->data_bits >= 0x1000ULL &&
            (uintptr_t)oc->data_bits < 0x800000000000ULL) {
            struct class_ro_2_t *ro = (struct class_ro_2_t *)(oc->data_bits & ~7ULL);
            if (ro && (uintptr_t)ro >= 0x1000ULL && (uintptr_t)ro < 0x800000000000ULL &&
                ro->baseMethodList) {
                const struct method_list_2_t *mlist = ro->baseMethodList;
                uint32_t entsize = mlist->entsizeAndFlags & 0xffff;
                if (entsize < sizeof(struct method_2_t)) entsize = sizeof(struct method_2_t);
                for (uint32_t i = 0; i < mlist->count; i++) {
                    struct method_2_t *m = (struct method_2_t *)
                        ((const uint8_t *)&mlist->methods[0] + i * entsize);
                    if (m->name && strcmp(m->name, sel_name) == 0) return m;
                }
            }
        }
        c = oc->superclass;
    }
    return NULL;
}

Method class_getInstanceMethod(Class cls, SEL name) {
    return (Method)find_method_entry(cls, sel_getName(name));
}

Method class_getClassMethod(Class cls, SEL name) {
    if (!cls) return NULL;
    return (Method)find_method_entry(object_getClass((id)cls), sel_getName(name));
}

SEL method_getName(Method method) {
    return method ? (SEL)((struct method_2_t *)method)->name : NULL;
}

const char *method_getTypeEncoding(Method method) {
    return method ? ((struct method_2_t *)method)->types : NULL;
}

IMP method_getImplementation(Method method) {
    return method ? (IMP)((struct method_2_t *)method)->imp : NULL;
}

unsigned method_getNumberOfArguments(Method method) {
    const char *types = method_getTypeEncoding(method);
    if (!types) return 0;
    /* types = return-type atom, then one atom per argument (self, _cmd, ...) */
    const char *t = types;
    int atoms = -1; /* first atom is the return type */
    int depth = 0;
    while (*t) {
        char c = *t;
        if (c == '{' || c == '[' || c == '(') {
            if (depth == 0) atoms++;
            depth++;
        } else if (c == '}' || c == ']' || c == ')') {
            depth--;
        } else if (depth == 0 && (c < '0' || c > '9')) {
            atoms++;
        }
        t++;
    }
    return (unsigned)(atoms + 1); /* arguments exclude the return atom */
}

BOOL class_isMetaClass(Class cls) {
    if (!cls || (uintptr_t)cls < 0x1000ULL || (uintptr_t)cls >= 0x800000000000ULL) return NO;
    struct objc_class_2 *c = (struct objc_class_2 *)cls;
    if (!c->data_bits) return NO;
    struct class_ro_2_t *ro = (struct class_ro_2_t *)(c->data_bits & ~7ULL);
    return ro && (ro->flags & 1) ? YES : NO;
}

Method *class_copyMethodList(Class cls, unsigned int *outCount) {
    if (outCount) *outCount = 0;
    if (!cls || (uintptr_t)cls < 0x1000ULL || (uintptr_t)cls >= 0x800000000000ULL) return NULL;
    struct objc_class_2 *c = (struct objc_class_2 *)cls;
    if (!c->data_bits) return NULL;
    struct class_ro_2_t *ro = (struct class_ro_2_t *)(c->data_bits & ~7ULL);
    if (!ro || !ro->baseMethodList || ro->baseMethodList->count == 0) return NULL;

    const struct method_list_2_t *mlist = ro->baseMethodList;
    uint32_t entsize = mlist->entsizeAndFlags & 0xffff;
    if (entsize < sizeof(struct method_2_t)) entsize = sizeof(struct method_2_t);
    Method *methods = (Method *)malloc(sizeof(Method) * mlist->count);
    if (!methods) return NULL;
    for (uint32_t i = 0; i < mlist->count; i++) {
        methods[i] = (Method)((const uint8_t *)&mlist->methods[0] + i * entsize);
    }
    if (outCount) *outCount = mlist->count;
    return methods;
}

Class object_setClass(id obj, Class cls) {
    if (!obj) return Nil;
    Class old = object_getClass(obj);
    *(Class *)obj = cls;
    return old;
}

/* XIU metadata is immutable at runtime; these report failure like a
 * read-only class would */
BOOL class_addMethod(Class cls, SEL name, IMP imp, const char *types) {
    (void)cls; (void)name; (void)imp; (void)types;
    return NO;
}

Class objc_allocateClassPair(Class superclass, const char *name, size_t extraBytes) {
    (void)superclass; (void)name; (void)extraBytes;
    return Nil;
}

void objc_registerClassPair(Class cls) {
    (void)cls;
}

Ivar class_getInstanceVariable(Class cls, const char *name) {
    (void)cls; (void)name;
    return NULL;
}

const char *ivar_getTypeEncoding(Ivar ivar) {
    (void)ivar;
    return NULL;
}

ptrdiff_t ivar_getOffset(Ivar ivar) {
    (void)ivar;
    return 0;
}

Ivar object_setInstanceVariable(id obj, const char *name, void *value) {
    (void)obj; (void)name; (void)value;
    return NULL;
}

struct dummy_objc_class {
    Class isa;
    Class superclass;
    void *cache;
    void *vtable;
    uintptr_t data_bits;
};
#define DEFINE_DUMMY_CLASS(name) \
    __attribute__((weak, used, visibility("default"))) struct dummy_objc_class OBJC_METACLASS_$_##name = {0}; \
    __attribute__((weak, used, visibility("default"))) struct dummy_objc_class OBJC_CLASS_$_##name = {(Class)&OBJC_METACLASS_$_##name, 0, 0, 0, 0};

DEFINE_DUMMY_CLASS(NSButtonImageSource)
DEFINE_DUMMY_CLASS(NSComboBoxCell)
DEFINE_DUMMY_CLASS(NSFileHandle)
DEFINE_DUMMY_CLASS(NSFontTypeface)
DEFINE_DUMMY_CLASS(NSGraphicsStyle)
DEFINE_DUMMY_CLASS(NSInvocation)
DEFINE_DUMMY_CLASS(NSMatrix)
DEFINE_DUMMY_CLASS(NSPopUpButtonCell)
DEFINE_DUMMY_CLASS(NSSet)
DEFINE_DUMMY_CLASS(NSColorList)
DEFINE_DUMMY_CLASS(NSColorPanel)
DEFINE_DUMMY_CLASS(NSColorSpace)
DEFINE_DUMMY_CLASS(NSColor_CGColor)
DEFINE_DUMMY_CLASS(NSColor_catalog)
DEFINE_DUMMY_CLASS(NSConditionLock)
DEFINE_DUMMY_CLASS(NSConditionLock_posix)
DEFINE_DUMMY_CLASS(NSCondition_posix)
DEFINE_DUMMY_CLASS(NSCursor)
DEFINE_DUMMY_CLASS(NSCursorRect)
DEFINE_DUMMY_CLASS(NSCustomImageRep)
DEFINE_DUMMY_CLASS(NSData_mapped)
DEFINE_DUMMY_CLASS(NSDelayedPerform)
DEFINE_DUMMY_CLASS(NSDocumentController)
DEFINE_DUMMY_CLASS(NSDraggingManager)
DEFINE_DUMMY_CLASS(NSDrawer)
DEFINE_DUMMY_CLASS(NSEPSImageRep)
DEFINE_DUMMY_CLASS(NSEvent_keyboard)
DEFINE_DUMMY_CLASS(NSEvent_mouse)
DEFINE_DUMMY_CLASS(NSEvent_other)
DEFINE_DUMMY_CLASS(NSEvent_periodic)
DEFINE_DUMMY_CLASS(NSFileManager)
DEFINE_DUMMY_CLASS(NSFontDescriptor)
DEFINE_DUMMY_CLASS(NSFontFamily)
DEFINE_DUMMY_CLASS(NSFontManager)
DEFINE_DUMMY_CLASS(NSImageView)
DEFINE_DUMMY_CLASS(NSInputStream)
DEFINE_DUMMY_CLASS(NSKeyedArchiver)
DEFINE_DUMMY_CLASS(NSKeyedUnarchiver)
DEFINE_DUMMY_CLASS(NSLock)
DEFINE_DUMMY_CLASS(NSLock_posix)
DEFINE_DUMMY_CLASS(NSModalSessionX)
DEFINE_DUMMY_CLASS(NSMutableSet)
DEFINE_DUMMY_CLASS(NSMutableString_proxyToMutableAttributedString)
DEFINE_DUMMY_CLASS(NSNotificationObserver)
DEFINE_DUMMY_CLASS(NSNotificationQueue)
DEFINE_DUMMY_CLASS(NSNull)
DEFINE_DUMMY_CLASS(NSNumberFormatter)
DEFINE_DUMMY_CLASS(NSObjectToObservers)
DEFINE_DUMMY_CLASS(NSOpenGLView)
DEFINE_DUMMY_CLASS(NSOrderedPerform)
DEFINE_DUMMY_CLASS(NSPDFImageRep)
DEFINE_DUMMY_CLASS(NSPageLayout)
DEFINE_DUMMY_CLASS(NSParagraphStyle)
DEFINE_DUMMY_CLASS(NSPersistantDomain_posix)
DEFINE_DUMMY_CLASS(NSPipe_posix)
DEFINE_DUMMY_CLASS(NSPoofAnimation)
DEFINE_DUMMY_CLASS(NSPrintOperation)
DEFINE_DUMMY_CLASS(NSPropertyListReader)
DEFINE_DUMMY_CLASS(NSPropertyListSerialization)
DEFINE_DUMMY_CLASS(NSPropertyListWriter_vintage)
DEFINE_DUMMY_CLASS(NSRecursiveLock_posix)
DEFINE_DUMMY_CLASS(NSRunLoopState)
DEFINE_DUMMY_CLASS(NSScanner)
DEFINE_DUMMY_CLASS(NSScrollView)
DEFINE_DUMMY_CLASS(NSSelectInputSource)
DEFINE_DUMMY_CLASS(NSSocketPort_posix)
DEFINE_DUMMY_CLASS(NSSocket_bsd)
DEFINE_DUMMY_CLASS(NSSpellChecker)
DEFINE_DUMMY_CLASS(NSSystemInfoPanel)
DEFINE_DUMMY_CLASS(NSTask_posix)
DEFINE_DUMMY_CLASS(NSTextView)
DEFINE_DUMMY_CLASS(NSTimeZone)
DEFINE_DUMMY_CLASS(NSTimeZone_posix)
DEFINE_DUMMY_CLASS(NSTimer_invocation)
DEFINE_DUMMY_CLASS(NSTimer_targetAction)
DEFINE_DUMMY_CLASS(NSToolTipWindow)
DEFINE_DUMMY_CLASS(NSToolbarView)
DEFINE_DUMMY_CLASS(NSTrackingArea)
DEFINE_DUMMY_CLASS(NSURLConnection)
DEFINE_DUMMY_CLASS(NSURLRequest)
/* not linked on XIU (need QuartzCore/PDF/CFUUID/bonjour), dummy for linkers */
DEFINE_DUMMY_CLASS(NSGradient)
DEFINE_DUMMY_CLASS(NSRichTextReader)
DEFINE_DUMMY_CLASS(NSURLCache)
DEFINE_DUMMY_CLASS(NSCIImageRep)
DEFINE_DUMMY_CLASS(NSNetServices)
DEFINE_DUMMY_CLASS(NSUndoManager)
DEFINE_DUMMY_CLASS(NSValue_placeholder)
DEFINE_DUMMY_CLASS(NSViewBackingLayer)
DEFINE_DUMMY_CLASS(CALayerContext)
DEFINE_DUMMY_CLASS(CATransaction)
DEFINE_DUMMY_CLASS(CIContext)
DEFINE_DUMMY_CLASS(NSAffineTransform)
DEFINE_DUMMY_CLASS(NSAlert)
DEFINE_DUMMY_CLASS(NSAlertPanel)
DEFINE_DUMMY_CLASS(NSAssertionHandler)
DEFINE_DUMMY_CLASS(NSCachedImageRep)
DEFINE_DUMMY_CLASS(NSCalendarDate)
DEFINE_DUMMY_CLASS(NSCellUndoManager)
DEFINE_DUMMY_CLASS(NSCharacterSet)
DEFINE_DUMMY_CLASS(NSClipView)
DEFINE_DUMMY_CLASS(NSKeyboardBindingManager)
DEFINE_DUMMY_CLASS(NSWindowAnimationContext)
DEFINE_DUMMY_CLASS(O2Context_builtin_FT)
DEFINE_DUMMY_CLASS(_NSTextFieldBinder)
DEFINE_DUMMY_CLASS(_NSControllerMarker)

BOOL class_respondsToSelector(Class cls, SEL sel) {
    if (!cls || !sel || (uintptr_t)cls < 0x1000ULL || (uintptr_t)cls >= 0x800000000000ULL) return NO;
    for (Class c = cls; c != Nil && (uintptr_t)c >= 0x1000ULL && (uintptr_t)c < 0x800000000000ULL; ) {
        void *imp = class_find_method_imp(c, sel_getName(sel));
        if (imp) return YES;
        struct objc_class_2 *oc = (struct objc_class_2 *)c;
        c = oc->superclass;
    }
    return NO;
}

IMP class_getMethodImplementation(Class cls, SEL sel) {
    if (!cls || !sel || (uintptr_t)cls < 0x1000ULL || (uintptr_t)cls >= 0x800000000000ULL) return NULL;
    for (Class c = cls; c != Nil && (uintptr_t)c >= 0x1000ULL && (uintptr_t)c < 0x800000000000ULL; ) {
        void *imp = class_find_method_imp(c, sel_getName(sel));
        if (imp) return (IMP)imp;
        struct objc_class_2 *oc = (struct objc_class_2 *)c;
        c = oc->superclass;
    }
    return NULL;
}

BOOL class_conformsToProtocol(Class cls, Protocol *proto) {
    (void)cls; (void)proto;
    return NO;
}

BOOL sel_isEqual(SEL a, SEL b) {
    if (a == b) return YES;
    if (!a || !b) return NO;
    return strcmp(sel_getName(a), sel_getName(b)) == 0;
}









