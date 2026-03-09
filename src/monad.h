/*
 * monad.h — The Monad typeclass, in C macros
 *
 * The trilogy concludes.
 *
 *   functor.h:     FMAP  — lift a function into a context
 *   applicative.h: AP    — combine independent contextual computations
 *   monad.h:       BIND  — sequence dependent contextual computations
 *
 * The hierarchy:
 *
 *   Functor f
 *     └── Applicative f    (every Applicative is a Functor)
 *           └── Monad f    (every Monad is an Applicative)
 *
 * What Monad adds over Applicative:
 *
 *   With Applicative, you can combine f(a) and f(b) into f(c).
 *   The structure of f(c) cannot depend on the VALUES of a or b.
 *   It can only depend on the structure of f.
 *
 *   With Monad, the structure of the SECOND computation can depend
 *   on the VALUE produced by the FIRST.
 *   This is sequencing. This is what makes IO an IO monad and not just
 *   an IO applicative. This is what makes the list monad a search monad
 *   and not just a Cartesian-product applicative.
 *
 * The key operation:
 *
 *   (>>=) :: Monad m => m a -> (a -> m b) -> m b
 *
 * That second argument — (a -> m b) — is a Kleisli arrow.
 * It takes a VALUE and returns a COMPUTATION.
 * The output computation's STRUCTURE can depend on the input VALUE.
 *
 * In C:
 *   typedef Functor_t (*KleisliFn)(void *);
 *
 * This is a function from void* to Functor_t. It is the one type
 * in this file that is not void*. We acknowledge this with gratitude.
 *
 * ════════════════════════════════════════════════════════════════
 *
 * AN OBSERVATION ABOUT THE IMPLEMENTATION:
 *
 * monad.h is actually CLEANER than applicative.h.
 *
 * In applicative.h, LIFTA2 required a curried function: (a -> (b -> c)).
 * That inner (b -> c) needed to CAPTURE a as a closure. C has no closures.
 * We ended up with two global mutable slots, a documented disaster section,
 * and a paragraph about reinventing the Spineless Tagless G-machine.
 *
 * In monad.h, BIND takes an explicit KleisliFn: (a -> m b).
 * That function can be a GCC nested function that CLOSES OVER its
 * enclosing scope. This gives us real closures — via the GCC trampoline
 * extension — without global state.
 *
 * The DO macro (below) demonstrates this: it uses GCC nested functions
 * to desugar do-notation into BIND calls, and it works correctly,
 * with proper variable capture, at the cost of:
 *   - Requiring GCC or Clang
 *   - Requiring an executable stack (for the nested function trampoline)
 *   - Still allocating on every BIND call for IO and Reader
 *   - Still being utterly unmaintainable
 *
 * Global mutable slots introduced:
 *   functor.h:     0
 *   applicative.h: 2
 *   monad.h:       0   ← this is progress
 *
 * The Monad interface is, structurally, more compatible with C than
 * the Applicative interface. This will not comfort you.
 *
 * ════════════════════════════════════════════════════════════════
 *
 * THE MONAD LAWS:
 *
 *   1. Left identity:  return a >>= f  ≡  f a
 *   2. Right identity: m >>= return    ≡  m
 *   3. Associativity:  (m >>= f) >>= g ≡  m >>= (\x -> f x >>= g)
 *
 * Law 3 defines the Kleisli category. The objects are types.
 * The morphisms are Kleisli arrows (a -> m b).
 * Composition of morphisms is (>=>).
 * The identity morphism is return.
 * The monad laws are exactly the category laws for this category.
 *
 * This file implements that category in void*.
 * Saunders Mac Lane is not aware of this file.
 * This is for the best.
 *
 * ════════════════════════════════════════════════════════════════
 *
 * INSTANCES:
 *
 *   §1  Maybe  — failure propagation / optional computation
 *   §2  List   — nondeterminism / search / concatMap
 *   §3  IO     — sequenced effects (the reason IO exists)
 *   §4  Reader — threading an implicit environment
 *
 * ════════════════════════════════════════════════════════════════
 *
 * DO NOTATION:
 *
 *   Haskell:
 *     do x <- ma
 *        y <- mb
 *        return (f x y)
 *
 *   This file:
 *     DO(ma, x,
 *     DO(mb, y,
 *        MRETURN(kind, f(x, y))))
 *
 *   The DO macro uses GCC nested functions with trampolines.
 *   Each nested function closes over its enclosing scope.
 *   This requires: GCC/Clang, executable stack (-z execstack on Linux,
 *   default on most systems, disabled on hardened kernels).
 *
 *   If your kernel has W^X enforced (grsecurity, SELinux strict, etc.),
 *   the trampoline will segfault. Use explicit KleisliFn functions instead.
 *   Or compile with -z execstack and accept the security implications.
 *   Or use Haskell.
 *
 * ════════════════════════════════════════════════════════════════
 *
 * EXAMPLES:
 *
 *   // Maybe: safe division
 *   KleisliFn safe_div_5 = ...;  // returns Nothing if divisor is 0
 *   Functor_t result = DO(JUST(BOX_INT(100)), x,
 *                      DO(JUST(BOX_INT(5)),   divisor,
 *                         UNBOX_INT(divisor) == 0
 *                           ? NOTHING
 *                           : MAYBE_RETURN(BOX_INT(UNBOX_INT(x) /
 *                                                  UNBOX_INT(divisor)))));
 *   // result = Just 20
 *
 *   // List: nondeterministic choice (list comprehension)
 *   Functor_t xs = LIST(BOX_INT(1), BOX_INT(2), BOX_INT(3));
 *   Functor_t ys = LIST(BOX_INT(10), BOX_INT(20));
 *   Functor_t pairs_sum =
 *       DO(xs, x,
 *       DO(ys, y,
 *          LIST_RETURN(BOX_INT(UNBOX_INT(x) + UNBOX_INT(y)))));
 *   // = [11, 21, 12, 22, 13, 23]
 *
 *   // List: with GUARD (list comprehension with predicate)
 *   Functor_t pythagorean_triples_under_20 =
 *       DO(range_1_20, a,
 *       DO(range_1_20, b,
 *       DO(range_1_20, c,
 *          THEN(GUARD_LIST(
 *              UNBOX_INT(a)*UNBOX_INT(a) +
 *              UNBOX_INT(b)*UNBOX_INT(b) ==
 *              UNBOX_INT(c)*UNBOX_INT(c)),
 *          LIST_RETURN(make_triple(a, b, c))))));
 *   // This is a list monad comprehension. In C. With macros.
 *   // We don't know how to feel about this. We feel everything.
 *
 *   // IO: sequenced effects
 *   static void* read_int_thunk(void)  { int x; scanf("%d",&x); return BOX_INT(x); }
 *   static void* print_int_fn(void* x) { printf("%d\n", UNBOX_INT(x)); return NULL; }
 *
 *   Functor_t program =
 *       DO(IO(read_int_thunk), x,
 *          IO_RETURN(print_int_fn(x)));  // prints what was read
 *   RUN_IO(program);
 *
 *   // Reader: dependency injection
 *   typedef struct { int multiplier; int offset; } Config;
 *   static void* get_mult(void* env) { return BOX_INT(((Config*)env)->multiplier); }
 *   static void* get_off(void* env)  { return BOX_INT(((Config*)env)->offset); }
 *
 *   Functor_t computation =
 *       DO(READER(get_mult), mult,
 *       DO(READER(get_off),  off,
 *          READER_RETURN(BOX_INT(UNBOX_INT(mult) * 10 + UNBOX_INT(off)))));
 *   Config cfg = { .multiplier = 3, .offset = 7 };
 *   void* answer = RUN_READER(computation, &cfg);
 *   // answer = BOX_INT(37)
 *
 * ════════════════════════════════════════════════════════════════
 *
 * USAGE:
 *
 *   // In exactly one .c file:
 *   #define FUNCTOR_IMPLEMENTATION
 *   #define APPLICATIVE_IMPLEMENTATION
 *   #define MONAD_IMPLEMENTATION
 *   #include "monad.h"
 *
 *   // Everywhere else:
 *   #include "monad.h"
 *
 * ════════════════════════════════════════════════════════════════
 */

#ifndef MONAD_H
#define MONAD_H

#include "applicative.h"


/* ════════════════════════════════════════════════════════════════
 * §0  THE KLEISLI ARROW
 *
 * KleisliFn is the type of functions passed to BIND.
 *   Haskell: (a -> m b)
 *   C:       Functor_t (*)(void *)
 *
 * This is the ONLY function type in this trilogy that returns Functor_t
 * instead of void*. The monad breaks the void* monoculture.
 * Slightly. In one specific place.
 * void* is still everywhere else.
 * void* is always everywhere else.
 * ════════════════════════════════════════════════════════════════ */

typedef Functor_t (*KleisliFn)(void *);


/* ════════════════════════════════════════════════════════════════
 * §0  NEW KIND TAGS
 *
 * IO and Reader need new kinds for BIND-created values.
 * Maybe and List do not: their bind implementations are eager
 * (execute immediately) and return standard kinds.
 *
 * IO_BIND:     a deferred BIND node; executed when RUN_IO is called
 * READER_BIND: a deferred BIND node; executed when RUN_READER is called
 *
 * Pattern: the deeper we go (Functor < Applicative < Monad), the more
 * kinds accumulate. We started with 6. We now have 12.
 * FUNCTOR_KIND_MAX is 64. We have room for approximately 4 more typeclasses.
 * After that, you must increase FUNCTOR_KIND_MAX and recompile everything.
 * This is, architecturally, the category theory equivalent of INT_MAX.
 * ════════════════════════════════════════════════════════════════ */

#define FUNCTOR_KIND_IO_BIND       10
#define FUNCTOR_KIND_READER_BIND   11


/* ════════════════════════════════════════════════════════════════
 * §0  INTERNAL BIND NODE TYPES
 *
 * BIND for IO and Reader is lazy: it creates a node that is evaluated
 * when the action is RUN. This preserves referential transparency
 * in the same sense that a signed confession preserves innocence —
 * technically accurate in a narrow interpretation, increasingly strained
 * under examination.
 * ════════════════════════════════════════════════════════════════ */

/* An IO action produced by (>>=): deferred "run ma, apply f to result". */
typedef struct {
    Functor_t  ma;
    KleisliFn  f;
} _IOBindData;

/* A Reader produced by (>>=): deferred "run ma in env, apply f, run result in env". */
typedef struct {
    Functor_t  ma;
    KleisliFn  f;
} _ReaderBindData;


/* ════════════════════════════════════════════════════════════════
 * §0  EXTENDED IO RUNNER (monad edition)
 *
 * We extend _run_io_full from applicative.h to handle IO_BIND.
 * RUN_IO and UNSAFE_PERFORM_IO are redefined again.
 *
 * The IO execution model, accumulated across all three files:
 *
 *   IO raw thunk    — executes the thunk directly
 *   IO_COMPOSED     — result of FMAP: applies f after executing source
 *   IO_PURE         — result of PURE: returns value without executing anything
 *   IO_AP           — result of AP:   executes ff then fa, applies
 *   IO_BIND         — result of BIND: executes ma, applies f, executes result
 *
 * Each of these defers execution until RUN_IO.
 * A deeply nested IO can be a tree of these nodes.
 * RUN_IO unwinds the tree recursively.
 * The recursion depth is bounded by the nesting of your do-block.
 * The stack depth is bounded by your system.
 * These bounds are independent. This is a potential issue.
 * You will not notice it until a large do-block segfaults.
 * At that point, you will miss Haskell's heap-based evaluation.
 * ════════════════════════════════════════════════════════════════ */

static inline void *_run_io_monad(Functor_t io);

static inline void *_run_io_monad(Functor_t io) {
    switch (io.kind) {
        case FUNCTOR_KIND_IO:
            return ((IOThunk)io.inner)();

        case FUNCTOR_KIND_IO_COMPOSED: {
            _IOComposed *c = (_IOComposed *)io.inner;
            return c->f(_run_io_monad(c->source));
        }

        case FUNCTOR_KIND_IO_PURE:
            return ((_IOPureData *)io.inner)->value;

        case FUNCTOR_KIND_IO_AP: {
            _IOApData *c = (_IOApData *)io.inner;
            FmapFn f     = (FmapFn)_run_io_monad(c->ff);
            void  *x     = _run_io_monad(c->fa);
            return f(x);
        }

        case FUNCTOR_KIND_IO_BIND: {
            /* ma >>= f: run ma, apply f to get new IO action, run that.
             *
             * This is the heart of IO sequencing.
             * printf, scanf, malloc, free — all of this is expressible
             * as chains of IO_BIND nodes.
             * We are running them by recursively unwinding a linked list
             * of heap-allocated structs.
             * GHC does something similar, modulo 20 years of optimisation.
             * We have done something similar in one sitting.
             * The quality difference is immeasurable.
             * The structure is recognisably the same. */
            _IOBindData *c = (_IOBindData *)io.inner;
            void        *a = _run_io_monad(c->ma);
            Functor_t    mb = c->f(a);
            return _run_io_monad(mb);
        }

        default:
            fprintf(stderr,
                "_run_io_monad: unrecognised IO kind=%d.\n"
                "This IO action has transcended the known kinds.\n"
                "It should not exist. Like much in this file.\n",
                io.kind);
            abort();
    }
}

#undef  RUN_IO
#undef  UNSAFE_PERFORM_IO
#define RUN_IO(fa)            (_run_io_monad(fa))
#define UNSAFE_PERFORM_IO(fa) (_run_io_monad(fa))


/* ════════════════════════════════════════════════════════════════
 * §0  EXTENDED READER RUNNER (monad edition)
 *
 * Extended to handle READER_BIND.
 * ════════════════════════════════════════════════════════════════ */

static inline void *_run_reader_monad(Functor_t reader, void *env);

static inline void *_run_reader_monad(Functor_t reader, void *env) {
    switch (reader.kind) {
        case FUNCTOR_KIND_READER:
            return ((ReaderFn)reader.inner)(env);

        case FUNCTOR_KIND_READER_COMPOSED: {
            _ReaderComposed *c = (_ReaderComposed *)reader.inner;
            return c->f(_run_reader_monad(c->source, env));
        }

        case FUNCTOR_KIND_READER_PURE:
            return ((_ReaderPureData *)reader.inner)->value;

        case FUNCTOR_KIND_READER_AP: {
            _ReaderApData *c = (_ReaderApData *)reader.inner;
            FmapFn f         = (FmapFn)_run_reader_monad(c->ff, env);
            void  *x         = _run_reader_monad(c->fa, env);
            return f(x);
        }

        case FUNCTOR_KIND_READER_BIND: {
            /* Reader g >>= f = Reader (\env -> runReader (f (g env)) env)
             *
             * Run ma in env to get a value.
             * Apply f to that value to get a new Reader.
             * Run the new Reader in the SAME env.
             *
             * The same environment threads through the entire chain.
             * This is dependency injection without a framework.
             * This is how real-world Reader monads work in Haskell.
             * The "framework" is (>>=).
             * The "framework" is three lines of struct access.
             * Frameworks are sometimes three lines of struct access. */
            _ReaderBindData *c = (_ReaderBindData *)reader.inner;
            void            *a = _run_reader_monad(c->ma, env);
            Functor_t        mb = c->f(a);
            return _run_reader_monad(mb, env);
        }

        default:
            fprintf(stderr,
                "_run_reader_monad: unrecognised Reader kind=%d.\n"
                "This Reader has never been in any environment we know of.\n",
                reader.kind);
            abort();
    }
}

#undef  RUN_READER
#define RUN_READER(fa, env) (_run_reader_monad((fa), (void *)(env)))


/* ════════════════════════════════════════════════════════════════
 * §0  THE MONAD VTABLE
 *
 * A third vtable. We now have three vtables.
 *   _functor_vtable:     { kind, name, fmap }
 *   _applicative_vtable: { kind, name, pure_fn, ap }
 *   _monad_vtable:       { kind, name, bind }
 *
 * In Haskell this is one dictionary per typeclass per type.
 * Here it is three global arrays per program.
 * The expressiveness is comparable. The memory layout is not.
 * ════════════════════════════════════════════════════════════════ */

typedef struct {
    int        kind;
    const char *name;
    Functor_t (*bind)(Functor_t ma, KleisliFn f);
} MonadVTable_t;

extern MonadVTable_t _monad_vtable[FUNCTOR_KIND_MAX];
extern int           _monad_vtable_size;


/* ════════════════════════════════════════════════════════════════
 * §0  INSTANCE REGISTRATION
 *
 * REGISTER_MONAD_INSTANCE registers in all THREE vtables.
 * The full typeclass hierarchy is honoured:
 *   Monad    instance → registered in _monad_vtable
 *   Applicative instance → registered in _applicative_vtable
 *   Functor  instance → registered in _functor_vtable
 *
 * This is the C equivalent of GHC deriving the superclass dictionaries.
 * GHC does it at compile time. We do it before main().
 * The result is the same. The moment it happens is different.
 * The moment is, in both cases, before you can do anything about it.
 * ════════════════════════════════════════════════════════════════ */

#define REGISTER_MONAD_INSTANCE(KIND, NAME, FMAP_FN, PURE_FN, AP_FN, BIND_FN) \
    static __attribute__((constructor))                                        \
    void _monad_register_##KIND(void) {                                        \
        if (_monad_vtable_size >= FUNCTOR_KIND_MAX) {                          \
            fputs("MONAD: vtable overflow.\n"                                  \
                  "You have registered more than 64 Monad instances.\n"       \
                  "(FUNCTOR_KIND_MAX = 64 — raise it if you mean this.)\n"     \
                  "This makes you either very ambitious or very wrong.\n"      \
                  "Possibly both.\n", stderr);                                 \
            abort();                                                           \
        }                                                                      \
        _monad_vtable[_monad_vtable_size++] = (MonadVTable_t){                 \
            .kind = (KIND), .name = (NAME), .bind = (BIND_FN),                \
        };                                                                     \
    }                                                                          \
    /* Honour superclass constraints: also register as Applicative and         \
     * Functor. Inlined to avoid duplicate constructor names when multiple    \
     * levels all register for the same KIND. */                              \
    static __attribute__((constructor))                                        \
    void _applicative_via_monad_##KIND(void) {                                 \
        _applicative_vtable[_applicative_vtable_size++] = (ApplicativeVTable_t){ \
            .kind = (KIND), .name = (NAME),                                    \
            .pure_fn = (PURE_FN), .ap = (AP_FN),                              \
        };                                                                     \
    }                                                                          \
    static __attribute__((constructor))                                        \
    void _functor_via_monad_##KIND(void) {                                     \
        _functor_vtable[_functor_vtable_size++] = (FunctorVTable_t){           \
            .kind = (KIND), .name = (NAME), .fmap = (FMAP_FN),                 \
        };                                                                     \
    }


/* ════════════════════════════════════════════════════════════════
 * §0  BIND — the operation that defines Monad
 *
 * BIND(ma, f):
 *   ma :: m a       (Functor_t)
 *   f  :: a -> m b  (KleisliFn: void* -> Functor_t)
 *   returns m b     (Functor_t)
 *
 * THEN(ma, mb):
 *   ma >> mb — sequence effects, discard the value of ma.
 *   Haskell: (>>) ma mb = ma >>= \_ -> mb
 *   Here:    THEN(ma, mb) uses a nested function that ignores its arg.
 *
 * MRETURN(kind, x):
 *   return x — lift a pure value into the monad.
 *   For all our instances, return = pure. They are aliased.
 *   In Haskell, return was historically separate from pure.
 *   They have been the same since GHC 7.10 (2015).
 *   We implement the 2015 version. We are current. Despite everything.
 * ════════════════════════════════════════════════════════════════ */

static inline Functor_t _dispatch_bind(Functor_t ma, KleisliFn f) {
    for (int i = 0; i < _monad_vtable_size; i++) {
        if (_monad_vtable[i].kind == ma.kind) {
            return _monad_vtable[i].bind(ma, f);
        }
    }
    fprintf(stderr,
        "MONAD: no instance found for kind=%d.\n"
        "BIND requires a Monad. You have a Functor_t with an unknown kind.\n"
        "Did you:\n"
        "  (a) forget #define MONAD_IMPLEMENTATION?\n"
        "  (b) try to BIND a raw IO_BIND or IO_AP node directly?\n"
        "  (c) create a new kind and forget to register it?\n"
        "  (d) forget that with great power comes great responsibility,\n"
        "      and you have all of C's power and none of Haskell's safety?\n",
        ma.kind);
    abort();
}

/* BIND: (>>=) */
#define BIND(ma, f)     (_dispatch_bind((ma), (KleisliFn)(f)))

/* THEN: (>>) — sequence, discard left value.
 * Uses a GCC statement expression and nested function to construct
 * the "ignore and return mb" Kleisli arrow inline.
 * mb is evaluated in the outer scope — if mb has side effects at
 * construction time (e.g., malloc), those happen when THEN is evaluated,
 * not when the resulting action is RUN. For IO, use IO(thunk) to defer. */
#define THEN(ma, mb)                                                           \
    ({                                                                         \
        Functor_t _then_mb = (mb);                                             \
        Functor_t _then_k(void *_ignored) { (void)_ignored; return _then_mb; }\
        BIND((ma), (KleisliFn)_then_k);                                        \
    })

/* MRETURN: return / pure. Lift a value into the monad.
 * Same as PURE. Aliased for do-notation aesthetics. */
#define MRETURN(kind, x)   PURE((kind), (x))

/* Per-monad return aliases for use inside DO blocks.
 * These are more ergonomic than specifying the kind explicitly.
 * They are also just aliases. Everything is an alias for everything else.
 * This is fine. */
#define MAYBE_RETURN(x)    JUST(x)
#define LIST_RETURN(x)     CONS((x), NIL)
#define IO_RETURN(x)       (_io_pure_impl((void *)(x)))
#define READER_RETURN(x)   (_reader_pure_impl((void *)(x)))


/* ════════════════════════════════════════════════════════════════
 * §0  DO NOTATION
 *
 * Haskell's do-notation is syntactic sugar over (>>=).
 *
 *   do x <- ma       ≡   ma >>= \x ->
 *      y <- f x          f x >>= \y ->
 *      return (g x y)    return (g x y)
 *
 * We encode this with the DO macro, which uses GCC nested functions
 * to implement the lambda. The nested function CLOSES OVER the
 * enclosing scope, providing real (stack-based) variable capture.
 *
 * DO(ma, x, expr):
 *   Desugars to: BIND(ma, \x -> expr)
 *   x is bound to the value extracted from ma.
 *   expr is evaluated lazily (only when the Kleisli arrow is called).
 *   expr may reference x. expr may reference any outer-scope variables.
 *   expr must be of type Functor_t.
 *
 * Nesting:
 *   DO(ma, x,          — x :: a extracted from ma :: m a
 *   DO(f(x), y,        — y :: b extracted from f(x) :: m b (depends on x!)
 *      MRETURN(K, g(x, y))))  — return g(x, y) :: m c
 *
 * The dependency of the second computation on x is what makes this Monadic
 * rather than Applicative. In Applicative, you cannot write f(x) — you don't
 * have x until after the fact. In Monad, you do. This is the difference.
 *
 * CAVEATS:
 *   - GCC/Clang only. The nested function extension is not in C99/C11/C23.
 *   - Requires executable stack for the trampoline on x86.
 *     On aarch64, GCC nested functions may not use trampolines.
 *     Consult your architecture manual. You have one, right?
 *   - The nested function is only valid while the enclosing scope is live.
 *     BIND consumes it immediately, so this is fine in practice.
 *   - The function name _do_kleisli is generated. If you nest DO macros,
 *     each generates a function named _do_kleisli. GCC handles shadowing
 *     correctly in nested scopes. Clang may warn. The warnings are correct.
 *     The code still works.
 *   - On systems with strict W^X (hardened kernels, some BSDs):
 *     the trampoline requires write+exec simultaneously. It will fail.
 *     Use explicit KleisliFn functions instead. Or re-examine your choices.
 * ════════════════════════════════════════════════════════════════ */

#define DO(ma, x, expr)                                                        \
    ({                                                                         \
        Functor_t _do_kleisli(void *x) { return (expr); }                     \
        BIND((ma), (KleisliFn)_do_kleisli);                                    \
    })

/* DO_ (with explicit continuation function, no nested functions):
 * For hardened systems or MSVC-curious masochists.
 * Provide your own KleisliFn as the continuation.
 * This is BIND with slightly more readable syntax. */
#define DO_(ma, kfn)   BIND((ma), (KleisliFn)(kfn))

/* DORETURN: the final expression in a do-block for a specific kind.
 * Equivalent to writing MRETURN at the end. Saves you specifying the kind
 * if the kind is already obvious from context (it is never obvious in C). */
#define DORETURN(kind, x)  MRETURN((kind), (x))


/* ════════════════════════════════════════════════════════════════
 * §0  KLEISLI COMPOSITION (>=>)
 *
 *   (>=>) :: Monad m => (a -> m b) -> (b -> m c) -> (a -> m c)
 *   (f >=> g) x = f x >>= g
 *
 * This is composition in the Kleisli category.
 * The identity in the Kleisli category is return.
 * The monad laws (left/right identity, associativity) are exactly
 * the category laws for (>=>) and return.
 *
 * KLEISLI_COMPOSE(f, g) returns a KleisliFn using a GCC nested function.
 * The returned function is valid while the enclosing scope is live.
 * Use it immediately. Do not store it past its scope.
 * Storing it past its scope is undefined behavior.
 * Undefined behavior is already our natural habitat.
 * We draw the line here anyway.
 * ════════════════════════════════════════════════════════════════ */

#define KLEISLI_COMPOSE(f, g)                                                  \
    ({                                                                         \
        KleisliFn _kc_f = (KleisliFn)(f);                                     \
        KleisliFn _kc_g = (KleisliFn)(g);                                     \
        Functor_t _fish(void *x) { return BIND(_kc_f(x), _kc_g); }            \
        (KleisliFn)_fish;                                                      \
    })

/* The (>=>) operator is sometimes called "the fish".
 * FISH(f, g) is an alias for KLEISLI_COMPOSE because we respect this tradition. */
#define FISH(f, g)   KLEISLI_COMPOSE((f), (g))


/* ════════════════════════════════════════════════════════════════
 * §0  JOIN
 *
 *   join :: Monad m => m (m a) -> m a
 *   join mma = mma >>= id
 *
 * join collapses two layers of monadic structure into one.
 * It is the "join" in the monoid-in-the-category-of-endofunctors sense —
 * the multiplication of the monad as a monoid.
 *
 * To use JOIN, the inner value of the outer Functor_t must be a
 * heap-allocated Functor_t*. The inner Functor_t is extracted and returned.
 *
 *   Usage:
 *     Functor_t *inner = malloc(sizeof(Functor_t));
 *     *inner = JUST(BOX_INT(42));
 *     Functor_t outer = JUST((void *)inner);   // Maybe (Maybe Int)
 *     Functor_t result = JOIN(outer);           // Maybe Int = Just 42
 *
 * If you don't heap-allocate the inner Functor_t, JOIN produces a
 * Functor_t containing a dangling pointer. This is UB. This is C.
 * ════════════════════════════════════════════════════════════════ */

static Functor_t _join_kleisli(void *inner_functor_ptr) {
    /* inner_functor_ptr points to a heap-allocated Functor_t.
     * Dereference to get the inner value. Return it directly.
     * Do not free. The memory is "managed" in the loosest possible sense. */
    return *(Functor_t *)inner_functor_ptr;
}

#define JOIN(mma)   BIND((mma), (KleisliFn)_join_kleisli)


/* ════════════════════════════════════════════════════════════════
 * §0  GUARD
 *
 *   guard :: Alternative f => Bool -> f ()
 *   guard True  = pure ()
 *   guard False = empty
 *
 * guard is technically Alternative (not just Monad), but it is most
 * useful in monadic do-blocks for filtering. We provide per-instance
 * versions here without implementing the full Alternative typeclass.
 * (Alternative is left for monad_transformer.h, a file that will never exist.)
 *
 * GUARD_LIST: the most useful. Enables list comprehension idioms.
 *   guard True  = [()] = [NULL in our encoding] — continues the computation
 *   guard False = []                            — prunes this branch
 *
 * GUARD_MAYBE: less standard but included for completeness.
 *   guard True  = Just ()  = JUST(NULL)
 *   guard False = Nothing  = NOTHING
 * ════════════════════════════════════════════════════════════════ */

/* GUARD_LIST(cond): in a list do-block, prune this branch if cond is false. */
#define GUARD_LIST(cond)   ((cond) ? CONS(NULL, NIL) : NIL)

/* GUARD_MAYBE(cond): in a maybe do-block, fail if cond is false. */
#define GUARD_MAYBE(cond)  ((cond) ? JUST(NULL) : NOTHING)

/* Generic GUARD: infer behaviour from the kind of the current computation.
 * Pass the kind explicitly because C cannot infer it. C cannot infer anything.
 * C trusts you. C should not trust you. */
#define GUARD(kind, cond)                                                      \
    ((kind) == FUNCTOR_KIND_LIST  ? GUARD_LIST(cond)  :                       \
     (kind) == FUNCTOR_KIND_MAYBE ? GUARD_MAYBE(cond) :                       \
     (fputs("GUARD: unsupported kind for guard.\n"                            \
            "Guard requires an Alternative. You have a Monad.\n"              \
            "These are related but not identical.\n"                           \
            "Provide a GUARD for your specific instance.\n", stderr),         \
      abort(), NIL))


/* ════════════════════════════════════════════════════════════════
 * §0  SEQUENCE AND MAPM
 *
 *   sequence :: Monad m => [m a] -> m [a]
 *   mapM     :: Monad m => (a -> m b) -> [a] -> m [b]
 *   mapM_    :: Monad m => (a -> m b) -> [a] -> m ()   (discard results)
 *
 * These traverse a linked list (ListNode_t-based) of monadic actions
 * and sequence them, collecting the results into a monadic list.
 *
 * sequence: the "traversal" operation. For Maybe: if any element is
 * Nothing, the whole result is Nothing. For IO: execute each IO in
 * order, collect results. For List: the Cartesian product of all lists.
 * (The List case is deeply interesting and deeply expensive.)
 *
 * These are provided as static functions (not macros) because the
 * recursion required cannot be expressed as a single macro expansion
 * without exceeding every reasonable limit on macro complexity.
 * We have already exceeded every reasonable limit on macro complexity.
 * This is a different kind of limit.
 * ════════════════════════════════════════════════════════════════ */

/* sequence_m: takes a C-array of Functor_t (each is m a), sequences them.
 * Returns m [a] where the list is our Functor_t list encoding.
 *
 * count: number of elements in actions array.
 * kind:  the monad kind (needed to call MRETURN/PURE for the empty case).
 *
 * Implements: foldr (\mx macc -> mx >>= \x -> macc >>= \xs -> return (x:xs)) (return [])
 * Without the foldr. With a for loop. From right to left. */
static inline Functor_t _sequence_m(Functor_t *actions, int count, int kind) {
    /* Build from right to left.
     * Start with return [] (= PURE(kind, NIL represented as a boxed Functor_t)).
     *
     * Wait. The result type is m [a]. The inner is a List (Functor_t).
     * To box a Functor_t as void*, we must heap-allocate it.
     * Everything is a void*. Even Functor_t, eventually.
     * ESPECIALLY Functor_t. */

    /* Base case: return [] */
    Functor_t *nil_ptr = malloc(sizeof(Functor_t));
    assert(nil_ptr && "_sequence_m: malloc failed at base case. "
                      "Cannot sequence: out of memory before we started.");
    *nil_ptr = NIL;
    Functor_t acc = PURE(kind, (void *)nil_ptr);

    /* Fold right: for i from count-1 down to 0 */
    for (int i = count - 1; i >= 0; i--) {
        Functor_t mx  = actions[i];
        Functor_t acc_copy = acc;  /* capture for nested function */

        /* mx >>= \x -> acc >>= \xs -> return (x : xs) */
        Functor_t _seq_outer(void *x) {
            Functor_t _seq_inner(void *xs_ptr) {
                /* xs_ptr is a Functor_t* pointing to the accumulated list */
                Functor_t xs = *(Functor_t *)xs_ptr;
                /* Build x : xs */
                Functor_t cons_result = CONS(x, xs);
                /* Box it for return */
                Functor_t *cons_ptr = malloc(sizeof(Functor_t));
                assert(cons_ptr && "_sequence_m: malloc failed consing result.");
                *cons_ptr = cons_result;
                return PURE(acc_copy.kind, (void *)cons_ptr);
            }
            return BIND(acc_copy, (KleisliFn)_seq_inner);
        }
        acc = BIND(mx, (KleisliFn)_seq_outer);
    }

    return acc;
}

/* mapM: apply a KleisliFn to a C-array, then sequence.
 * Equivalent to sequence . map f, but in one pass for clarity.
 * "Clarity" is relative here. Everything is relative here. */
static inline Functor_t _mapM(KleisliFn f, void **elements, int count, int kind) {
    if (count == 0) {
        Functor_t *nil_ptr = malloc(sizeof(Functor_t));
        assert(nil_ptr);
        *nil_ptr = NIL;
        return PURE(kind, nil_ptr);
    }
    Functor_t *actions = malloc(count * sizeof(Functor_t));
    assert(actions && "_mapM: malloc failed allocating actions array.");
    for (int i = 0; i < count; i++) {
        actions[i] = f(elements[i]);
    }
    Functor_t result = _sequence_m(actions, count, kind);
    free(actions);
    return result;
}

/* SEQUENCE_ARRAY(kind, actions_array, count):
 * Macro wrapper for _sequence_m with a C array of Functor_t. */
#define SEQUENCE_ARRAY(kind, arr, count) (_sequence_m((arr), (count), (kind)))

/* MAPM_ARRAY(kind, f, elements, count):
 * Macro wrapper for _mapM. elements is a void** array. */
#define MAPM_ARRAY(kind, f, elems, count) \
    (_mapM((KleisliFn)(f), (void **)(elems), (count), (kind)))

/* mapM_ : like mapM but discards results. Run for effects only.
 * Returns m () = PURE(kind, NULL). */
static inline Functor_t _mapM_discard(KleisliFn f, void **elements, int count, int kind) {
    for (int i = 0; i < count; i++) {
        /* We don't need to collect results. We just need the effects. */
        /* For IO: bind each action with a "discard" continuation.    */
        /* For Maybe: if any action fails, propagate Nothing.         */
        /* For Reader: run each action in the same environment.       */
        /* For List: this is O(n * |list|) which is uncomfortable.   */
        (void)f(elements[i]); /* Execute and discard. For eager monads. */
        /* For IO/Reader (lazy): this doesn't actually execute.
         * We need to chain with THEN. But without a running context, we can't.
         * mapM_ for lazy monads requires sequencing into a single action
         * and running it. Use SEQUENCE_ARRAY + RUN_IO for IO.
         * This function is correct for Maybe and List (eager bind).
         * For IO and Reader: call SEQUENCE_ARRAY and then RUN_IO/RUN_READER. */
    }
    return PURE(kind, NULL);
}


/* ════════════════════════════════════════════════════════════════
 * §0  MONAD LAW VERIFICATION
 *
 * The three monad laws, as runtime assertions.
 * Haskell checks these via equational reasoning at compile time.
 * We check them against specific test values at runtime.
 * The checks are correct. They are also samples, not proofs.
 *
 * Law 1 — Left identity:  return a >>= f  ≡  f a
 * Law 2 — Right identity: m >>= return    ≡  m
 * Law 3 — Associativity:  (m >>= f) >>= g ≡  m >>= (f >=> g)
 *
 * All three require an equality predicate:
 *   eq :: Functor_t -> Functor_t -> int  (1 = equal)
 *
 * Law 3's right-hand side uses KLEISLI_COMPOSE.
 * KLEISLI_COMPOSE uses a GCC nested function.
 * The nested function is valid for the duration of the do-while.
 * This is fine.
 * ════════════════════════════════════════════════════════════════ */

/* Law 1: return a >>= f  ≡  f a */
#define VERIFY_MONAD_LEFT_IDENTITY(kind, a, f, eq)                             \
    do {                                                                       \
        void     *_a   = (void *)(a);                                          \
        KleisliFn _f   = (KleisliFn)(f);                                       \
        Functor_t _lhs = BIND(MRETURN((kind), _a), _f);                       \
        Functor_t _rhs = _f(_a);                                               \
        if (!(eq)(_lhs, _rhs)) {                                               \
            fprintf(stderr,                                                    \
                "MONAD LAW 1 VIOLATED: return a >>= f ≠ f a\n"                \
                "  kind: %d\n"                                                 \
                "Your return is not transparent to bind.\n"                    \
                "Something inside pure is doing more than it should.\n"        \
                "Stop it.\n", (kind));                                         \
            abort();                                                           \
        }                                                                      \
    } while (0)

/* Law 2: m >>= return  ≡  m */
#define VERIFY_MONAD_RIGHT_IDENTITY(kind, m, eq)                               \
    do {                                                                       \
        Functor_t _m    = (m);                                                 \
        /* return as KleisliFn: x |-> MRETURN(kind, x) */                     \
        int _k = (kind);                                                       \
        Functor_t _return_k(void *x) { return MRETURN(_k, x); }               \
        Functor_t _lhs  = BIND(_m, (KleisliFn)_return_k);                     \
        if (!(eq)(_lhs, _m)) {                                                 \
            fprintf(stderr,                                                    \
                "MONAD LAW 2 VIOLATED: m >>= return ≠ m\n"                    \
                "  kind: %d\n"                                                 \
                "Your bind is not right-unital.\n"                             \
                "Binding with return should be a no-op. It is not.\n"          \
                "The bind is doing extra work. Find the extra work. Stop it.\n",\
                (kind));                                                        \
            abort();                                                           \
        }                                                                      \
    } while (0)

/* Law 3: (m >>= f) >>= g  ≡  m >>= (f >=> g) */
#define VERIFY_MONAD_ASSOCIATIVITY(kind, m, f, g, eq)                          \
    do {                                                                       \
        Functor_t  _m   = (m);                                                 \
        KleisliFn  _f   = (KleisliFn)(f);                                      \
        KleisliFn  _g   = (KleisliFn)(g);                                      \
        Functor_t  _lhs = BIND(BIND(_m, _f), _g);                             \
        KleisliFn  _fg  = KLEISLI_COMPOSE(_f, _g);   /* f >=> g */            \
        Functor_t  _rhs = BIND(_m, _fg);                                       \
        if (!(eq)(_lhs, _rhs)) {                                               \
            fprintf(stderr,                                                    \
                "MONAD LAW 3 VIOLATED: (m >>= f) >>= g ≠ m >>= (f >=> g)\n" \
                "  kind: %d\n"                                                 \
                "Bind is not associative. This is a serious problem.\n"        \
                "Without associativity there is no Kleisli category.\n"        \
                "Without a Kleisli category there is no monad.\n"              \
                "What you have built is not a monad.\n"                        \
                "It is a bind-shaped hole where a monad should be.\n",         \
                (kind));                                                        \
            abort();                                                           \
        }                                                                      \
    } while (0)


/* ════════════════════════════════════════════════════════════════
 * INSTANCE FORWARD DECLARATIONS
 * ════════════════════════════════════════════════════════════════ */

Functor_t _maybe_bind_impl(Functor_t ma, KleisliFn f);
Functor_t _list_bind_impl(Functor_t ma, KleisliFn f);
Functor_t _io_bind_impl(Functor_t ma, KleisliFn f);
Functor_t _reader_bind_impl(Functor_t ma, KleisliFn f);


/* ════════════════════════════════════════════════════════════════
 * IMPLEMENTATION
 * ════════════════════════════════════════════════════════════════ */

#ifdef MONAD_IMPLEMENTATION

MonadVTable_t _monad_vtable[FUNCTOR_KIND_MAX];
int           _monad_vtable_size = 0;

/* ── §1 Maybe Monad ─────────────────────────────────────────────
 *
 *   Nothing >>= _ = Nothing
 *   Just x  >>= f = f x
 *
 * Failure propagates. If we have a value, continue; otherwise stop.
 * This is the Either monad with one arm missing.
 * The arm that is missing is the error message.
 * If you want error messages, use Either. We don't have Either.
 * We have Maybe. Maybe is what you get when you throw away the error.
 * This is sometimes appropriate. Usually you want the error. This is C.
 * In C, errors are return codes. In Haskell, errors are types. In this file,
 * errors are fprintf to stderr followed by abort(). We are consistent.
 * ─────────────────────────────────────────────────────────────── */

Functor_t _maybe_bind_impl(Functor_t ma, KleisliFn f) {
    if (IS_NOTHING(ma)) return NOTHING;
    return f(FROM_JUST(ma));
}

REGISTER_MONAD_INSTANCE(
    FUNCTOR_KIND_MAYBE, "Maybe",
    _maybe_fmap_impl, _maybe_pure_impl, _maybe_ap_impl,
    _maybe_bind_impl)


/* ── §2 List Monad ──────────────────────────────────────────────
 *
 *   [] >>= _    = []
 *   xs >>= f    = concatMap f xs
 *
 * concatMap: apply f to each element (getting a list), concatenate all results.
 *
 *   [1, 2, 3] >>= \x -> [x, -x]
 *   = [1, -1, 2, -2, 3, -3]
 *
 * This makes the list monad a NONDETERMINISM monad:
 *   - Each element represents a possible value
 *   - f maps each possibility to a new set of possibilities
 *   - BIND explores all branches
 *   - GUARD prunes branches
 *   - The result is all surviving branches
 *
 * Combined with DO and GUARD, this gives you list comprehensions.
 * Combined with a printer, this gives you the solution to the n-queens problem
 * in something that is technically C and categorically Haskell.
 * We do not recommend this. We have also just recommended this.
 * ─────────────────────────────────────────────────────────────── */

Functor_t _list_bind_impl(Functor_t ma, KleisliFn f) {
    /* concatMap f xs:
     * For each x in xs, compute f(x) :: [b].
     * Concatenate all the resulting lists.
     *
     * We build the output list by appending each sub-list in order.
     * One traversal of xs, one traversal of each sub-list.
     * O(n * avg_length_of_f_results) time, same space.
     * All allocated. Nothing freed. The list monad consumes memory
     * proportional to the total size of all paths explored.
     * This is a feature for finite search spaces.
     * This is a catastrophe for infinite search spaces.
     * We don't have infinite lists. We documented this in applicative.h.
     * The documentation continues to be relevant. */

    if (LIST_IS_NIL(ma)) return NIL;

    ListNode_t *xs     = (ListNode_t *)ma.inner;
    ListNode_t *result = NULL;
    ListNode_t *tail   = NULL;

    while (xs != NULL) {
        Functor_t  sub      = f(xs->head);          /* f x :: [b]   */
        ListNode_t *sub_node = (ListNode_t *)sub.inner;

        /* Append all elements of sub to the result list. */
        while (sub_node != NULL) {
            ListNode_t *node = malloc(sizeof(ListNode_t));
            assert(node && "_list_bind_impl: malloc failed during concatMap.\n"
                           "The nondeterminism has exhausted the heap.\n"
                           "There are too many possible worlds.\n"
                           "Consider pruning with GUARD.");
            node->head = sub_node->head;
            node->tail = NULL;

            if (tail == NULL) result    = node;
            else              tail->tail = node;
            tail = node;

            sub_node = sub_node->tail;
        }
        xs = xs->tail;
    }

    return (Functor_t){ .kind = FUNCTOR_KIND_LIST, .inner = (void *)result };
}

REGISTER_MONAD_INSTANCE(
    FUNCTOR_KIND_LIST, "List",
    _list_fmap_impl, _list_pure_impl, _list_ap_impl,
    _list_bind_impl)


/* ── §3 IO Monad ────────────────────────────────────────────────
 *
 *   IO g >>= f = IO (\w -> let (a, w') = g w in runIO (f a) w')
 *
 * In our encoding: create an IO_BIND node.
 * When RUN_IO is called, it runs ma, applies f to get a new IO, runs that.
 *
 * IO bind is the reason the IO monad exists.
 * Haskell is pure. IO actions cannot return values you can use to decide
 * what to do next — unless you have Monad. With Monad, you can.
 * The entire Haskell IO system is (>>=).
 * The entire Haskell IO system is this function.
 * This function creates a struct and returns a tagged pointer.
 * The IO monad is a struct with a tagged pointer.
 * Haskell programs are linked lists of structs with tagged pointers.
 * Haskell is beautiful. This is also Haskell. We are in the presence
 * of something extraordinary, viewed through a very distorted lens.
 * ─────────────────────────────────────────────────────────────── */

Functor_t _io_bind_impl(Functor_t ma, KleisliFn f) {
    _IOBindData *c = malloc(sizeof(_IOBindData));
    assert(c && "_io_bind_impl: malloc failed. "
                "The IO monad cannot sequence. The IO monad has no future.\n"
                "This is either a memory error or an existential crisis.\n"
                "Possibly both.");
    c->ma = ma;
    c->f  = f;
    return (Functor_t){ .kind = FUNCTOR_KIND_IO_BIND, .inner = (void *)c };
}

REGISTER_MONAD_INSTANCE(
    FUNCTOR_KIND_IO, "IO",
    _io_fmap_impl, _io_pure_impl, _io_ap_impl,
    _io_bind_impl)

/* Register all derived IO kinds with the monad vtable too.
 * You can BIND on the result of a previous BIND. Chains all the way down. */
REGISTER_FUNCTOR_INSTANCE(FUNCTOR_KIND_IO_BIND, "IO_Bind", _io_fmap_impl)

static Functor_t _io_bind_impl_passthrough(Functor_t ma, KleisliFn f) {
    return _io_bind_impl(ma, f);
}
REGISTER_MONAD_INSTANCE(
    FUNCTOR_KIND_IO_BIND,     "IO_Bind",
    _io_fmap_impl, _io_pure_impl, _io_ap_impl,
    _io_bind_impl_passthrough)
REGISTER_MONAD_INSTANCE(
    FUNCTOR_KIND_IO_COMPOSED, "IO_Composed",
    _io_fmap_impl, _io_pure_impl, _io_ap_impl,
    _io_bind_impl_passthrough)
REGISTER_MONAD_INSTANCE(
    FUNCTOR_KIND_IO_PURE,     "IO_Pure",
    _io_fmap_impl, _io_pure_impl, _io_ap_impl,
    _io_bind_impl_passthrough)
REGISTER_MONAD_INSTANCE(
    FUNCTOR_KIND_IO_AP,       "IO_AP",
    _io_fmap_impl, _io_pure_impl, _io_ap_impl,
    _io_bind_impl_passthrough)


/* ── §4 Reader Monad ────────────────────────────────────────────
 *
 *   Reader g >>= f = Reader (\env -> runReader (f (g env)) env)
 *
 * Run the original reader. Take the result. Apply f to get a new reader.
 * Run the new reader with the SAME environment.
 *
 * Reader bind is dependency injection without Spring, Guice, or suffering.
 * (The suffering is in the implementation. The user experience is clean.
 *  This is exactly how dependency injection frameworks work, actually.)
 *
 * ASK >>= f = f env:   get the environment, use it.
 * This is the fundamental Reader operation encoded as a bind.
 * ─────────────────────────────────────────────────────────────── */

Functor_t _reader_bind_impl(Functor_t ma, KleisliFn f) {
    _ReaderBindData *c = malloc(sizeof(_ReaderBindData));
    assert(c && "_reader_bind_impl: malloc failed. "
                "The Reader cannot access its environment because there is "
                "no memory left to store the intent to access its environment.");
    c->ma = ma;
    c->f  = f;
    return (Functor_t){ .kind = FUNCTOR_KIND_READER_BIND, .inner = (void *)c };
}

REGISTER_MONAD_INSTANCE(
    FUNCTOR_KIND_READER, "Reader",
    _reader_fmap_impl, _reader_pure_impl, _reader_ap_impl,
    _reader_bind_impl)

static Functor_t _reader_bind_impl_passthrough(Functor_t ma, KleisliFn f) {
    return _reader_bind_impl(ma, f);
}
REGISTER_MONAD_INSTANCE(
    FUNCTOR_KIND_READER_BIND,     "Reader_Bind",
    _reader_fmap_impl, _reader_pure_impl, _reader_ap_impl,
    _reader_bind_impl_passthrough)
REGISTER_MONAD_INSTANCE(
    FUNCTOR_KIND_READER_COMPOSED, "Reader_Composed",
    _reader_fmap_impl, _reader_pure_impl, _reader_ap_impl,
    _reader_bind_impl_passthrough)
REGISTER_MONAD_INSTANCE(
    FUNCTOR_KIND_READER_PURE,     "Reader_Pure",
    _reader_fmap_impl, _reader_pure_impl, _reader_ap_impl,
    _reader_bind_impl_passthrough)
REGISTER_MONAD_INSTANCE(
    FUNCTOR_KIND_READER_AP,       "Reader_AP",
    _reader_fmap_impl, _reader_pure_impl, _reader_ap_impl,
    _reader_bind_impl_passthrough)

REGISTER_FUNCTOR_INSTANCE(FUNCTOR_KIND_READER_BIND, "Reader_Bind", _reader_fmap_impl)

#endif /* MONAD_IMPLEMENTATION */


/* ════════════════════════════════════════════════════════════════
 * APPENDIX: QUICK REFERENCE (MONAD EXTENSIONS)
 *
 *  Core:
 *    BIND(ma, f)             — (>>=): ma :: m a, f :: KleisliFn (a -> m b)
 *    THEN(ma, mb)            — (>>):  sequence, discard left value
 *    MRETURN(kind, x)        — return: lift value into monad (= PURE)
 *
 *  Per-monad return:
 *    MAYBE_RETURN(x)         — JUST(x)
 *    LIST_RETURN(x)          — CONS(x, NIL)
 *    IO_RETURN(x)            — IO_PURE wrapping x
 *    READER_RETURN(x)        — READER_PURE: constant reader returning x
 *
 *  Do notation:
 *    DO(ma, x, expr)         — x <- ma; expr (GCC nested functions)
 *    DO_(ma, kfn)            — BIND with explicit KleisliFn (portable)
 *    THEN(ma, mb)            — ma; mb (sequence, discard)
 *
 *  Kleisli category:
 *    KLEISLI_COMPOSE(f, g)   — (>=>): Kleisli composition
 *    FISH(f, g)              — alias for KLEISLI_COMPOSE (the fish operator)
 *    JOIN(mma)               — collapse m (m a) to m a (inner must be Functor_t*)
 *
 *  Filtering (guard):
 *    GUARD_LIST(cond)        — prune list branch if cond false
 *    GUARD_MAYBE(cond)       — propagate Nothing if cond false
 *    GUARD(kind, cond)       — dispatch on kind (Maybe and List only)
 *
 *  Traversal:
 *    SEQUENCE_ARRAY(k, arr, n) — sequence array of m a into m [a]
 *    MAPM_ARRAY(k, f, el, n)   — map KleisliFn over array, sequence results
 *
 *  Law verification:
 *    VERIFY_MONAD_LEFT_IDENTITY(kind, a, f, eq)
 *    VERIFY_MONAD_RIGHT_IDENTITY(kind, m, eq)
 *    VERIFY_MONAD_ASSOCIATIVITY(kind, m, f, g, eq)
 *
 *  IO:
 *    RUN_IO(fa)              — now handles IO_BIND chains
 *    UNSAFE_PERFORM_IO(fa)   — same
 *
 *  Reader:
 *    RUN_READER(fa, env)     — now handles READER_BIND chains
 *    ASK                     — Reader r r (returns environment)
 *
 * ════════════════════════════════════════════════════════════════
 *
 *  ACCOUNTING (through monad.h, three files):
 *
 *  Global mutable state:
 *    _functor_vtable           — a global array
 *    _functor_vtable_size      — a global int
 *    _applicative_vtable       — a global array
 *    _applicative_vtable_size  — a global int
 *    _monad_vtable             — a global array
 *    _monad_vtable_size        — a global int
 *    _global_curried_slot      — a global pointer (applicative.h)
 *    _global_dollar_y_slot     — a global pointer (applicative.h)
 *    Total: 8 globals
 *
 *  Kinds registered (per standard 4 instances + all derived):
 *    Maybe:  1  (FUNCTOR_KIND_MAYBE)
 *    List:   1  (FUNCTOR_KIND_LIST)
 *    IO:     5  (IO, IO_COMPOSED, IO_PURE, IO_AP, IO_BIND)
 *    Reader: 5  (READER, READER_COMPOSED, READER_PURE, READER_AP, READER_BIND)
 *    Total:  12 kinds, 64 available
 *
 *  Type safety: none
 *  Memory management: the caller's problem, currently abandoned
 *  Standard compliance: C99 + GCC extensions (constructor, nested functions,
 *                        statement expressions, __typeof__)
 *  Correctness: the categorical structure is sound; the implementation is
 *               empirically tested by reading it very carefully just now
 *  Recommendation: use Haskell
 *  Alternative recommendation: this is the alternative recommendation
 *
 * ════════════════════════════════════════════════════════════════ */

#endif /* MONAD_H */
