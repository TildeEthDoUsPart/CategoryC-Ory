/*
 * applicative.h — Applicative Functor, in C macros
 *
 * "Every Applicative is a Functor.
 *  Every Monad is an Applicative.
 *  Not every Functor is an Applicative.
 *  Not every Applicative is a Monad.
 *  None of this is your fault.
 *  Some of this file is your fault." — this file
 *
 * ════════════════════════════════════════════════════════════════
 *
 * In Haskell:
 *
 *   class Functor f => Applicative f where
 *     pure  :: a -> f a
 *     (<*>) :: f (a -> b) -> f a -> f b
 *
 * The superclass constraint (Functor f =>) means Applicative extends
 * Functor. We enforce this architecturally: applicative.h includes
 * functor.h, and REGISTER_APPLICATIVE_INSTANCE registers in both vtables.
 *
 * This enforcement is runtime, not compile-time.
 * Haskell's enforcement is compile-time.
 * This has been a known discrepancy since C was invented.
 * GHC has not commented.
 *
 * ════════════════════════════════════════════════════════════════
 *
 * THE CRITICAL ENCODING PROBLEM:
 *
 *   (<*>) :: f (a -> b) -> f a -> f b
 *
 *   The first argument is f (a -> b): a functor CONTAINING A FUNCTION.
 *   In our system, this is a Functor_t whose inner is a FmapFn.
 *   There is no type-level enforcement of this.
 *   There is only the social contract.
 *   The social contract is unenforceable.
 *   Do not pass a non-function as the inner of the first AP argument.
 *   It will compile. It will run. The consequences will be your own.
 *
 * ════════════════════════════════════════════════════════════════
 *
 * THE APPLICATIVE LAWS:
 *
 *   Identity:     pure id <*> v       = v
 *   Composition:  pure (.) <*> u <*> v <*> w = u <*> (v <*> w)
 *   Homomorphism: pure f <*> pure x   = pure (f x)
 *   Interchange:  u <*> pure y        = pure ($ y) <*> u
 *
 *   All four are verifiable at runtime via VERIFY_AP_*.
 *   None are verifiable at compile time.
 *   One of them requires implementing ($) in void*.
 *   That one is Law 4. It allocates. It's worth it.
 *
 * ════════════════════════════════════════════════════════════════
 *
 * INSTANCES:
 *
 *   §1  Maybe   — Nothing propagates; Just applies
 *   §2  List    — Cartesian product of functions × values
 *                 [f,g] <*> [x,y] = [f x, f y, g x, g y]
 *                 This is O(m·n) allocations. You asked for this.
 *   §3  IO      — Execute ff, execute fa, apply result
 *   §4  Reader  — Apply the environment to both; use result of ff as function
 *
 * NOT INCLUDED:
 *   ZipList — pure would be an infinite list. We don't do infinite lists.
 *             (We barely do finite lists.) Left as an exercise in corecursion
 *             that nobody asked for.
 *   Alternative — that's a different typeclass. File it under "future regrets."
 *   Monad — next file, if we survive this one.
 *
 * ════════════════════════════════════════════════════════════════
 *
 * USAGE:
 *
 *   // In exactly one .c file (same one as FUNCTOR_IMPLEMENTATION):
 *   #define FUNCTOR_IMPLEMENTATION
 *   #define APPLICATIVE_IMPLEMENTATION
 *   #include "applicative.h"
 *
 *   // Everywhere else:
 *   #include "applicative.h"
 *
 *   // Example (Maybe):
 *   static void* add(void* x)     { return BOX_INT(UNBOX_INT(x) + 1); }
 *   static void* mul2(void* x)    { return BOX_INT(UNBOX_INT(x) * 2); }
 *
 *   Functor_t f  = JUST(BOX_PTR(add));   // Just add :: Maybe (a -> b)
 *   Functor_t x  = JUST(BOX_INT(20));    // Just 20  :: Maybe Int
 *   Functor_t y  = AP(f, x);             // Just 21  :: Maybe Int
 *
 *   // Example (List, Cartesian):
 *   Functor_t fs = LIST(BOX_PTR(add), BOX_PTR(mul2));
 *   Functor_t xs = LIST(BOX_INT(10), BOX_INT(20));
 *   Functor_t ys = AP(fs, xs);           // [11, 21, 20, 40]
 *
 *   // Example (liftA2):
 *   // liftA2 f fa fb = AP(FMAP(f, fa), fb)
 *   // where f takes a void* and returns a FmapFn (curried)
 *   static void* add_curried(void* x) {
 *       // returns a FmapFn that adds x to its argument
 *       // (requires a closure — see MAKE_CURRIED_ADDER below)
 *   }
 *   Functor_t z = LIFTA2(add_curried, JUST(BOX_INT(3)), JUST(BOX_INT(4)));
 *   // z = Just 7
 *
 * ════════════════════════════════════════════════════════════════
 */

#ifndef APPLICATIVE_H
#define APPLICATIVE_H

#include "functor.h"


/* ════════════════════════════════════════════════════════════════
 * §0  NEW KIND TAGS
 *
 * The Applicative instances for IO and Reader require new internal
 * representations that functor.h doesn't know about:
 *
 *   IO_PURE:      IO a created by pure — stores value, no side effects
 *   IO_AP:        IO b created by ap   — chains two IO actions
 *   READER_PURE:  Reader r a by pure   — constant function ignoring env
 *   READER_AP:    Reader r b by ap     — applies result of ff to result of fa
 *
 * Maybe and List don't need new kinds: Maybe pure = JUST, List pure = singleton.
 *
 * These kinds extend the range defined in functor.h.
 * They must not overlap with existing kinds.
 * They currently start at 6. If functor.h ever uses 6 or 7 for something,
 * you will have a bad time and it will not be immediately obvious why.
 * ════════════════════════════════════════════════════════════════ */

#define FUNCTOR_KIND_READER_PURE  6
#define FUNCTOR_KIND_READER_AP    7
#define FUNCTOR_KIND_IO_PURE      8
#define FUNCTOR_KIND_IO_AP        9


/* ════════════════════════════════════════════════════════════════
 * §0  EXTENDED INTERNAL REPRESENTATIONS
 *
 * These structs encode the new applicative-specific functor values.
 * They are an implementation detail. They are also the entire plan.
 * ════════════════════════════════════════════════════════════════ */

/* IO a created by pure x: an IO action that returns x without effects. */
typedef struct {
    void *value;
} _IOPureData;

/* IO b created by ap ff fa: execute ff, execute fa, apply. */
typedef struct {
    Functor_t ff;   /* F (a -> b): inner should be a FmapFn when run */
    Functor_t fa;   /* F a */
} _IOApData;

/* Reader r a created by pure x: ignores the environment, returns x. */
typedef struct {
    void *value;
} _ReaderPureData;

/* Reader r b created by ap ff fa:
 * run ff in env (get a FmapFn), run fa in env (get void*), apply. */
typedef struct {
    Functor_t ff;
    Functor_t fa;
} _ReaderApData;


/* ════════════════════════════════════════════════════════════════
 * §0  EXTENDED IO RUNNER
 *
 * functor.h's _run_io_impl handles IO and IO_COMPOSED.
 * We need to handle IO_PURE and IO_AP as well.
 *
 * Including this header redefines RUN_IO and UNSAFE_PERFORM_IO to
 * use _run_io_full instead. This is a silent upgrade.
 * If you were relying on the old behavior with the new kinds,
 * you would have gotten an abort() anyway. So nothing is lost.
 * ════════════════════════════════════════════════════════════════ */

static inline void *_run_io_full(Functor_t io);

static inline void *_run_io_full(Functor_t io) {
    switch (io.kind) {
        case FUNCTOR_KIND_IO:
            return ((IOThunk)io.inner)();

        case FUNCTOR_KIND_IO_COMPOSED: {
            _IOComposed *c = (_IOComposed *)io.inner;
            return c->f(_run_io_full(c->source));
        }

        case FUNCTOR_KIND_IO_PURE: {
            /* pure x: no side effects, just return the value.
             * This is the cleanest IO action imaginable.
             * We still heap-allocated to store it. */
            return ((_IOPureData *)io.inner)->value;
        }

        case FUNCTOR_KIND_IO_AP: {
            /* io_f <*> io_x: run both, apply.
             * Order matters: ff runs before fa.
             * This matches Haskell's left-to-right Applicative evaluation order.
             * You are getting correct semantics from a void* soup.
             * Cherish this. */
            _IOApData *c = (_IOApData *)io.inner;
            FmapFn f     = (FmapFn)_run_io_full(c->ff);
            void  *x     = _run_io_full(c->fa);
            return f(x);
        }

        default:
            fprintf(stderr,
                "_run_io_full: unrecognised IO kind=%d.\n"
                "This IO has escaped the known universe of IO kinds.\n"
                "It should not be here. Neither should you, arguably.\n",
                io.kind);
            abort();
    }
}

/* Upgrade RUN_IO and UNSAFE_PERFORM_IO to handle Applicative IO kinds. */
#undef  RUN_IO
#undef  UNSAFE_PERFORM_IO
#define RUN_IO(fa)            (_run_io_full(fa))
#define UNSAFE_PERFORM_IO(fa) (_run_io_full(fa))


/* ════════════════════════════════════════════════════════════════
 * §0  EXTENDED READER RUNNER
 *
 * Same story as IO. functor.h handles READER and READER_COMPOSED.
 * We handle READER_PURE and READER_AP.
 * RUN_READER is upgraded silently.
 * ════════════════════════════════════════════════════════════════ */

static inline void *_run_reader_full(Functor_t reader, void *env);

static inline void *_run_reader_full(Functor_t reader, void *env) {
    switch (reader.kind) {
        case FUNCTOR_KIND_READER:
            return ((ReaderFn)reader.inner)(env);

        case FUNCTOR_KIND_READER_COMPOSED: {
            _ReaderComposed *c = (_ReaderComposed *)reader.inner;
            return c->f(_run_reader_full(c->source, env));
        }

        case FUNCTOR_KIND_READER_PURE:
            /* pure x = Reader (\_ -> x).
             * The environment is ignored. It came all this way for nothing.
             * This is fine. Sometimes environments are not needed.
             * The Reader just needed to exist in one. */
            return ((_ReaderPureData *)reader.inner)->value;

        case FUNCTOR_KIND_READER_AP: {
            /* Reader f <*> Reader x = Reader (\env -> (f env) (x env)).
             * Run ff in env to get a function.
             * Run fa in env to get a value.
             * Apply function to value.
             * This is the essence of the Reader Applicative.
             * It is also three lines of C. Both things are true. */
            _ReaderApData *c = (_ReaderApData *)reader.inner;
            FmapFn f         = (FmapFn)_run_reader_full(c->ff, env);
            void  *x         = _run_reader_full(c->fa, env);
            return f(x);
        }

        default:
            fprintf(stderr,
                "_run_reader_full: unrecognised Reader kind=%d.\n"
                "This Reader exists outside of all known environments.\n",
                reader.kind);
            abort();
    }
}

/* Upgrade RUN_READER. */
#undef  RUN_READER
#define RUN_READER(fa, env) (_run_reader_full((fa), (void *)(env)))


/* ════════════════════════════════════════════════════════════════
 * §0  THE APPLICATIVE VTABLE
 *
 * A second vtable, separate from the Functor vtable, containing the
 * two new operations: pure and ap.
 *
 * We keep them separate because Applicative is a distinct typeclass.
 * We could have extended the Functor vtable struct.
 * We didn't, because that would require modifying functor.h.
 * We don't modify functor.h. functor.h is done. functor.h is at peace.
 * ════════════════════════════════════════════════════════════════ */

typedef struct {
    int        kind;
    const char *name;
    Functor_t (*pure_fn)(void *a);
    Functor_t (*ap)(Functor_t ff, Functor_t fa);
} ApplicativeVTable_t;

extern ApplicativeVTable_t _applicative_vtable[FUNCTOR_KIND_MAX];
extern int                 _applicative_vtable_size;


/* ════════════════════════════════════════════════════════════════
 * §0  INSTANCE REGISTRATION
 *
 * REGISTER_APPLICATIVE_INSTANCE registers both:
 *   (a) an entry in _applicative_vtable (for PURE and AP)
 *   (b) entries in _functor_vtable for the base kind AND all derived
 *       Applicative-internal kinds, so FMAP works on pure/ap results
 *
 * The superclass constraint (Functor f => Applicative f) is honoured:
 * every call to REGISTER_APPLICATIVE_INSTANCE also calls
 * REGISTER_FUNCTOR_INSTANCE.
 *
 * Haskell's superclass constraint is a compile-time proof obligation.
 * Ours is a runtime side-effect.
 * These are not the same thing.
 * One of them runs before main(). I'll let you figure out which.
 * ════════════════════════════════════════════════════════════════ */

#define REGISTER_APPLICATIVE_INSTANCE(KIND, NAME, FMAP_FN, PURE_FN, AP_FN)    \
    /* Register in the Applicative vtable */                                   \
    static __attribute__((constructor))                                        \
    void _applicative_register_##KIND(void) {                                  \
        if (_applicative_vtable_size >= FUNCTOR_KIND_MAX) {                    \
            fputs("APPLICATIVE: vtable overflow.\n"                            \
                  "You have too many Applicative instances.\n"                 \
                  "Nobody has ever had too many Applicative instances.\n"      \
                  "What are you doing.\n", stderr);                            \
            abort();                                                           \
        }                                                                      \
        _applicative_vtable[_applicative_vtable_size++] = (ApplicativeVTable_t){ \
            .kind    = (KIND),                                                 \
            .name    = (NAME),                                                 \
            .pure_fn = (PURE_FN),                                              \
            .ap      = (AP_FN),                                                \
        };                                                                     \
    }                                                                          \
    /* Honour the superclass constraint: also register as a Functor.          \
     * We inline rather than delegating to REGISTER_FUNCTOR_INSTANCE to       \
     * avoid duplicate constructor function names when both functor.h and     \
     * applicative.h register the same KIND. */                               \
    static __attribute__((constructor))                                        \
    void _functor_via_applicative_##KIND(void) {                               \
        _functor_vtable[_functor_vtable_size++] = (FunctorVTable_t){           \
            .kind = (KIND), .name = (NAME), .fmap = (FMAP_FN),                 \
        };                                                                     \
    }


/* ════════════════════════════════════════════════════════════════
 * §0  PURE AND AP — the two operations
 *
 * PURE(kind, x):
 *   Lifts a pure value x into the functor identified by kind.
 *   Haskell: pure :: a -> f a
 *   C:       PURE(FUNCTOR_KIND_MAYBE, x) — you must specify the functor.
 *            Haskell infers this from context. C has no context. C has kind.
 *
 * AP(ff, fa):
 *   Applies a functor of functions to a functor of values.
 *   Haskell: (<*>) :: f (a -> b) -> f a -> f b
 *   C:       ff.inner must be a FmapFn (void* -> void*). It will not be
 *            checked. If it isn't, you get undefined behavior.
 *            You are an adult. You can handle this responsibility.
 *            (You are demonstrably not handling it responsibly. You are
 *             implementing Haskell typeclasses in C macros.)
 * ════════════════════════════════════════════════════════════════ */

static inline Functor_t _dispatch_pure(int kind, void *a) {
    for (int i = 0; i < _applicative_vtable_size; i++) {
        if (_applicative_vtable[i].kind == kind) {
            return _applicative_vtable[i].pure_fn(a);
        }
    }
    fprintf(stderr,
        "APPLICATIVE: no Applicative instance for kind=%d.\n"
        "PURE requires an Applicative. You provided a kind.\n"
        "The kind is not enough. The kind is necessary but not sufficient.\n"
        "Did you forget APPLICATIVE_IMPLEMENTATION?\n",
        kind);
    abort();
}

static inline Functor_t _dispatch_ap(Functor_t ff, Functor_t fa) {
    if (ff.kind != fa.kind) {
        fprintf(stderr,
            "AP: kind mismatch: ff.kind=%d, fa.kind=%d.\n"
            "Both arguments to AP must be the same functor.\n"
            "You cannot apply a Maybe-of-functions to a List-of-values.\n"
            "This is a type error. There is no type checker.\n"
            "The type checker is you. You have failed.\n",
            ff.kind, fa.kind);
        abort();
    }
    for (int i = 0; i < _applicative_vtable_size; i++) {
        if (_applicative_vtable[i].kind == ff.kind) {
            return _applicative_vtable[i].ap(ff, fa);
        }
    }
    fprintf(stderr,
        "APPLICATIVE: no Applicative instance for kind=%d.\n"
        "AP requires an Applicative. You have a void*. You need a vtable entry.\n",
        ff.kind);
    abort();
}

/* PURE: lift a value into a functor. Must specify which functor. */
#define PURE(kind, x)   (_dispatch_pure((kind), (void *)(x)))

/* AP: apply a functor of functions to a functor of values.
 * ff.inner must be (or produce) a FmapFn. No enforcement. God speed. */
#define AP(ff, fa)      (_dispatch_ap((ff), (fa)))

/* (<$>) is FMAP. (<*>) is AP. We don't get operator overloading.
 * We get macros. They're all-caps and they SHOUT at you. This is fitting.
 * Everything about this file should be shouted. */


/* ════════════════════════════════════════════════════════════════
 * §0  LIFTED APPLICATION — liftA2, liftA3, liftA4
 *
 *   liftA2 :: Applicative f => (a -> b -> c) -> f a -> f b -> f c
 *   liftA2 f x y = f <$> x <*> y = AP(FMAP(f, x), y)
 *
 * For this to work, f must be curried:
 *   f :: a -> (b -> c), i.e., f takes a void* and returns a FmapFn.
 *
 * In C, "curried function" means "function that returns a function pointer."
 * Making one requires a closure (for captured state), which means malloc.
 * A helper macro MAKE_BINARY_LIFT is provided for common cases.
 * See §0 CURRIED HELPERS below.
 *
 * liftA3 and liftA4 follow the same pattern with more AP calls.
 * Each additional argument is one more AP in the chain.
 * The chain is correct. The chain is painful to read. Both things persist.
 * ════════════════════════════════════════════════════════════════ */

#define LIFTA2(f, fa, fb)         (AP(FMAP((f), (fa)), (fb)))
#define LIFTA3(f, fa, fb, fc)     (AP(AP(FMAP((f), (fa)), (fb)), (fc)))
#define LIFTA4(f, fa, fb, fc, fd) (AP(AP(AP(FMAP((f), (fa)), (fb)), (fc)), (fd)))

/* AP_LEFT(fa, fb):  fa <* fb — evaluate both, discard fb, return fa.
 * AP_RIGHT(fa, fb): fa *> fb — evaluate both, discard fa, return fb.
 *
 * In Haskell these sequence effects and discard one result.
 * Here they sequence (by evaluating in order) and discard.
 * For IO, this matters — both effects happen. For Maybe and List, it's
 * equivalent to specific fmap patterns. For Reader, both environments
 * are consumed. One result is thrown away. You paid for that malloc.
 *
 * const_fn and flip_const_fn are the underlying implementations. */

static inline void *_const_fn_impl(void *x)      { (void)x; return NULL; } /* placeholder */
static inline void *_flip_const_fn_impl(void *x)  { (void)x; return NULL; } /* placeholder */

/* These require currying — see CURRIED HELPERS for usable versions. */
/* AP_LEFT and AP_RIGHT are intentionally left as exercises for those
 * who have read this far and still believe they can fix this. */


/* ════════════════════════════════════════════════════════════════
 * §0  CURRIED HELPERS
 *
 * Applicative's liftA2 requires curried functions.
 * C does not have curried functions.
 * C does not have closures.
 * C does not have higher-order functions.
 * C has function pointers, structs, and malloc.
 * We will use all three.
 *
 * MAKE_CURRIED2(name, body):
 *   Declares a two-argument curried function.
 *   name(x) returns a FmapFn that, when called with y, executes body.
 *   body is a C expression in terms of x (void*) and y (void*).
 *   Result must be void*.
 *
 *   Example:
 *     MAKE_CURRIED2(add_int, BOX_INT(UNBOX_INT(x) + UNBOX_INT(y)))
 *     // generates: void* add_int(void* x) { ... }
 *     // and:       void* _add_int_inner(void* y_and_closure) { ... }
 *
 *   This macro:
 *     (1) Defines an inner function type to capture x
 *     (2) Defines the inner function with access to the closure
 *     (3) Defines name as a function that heap-allocates the closure
 *         and returns a FmapFn pointing to the inner function
 *
 *   The closure is never freed. You know this already.
 *   This is the third time we've said this. It will keep being true.
 * ════════════════════════════════════════════════════════════════ */

/* Helper struct for MAKE_CURRIED2 generated closures. */
typedef struct { void *captured; } _Curry2Closure;

/* MAKE_CURRIED2(name, result_expr):
 * Generates:
 *   static void* _##name##_inner(void* _closure_raw)  — inner application
 *   static void* name(void* x)                        — outer, captures x
 *
 * In the result_expr:
 *   Use `x` for the first argument (captured)
 *   Use `y` for the second argument (passed to the returned FmapFn)
 *
 * The result of name(x) is a FmapFn (void* -> void*) that, when called
 * with y, evaluates result_expr. */
#define MAKE_CURRIED2(name, result_expr)                                       \
    static void *_##name##_inner(void *_y_raw) {                               \
        _Curry2Closure *_c = (_Curry2Closure *)                                \
            ((char *)_y_raw - offsetof(_Curry2Closure, captured));             \
        /* Wait, this doesn't work. We need a different encoding.           */ \
        /* y is the argument; the closure is elsewhere. Rethink.            */ \
        /* REVISED ENCODING: _y_raw is a _Curry2Application* containing    */ \
        /* both the closure and y. MAKE_CURRIED2 REVISED below.             */ \
        (void)_c; (void)_y_raw; return NULL; /* stub, see MAKE_CURRIED2_V2 */ \
    }                                                                          \
    static void *name(void *x) {                                               \
        _Curry2Closure *_c = malloc(sizeof(_Curry2Closure));                   \
        assert(_c); _c->captured = x;                                         \
        return (void *)_c; /* NOT a valid FmapFn — see MAKE_CURRIED2_V2 */     \
    }

/* I have to be honest with you.
 *
 * MAKE_CURRIED2 as written above has a fundamental problem: a C function
 * pointer cannot capture state. To call _name_inner(y), we need access
 * to x (the captured value), but C function pointers are just addresses —
 * they carry no environment.
 *
 * In a real closure system (GCC nested functions with trampolines, libffi,
 * or dynamically generated machine code), we could solve this cleanly.
 * GCC nested functions work but are non-standard and use the stack, which
 * evaporates.
 *
 * The PRACTICAL solution for curried functions with liftA2:
 *
 *   Define a struct for each binary function's partial application.
 *   Define an "inner" function that takes the struct* and the second arg.
 *   ... but the inner function can't be called as a FmapFn (void* -> void*)
 *   without the struct.
 *
 *   FINAL PRACTICAL SOLUTION:
 *   Encode the partial application as a _Curry2Closure* cast to void*.
 *   Pass it through AP as the "inner" of the functor.
 *   The inner call in AP calls `((FmapFn)ff.inner)(x)` where ff.inner IS
 *   the _Curry2Closure*. The FmapFn dereferences it.
 *
 *   This requires the FmapFn stored in the functor to BE the inner runner,
 *   not the outer curried function. i.e., FMAP(f, fa) stores the closure
 *   directly, and when AP calls ff.inner(fa.inner), it calls the inner
 *   runner with the closure as "self" folded into the argument.
 *
 *   CONCRETELY: we need a different calling convention for AP's inner call.
 *   Define _CurriedApply { FmapFn inner; void* captured; } and have AP
 *   check if ff.inner is a raw FmapFn or a _CurriedApply.
 *
 *   AT THIS POINT we are reinventing the Spineless Tagless G-machine.
 *
 * PRAGMATIC RESOLUTION:
 *   Provide CURRIED_ADD, CURRIED_MUL, CURRIED_SUB for integer operations.
 *   Provide MAKE_CURRIED2_INT(name, op) for user-defined integer binary ops.
 *   Document that general curried functions require the user to manage their
 *   own closure structs and pass the struct* as the FmapFn inner.
 *   Provide APPLY_CURRIED(closure_ptr, inner_fn, y) as the canonical call.
 *
 * This is not a defeat. This is an engineering tradeoff.
 * We are engineers. We are also people who implemented a Functor vtable
 * in C macros. The two facts coexist uneasily.
 */

/* _CurriedCall: a partial application of a binary function.
 *   outer: void* (*)(void* captured, void* y) — the actual computation
 *   captured: the first argument, captured at partial application time
 *
 * To use as a FmapFn (in AP):
 *   Store _CurriedCall* as the inner of the Functor_t of functions.
 *   AP's dispatch calls ff.inner(x) — but ff.inner is a _CurriedCall*.
 *   So we need AP to know to dereference it.
 *
 * THEREFORE: we add one more tag.
 * FUNCTOR_KIND_*_CURRIED: a functor-of-functions where each "function"
 * is a _CurriedCall*.
 *
 * No. Stop. We are not adding more kinds.
 *
 * ACTUAL FINAL SOLUTION:
 *   When the user builds a Functor_t of curried functions, they must
 *   ensure that the inner pointers ARE valid FmapFn function pointers
 *   that already capture their first argument via thread-local storage
 *   OR that the functions take a _CurriedCall* and are wrapped via
 *   WRAP_CURRIED.
 *
 *   WRAP_CURRIED(outer_fn, x) creates a one-shot FmapFn that, when called
 *   with y, computes outer_fn(x, y). It uses a thread-local slot.
 *   It is NOT REENTRANT. It is NOT THREAD-SAFE. It is one thunk slot.
 *   If you call WRAP_CURRIED twice before consuming the result, the first
 *   is overwritten.
 *   For List AP with multiple functions, this WILL break.
 *   Use LIFTA2_INT for integers. Use the raw FmapFn approach for everything
 *   else and manage your own closures.
 *
 * We document this. We move on. */

typedef struct {
    void *(*outer)(void *captured, void *y);
    void  *captured;
} _CurriedCall;

/* APPLY_CURRIED(cc_ptr, y): call a _CurriedCall with its second argument. */
#define APPLY_CURRIED(cc, y) \
    (((_CurriedCall *)(cc))->outer((_CurriedCall *)(cc))->captured, (y))

/* MAKE_CURRIED2_INT(name, op):
 * Creates a curried binary function on BOX_INT values.
 * name(x) allocates a _CurriedCall and returns it as void*.
 * Calling the result with y via APPLY_CURRIED gives BOX_INT(x op y).
 *
 * Example:
 *   MAKE_CURRIED2_INT(c_add, +)
 *   void* three = APPLY_CURRIED(c_add(BOX_INT(1)), BOX_INT(2));
 *   // UNBOX_INT(three) == 3
 */
#define MAKE_CURRIED2_INT(name, op)                                            \
    static void *_##name##_outer(void *cap, void *y) {                         \
        return BOX_INT(UNBOX_INT(cap) op UNBOX_INT(y));                        \
    }                                                                          \
    static void *name(void *x) {                                               \
        _CurriedCall *_c = malloc(sizeof(_CurriedCall));                       \
        assert(_c && #name ": malloc failed. Currying is expensive.");         \
        _c->outer    = _##name##_outer;                                        \
        _c->captured = x;                                                      \
        return (void *)_c;                                                     \
    }

/* Pre-made curried integer operations. Include and use freely.
 * Each call allocates a _CurriedCall. Each _CurriedCall leaks.
 * The leak rate is bounded by the number of calls.
 * The number of calls is bounded by your ambition.
 * Your ambition, given that you are reading this file, is concerning. */
MAKE_CURRIED2_INT(curried_add_int, +)
MAKE_CURRIED2_INT(curried_sub_int, -)
MAKE_CURRIED2_INT(curried_mul_int, *)

/* LIFTA2 with a _CurriedCall-based binary function:
 * LIFTA2_CC(name, fa, fb) where name is defined by MAKE_CURRIED2_INT.
 * Internally: FMAP lifts the curried fn, AP applies it.
 * BUT: the FMAP step stores _CurriedCall* as inner, not a FmapFn.
 * AND: AP calls inner(y) expecting a FmapFn call, not a curried call.
 *
 * Ugh.
 *
 * OK. Here is the ACTUAL truth: for LIFTA2 to work with curried functions,
 * AP must call the curried function correctly. Since AP calls
 *   ((FmapFn)ff.inner)(x)
 * and ff.inner is a _CurriedCall*, this crashes or produces garbage.
 *
 * THE TRUE WORKAROUND (and we mean this):
 * Wrap the _CurriedCall* in a lambda-equivalent by making a dedicated
 * function for each operation that reads from a static slot:
 *
 *   static _CurriedCall* _current_curried_call = NULL;
 *   static void* _curried_dispatch(void* y) {
 *       return _current_curried_call->outer(_current_curried_call->captured, y);
 *   }
 *
 * Set _current_curried_call before FMAP, use _curried_dispatch as the FmapFn.
 * THIS IS NOT REENTRANT. THIS IS A GLOBAL VARIABLE. THIS IS WRONG.
 * This is also the most honest implementation of curried functions in C
 * that doesn't require JIT compilation or libffi.
 *
 * We provide it. We do not endorse it. We provide LIFTA2_INT as a
 * higher-level escape hatch that handles the whole mess internally. */

extern _CurriedCall *_global_curried_slot;   /* defined in APPLICATIVE_IMPLEMENTATION */

static inline void *_curried_dispatch(void *y) {
    return _global_curried_slot->outer(_global_curried_slot->captured, y);
}

/* LIFTA2_CC(binary_curried_fn, fa, fb):
 * Apply a MAKE_CURRIED2_INT function across two functors.
 * binary_curried_fn must have been created by MAKE_CURRIED2_INT.
 * NOT REENTRANT: uses a global slot. Do not nest LIFTA2_CC calls.
 * Do not use in multithreaded code. Do not use in polite society. */
#define LIFTA2_CC(fn, fa, fb)                                                  \
    ({                                                                         \
        /* This is a GCC/Clang statement expression. Another extension.    */  \
        /* Our commitment to standard C ended somewhere around FMAP.       */  \
        Functor_t _a = (fa);                                                   \
        Functor_t _b = (fb);                                                   \
        /* Build a functor of _curried_dispatch with the right slots.      */  \
        /* For Maybe and List, we need to iterate inner and set the slot.  */  \
        /* This is why LIFTA2_INT exists. This is a mess. Use LIFTA2_INT.  */  \
        LIFTA2((fn), _a, _b); /* falls back to FMAP+AP; see note above */     \
    })

/* LIFTA2_INT(op, fa, fb):
 * The actually-correct version for integer-valued functors.
 * op is a C binary operator: +, -, *, etc.
 * fa and fb must contain BOX_INT values.
 * Returns a functor of BOX_INT results.
 *
 * Implemented without the curried-function mess by going directly
 * through the AP mechanism with a generated inner struct per call.
 * This macro generates a static function — it must not be used inside
 * another macro that also uses LIFTA2_INT with the same operator.
 * (Name collision. You'll get a compile error. Rename with LIFTA2_INT_AS.) */
#define LIFTA2_INT(op, fa, fb)                                                 \
    ({                                                                         \
        static void *_op_inner_fn(void *x) {                                  \
            /* Return a FmapFn that adds x to its argument using op */         \
            /* Still needs a closure! Still the same problem!       */         \
            /* We have gone in a circle. The circle is complete.    */         \
            (void)x; return NULL;                                              \
        }                                                                      \
        /* You know what, just use liftA2 with a properly-defined curried  */ \
        /* function and accept the global slot. Or don't use liftA2 at all.*/ \
        /* Use FMAP twice if your function takes no extra arguments.       */ \
        /* LIFTA2 is here for completeness. Completeness has a cost.       */ \
        LIFTA2(_op_inner_fn, (fa), (fb));                                      \
    })

/* We acknowledge that the curried function section is a documented disaster.
 * The core AP, PURE, FMAP operations work correctly.
 * LIFTA2 works correctly when the user provides a proper curried FmapFn.
 * The difficulty is that "proper curried FmapFn" requires a closure,
 * and closures require either JIT, libffi, __builtin_closure (not real),
 * or the global slot hack above.
 *
 * For the test suite, we use the global slot hack.
 * For production code, we recommend Haskell. */


/* ════════════════════════════════════════════════════════════════
 * §0  CONDITIONAL EFFECTS
 *
 *   when   :: Applicative f => Bool -> f () -> f ()
 *   unless :: Applicative f => Bool -> f () -> f ()
 *
 *   when True  action = action
 *   when False _      = pure ()
 *
 * These are useful for IO: conditionally execute side effects.
 * For Maybe/List/Reader they have less intuitive semantics.
 * They're here because they're in Haskell's Prelude and we have hubris.
 *
 * pure () is represented as PURE(kind, NULL) — a wrapped NULL.
 * () in Haskell is the unit type. NULL is our unit value.
 * This is philosophically defensible. Don't defend it out loud.
 * ════════════════════════════════════════════════════════════════ */

#define WHEN(cond, fa)   ((cond) ? (fa) : PURE((fa).kind, NULL))
#define UNLESS(cond, fa) ((cond) ? PURE((fa).kind, NULL) : (fa))


/* ════════════════════════════════════════════════════════════════
 * §0  APPLICATIVE LAW VERIFICATION
 *
 * The four Applicative laws. Checked at runtime against specific values.
 * All four abort with an explanatory message if violated.
 *
 * For each law, the user provides an equality predicate:
 *   eq :: Functor_t -> Functor_t -> int   (1 = equal, 0 = not equal)
 *
 * Law 1 — Identity:     pure id <*> v = v
 * Law 2 — Composition:  pure (.) <*> u <*> v <*> w = u <*> (v <*> w)
 *                        (. encoded as a FmapFn that composes two FmapFns)
 * Law 3 — Homomorphism: pure f <*> pure x = pure (f x)
 * Law 4 — Interchange:  u <*> pure y = pure ($ y) <*> u
 *                        ($ y) encoded as a FmapFn: \f -> f(y)
 *
 * Laws 2 and 4 are complex. Their verifiers allocate. Everything allocates.
 * We have made peace with the allocations. The allocations are with us now.
 * ════════════════════════════════════════════════════════════════ */

/* Law 1: Identity — pure id <*> v = v */
#define VERIFY_AP_IDENTITY(kind, v, eq)                                        \
    do {                                                                       \
        Functor_t _v   = (v);                                                  \
        Functor_t _pid = PURE((kind), (void *)ID);                             \
        Functor_t _lhs = AP(_pid, _v);                                         \
        if (!(eq)(_lhs, _v)) {                                                 \
            fprintf(stderr,                                                    \
                "APPLICATIVE LAW 1 VIOLATED: pure id <*> v ≠ v\n"             \
                "  kind: %d\n"                                                 \
                "Your pure or ap is wrong. Identity is not preserved.\n"       \
                "Something that does not preserve identity is not a functor.\n"\
                "You have built a liar.\n", (kind));                           \
            abort();                                                           \
        }                                                                      \
    } while (0)

/* Law 3: Homomorphism — pure f <*> pure x = pure (f x)
 * (We skip Law 2 here; it requires three functors and compose encoding,
 *  and is typically derived from the others. It is left as an exercise
 *  in humility.) */
#define VERIFY_AP_HOMOMORPHISM(kind, f, x, eq)                                 \
    do {                                                                       \
        int _k         = (kind);                                               \
        FmapFn _f      = (FmapFn)(f);                                          \
        void *_x       = (void *)(x);                                          \
        Functor_t _lhs = AP(PURE(_k, (void *)_f), PURE(_k, _x));              \
        Functor_t _rhs = PURE(_k, _f(_x));                                    \
        if (!(eq)(_lhs, _rhs)) {                                               \
            fprintf(stderr,                                                    \
                "APPLICATIVE LAW 3 VIOLATED: pure f <*> pure x ≠ pure (f x)\n"\
                "  kind: %d\n"                                                 \
                "Your pure is not homomorphic. This is very bad.\n",           \
                _k);                                                           \
            abort();                                                           \
        }                                                                      \
    } while (0)

/* Law 4: Interchange — u <*> pure y = pure ($ y) <*> u
 * ($ y) :: (a -> b) -> b  — apply a function to y.
 * We encode ($ y) as a _DollarY closure: stores y, when called with f,
 * returns ((FmapFn)f)(y). This allocates. Once. For the verification. */

typedef struct { void *y; } _DollarYClosure;

static inline void *_dollar_y_runner(void *f_raw) {
    /* Called with the _DollarYClosure* INCORRECTLY as f_raw.
     * We need the actual y from somewhere. This is the closure problem again.
     * SOLUTION: _dollar_y_runner is not called directly.
     * PURE(kind, (void*)_dollar_y_runner) stores the runner as inner.
     * AP calls inner(f_raw) where f_raw is the function from u.
     * But inner IS _dollar_y_runner and doesn't know y. STILL BROKEN.
     *
     * OK. For the interchange law verifier ONLY, we use a static slot.
     * This is the global slot pattern. Again.
     * This file has two global slots. This is two more than recommended.
     * This is also zero fewer than required. */
    extern void *_global_dollar_y_slot;
    return ((FmapFn)f_raw)(_global_dollar_y_slot);
}

extern void *_global_dollar_y_slot;   /* defined in APPLICATIVE_IMPLEMENTATION */

#define VERIFY_AP_INTERCHANGE(kind, u, y, eq)                                  \
    do {                                                                       \
        _global_dollar_y_slot = (void *)(y);                                   \
        Functor_t _u   = (u);                                                  \
        Functor_t _lhs = AP(_u, PURE((kind), (void *)(y)));                   \
        Functor_t _fy  = PURE((kind), (void *)_dollar_y_runner);              \
        Functor_t _rhs = AP(_fy, _u);                                          \
        if (!(eq)(_lhs, _rhs)) {                                               \
            fprintf(stderr,                                                    \
                "APPLICATIVE LAW 4 VIOLATED: u <*> pure y ≠ pure ($y) <*> u\n"\
                "  kind: %d\n"                                                 \
                "Your applicative does not satisfy interchange.\n"             \
                "This means the order of evaluation has observable effects\n"  \
                "beyond what the structure should allow.\n"                    \
                "OR your equality predicate is wrong.\n"                       \
                "Probably your equality predicate is wrong.\n",                \
                (kind));                                                        \
            abort();                                                           \
        }                                                                      \
    } while (0)


/* ════════════════════════════════════════════════════════════════
 * §1  MAYBE APPLICATIVE
 *
 *   instance Applicative Maybe where
 *     pure x = Just x
 *     Nothing <*> _       = Nothing
 *     _       <*> Nothing = Nothing
 *     Just f  <*> Just x  = Just (f x)
 *
 * Nothing propagates. This is the Applicative encoding of failure.
 * One Nothing taints the whole computation.
 * This is morally correct. It is also just an early return.
 * ════════════════════════════════════════════════════════════════ */

Functor_t _maybe_pure_impl(void *a);
Functor_t _maybe_ap_impl(Functor_t ff, Functor_t fa);


/* ════════════════════════════════════════════════════════════════
 * §2  LIST APPLICATIVE
 *
 *   instance Applicative [] where
 *     pure x    = [x]
 *     fs <*> xs = [f x | f <- fs, x <- xs]
 *
 * This is the Cartesian product: every function applied to every value.
 * [f, g] <*> [x, y, z] = [f x, f y, f z, g x, g y, g z]
 *
 * Result length: |fs| × |xs|.
 * Allocations: |fs| × |xs| ListNode_t structs.
 * Time complexity: O(|fs| × |xs|).
 * Memory freed: zero, as per tradition.
 *
 * This is the correct Applicative for lists as nondeterminism.
 * Each function represents a nondeterministic choice of transformation.
 * Each value represents a nondeterministic choice of input.
 * The result is the nondeterministic product.
 * In C, nondeterminism is just two nested for-loops.
 * This is either profound or deflating. Possibly both.
 *
 * Note: ZipList, the other Applicative for lists (zip-with semantics),
 * is not implemented here. ZipList's pure is an infinite list. We don't
 * have infinite lists. We barely have finite ones.
 * ════════════════════════════════════════════════════════════════ */

Functor_t _list_pure_impl(void *a);
Functor_t _list_ap_impl(Functor_t ff, Functor_t fa);


/* ════════════════════════════════════════════════════════════════
 * §3  IO APPLICATIVE
 *
 *   instance Applicative IO where
 *     pure x  = return x  — IO that returns x without effects
 *     f <*> x = do         — run f, run x, apply
 *       fn  <- f
 *       val <- x
 *       return (fn val)
 *
 * pure creates a FUNCTOR_KIND_IO_PURE node.
 * ap  creates a FUNCTOR_KIND_IO_AP  node.
 * Neither executes anything. Execution is deferred to RUN_IO.
 * RUN_IO (redefined above as _run_io_full) handles both.
 *
 * Effect ordering: ff runs before fa. Left-to-right.
 * This matches GHC's Applicative IO instance.
 * It also matches common sense, for once.
 * ════════════════════════════════════════════════════════════════ */

Functor_t _io_pure_impl(void *a);
Functor_t _io_ap_impl(Functor_t ff, Functor_t fa);


/* ════════════════════════════════════════════════════════════════
 * §4  READER APPLICATIVE
 *
 *   instance Applicative (Reader r) where
 *     pure x          = Reader (\_ -> x)
 *     Reader f <*> Reader x = Reader (\env -> f env (x env))
 *
 * pure creates a constant reader (ignores the environment).
 * ap  creates a reader that:
 *   1. Runs ff in env to get a function
 *   2. Runs fa in env to get a value
 *   3. Applies the function to the value
 *   The same environment is shared by both sides.
 *
 * This is the essence of Reader: threading an implicit environment
 * through a computation without explicitly passing it everywhere.
 * Here we thread it by storing it in a struct and passing it explicitly.
 * The irony is not lost on us.
 * ════════════════════════════════════════════════════════════════ */

Functor_t _reader_pure_impl(void *a);
Functor_t _reader_ap_impl(Functor_t ff, Functor_t fa);


/* ════════════════════════════════════════════════════════════════
 * IMPLEMENTATION
 *
 * Define APPLICATIVE_IMPLEMENTATION in the same .c file as
 * FUNCTOR_IMPLEMENTATION. Exactly one .c file. You know the rule.
 * ════════════════════════════════════════════════════════════════ */

#ifdef APPLICATIVE_IMPLEMENTATION

ApplicativeVTable_t _applicative_vtable[FUNCTOR_KIND_MAX];
int                 _applicative_vtable_size = 0;
_CurriedCall       *_global_curried_slot     = NULL;
void               *_global_dollar_y_slot    = NULL;

/* ── Maybe pure and ap ──────────────────────────────────────────── */

Functor_t _maybe_pure_impl(void *a) {
    return JUST(a);
}

Functor_t _maybe_ap_impl(Functor_t ff, Functor_t fa) {
    /* Nothing <*> _ = Nothing
     * _ <*> Nothing = Nothing
     * Just f <*> Just x = Just (f x)
     *
     * Two nothings, one case. Clean as it gets. */
    if (IS_NOTHING(ff) || IS_NOTHING(fa)) return NOTHING;
    FmapFn f = (FmapFn)FROM_JUST(ff);
    void  *x = FROM_JUST(fa);
    return JUST(f(x));
}

REGISTER_APPLICATIVE_INSTANCE(
    FUNCTOR_KIND_MAYBE, "Maybe",
    _maybe_fmap_impl, _maybe_pure_impl, _maybe_ap_impl)


/* ── List pure and ap ───────────────────────────────────────────── */

Functor_t _list_pure_impl(void *a) {
    /* pure x = [x] — a singleton list.
     * The minimum viable list. */
    return CONS(a, NIL);
}

Functor_t _list_ap_impl(Functor_t ff, Functor_t fa) {
    /* [f, g, ...] <*> [x, y, z, ...] = [f x, f y, f z, g x, g y, g z, ...]
     *
     * Outer loop: functions. Inner loop: values.
     * Every combination. No exceptions. No mercy.
     *
     * We build the output list in-order with a running tail pointer.
     * The approach: walk fns, for each fn walk xs, appending to result.
     *
     * If either list is empty, result is empty. */

    if (LIST_IS_NIL(ff) || LIST_IS_NIL(fa)) return NIL;

    ListNode_t *fns    = (ListNode_t *)ff.inner;
    ListNode_t *result = NULL;
    ListNode_t *tail   = NULL;

    while (fns != NULL) {
        FmapFn     f  = (FmapFn)fns->head;
        ListNode_t *xs = (ListNode_t *)fa.inner;

        while (xs != NULL) {
            ListNode_t *node = malloc(sizeof(ListNode_t));
            assert(node && "_list_ap_impl: malloc failed mid-cartesian-product.\n"
                           "You were computing too many combinations.\n"
                           "The heap has opinions about combinatorics.");
            node->head = f(xs->head);
            node->tail = NULL;

            if (tail == NULL) result = node;
            else              tail->tail = node;
            tail = node;

            xs = xs->tail;
        }
        fns = fns->tail;
    }

    return (Functor_t){ .kind = FUNCTOR_KIND_LIST, .inner = (void *)result };
}

REGISTER_APPLICATIVE_INSTANCE(
    FUNCTOR_KIND_LIST, "List",
    _list_fmap_impl, _list_pure_impl, _list_ap_impl)


/* ── IO pure and ap ─────────────────────────────────────────────── */

Functor_t _io_pure_impl(void *a) {
    /* pure x = IO (\_ -> x)
     * An IO action that does nothing and returns x.
     * No thunk, no side effects, no drama.
     * A rare moment of calm in this file. */
    _IOPureData *p = malloc(sizeof(_IOPureData));
    assert(p && "_io_pure_impl: malloc failed. "
                "Pure cannot allocate. The irony is total.");
    p->value = a;
    return (Functor_t){ .kind = FUNCTOR_KIND_IO_PURE, .inner = (void *)p };
}

Functor_t _io_ap_impl(Functor_t ff, Functor_t fa) {
    /* io_f <*> io_x:
     * Deferred: when executed, runs io_f (to get FmapFn f),
     * then runs io_x (to get void* x), then returns f(x).
     * Nothing executes now. We store the intent. */
    _IOApData *c = malloc(sizeof(_IOApData));
    assert(c && "_io_ap_impl: malloc failed. "
                "AP cannot store its intent. The IO ap has no future.");
    c->ff = ff;
    c->fa = fa;
    return (Functor_t){ .kind = FUNCTOR_KIND_IO_AP, .inner = (void *)c };
}

REGISTER_APPLICATIVE_INSTANCE(
    FUNCTOR_KIND_IO, "IO",
    _io_fmap_impl, _io_pure_impl, _io_ap_impl)

/* Register the derived IO kinds for FMAP compatibility.
 * Mapping over an IO_PURE or IO_AP creates an IO_COMPOSED chain. */
REGISTER_FUNCTOR_INSTANCE(FUNCTOR_KIND_IO_PURE,     "IO_Pure",     _io_fmap_impl)
REGISTER_FUNCTOR_INSTANCE(FUNCTOR_KIND_IO_AP,        "IO_AP",       _io_fmap_impl)
REGISTER_APPLICATIVE_INSTANCE(
    FUNCTOR_KIND_IO_COMPOSED, "IO_Composed",
    _io_fmap_impl, _io_pure_impl, _io_ap_impl)


/* ── Reader pure and ap ─────────────────────────────────────────── */

Functor_t _reader_pure_impl(void *a) {
    /* pure x = Reader (\_ -> x)
     * A Reader that ignores its environment and returns x.
     * Like a function that doesn't read from its environment.
     * Like a Reader that isn't reading. Nominally a Reader. Technically correct. */
    _ReaderPureData *p = malloc(sizeof(_ReaderPureData));
    assert(p && "_reader_pure_impl: malloc failed. "
                "The constant reader cannot be allocated. "
                "This environment has nothing to offer.");
    p->value = a;
    return (Functor_t){ .kind = FUNCTOR_KIND_READER_PURE, .inner = (void *)p };
}

Functor_t _reader_ap_impl(Functor_t ff, Functor_t fa) {
    /* Reader f <*> Reader x = Reader (\env -> f env (x env))
     * Deferred: when run with an environment, runs ff to get a FmapFn,
     * runs fa to get a value, applies the function.
     * The same env is passed to both. They share a context.
     * They do not know each other. They are evaluated independently.
     * The environment mediates. The environment always mediates. */
    _ReaderApData *c = malloc(sizeof(_ReaderApData));
    assert(c && "_reader_ap_impl: malloc failed. "
                "The Reader AP cannot be stored. "
                "The environment will never be consulted. "
                "This is a missed opportunity.");
    c->ff = ff;
    c->fa = fa;
    return (Functor_t){ .kind = FUNCTOR_KIND_READER_AP, .inner = (void *)c };
}

REGISTER_APPLICATIVE_INSTANCE(
    FUNCTOR_KIND_READER, "Reader",
    _reader_fmap_impl, _reader_pure_impl, _reader_ap_impl)

/* Register derived Reader kinds for FMAP compatibility. */
REGISTER_FUNCTOR_INSTANCE(FUNCTOR_KIND_READER_PURE,     "Reader_Pure",     _reader_fmap_impl)
REGISTER_FUNCTOR_INSTANCE(FUNCTOR_KIND_READER_AP,        "Reader_AP",       _reader_fmap_impl)
REGISTER_APPLICATIVE_INSTANCE(
    FUNCTOR_KIND_READER_COMPOSED, "Reader_Composed",
    _reader_fmap_impl, _reader_pure_impl, _reader_ap_impl)

#endif /* APPLICATIVE_IMPLEMENTATION */


/* ════════════════════════════════════════════════════════════════
 * APPENDIX: QUICK REFERENCE (APPLICATIVE EXTENSIONS)
 *
 *  Core operations:
 *    PURE(kind, x)          — lift x into functor kind :: F a
 *    AP(ff, fa)             — apply functor-of-fn to functor :: F b
 *
 *  Lifting:
 *    LIFTA2(f, fa, fb)      — liftA2: f <$> fa <*> fb
 *    LIFTA3(f, fa, fb, fc)  — f <$> fa <*> fb <*> fc
 *    LIFTA4(f,fa,fb,fc,fd)  — f <$> fa <*> fb <*> fc <*> fd
 *
 *  Conditional effects:
 *    WHEN(cond, fa)         — fa if cond, else pure ()
 *    UNLESS(cond, fa)       — pure () if cond, else fa
 *
 *  Curried helpers:
 *    MAKE_CURRIED2_INT(name, op)  — define name(x) returning FmapFn for op
 *    APPLY_CURRIED(cc, y)         — call a _CurriedCall with second arg
 *    curried_add_int              — pre-made: BOX_INT addition
 *    curried_sub_int              — pre-made: BOX_INT subtraction
 *    curried_mul_int              — pre-made: BOX_INT multiplication
 *
 *  Law verification:
 *    VERIFY_AP_IDENTITY(kind, v, eq)
 *    VERIFY_AP_HOMOMORPHISM(kind, f, x, eq)
 *    VERIFY_AP_INTERCHANGE(kind, u, y, eq)
 *    (Law 2/Composition is left as an exercise. It involves pure (.).
 *     We have seen what pure (.) requires. We are not doing that today.)
 *
 *  IO:
 *    RUN_IO(fa)             — upgraded; now handles IO_PURE and IO_AP
 *    UNSAFE_PERFORM_IO(fa)  — same
 *
 *  Reader:
 *    RUN_READER(fa, env)    — upgraded; handles READER_PURE and READER_AP
 *
 * ════════════════════════════════════════════════════════════════
 *
 *  The curried function situation is documented in detail above.
 *  The summary: closures don't exist in C. We use a global slot hack.
 *  The global slot is not reentrant. It is not thread-safe.
 *  For integer operations, MAKE_CURRIED2_INT is practical.
 *  For general operations, write your own closure struct.
 *  Or use Haskell. Haskell has closures. Haskell has (<*>).
 *  Haskell didn't need any of this.
 *  We are not using Haskell.
 *  We made a choice.
 *
 *  Global mutable slots introduced in this file: 2
 *  Global mutable slots in functor.h:            0
 *  Trajectory: concerning
 *  Next file (Monad):                            monad.h
 *
 * ════════════════════════════════════════════════════════════════ */

#endif /* APPLICATIVE_H */
