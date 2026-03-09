/*
 * functor.h — A Haskell Functor typeclass, implemented in C macros
 *
 * "A functor is a mapping between categories that preserves the structure
 *  of the source category." — Saunders Mac Lane, Categories for the Working
 *  Mathematician (1971)
 *
 * "I'm going to put this in a C header file." — this file (now)
 *
 * ════════════════════════════════════════════════════════════════
 *
 * In Haskell:
 *
 *   class Functor f where
 *     fmap :: (a -> b) -> f a -> f b
 *
 * Subject to the Functor laws:
 *
 *   fmap id      ≡ id              (identity)
 *   fmap (f ∘ g) ≡ fmap f ∘ fmap g (composition)
 *
 * In this file:
 *
 *   - Type constructors are integer "kind tags"          (an int)
 *   - F a is encoded as Functor_t { .kind=K, .inner=x } (a void*)
 *   - fmap dispatches at runtime through a global table  (mutable global state)
 *   - Type safety is a distant memory                   (gone)
 *   - Your dignity is a sunk cost                       (also gone)
 *
 * ════════════════════════════════════════════════════════════════
 *
 * INSTANCES PROVIDED:
 *
 *   §1  Maybe   — data Maybe a = Nothing | Just a
 *   §2  List    — data [a] = [] | a : [a]
 *   §3  IO      — newtype IO a = IO (RealWorld# -> (# RealWorld#, a #))
 *                 (we drop the RealWorld. it was already ruined anyway.)
 *   §4  Reader  — newtype Reader r a = Reader { runReader :: r -> a }
 *                 (function composition. the cleanest one. the weirdest to see here.)
 *
 * ════════════════════════════════════════════════════════════════
 *
 * USAGE:
 *
 *   // In exactly one .c file:
 *   #define FUNCTOR_IMPLEMENTATION
 *   #include "functor.h"
 *
 *   // Everywhere else:
 *   #include "functor.h"
 *
 *   // Example:
 *   static void* double_it(void* x) { return BOX_INT(UNBOX_INT(x) * 2); }
 *
 *   Functor_t x = JUST(BOX_INT(21));
 *   Functor_t y = FMAP(double_it, x);
 *   printf("%d\n", UNBOX_INT(FROM_JUST(y)));  // 42
 *
 *   Functor_t xs = LIST(BOX_INT(1), BOX_INT(2), BOX_INT(3));
 *   Functor_t ys = FMAP(double_it, xs);
 *   // ys = [2, 4, 6], allocated on the heap, never freed, haunting you
 *
 * ════════════════════════════════════════════════════════════════
 *
 * REQUIREMENTS:
 *   - C99 or later
 *   - GCC or Clang (__attribute__((constructor)) for instance registration)
 *   - malloc/free in scope
 *   - A high tolerance for void*
 *   - MSVC: not supported. not considered. not missed.
 *
 */

#ifndef FUNCTOR_H
#define FUNCTOR_H

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>


/* ════════════════════════════════════════════════════════════════
 * §0  THE KIND SYSTEM
 *
 * In Haskell, the "kind" of a type determines how many type arguments
 * it takes. Functor requires kind (* -> *): one argument, one result.
 *
 * We encode kinds as integers. This is equivalent to GHC's kind system
 * in the same way that a crayon drawing is equivalent to the Mona Lisa.
 * ════════════════════════════════════════════════════════════════ */

#define FUNCTOR_KIND_MAYBE            0
#define FUNCTOR_KIND_LIST             1
#define FUNCTOR_KIND_IO               2   /* raw IOThunk: void* (*)(void)        */
#define FUNCTOR_KIND_IO_COMPOSED      3   /* result of fmap on IO                */
#define FUNCTOR_KIND_READER           4   /* raw ReaderFn: void* (*)(void* env)  */
#define FUNCTOR_KIND_READER_COMPOSED  5   /* result of fmap on Reader            */

/* Add your own here. Extend FUNCTOR_KIND_MAX accordingly.
 * Each new instance is a choice you made. Live with it. */
#define FUNCTOR_KIND_MAX              64


/* ════════════════════════════════════════════════════════════════
 * §0  CORE TYPES
 *
 * Functor_t: the universal carrier. "F a" for any F, any a.
 *   .kind  — which functor we're in (the endofunctor, in categorical terms)
 *   .inner — the contained value, erased to void*
 *
 * FmapFn: the type of functions we can lift.
 *   Haskell: (a -> b)
 *   C:       (void* -> void*)
 *   Gap between these two descriptions: immeasurable
 * ════════════════════════════════════════════════════════════════ */

typedef struct {
    int   kind;
    void *inner;
} Functor_t;

typedef void *(*FmapFn)(void *);


/* ════════════════════════════════════════════════════════════════
 * §0  THE DISPATCH TABLE
 *
 * In Haskell, typeclass dispatch is handled by dictionary passing:
 * the compiler implicitly threads a record of method implementations
 * through every polymorphic call.
 *
 * We do it with a global mutable array because we are in C and this
 * is what we deserve.
 *
 * Lookup is O(n) where n = number of registered instances.
 * n is small. This is fine. It should not feel fine. But it is.
 * ════════════════════════════════════════════════════════════════ */

typedef struct {
    int        kind;
    const char *name;   /* human-readable name, for error messages and grief */
    Functor_t (*fmap)(FmapFn f, Functor_t fa);
} FunctorVTable_t;

extern FunctorVTable_t _functor_vtable[FUNCTOR_KIND_MAX];
extern int             _functor_vtable_size;


/* ════════════════════════════════════════════════════════════════
 * §0  INSTANCE REGISTRATION
 *
 * REGISTER_FUNCTOR_INSTANCE(KIND, "Name", fmap_fn) registers a
 * Functor instance in the global dispatch table before main() runs.
 *
 * This uses __attribute__((constructor)), a GCC/Clang extension that
 * runs a function at dynamic-link time, before main().
 *
 * This is the C equivalent of GHC's instance resolution, except:
 *   - GHC's version is compile-time, type-safe, and principled.
 *   - This version is runtime, type-erased, and held together with
 *     token-pasting and hope.
 * ════════════════════════════════════════════════════════════════ */

#define REGISTER_FUNCTOR_INSTANCE(KIND, NAME, FMAP_FN)                         \
    static __attribute__((constructor))                                        \
    void _functor_register_##KIND(void) {                                      \
        if (_functor_vtable_size >= FUNCTOR_KIND_MAX) {                        \
            fputs("FUNCTOR: vtable overflow. "                                 \
                  "You have registered more than 64 Functor instances.\n"     \
                  "(FUNCTOR_KIND_MAX = 64 — raise it if you mean this.)\n"     \
                  "This is not a problem any reasonable person has.\n",        \
                  stderr);                                                     \
            abort();                                                           \
        }                                                                      \
        _functor_vtable[_functor_vtable_size++] = (FunctorVTable_t){           \
            .kind = (KIND),                                                    \
            .name = (NAME),                                                    \
            .fmap = (FMAP_FN),                                                 \
        };                                                                     \
    }


/* ════════════════════════════════════════════════════════════════
 * §0  FMAP — the entire point of this file
 *
 *   FMAP(f, fa)
 *
 *   f  :: a -> b    (as FmapFn: void* -> void*)
 *   fa :: F a       (as Functor_t)
 *   returns F b     (as Functor_t)
 *
 * Dispatches at runtime by scanning the vtable for fa.kind.
 * If no instance is found, prints a diagnostic and aborts.
 * The diagnostic is more informative than the type error would have been.
 * This is the only advantage of this approach over Haskell.
 *
 * Haskell also spells this (<$>). We spell it FMAP. We are in C.
 * We do not get to use (<$>). We do not deserve (<$>).
 * ════════════════════════════════════════════════════════════════ */

static inline Functor_t _dispatch_fmap(FmapFn f, Functor_t fa) {
    for (int i = 0; i < _functor_vtable_size; i++) {
        if (_functor_vtable[i].kind == fa.kind) {
            return _functor_vtable[i].fmap(f, fa);
        }
    }
    fprintf(stderr,
        "FUNCTOR: no instance found for kind=%d.\n"
        "Did you:\n"
        "  (a) forget to #define FUNCTOR_IMPLEMENTATION in one translation unit?\n"
        "  (b) invent a new Functor and forget to register it?\n"
        "  (c) corrupt the kind field through undefined behavior?\n"
        "  (d) deserve this?\n",
        fa.kind);
    abort();
}

/* THE MACRO. All that effort. Four tokens. */
#define FMAP(f, fa)     (_dispatch_fmap((FmapFn)(f), (fa)))

/* Alias. For when FMAP feels too imperative and you want to feel
 * more functional. You are using C. You are not more functional. */
#define LIFT(f, fa)     FMAP((f), (fa))

/* Debug: print the registered instances to stderr. */
#define FUNCTOR_PRINT_REGISTRY()                                               \
    do {                                                                       \
        fprintf(stderr, "Registered Functor instances (%d):\n",               \
                _functor_vtable_size);                                         \
        for (int _i = 0; _i < _functor_vtable_size; _i++) {                   \
            fprintf(stderr, "  [%d] kind=%-3d  name=%s\n", _i,                \
                    _functor_vtable[_i].kind, _functor_vtable[_i].name);       \
        }                                                                      \
    } while (0)


/* ════════════════════════════════════════════════════════════════
 * §0  VALUE BOXING
 *
 * All values are void*. Primitives must be "boxed" to live here.
 *
 * Integers: encoded directly into the pointer bits via intptr_t.
 *           Works as long as sizeof(int) <= sizeof(void*).
 *           This holds on every platform you will use.
 *           This holds on no platform you should trust.
 *
 * Floats:   heap-allocated, because they don't fit in pointer bits cleanly.
 *           You must free them. You won't free them. This is the way.
 *
 * Pointers: already void*. No boxing required. Just cast.
 *           The cast is still lying. But it's a comfortable lie.
 * ════════════════════════════════════════════════════════════════ */

#define BOX_INT(x)        ((void *)(intptr_t)(x))
#define UNBOX_INT(p)      ((int)(intptr_t)(p))

#define BOX_LONG(x)       ((void *)(intptr_t)(x))
#define UNBOX_LONG(p)     ((long)(intptr_t)(p))

#define BOX_PTR(p)        ((void *)(p))
#define UNBOX_PTR(T, p)   ((T *)(p))

/* Float boxing. Heap-allocates. Caller is responsible for free().
 * Caller will not call free(). We all know this. */
#define BOX_FLOAT(x)      (_box_float_impl(x))
#define UNBOX_FLOAT(p)    (*(float *)(p))

static inline void *_box_float_impl(float x) {
    float *p = malloc(sizeof(float));
    assert(p && "BOX_FLOAT: malloc failed. Your functor has run out of heap.");
    *p = x;
    return (void *)p;
}


/* ════════════════════════════════════════════════════════════════
 * §0  IDENTITY AND COMPOSITION
 *
 * The identity morphism (id) and morphism composition (.) are the
 * structural backbone of category theory. Without them, there is no
 * category. Without a category, there is no functor. Without a functor,
 * this file has no purpose.
 *
 * id is trivial in C.
 * Composition in C requires a heap-allocated closure because C has
 * had no closures since 1972 and it has not missed them, apparently.
 * ════════════════════════════════════════════════════════════════ */

static inline void *_id_impl(void *x) { return x; }

/* ID: the identity morphism. fmap ID fa ≡ fa (by the identity law). */
#define ID ((FmapFn)_id_impl)

/* FMAP_COMPOSE(f, g, fa): apply g then f via two fmap calls.
 * Equivalent to fmap (f . g) fa by the composition law.
 *
 * We implement this as two sequential fmaps rather than a true
 * first-class composed function, because making a composed FmapFn
 * requires a closure, and closures in C require malloc, and at some
 * point you have to draw a line.
 *
 * The line is here. This is the line.
 *
 * (If you need a first-class composed function, see COMPOSE_FN below.
 *  COMPOSE_FN allocates. You must free it. You won't.) */
#define FMAP_COMPOSE(f, g, fa)   (FMAP((f), FMAP((g), (fa))))

/* COMPOSE_FN(f, g): returns a heap-allocated function object representing (f . g).
 *
 * This is NOT an FmapFn. It is a _ComposeClosure* masquerading as one.
 * You MUST call it via APPLY_COMPOSED, not as a raw function pointer.
 * Calling it as a raw function pointer is undefined behavior.
 * Most things in this file are undefined behavior if you squint hard enough.
 *
 * Intended use: verifying the composition law. Or academic masochism. */
typedef struct { FmapFn f; FmapFn g; } _ComposeClosure;

#define COMPOSE_FN(f, g)         (_make_compose_closure((FmapFn)(f), (FmapFn)(g)))
#define APPLY_COMPOSED(h, x)     (((_ComposeClosure *)(void *)(h))->f( \
                                   ((_ComposeClosure *)(void *)(h))->g((x))))

static inline _ComposeClosure *_make_compose_closure(FmapFn f, FmapFn g) {
    _ComposeClosure *c = malloc(sizeof(_ComposeClosure));
    assert(c && "COMPOSE_FN: malloc failed. Composition has run out of heap.");
    c->f = f; c->g = g;
    return c;
}


/* ════════════════════════════════════════════════════════════════
 * §0  FUNCTOR LAW VERIFICATION
 *
 * The Functor laws cannot be proven statically. This is not Coq.
 * This is not even Haskell, where you could at least write a QuickCheck.
 * This is C. We assert.
 *
 * VERIFY_FUNCTOR_IDENTITY(fa, eq):
 *   Checks that fmap id fa ≡ fa.
 *   eq :: Functor_t -> Functor_t -> int   (1 = equal, 0 = not equal)
 *
 * VERIFY_FUNCTOR_COMPOSITION(f, g, fa, eq):
 *   Checks that fmap f (fmap g fa) ≡ fmap (f∘g) fa.
 *   (We use FMAP_COMPOSE for the right-hand side.)
 *
 * If a law is violated, we print a message and abort().
 * The message is more useful than a segfault would have been.
 * This is, again, the only advantage of this approach.
 * ════════════════════════════════════════════════════════════════ */

#define VERIFY_FUNCTOR_IDENTITY(fa, eq)                                        \
    do {                                                                       \
        Functor_t _vfi_orig   = (fa);                                          \
        Functor_t _vfi_result = FMAP(ID, _vfi_orig);                          \
        if (!(eq)(_vfi_orig, _vfi_result)) {                                   \
            fprintf(stderr,                                                    \
                "FUNCTOR LAW VIOLATED: fmap id ≠ id\n"                        \
                "  kind: %d\n"                                                 \
                "Your structure is not a Functor.\n"                           \
                "It is, at best, an endofunctor-shaped disappointment.\n",     \
                _vfi_orig.kind);                                               \
            abort();                                                           \
        }                                                                      \
    } while (0)

#define VERIFY_FUNCTOR_COMPOSITION(f, g, fa, eq)                               \
    do {                                                                       \
        Functor_t _vfc_fa  = (fa);                                             \
        Functor_t _vfc_lhs = FMAP((f), FMAP((g), _vfc_fa));                   \
        Functor_t _vfc_rhs = FMAP_COMPOSE((f), (g), _vfc_fa);                 \
        if (!(eq)(_vfc_lhs, _vfc_rhs)) {                                       \
            fprintf(stderr,                                                    \
                "FUNCTOR LAW VIOLATED: fmap f . fmap g ≠ fmap (f∘g)\n"       \
                "  kind: %d\n"                                                 \
                "Go sit in the corner and think about what you've done.\n",    \
                _vfc_fa.kind);                                                 \
            abort();                                                           \
        }                                                                      \
    } while (0)


/* ════════════════════════════════════════════════════════════════
 * §1  MAYBE FUNCTOR
 *
 *   data Maybe a = Nothing | Just a
 *
 *   instance Functor Maybe where
 *     fmap _ Nothing  = Nothing
 *     fmap f (Just a) = Just (f a)
 *
 * Encoding:
 *   Nothing: inner = NULL
 *   Just x:  inner = heap-allocated Maybe_t { .is_just=1, .value=x }
 *
 * Note on Nothing = NULL: this means a null inner pointer is always
 * Nothing, regardless of kind. This is a valid design choice that we
 * are committing to and will never regret.
 * ════════════════════════════════════════════════════════════════ */

typedef struct {
    int   is_just;
    void *value;
} Maybe_t;

static inline Functor_t _just_impl(void *x) {
    Maybe_t *m = malloc(sizeof(Maybe_t));
    assert(m && "JUST: malloc failed. "
                "Even Nothing would be better than this.");
    m->is_just = 1;
    m->value   = x;
    return (Functor_t){ .kind = FUNCTOR_KIND_MAYBE, .inner = (void *)m };
}

/* Construct a Just. Heap-allocates a Maybe_t wrapper. */
#define JUST(x)             (_just_impl((void *)(x)))

/* Construct a Nothing. No allocation. Nothing never needs anything. */
#define NOTHING             ((Functor_t){ .kind = FUNCTOR_KIND_MAYBE, .inner = NULL })

/* Deconstruct a Maybe. */
#define IS_JUST(fa)         ((fa).inner != NULL && ((Maybe_t *)(fa).inner)->is_just)
#define IS_NOTHING(fa)      (!IS_JUST(fa))
#define FROM_JUST(fa)       (((Maybe_t *)(fa).inner)->value)

/* FROM_JUST_OR(fa, default): safe extraction. Total. Won't crash.
 * (Unlike FROM_JUST on a Nothing, which will. Enthusiastically.) */
#define FROM_JUST_OR(fa, def) (IS_JUST(fa) ? FROM_JUST(fa) : (void *)(intptr_t)(def))

/* MAYBE_ELIM: the full eliminator. maybe :: b -> (a -> b) -> Maybe a -> b */
#define MAYBE_ELIM(def, f, fa) \
    (IS_JUST(fa) ? ((FmapFn)(f))(FROM_JUST(fa)) : (void *)(intptr_t)(def))

/* Forward declaration. Implementation in FUNCTOR_IMPLEMENTATION. */
Functor_t _maybe_fmap_impl(FmapFn f, Functor_t fa);


/* ════════════════════════════════════════════════════════════════
 * §2  LIST FUNCTOR
 *
 *   data [a] = [] | a : [a]
 *
 *   instance Functor [] where
 *     fmap _ []     = []
 *     fmap f (x:xs) = f x : fmap f xs
 *
 * Encoded as a singly-linked list. Purely functional (no mutation).
 * fmap traverses the entire list and allocates a fresh new one.
 * The original list is not freed. Memory is a social construct.
 * Valgrind will have opinions. Valgrind is right.
 * ════════════════════════════════════════════════════════════════ */

typedef struct _ListNode {
    void            *head;
    struct _ListNode *tail;
} ListNode_t;

static inline Functor_t _cons_impl(void *head, Functor_t tail) {
    if (tail.kind != FUNCTOR_KIND_LIST) {
        fputs("CONS: second argument must be a List.\n"
              "You passed something else. Reflect on this.\n", stderr);
        abort();
    }
    ListNode_t *node = malloc(sizeof(ListNode_t));
    assert(node && "CONS: malloc failed. The list is out of memory. "
                   "The list is also out of patience.");
    node->head = head;
    node->tail = (ListNode_t *)tail.inner;
    return (Functor_t){ .kind = FUNCTOR_KIND_LIST, .inner = (void *)node };
}

/* The empty list. [] */
#define NIL                 ((Functor_t){ .kind = FUNCTOR_KIND_LIST, .inner = NULL })

/* Prepend an element. CONS(head, tail). Allocates. */
#define CONS(h, t)          (_cons_impl((void *)(h), (t)))

/* Deconstruct a list. */
#define LIST_IS_NIL(fa)     ((fa).inner == NULL)
#define LIST_HEAD(fa)       (((ListNode_t *)(fa).inner)->head)
#define LIST_TAIL(fa)       ((Functor_t){ .kind = FUNCTOR_KIND_LIST, \
                                .inner = (void *)((ListNode_t *)(fa).inner)->tail })

/* LIST(a, b, c, ...): construct a list from up to 5 elements.
 * For more elements, nest CONS manually and question your choices.
 *
 * Implemented via the X-macro-adjacent technique of overloading
 * on argument count. This is the peak of C metaprogramming.
 * This is also the floor of dignified programming. They coincide here. */
#define _LIST_1(a)           CONS((a), NIL)
#define _LIST_2(a, b)        CONS((a), _LIST_1(b))
#define _LIST_3(a, b, c)     CONS((a), _LIST_2(b, c))
#define _LIST_4(a, b, c, d)  CONS((a), _LIST_3(b, c, d))
#define _LIST_5(a,b,c,d,e)   CONS((a), _LIST_4(b, c, d, e))
#define _LIST_PICK(_1,_2,_3,_4,_5, NAME, ...) NAME
#define LIST(...) \
    _LIST_PICK(__VA_ARGS__, _LIST_5, _LIST_4, _LIST_3, _LIST_2, _LIST_1)(__VA_ARGS__)

Functor_t _list_fmap_impl(FmapFn f, Functor_t fa);


/* ════════════════════════════════════════════════════════════════
 * §3  IO FUNCTOR
 *
 *   newtype IO a = IO (RealWorld# -> (# RealWorld#, a #))
 *
 *   instance Functor IO where
 *     fmap f (IO g) = IO (\w -> f (g w))
 *
 * We simplify: IO a is a thunk, void* (*)(void), that performs side
 * effects and returns a. The RealWorld threading is implicit — it's
 * your actual world; it was already in that state; we didn't do it.
 *
 * IO fmap creates a _IOComposed struct that chains the thunk and the
 * function. Nothing is executed until RUN_IO is called.
 * This preserves laziness. Sort of. In a metaphorical sense.
 *
 * Multiple fmaps compose into a chain of _IOComposed nodes.
 * RUN_IO unwinds the chain recursively.
 * The recursion depth is bounded by the number of fmaps.
 * If you fmap 10,000 times before running, that's on you.
 *
 * UNSAFE_PERFORM_IO: executes the IO action. Like unsafePerformIO
 * in Haskell, but less controversial here because this entire file
 * is already past the point of controversy.
 * ════════════════════════════════════════════════════════════════ */

typedef void *(*IOThunk)(void);

typedef struct {
    FmapFn    f;
    Functor_t source;   /* the IO action we're mapping over */
} _IOComposed;

/* Internal runner. Handles raw IO and chains of fmap'd IOs. Recursive. */
static inline void *_run_io_impl(Functor_t io) {
    if (io.kind == FUNCTOR_KIND_IO) {
        return ((IOThunk)io.inner)();
    } else if (io.kind == FUNCTOR_KIND_IO_COMPOSED) {
        _IOComposed *c = (_IOComposed *)io.inner;
        return c->f(_run_io_impl(c->source));
    } else {
        fprintf(stderr,
            "RUN_IO: expected an IO action, got kind=%d.\n"
            "This is not an IO action. Please do not RUN_IO a Maybe.\n"
            "Please do not RUN_IO a List.\n"
            "Please think about what you are doing.\n",
            io.kind);
        abort();
    }
}

/* Wrap a thunk as an IO action. */
#define IO(thunk_fn)         ((Functor_t){ .kind = FUNCTOR_KIND_IO, \
                                 .inner = (void *)(IOThunk)(thunk_fn) })

/* Execute an IO action. Side effects occur. You were warned. */
#define RUN_IO(fa)           (_run_io_impl(fa))

/* Same as RUN_IO but the name communicates danger. Use this one.
 * It makes the reviewer uncomfortable, which is appropriate. */
#define UNSAFE_PERFORM_IO(fa) (_run_io_impl(fa))

Functor_t _io_fmap_impl(FmapFn f, Functor_t fa);


/* ════════════════════════════════════════════════════════════════
 * §4  READER FUNCTOR
 *
 *   newtype Reader r a = Reader { runReader :: r -> a }
 *
 *   instance Functor (Reader r) where
 *     fmap f (Reader g) = Reader (f . g)
 *
 * The Reader functor is function composition. That's it.
 * fmap f reader = a new reader that runs the original and applies f.
 *
 * This is the cleanest instance in this file.
 * It is also the most conceptually pure: Reader is literally the
 * free Functor over function types. The fmap IS (.).
 *
 * Seeing this elegant mathematical object implemented as a chain of
 * heap-allocated _ReaderComposed structs in a C macro system is,
 * we admit, a form of violence.
 * ════════════════════════════════════════════════════════════════ */

typedef void *(*ReaderFn)(void *env);

typedef struct {
    FmapFn    f;
    Functor_t source;   /* the Reader we're mapping over */
} _ReaderComposed;

/* Internal runner. Handles raw Readers and fmap chains. Recursive. */
static inline void *_run_reader_impl(Functor_t reader, void *env) {
    if (reader.kind == FUNCTOR_KIND_READER) {
        return ((ReaderFn)reader.inner)(env);
    } else if (reader.kind == FUNCTOR_KIND_READER_COMPOSED) {
        _ReaderComposed *c = (_ReaderComposed *)reader.inner;
        return c->f(_run_reader_impl(c->source, env));
    } else {
        fprintf(stderr,
            "RUN_READER: expected a Reader, got kind=%d.\n"
            "This is not a Reader. You cannot run it in an environment.\n"
            "You cannot run it at all. It is not that kind of thing.\n",
            reader.kind);
        abort();
    }
}

/* Wrap a function as a Reader. */
#define READER(fn)           ((Functor_t){ .kind = FUNCTOR_KIND_READER, \
                                 .inner = (void *)(ReaderFn)(fn) })

/* Run a Reader by providing the environment it was waiting for. */
#define RUN_READER(fa, env)  (_run_reader_impl((fa), (void *)(env)))

/* ask :: Reader r r  — the fundamental Reader action: return the environment */
static inline void *_ask_impl(void *env) { return env; }
#define ASK (READER(_ask_impl))

Functor_t _reader_fmap_impl(FmapFn f, Functor_t fa);


/* ════════════════════════════════════════════════════════════════
 * IMPLEMENTATION
 *
 * Define FUNCTOR_IMPLEMENTATION in exactly one .c file.
 *
 * Exactly one. Not zero (linker errors). Not two (ODR violation,
 * undefined behavior, the linker does something and you don't ask what).
 * One. The loneliest number. The only correct number.
 * ════════════════════════════════════════════════════════════════ */

#ifdef FUNCTOR_IMPLEMENTATION

FunctorVTable_t _functor_vtable[FUNCTOR_KIND_MAX];
int             _functor_vtable_size = 0;

/* ── Maybe fmap ─────────────────────────────────────────────────
 *
 *   fmap _ Nothing  = Nothing
 *   fmap f (Just a) = Just (f a)
 *
 * Two cases. Clean. Haskell pattern matching encoded as an if statement.
 * The if statement does not spark joy. The if statement is correct.
 * ─────────────────────────────────────────────────────────────── */

Functor_t _maybe_fmap_impl(FmapFn f, Functor_t fa) {
    if (IS_NOTHING(fa)) return NOTHING;
    return JUST(f(FROM_JUST(fa)));
}

REGISTER_FUNCTOR_INSTANCE(FUNCTOR_KIND_MAYBE, "Maybe", _maybe_fmap_impl)


/* ── List fmap ──────────────────────────────────────────────────
 *
 *   fmap _ []     = []
 *   fmap f (x:xs) = f x : fmap f xs
 *
 * Iterative, because recursive list traversal in C would overflow
 * the stack for large lists. We chose iteration over elegance.
 * We chose C over Haskell. We are consistent in our choices.
 *
 * Allocates a new list. Original list is not freed.
 * Free it yourself if you care. You won't care. It's fine.
 * ─────────────────────────────────────────────────────────────── */

Functor_t _list_fmap_impl(FmapFn f, Functor_t fa) {
    if (LIST_IS_NIL(fa)) return NIL;

    ListNode_t *src  = (ListNode_t *)fa.inner;
    ListNode_t *head = NULL;
    ListNode_t *prev = NULL;

    while (src != NULL) {
        ListNode_t *node = malloc(sizeof(ListNode_t));
        assert(node && "_list_fmap_impl: malloc failed mid-list. "
                       "The list is now half-mapped. This is worse than unmapped.");
        node->head = f(src->head);
        node->tail = NULL;

        if (prev == NULL) head      = node;
        else              prev->tail = node;
        prev = node;
        src  = src->tail;
    }

    return (Functor_t){ .kind = FUNCTOR_KIND_LIST, .inner = (void *)head };
}

REGISTER_FUNCTOR_INSTANCE(FUNCTOR_KIND_LIST, "List", _list_fmap_impl)


/* ── IO fmap ────────────────────────────────────────────────────
 *
 *   fmap f (IO g) = IO (\w -> f (g w))
 *
 * We don't execute the IO here. We build a deferred composition node.
 * This is lazy evaluation in C via heap allocation.
 * Haskell does this automatically. We do it manually.
 * Haskell does a lot of things automatically that we do manually.
 * We are aware of this. We are at peace with this. Mostly.
 *
 * Both FUNCTOR_KIND_IO and FUNCTOR_KIND_IO_COMPOSED are registered
 * with this same fmap, so FMAP works on the result of a previous FMAP.
 * You can fmap IO as many times as you want before running it.
 * Each fmap allocates. Each node lives until the process exits.
 * Or until you free it. You won't free it.
 * ─────────────────────────────────────────────────────────────── */

Functor_t _io_fmap_impl(FmapFn f, Functor_t fa) {
    _IOComposed *c = malloc(sizeof(_IOComposed));
    assert(c && "_io_fmap_impl: malloc failed. "
                "The IO functor has run out of memory. "
                "The IO functor is, fittingly, experiencing real-world consequences.");
    c->f      = f;
    c->source = fa;
    return (Functor_t){ .kind = FUNCTOR_KIND_IO_COMPOSED, .inner = (void *)c };
}

REGISTER_FUNCTOR_INSTANCE(FUNCTOR_KIND_IO,          "IO",          _io_fmap_impl)
REGISTER_FUNCTOR_INSTANCE(FUNCTOR_KIND_IO_COMPOSED,  "IO_Composed", _io_fmap_impl)


/* ── Reader fmap ────────────────────────────────────────────────
 *
 *   fmap f (Reader g) = Reader (f . g)
 *
 * Reader fmap is (.). It is the simplest thing imaginable.
 * In Haskell, this compiles to a single function composition.
 * Here, it malloc's a struct. Because C has no closures.
 * Because C was designed in 1972.
 * Because Dennis Ritchie did not anticipate that someone would
 * try to implement Functor in the preprocessor in 2024.
 * To be fair, neither did we, until about twenty minutes ago.
 * ─────────────────────────────────────────────────────────────── */

Functor_t _reader_fmap_impl(FmapFn f, Functor_t fa) {
    _ReaderComposed *c = malloc(sizeof(_ReaderComposed));
    assert(c && "_reader_fmap_impl: malloc failed. "
                "The Reader cannot access its environment. "
                "The Reader is having a crisis.");
    c->f      = f;
    c->source = fa;
    return (Functor_t){ .kind = FUNCTOR_KIND_READER_COMPOSED, .inner = (void *)c };
}

REGISTER_FUNCTOR_INSTANCE(FUNCTOR_KIND_READER,          "Reader",          _reader_fmap_impl)
REGISTER_FUNCTOR_INSTANCE(FUNCTOR_KIND_READER_COMPOSED,  "Reader_Composed", _reader_fmap_impl)

#endif /* FUNCTOR_IMPLEMENTATION */


/* ════════════════════════════════════════════════════════════════
 * APPENDIX: QUICK REFERENCE
 *
 *  Types:
 *    Functor_t          — F a, for any F and any a
 *    FmapFn             — void* (*)(void*), i.e., (a -> b) erased
 *
 *  Core:
 *    FMAP(f, fa)        — fmap f fa :: F b
 *    ID                 — id :: a -> a
 *    FMAP_COMPOSE(f,g,fa) — fmap f (fmap g fa) :: F c
 *
 *  Maybe:
 *    JUST(x)            — Just x :: Maybe a      (allocates)
 *    NOTHING            — Nothing :: Maybe a      (no allocation)
 *    IS_JUST(fa)        — Bool (1/0)
 *    IS_NOTHING(fa)     — Bool (1/0)
 *    FROM_JUST(fa)      — void* (UB if Nothing!)
 *    FROM_JUST_OR(fa,d) — void* (safe)
 *    MAYBE_ELIM(d,f,fa) — void* (total eliminator)
 *
 *  List:
 *    CONS(h, t)         — h : t :: [a]            (allocates)
 *    NIL                — [] :: [a]               (no allocation)
 *    LIST(a,b,c,...)    — [a,b,c,...] :: [a]       (allocates, max 5 elems)
 *    LIST_IS_NIL(fa)    — Bool
 *    LIST_HEAD(fa)      — void* (UB if NIL!)
 *    LIST_TAIL(fa)      — Functor_t (UB if NIL!)
 *
 *  IO:
 *    IO(thunk_fn)       — wrap a void*(void) thunk as IO a
 *    RUN_IO(fa)         — execute; side effects happen NOW
 *    UNSAFE_PERFORM_IO  — alias for RUN_IO, scarier typography
 *
 *  Reader:
 *    READER(fn)         — wrap a void*(void*) function as Reader r a
 *    RUN_READER(fa,env) — apply the reader to an environment
 *    ASK                — Reader r r (returns the environment itself)
 *
 *  Laws:
 *    VERIFY_FUNCTOR_IDENTITY(fa, eq)
 *    VERIFY_FUNCTOR_COMPOSITION(f, g, fa, eq)
 *
 *  Debug:
 *    FUNCTOR_PRINT_REGISTRY()
 *
 * ════════════════════════════════════════════════════════════════
 *
 *  Total macro count: more than is reasonable.
 *  Total type safety: none.
 *  Total correctness: conditional on you not lying to the type system.
 *  You will lie to the type system.
 *  The type system cannot stop you.
 *  There is no type system. There is only void*.
 *  Void* all the way down.
 *
 * ════════════════════════════════════════════════════════════════ */

#endif /* FUNCTOR_H */
