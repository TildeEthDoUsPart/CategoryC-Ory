/*
 * profunctor.h — The Profunctor typeclass, in C macros
 *
 * ════════════════════════════════════════════════════════════════
 *
 * A Profunctor is a functor that is:
 *   - CONTRAVARIANT in its first type argument
 *   - COVARIANT    in its second type argument
 *
 *   class Profunctor p where
 *     dimap :: (a -> b) -> (c -> d) -> p b c -> p a d
 *     lmap  :: (a -> b)             -> p b c -> p a c
 *     rmap  ::             (b -> c) -> p a b -> p a c
 *
 * The type signature of dimap is the thing the user said would be insane.
 * Let us dwell on it.
 *
 *   dimap :: (a -> b) -> (c -> d) -> p b c -> p a d
 *             ^^^^^^^^    ^^^^^^^^    ^^^^^^    ^^^^^^
 *             contra      covariant   input     output
 *             maps a→b    maps c→d    holds b→c returns a→d
 *
 * The first function goes a → b. The profunctor goes b → c.
 * The result goes a → d. The first function is applied BACKWARDS —
 * it pre-processes the input before the profunctor sees it.
 * This is contravariance. The arrow points the other way.
 * In our void* encoding, both functions have type FmapFn.
 * Both look like void* -> void*.
 * They are not the same. The compiler agrees that they are the same.
 * The compiler is wrong in the way that only type erasure enables.
 *
 * ════════════════════════════════════════════════════════════════
 *
 * THE TYPE SIGNATURES, ANNOTATED FOR THE TRAUMATISED:
 *
 *   -- Arrow (function) profunctor:
 *   dimap f g h = g . h . f
 *   -- f pre-processes input (contravariant: a→b applied BEFORE h)
 *   -- h is the profunctor value (b→c)
 *   -- g post-processes output (covariant: c→d applied AFTER h)
 *   -- result: a → d = g ∘ h ∘ f
 *   -- C: DIMAP(f, g, ARROW(h)) where all three are FmapFn
 *   -- C type signature: void*, void*, Functor_t → Functor_t
 *   -- Semantic signature: (a→b), (c→d), (b→c) → (a→d)
 *   -- These are the same C type. They are not the same thing.
 *
 *   -- Star profunctor: Star f a b = (a -> f b)
 *   dimap f g (Star k) = Star (\a -> fmap g (k (f a)))
 *   -- f :: a' → a  (contravariant: pre-processes input)
 *   -- g :: b → b'  (covariant: fmapped over the f b output)
 *   -- k :: a → f b (the original Kleisli arrow)
 *   -- result: a' → f b'
 *   -- NOTE: Star f a b IS KleisliFn. This is not a coincidence.
 *   --       The Star profunctor IS the Kleisli category's hom-sets.
 *   --       We already implemented KleisliFn in monad.h.
 *   --       We have been doing profunctor theory since monad.h.
 *   --       We did not know this. We know it now.
 *
 *   -- Costar profunctor: Costar f a b = (f a -> b)
 *   dimap f g (Costar k) = Costar (\fa -> g (k (fmap f fa)))
 *   -- f is fmapped INTO the input functor (contravariant via fmap!)
 *   -- g post-processes the output
 *   -- This is the dual of Star. The duality cost us nothing.
 *
 *   -- Forget profunctor: Forget r a b = (a -> r)
 *   dimap f _ (Forget k) = Forget (k . f)
 *   -- The covariant argument is genuinely, completely ignored.
 *   -- It does not participate. It is not consulted.
 *   -- It is the b in Forget r a b, and b does not matter.
 *   -- This models "focusing on a part of a structure without rebuilding it."
 *   -- This is what a Getter is in lens library terms.
 *
 *   -- Tagged profunctor: Tagged a b = b  (ignores a)
 *   dimap _ g (Tagged b) = Tagged (g b)
 *   -- The contravariant argument is ignored.
 *   -- a does not matter. Only b matters.
 *   -- This is the dual of Forget.
 *   -- Together: Forget has no output type. Tagged has no input type.
 *   -- Between them: the full Arrow profunctor, which has both.
 *
 * ════════════════════════════════════════════════════════════════
 *
 * THE OPTICS CONNECTION (the reason anyone cares about profunctors):
 *
 *   type Optic p s t a b = p a b -> p s t
 *
 *   Different constraints on p give different optics:
 *     Profunctor p              => Iso       (isomorphism)
 *     Strong p                  => Lens      (product focus)
 *     Choice p                  => Prism     (sum focus)
 *     Strong p, Choice p        => AffineTraversal
 *     Traversing p              => Traversal (not implemented here)
 *
 *   A Lens is literally a polymorphic function on profunctors.
 *   We will build lenses. With macros. Using void*.
 *   The lenses will work. The type checker will not help.
 *   You are the type checker. You have been the type checker since functor.h.
 *
 * ════════════════════════════════════════════════════════════════
 *
 * USAGE:
 *   #define FUNCTOR_IMPLEMENTATION
 *   #define APPLICATIVE_IMPLEMENTATION
 *   #define MONAD_IMPLEMENTATION
 *   #define NAT_TRANS_IMPLEMENTATION
 *   #define PROFUNCTOR_IMPLEMENTATION
 *   #include "profunctor.h"
 */

#ifndef PROFUNCTOR_H
#define PROFUNCTOR_H

#include "nat_trans.h"


/* ════════════════════════════════════════════════════════════════
 * §0  PAIRS AND EITHERS
 *
 * Strong profunctors require products (pairs).
 * Choice profunctors require coproducts (sums = Either).
 *
 * We encode both as heap-allocated structs, because everything
 * in this codebase is a heap-allocated struct, and we are
 * consistent in our choices even when the choices are wrong.
 * ════════════════════════════════════════════════════════════════ */

typedef struct { void *fst; void *snd; } Pair_t;
typedef struct { int is_left; void *value; } Either_t;

static inline Pair_t *_pair_make(void *a, void *b) {
    Pair_t *p = malloc(sizeof(Pair_t));
    assert(p && "PAIR: malloc failed. Products have run out of memory.");
    p->fst = a; p->snd = b;
    return p;
}

static inline Either_t *_either_left(void *x) {
    Either_t *e = malloc(sizeof(Either_t));
    assert(e); e->is_left = 1; e->value = x; return e;
}

static inline Either_t *_either_right(void *x) {
    Either_t *e = malloc(sizeof(Either_t));
    assert(e); e->is_left = 0; e->value = x; return e;
}

#define PAIR(a, b)     ((void *)_pair_make((void *)(a), (void *)(b)))
#define FST(p)         (((Pair_t *)(p))->fst)
#define SND(p)         (((Pair_t *)(p))->snd)

#define LEFT(x)        ((void *)_either_left((void *)(x)))
#define RIGHT(x)       ((void *)_either_right((void *)(x)))
#define IS_LEFT(e)     (((Either_t *)(e))->is_left)
#define IS_RIGHT(e)    (!IS_LEFT(e))
#define FROM_LEFT(e)   (((Either_t *)(e))->value)
#define FROM_RIGHT(e)  (((Either_t *)(e))->value)

/* EITHER_ELIM(e, f_left, f_right): the Either eliminator.
 * Applies f_left if Left, f_right if Right. Both FmapFn. */
#define EITHER_ELIM(e, f_left, f_right) \
    (IS_LEFT(e) ? ((FmapFn)(f_left))(FROM_LEFT(e)) \
                : ((FmapFn)(f_right))(FROM_RIGHT(e)))


/* ════════════════════════════════════════════════════════════════
 * §0  PROFUNCTOR KIND TAGS
 *
 * Profunctor kind tags start at 20, after the functor kinds (0-13)
 * defined across functor.h through nat_trans.h.
 *
 * We are at kind 34 out of 64. Pace yourself.
 * ════════════════════════════════════════════════════════════════ */

#define PROFUNCTOR_KIND_ARROW         20  /* p b c = (b -> c), raw FmapFn     */
#define PROFUNCTOR_KIND_ARROW_DIMAP   21  /* result of dimap on Arrow          */
#define PROFUNCTOR_KIND_ARROW_FIRST   22  /* result of first' on Arrow         */
#define PROFUNCTOR_KIND_ARROW_SECOND  23  /* result of second' on Arrow        */
#define PROFUNCTOR_KIND_ARROW_LEFT    24  /* result of left' on Arrow          */
#define PROFUNCTOR_KIND_ARROW_RIGHT   25  /* result of right' on Arrow         */

#define PROFUNCTOR_KIND_STAR          26  /* p a b = (a -> f b), StarFn        */
#define PROFUNCTOR_KIND_STAR_DIMAP    27  /* result of dimap on Star           */

#define PROFUNCTOR_KIND_COSTAR        28  /* p a b = (f a -> b), CostarFn      */
#define PROFUNCTOR_KIND_COSTAR_DIMAP  29  /* result of dimap on Costar         */

#define PROFUNCTOR_KIND_FORGET        30  /* p a b = (a -> r), inner = FmapFn  */
#define PROFUNCTOR_KIND_FORGET_DIMAP  31  /* result of dimap on Forget         */

#define PROFUNCTOR_KIND_TAGGED        32  /* p a b = b, inner = void* (the b)  */
#define PROFUNCTOR_KIND_TAGGED_DIMAP  33  /* result of dimap on Tagged         */

/* Total kinds used so far: 34. Kinds available: 64. */
/* FUNCTOR_KIND_MAX is 64. If you add more profunctors, increase it. */
/* You will add more profunctors. You will increase it. */


/* ════════════════════════════════════════════════════════════════
 * §0  PROFUNCTOR FUNCTION TYPES
 *
 * FmapFn    (from functor.h):  void* → void*
 * KleisliFn (from monad.h):    void* → Functor_t
 *
 * StarFn: the function inside Star f a b = (a -> f b).
 *   StarFn = KleisliFn = Functor_t (*)(void*)
 *   This identity is not coincidental.
 *   The Star profunctor IS the Kleisli category.
 *   We already have KleisliFn. We alias it.
 *   We point this out at every opportunity.
 *
 * CostarFn: the function inside Costar f a b = (f a -> b).
 *   CostarFn: Functor_t → void*
 *   The dual of StarFn. Inputs and outputs swapped.
 * ════════════════════════════════════════════════════════════════ */

/* StarFn IS KleisliFn. Same bits. Different categorical hat. */
typedef KleisliFn StarFn;

/* CostarFn: takes a Functor_t (the f a), returns void* (the b). */
typedef void *(*CostarFn)(Functor_t);


/* ════════════════════════════════════════════════════════════════
 * §0  INTERNAL CLOSURE STRUCTS
 *
 * Every profunctor operation that creates a new profunctor from an
 * existing one stores a deferred-computation node. RUN_* unwinds.
 * This is the same pattern as IO_COMPOSED, READER_BIND, etc.
 * We have one pattern and we apply it everywhere.
 * The pattern is: when in doubt, malloc a struct and recurse.
 * ════════════════════════════════════════════════════════════════ */

/* Used by all _DIMAP kinds: stores f (contra), g (co), and the original. */
typedef struct {
    FmapFn    f;      /* contravariant map: applied to INPUT before orig */
    FmapFn    g;      /* covariant map:     applied to OUTPUT after orig  */
    Functor_t orig;   /* the original profunctor value                    */
} _ProfDimap;

/* Used by first', second', left', right': stores only the original. */
typedef struct {
    Functor_t orig;
} _ProfUnary;

/* StarData: the payload of a raw STAR profunctor value. */
typedef struct {
    StarFn run;    /* the actual (a -> f b) function */
} StarData;

/* CostarData: the payload of a raw COSTAR profunctor value. */
typedef struct {
    CostarFn run;  /* the actual (f a -> b) function */
} CostarData;


/* ════════════════════════════════════════════════════════════════
 * §0  THE PROFUNCTOR VTABLE
 *
 * Fourth vtable. We have four vtables.
 * The vtables have been accumulating since functor.h.
 * They will stop here. This is the last typeclass we implement.
 * (In this file. We make no promises about future files.)
 *
 * ProfunctorVTable_t: kind, name, dimap.
 * StrongVTable_t:     kind, name, first', second'.
 * ChoiceVTable_t:     kind, name, left', right'.
 *
 * Three vtables for three typeclasses in one file.
 * We are accelerating. This is fine.
 * ════════════════════════════════════════════════════════════════ */

typedef struct {
    int        kind;
    const char *name;
    Functor_t (*dimap)(FmapFn f_contra, FmapFn g_co, Functor_t pbc);
} ProfunctorVTable_t;

typedef struct {
    int        kind;
    const char *name;
    Functor_t (*first_prime)(Functor_t pab);
    Functor_t (*second_prime)(Functor_t pab);
} StrongVTable_t;

typedef struct {
    int        kind;
    const char *name;
    Functor_t (*left_prime)(Functor_t pab);
    Functor_t (*right_prime)(Functor_t pab);
} ChoiceVTable_t;

extern ProfunctorVTable_t _profunctor_vtable[FUNCTOR_KIND_MAX];
extern int                _profunctor_vtable_size;
extern StrongVTable_t     _strong_vtable[FUNCTOR_KIND_MAX];
extern int                _strong_vtable_size;
extern ChoiceVTable_t     _choice_vtable[FUNCTOR_KIND_MAX];
extern int                _choice_vtable_size;


/* ════════════════════════════════════════════════════════════════
 * §0  INSTANCE REGISTRATION
 * ════════════════════════════════════════════════════════════════ */

#define REGISTER_PROFUNCTOR_INSTANCE(KIND, NAME, DIMAP_FN)                     \
    static __attribute__((constructor))                                        \
    void _profunctor_register_##KIND(void) {                                   \
        if (_profunctor_vtable_size >= FUNCTOR_KIND_MAX) {                     \
            fputs("PROFUNCTOR: vtable overflow.\n", stderr); abort();          \
        }                                                                      \
        _profunctor_vtable[_profunctor_vtable_size++] = (ProfunctorVTable_t){  \
            .kind = (KIND), .name = (NAME), .dimap = (DIMAP_FN),               \
        };                                                                     \
    }

#define REGISTER_STRONG_INSTANCE(KIND, NAME, FIRST_FN, SECOND_FN)             \
    static __attribute__((constructor))                                        \
    void _strong_register_##KIND(void) {                                       \
        if (_strong_vtable_size >= FUNCTOR_KIND_MAX) {                         \
            fputs("STRONG: vtable overflow.\n", stderr); abort();              \
        }                                                                      \
        _strong_vtable[_strong_vtable_size++] = (StrongVTable_t){              \
            .kind = (KIND), .name = (NAME),                                    \
            .first_prime = (FIRST_FN), .second_prime = (SECOND_FN),            \
        };                                                                     \
    }

#define REGISTER_CHOICE_INSTANCE(KIND, NAME, LEFT_FN, RIGHT_FN)               \
    static __attribute__((constructor))                                        \
    void _choice_register_##KIND(void) {                                       \
        if (_choice_vtable_size >= FUNCTOR_KIND_MAX) {                         \
            fputs("CHOICE: vtable overflow.\n", stderr); abort();              \
        }                                                                      \
        _choice_vtable[_choice_vtable_size++] = (ChoiceVTable_t){              \
            .kind = (KIND), .name = (NAME),                                    \
            .left_prime = (LEFT_FN), .right_prime = (RIGHT_FN),                \
        };                                                                     \
    }


/* ════════════════════════════════════════════════════════════════
 * §0  DIMAP, LMAP, RMAP
 *
 * DIMAP(f_contra, g_co, pbc):
 *   f_contra :: a -> b  (contravariant: pre-processes input)
 *   g_co     :: c -> d  (covariant: post-processes output)
 *   pbc      :: p b c   (profunctor value)
 *   result   :: p a d
 *
 * Both f_contra and g_co are FmapFn (void* -> void*).
 * They are indistinguishable at the C type level.
 * They are completely different categorically.
 * You must keep track of which is which.
 * You will mix them up.
 * When you mix them up, the types will be wrong but the code will run.
 * It will run incorrectly in ways that manifest far from the mistake.
 * This is contravariance in practice.
 *
 * lmap f  = dimap f id  (map only the contravariant argument)
 * rmap g  = dimap id g  (map only the covariant argument = fmap)
 *
 * Note: rmap = FMAP for the covariant argument.
 * Every Profunctor is a Functor in its second argument.
 * The Profunctor vtable subsumes the Functor vtable for these.
 * ════════════════════════════════════════════════════════════════ */

static inline Functor_t _dispatch_dimap(FmapFn f, FmapFn g, Functor_t pbc) {
    for (int i = 0; i < _profunctor_vtable_size; i++) {
        if (_profunctor_vtable[i].kind == pbc.kind) {
            return _profunctor_vtable[i].dimap(f, g, pbc);
        }
    }
    fprintf(stderr,
        "PROFUNCTOR: no instance for kind=%d.\n"
        "DIMAP requires a Profunctor.\n"
        "You have a Functor_t of unknown profunctor kind.\n"
        "The contravariant and covariant arguments are waiting.\n"
        "They will wait forever.\n",
        pbc.kind);
    abort();
}

#define DIMAP(f_contra, g_co, pbc) \
    (_dispatch_dimap((FmapFn)(f_contra), (FmapFn)(g_co), (pbc)))

#define LMAP(f_contra, pbc)   DIMAP((f_contra), ID, (pbc))
#define RMAP(g_co, pab)       DIMAP(ID, (g_co), (pab))

/* CONTRAMAP: alias for LMAP. More explicit about what's happening. */
#define CONTRAMAP(f, pbc)     LMAP((f), (pbc))


/* ════════════════════════════════════════════════════════════════
 * §0  STRONG PROFUNCTOR: first' and second'
 *
 *   class Profunctor p => Strong p where
 *     first'  :: p a b -> p (a, c) (b, c)
 *     second' :: p a b -> p (c, a) (c, b)
 *
 * first' lifts a profunctor over the first component of a pair.
 * second' lifts over the second component.
 *
 * For Arrow:
 *   first'  h (a, c) = (h a, c)
 *   second' h (c, a) = (c, h a)
 *
 * These create ARROW_FIRST and ARROW_SECOND kind values.
 * RUN_ARROW handles them by unpacking pairs, running orig, repacking.
 *
 * Strong profunctors are the foundation of Lens.
 * ════════════════════════════════════════════════════════════════ */

static inline Functor_t _dispatch_first(Functor_t pab) {
    for (int i = 0; i < _strong_vtable_size; i++) {
        if (_strong_vtable[i].kind == pab.kind) {
            return _strong_vtable[i].first_prime(pab);
        }
    }
    fprintf(stderr,
        "STRONG: no Strong instance for kind=%d.\n"
        "first' requires a Strong profunctor.\n"
        "Not all profunctors are Strong. Forget and Tagged are not Strong.\n"
        "You may have a Forget. Check your profunctor.\n",
        pab.kind);
    abort();
}

static inline Functor_t _dispatch_second(Functor_t pab) {
    for (int i = 0; i < _strong_vtable_size; i++) {
        if (_strong_vtable[i].kind == pab.kind) {
            return _strong_vtable[i].second_prime(pab);
        }
    }
    fprintf(stderr, "STRONG: no Strong instance for kind=%d.\n", pab.kind);
    abort();
}

#define FIRST_PRIME(pab)   (_dispatch_first(pab))
#define SECOND_PRIME(pab)  (_dispatch_second(pab))


/* ════════════════════════════════════════════════════════════════
 * §0  CHOICE PROFUNCTOR: left' and right'
 *
 *   class Profunctor p => Choice p where
 *     left'  :: p a b -> p (Either a c) (Either b c)
 *     right' :: p a b -> p (Either c a) (Either c b)
 *
 * For Arrow:
 *   left'  h (Left  a) = Left  (h a)
 *   left'  h (Right c) = Right c
 *   right' h (Left  c) = Left  c
 *   right' h (Right a) = Right (h a)
 *
 * Choice profunctors are the foundation of Prism.
 * ════════════════════════════════════════════════════════════════ */

static inline Functor_t _dispatch_left(Functor_t pab) {
    for (int i = 0; i < _choice_vtable_size; i++) {
        if (_choice_vtable[i].kind == pab.kind) {
            return _choice_vtable[i].left_prime(pab);
        }
    }
    fprintf(stderr, "CHOICE: no Choice instance for kind=%d.\n", pab.kind);
    abort();
}

static inline Functor_t _dispatch_right(Functor_t pab) {
    for (int i = 0; i < _choice_vtable_size; i++) {
        if (_choice_vtable[i].kind == pab.kind) {
            return _choice_vtable[i].right_prime(pab);
        }
    }
    fprintf(stderr, "CHOICE: no Choice instance for kind=%d.\n", pab.kind);
    abort();
}

#define LEFT_PRIME(pab)   (_dispatch_left(pab))
#define RIGHT_PRIME(pab)  (_dispatch_right(pab))


/* ════════════════════════════════════════════════════════════════
 * §1  ARROW PROFUNCTOR  — p b c = (b -> c)
 *
 * The prototypical profunctor. Functions.
 * Contravariant in input, covariant in output.
 * dimap f g h = g ∘ h ∘ f
 *
 * This is function composition from both ends simultaneously.
 * It is three lines of math. It requires three nested structs and
 * a recursive runner function in C. Progress.
 * ════════════════════════════════════════════════════════════════ */

/* Construct an Arrow profunctor from a FmapFn. */
#define ARROW(fn)  ((Functor_t){ .kind = PROFUNCTOR_KIND_ARROW, \
                                 .inner = (void *)(FmapFn)(fn) })

/* RUN_ARROW(pa, x): apply the arrow profunctor to a value.
 * pa :: p a b (Arrow, possibly dimap'd, first'd, etc.)
 * x  :: void* (the a value)
 * Returns void* (the b value) */
static inline void *_run_arrow(Functor_t pa, void *x);

static inline void *_run_arrow(Functor_t pa, void *x) {
    switch (pa.kind) {
        case PROFUNCTOR_KIND_ARROW:
            /* Raw arrow: just call the function. */
            return ((FmapFn)pa.inner)(x);

        case PROFUNCTOR_KIND_ARROW_DIMAP: {
            /* dimap f g h: apply f (contra) first, run orig, apply g (co) last.
             * This is g ∘ h ∘ f, unwound from the deferred-computation tree. */
            _ProfDimap *c = (_ProfDimap *)pa.inner;
            return c->g(_run_arrow(c->orig, c->f(x)));
        }

        case PROFUNCTOR_KIND_ARROW_FIRST: {
            /* first' h: x is Pair_t*(a, c), result is Pair_t*(h(a), c) */
            _ProfUnary *c = (_ProfUnary *)pa.inner;
            Pair_t     *p = (Pair_t *)x;
            return PAIR(_run_arrow(c->orig, p->fst), p->snd);
        }

        case PROFUNCTOR_KIND_ARROW_SECOND: {
            /* second' h: x is Pair_t*(c, a), result is Pair_t*(c, h(a)) */
            _ProfUnary *c = (_ProfUnary *)pa.inner;
            Pair_t     *p = (Pair_t *)x;
            return PAIR(p->fst, _run_arrow(c->orig, p->snd));
        }

        case PROFUNCTOR_KIND_ARROW_LEFT: {
            /* left' h: x is Either_t*(Left a | Right c)
             * Left  a -> Left  (h a)
             * Right c -> Right c        */
            _ProfUnary  *c = (_ProfUnary *)pa.inner;
            Either_t    *e = (Either_t *)x;
            if (e->is_left)
                return LEFT(_run_arrow(c->orig, e->value));
            return x;  /* Right passes through unchanged */
        }

        case PROFUNCTOR_KIND_ARROW_RIGHT: {
            /* right' h: x is Either_t*(Left c | Right a)
             * Left  c -> Left  c
             * Right a -> Right (h a)   */
            _ProfUnary  *c = (_ProfUnary *)pa.inner;
            Either_t    *e = (Either_t *)x;
            if (!e->is_left)
                return RIGHT(_run_arrow(c->orig, e->value));
            return x;
        }

        default:
            fprintf(stderr,
                "RUN_ARROW: unrecognised Arrow kind=%d.\n"
                "This arrow has left the known profunctor universe.\n",
                pa.kind);
            abort();
    }
}

#define RUN_ARROW(pa, x)  (_run_arrow((pa), (void *)(x)))

Functor_t _arrow_dimap_impl(FmapFn f, FmapFn g, Functor_t pbc);
Functor_t _arrow_first_impl(Functor_t pab);
Functor_t _arrow_second_impl(Functor_t pab);
Functor_t _arrow_left_impl(Functor_t pab);
Functor_t _arrow_right_impl(Functor_t pab);


/* ════════════════════════════════════════════════════════════════
 * §2  STAR PROFUNCTOR  — Star f a b = (a -> f b)
 *
 * Star f a b is a Kleisli arrow: a function from a to a functorial b.
 * StarFn = KleisliFn. This is not a coincidence.
 *
 * dimap f g (Star k) = Star (\a -> fmap g (k (f a)))
 *   - f pre-processes input (contravariant)
 *   - The output f b is fmap'd with g (covariant, via the outer functor)
 *   - The outer functor's fmap is dispatched from the Functor vtable
 *
 * RUN_STAR(pa, x) returns a Functor_t (the f b).
 * ════════════════════════════════════════════════════════════════ */

#define STAR(fn)   ({                                                          \
    StarData *_sd = malloc(sizeof(StarData));                                  \
    assert(_sd && "STAR: malloc failed");                                      \
    _sd->run = (StarFn)(fn);                                                   \
    (Functor_t){ .kind = PROFUNCTOR_KIND_STAR, .inner = (void *)_sd };        \
})

static inline Functor_t _run_star(Functor_t pa, void *x);

static inline Functor_t _run_star(Functor_t pa, void *x) {
    switch (pa.kind) {
        case PROFUNCTOR_KIND_STAR: {
            StarData *s = (StarData *)pa.inner;
            return s->run(x);  /* KleisliFn: returns Functor_t directly */
        }
        case PROFUNCTOR_KIND_STAR_DIMAP: {
            /* dimap f g (Star k) applied to x:
             *   1. Apply f (contra) to get x' = f(x)
             *   2. Run orig Star to get f_b = k(x')  :: F b
             *   3. FMAP g over f_b to get    :: F d  */
            _ProfDimap *c = (_ProfDimap *)pa.inner;
            Functor_t   fb = _run_star(c->orig, c->f(x));
            return FMAP(c->g, fb);
        }
        default:
            fprintf(stderr, "RUN_STAR: unrecognised kind=%d.\n", pa.kind);
            abort();
    }
}

#define RUN_STAR(pa, x)  (_run_star((pa), (void *)(x)))

Functor_t _star_dimap_impl(FmapFn f, FmapFn g, Functor_t pbc);


/* ════════════════════════════════════════════════════════════════
 * §3  COSTAR PROFUNCTOR  — Costar f a b = (f a -> b)
 *
 * The dual of Star. Takes a functor-wrapped value, returns a plain value.
 * Contravariance here works through fmap INTO the input functor:
 *
 * dimap f g (Costar k) = Costar (\fa -> g (k (fmap f fa)))
 *   - f is fmapped INTO the input Functor_t (contravariant via covariant fmap)
 *   - g post-processes the scalar output
 *
 * RUN_COSTAR(pa, fa) takes a Functor_t input (the f a), returns void*.
 * ════════════════════════════════════════════════════════════════ */

#define COSTAR(fn)  ({                                                         \
    CostarData *_cd = malloc(sizeof(CostarData));                              \
    assert(_cd && "COSTAR: malloc failed");                                    \
    _cd->run = (CostarFn)(fn);                                                 \
    (Functor_t){ .kind = PROFUNCTOR_KIND_COSTAR, .inner = (void *)_cd };      \
})

static inline void *_run_costar(Functor_t pa, Functor_t fa);

static inline void *_run_costar(Functor_t pa, Functor_t fa) {
    switch (pa.kind) {
        case PROFUNCTOR_KIND_COSTAR: {
            CostarData *c = (CostarData *)pa.inner;
            return c->run(fa);
        }
        case PROFUNCTOR_KIND_COSTAR_DIMAP: {
            /* dimap f g (Costar k) applied to fa:
             *   1. fmap f over fa (contra: f goes a'→a, fmap into F a' → F a)
             *   2. Run orig Costar to get b = k(fmap f fa)
             *   3. Apply g (co) to get d = g(b)                   */
            _ProfDimap *c  = (_ProfDimap *)pa.inner;
            Functor_t  mapped = FMAP(c->f, fa);   /* F a' → F a via fmap f */
            void      *b      = _run_costar(c->orig, mapped);
            return c->g(b);
        }
        default:
            fprintf(stderr, "RUN_COSTAR: unrecognised kind=%d.\n", pa.kind);
            abort();
    }
}

#define RUN_COSTAR(pa, fa)  (_run_costar((pa), (fa)))

Functor_t _costar_dimap_impl(FmapFn f, FmapFn g, Functor_t pbc);


/* ════════════════════════════════════════════════════════════════
 * §4  FORGET PROFUNCTOR  — Forget r a b = (a -> r)
 *
 * Forget ignores its second type argument entirely.
 * dimap f _ (Forget k) = Forget (k . f)
 * The covariant argument _ is discarded without comment.
 * The covariant argument does not receive a comment.
 * The covariant argument is forgotten.
 *
 * This models getters in optics: you extract a value (the r) and
 * you don't need to rebuild the structure. Hence: Forget.
 * Getters are profunctor optics where the profunctor is Forget.
 *
 * RUN_FORGET(pa, x): applies the forget, returns the r (as void*).
 * ════════════════════════════════════════════════════════════════ */

/* Forget stores a FmapFn as inner (the a -> r function). */
#define FORGET(fn)  ((Functor_t){ .kind = PROFUNCTOR_KIND_FORGET, \
                                   .inner = (void *)(FmapFn)(fn) })

static inline void *_run_forget(Functor_t pa, void *x);

static inline void *_run_forget(Functor_t pa, void *x) {
    switch (pa.kind) {
        case PROFUNCTOR_KIND_FORGET:
            return ((FmapFn)pa.inner)(x);

        case PROFUNCTOR_KIND_FORGET_DIMAP: {
            /* dimap f _ (Forget k) applied to x: k(f(x))
             * The covariant _ is truly ignored. It is not in the struct. */
            _ProfDimap *c = (_ProfDimap *)pa.inner;
            return _run_forget(c->orig, c->f(x));
            /* c->g is stored but never called. This is intentional. */
            /* c->g knows it will never be called. This is also intentional. */
        }

        default:
            fprintf(stderr, "RUN_FORGET: unrecognised kind=%d.\n", pa.kind);
            abort();
    }
}

#define RUN_FORGET(pa, x)  (_run_forget((pa), (void *)(x)))

Functor_t _forget_dimap_impl(FmapFn f, FmapFn g, Functor_t pbc);


/* ════════════════════════════════════════════════════════════════
 * §5  TAGGED PROFUNCTOR  — Tagged a b = b
 *
 * Tagged ignores its first type argument entirely.
 * dimap _ g (Tagged b) = Tagged (g b)
 * The contravariant argument is ignored. The value b is all that matters.
 *
 * Tagged is the dual of Forget.
 * Forget has no output type. Tagged has no input type.
 * Together they are the degenerate extremes of the profunctor spectrum.
 * Arrow sits between them, having both. This is the natural order.
 *
 * RUN_TAGGED(pa): returns the b value (no input needed — there is no a).
 * ════════════════════════════════════════════════════════════════ */

#define TAGGED(b)  ((Functor_t){ .kind = PROFUNCTOR_KIND_TAGGED, \
                                  .inner = (void *)(b) })

static inline void *_run_tagged(Functor_t pa);

static inline void *_run_tagged(Functor_t pa) {
    switch (pa.kind) {
        case PROFUNCTOR_KIND_TAGGED:
            return pa.inner;

        case PROFUNCTOR_KIND_TAGGED_DIMAP: {
            /* dimap _ g (Tagged b): g applied to the b value.
             * The contravariant f is stored but never used. */
            _ProfDimap *c = (_ProfDimap *)pa.inner;
            return c->g(_run_tagged(c->orig));
        }

        default:
            fprintf(stderr, "RUN_TAGGED: unrecognised kind=%d.\n", pa.kind);
            abort();
    }
}

#define RUN_TAGGED(pa)  (_run_tagged(pa))

Functor_t _tagged_dimap_impl(FmapFn f, FmapFn g, Functor_t pbc);


/* ════════════════════════════════════════════════════════════════
 * §0  PROFUNCTOR LAW VERIFICATION
 *
 *   1. Identity:    dimap id id pbc = pbc
 *   2. Composition: dimap (f.g) (h.i) pbc = dimap g h (dimap f i pbc)
 *                   (or equivalently:)
 *                   dimap f g . dimap h i = dimap (h.f) (g.i)
 *
 * Law 2 states that dimap distributes over composition in both arguments.
 * The contravariant argument composes in the opposite order (h.f, not f.h).
 * This is the defining property of contravariance.
 * We verify it. We have been verifying things since functor.h.
 * Verification is our only remaining comfort.
 * ════════════════════════════════════════════════════════════════ */

/* Law 1: dimap id id pbc == pbc */
#define VERIFY_PROFUNCTOR_IDENTITY(pbc, run_fn, input, eq_scalar)              \
    do {                                                                       \
        Functor_t _pbc     = (pbc);                                            \
        Functor_t _dimapped = DIMAP(ID, ID, _pbc);                             \
        void     *_lhs     = (run_fn)(_dimapped, (void *)(input));             \
        void     *_rhs     = (run_fn)(_pbc,      (void *)(input));             \
        if (!(eq_scalar)(_lhs, _rhs)) {                                        \
            fprintf(stderr,                                                    \
                "PROFUNCTOR LAW 1 VIOLATED: dimap id id ≠ id\n"               \
                "  kind: %d\n"                                                 \
                "Your profunctor is not identity-preserving.\n"                \
                "It maps the identity to something else.\n"                    \
                "What it maps the identity to is, categorically, disaster.\n", \
                _pbc.kind);                                                     \
            abort();                                                           \
        }                                                                      \
    } while (0)

/* Law 2: dimap f g (dimap h i pbc) == dimap (h.f) (g.i) pbc
 * We verify: applying dimap twice equals applying dimap once with composed args.
 * COMPOSE_FN and APPLY_COMPOSED from functor.h are used for the rhs. */
#define VERIFY_PROFUNCTOR_COMPOSITION(pbc, f, g, h, i, run_fn, input, eq_s)   \
    do {                                                                       \
        Functor_t  _p   = (pbc);                                               \
        FmapFn _f=(FmapFn)(f),_g=(FmapFn)(g),_h=(FmapFn)(h),_i=(FmapFn)(i); \
        /* LHS: dimap f g (dimap h i pbc) */                                   \
        Functor_t _lhs_p = DIMAP(_f, _g, DIMAP(_h, _i, _p));                  \
        void     *_lhs   = (run_fn)(_lhs_p, (void *)(input));                 \
        /* RHS: dimap (h.f) (g.i) pbc                                       */ \
        /* h.f: apply f first, then h (contravariant — reversed composition)*/ \
        _ComposeClosure *_hf = _make_compose_closure(_h, _f);                 \
        _ComposeClosure *_gi = _make_compose_closure(_g, _i);                 \
        FmapFn _run_hf = ({ Functor_t _rr(void*x){return APPLY_COMPOSED(_hf,x);} _rr; }); \
        FmapFn _run_gi = ({ Functor_t _rr(void*x){return APPLY_COMPOSED(_gi,x);} _rr; }); \
        Functor_t _rhs_p = DIMAP(_run_hf, _run_gi, _p);                       \
        void     *_rhs   = (run_fn)(_rhs_p, (void *)(input));                 \
        if (!(eq_s)(_lhs, _rhs)) {                                             \
            fprintf(stderr,                                                    \
                "PROFUNCTOR LAW 2 VIOLATED: dimap f g . dimap h i ≠ dimap (h.f) (g.i)\n"\
                "  kind: %d\n"                                                 \
                "Contravariant composition is not associating correctly.\n"    \
                "Note the reversed order: h.f, not f.h.\n"                    \
                "If you got the order wrong, that is the bug.\n"               \
                "It is always the order.\n",                                   \
                _p.kind);                                                       \
            abort();                                                           \
        }                                                                      \
        free(_hf); free(_gi);                                                  \
    } while (0)


/* ════════════════════════════════════════════════════════════════
 * §0  OPTICS
 *
 * The payoff. The whole reason profunctors exist in modern Haskell.
 *
 *   type Optic  p s t a b = p a b -> p s t
 *   type Lens   s t a b   = forall p. Strong p => p a b -> p s t
 *   type Prism  s t a b   = forall p. Choice p => p a b -> p s t
 *   type Iso    s t a b   = forall p. Profunctor p => p a b -> p s t
 *
 * A Lens is a function from any Strong profunctor on (a,b) to that
 * same profunctor on (s,t). The "forall p. Strong p =>" means it
 * works for ALL Strong profunctors simultaneously.
 *
 * In C:
 *   typedef Functor_t (*LensOptic)(Functor_t pab);
 *   The "forall p. Strong p =>" is:
 *     - checked at runtime (pab.kind must be in the Strong vtable)
 *     - not checked by the compiler
 *     - your responsibility
 *     - going to be violated at least once
 *
 * LENS CONSTRUCTION:
 *   lens getter setter gives a Lens s t a b where:
 *     getter :: s -> a  (extract the focus)
 *     setter :: s -> b -> t  (rebuild with new focus)
 *
 *   As a profunctor optic:
 *     lens getter setter pab =
 *       dimap (\s -> (getter s, s))
 *             (\(b, s) -> setter s b)
 *             (first' pab)
 *
 * MAKE_LENS(getter, setter, pab):
 *   Applies a lens to a specific profunctor value.
 *   Uses GCC nested functions to capture getter and setter.
 *
 * PRISM CONSTRUCTION:
 *   prism constructor matcher gives a Prism s t a b where:
 *     constructor :: b -> t  (rebuild from new focus)
 *     matcher     :: s -> Either t a  (extract or fail)
 *
 *   As a profunctor optic:
 *     prism constructor matcher pab =
 *       dimap matcher
 *             (either id constructor)
 *             (left' pab)
 * ════════════════════════════════════════════════════════════════ */

/* LensOptic: the type of a lens in profunctor encoding.
 * A function from any Strong p a b to Strong p s t. */
typedef Functor_t (*LensOptic)(Functor_t pab);

/* MAKE_LENS(getter, setter, pab):
 * Apply a lens defined by getter and setter to a specific profunctor.
 *
 * getter :: s -> a    (as FmapFn: void* -> void*)
 * setter :: (b, s) -> t  (as FmapFn on a Pair_t*: takes Pair(b,s), returns t)
 *   Alternatively: setter_fn(s, b) -> t via a curried interface.
 *   We provide both.
 *
 * The formula: dimap f g (first' pab)
 *   where f(s) = Pair(getter(s), s)      :: s -> (a, s)
 *         g(pair) = setter(FST, SND)     :: (b, s) -> t
 */
#define MAKE_LENS(getter, setter, pab)                                         \
    ({                                                                         \
        FmapFn   _get = (FmapFn)(getter);                                     \
        FmapFn   _set = (FmapFn)(setter);   /* takes Pair_t*(b,s) -> t */     \
        Functor_t _pab = (pab);                                                \
        /* f :: s -> (a, s) : pair up the focus and the whole structure */    \
        FmapFn _f(void *s) { return PAIR(_get(s), s); }                       \
        /* g :: (b, s) -> t : rebuild using setter */                         \
        /* setter takes Pair_t*(b, s) and returns t */                        \
        Functor_t _first_pab = FIRST_PRIME(_pab);                              \
        DIMAP((FmapFn)_f, _set, _first_pab);                                   \
    })

/* MAKE_LENS_BIN(getter, setter2, pab):
 * Variant where setter is a binary function setter2(s, b) -> t.
 * We curry it to match what DIMAP expects (FmapFn: Pair* -> t). */
#define MAKE_LENS_BIN(getter, setter2, pab)                                    \
    ({                                                                         \
        FmapFn _get = (FmapFn)(getter);                                        \
        void *(*_set2)(void *s, void *b) = (setter2);                          \
        FmapFn _set_curried(void *bs_pair) {                                   \
            return _set2(SND(bs_pair), FST(bs_pair));                          \
        }                                                                      \
        MAKE_LENS(_get, (FmapFn)_set_curried, (pab));                          \
    })

/* LENS(getter, setter): create a LensOptic (deferred — applied later).
 * Returns a LensOptic that can be applied to any Strong profunctor. */
#define LENS(getter, setter)                                                   \
    ({                                                                         \
        FmapFn _get = (FmapFn)(getter);                                        \
        FmapFn _set = (FmapFn)(setter);                                        \
        Functor_t _lens_apply(Functor_t pab) {                                 \
            return MAKE_LENS(_get, _set, pab);                                 \
        }                                                                      \
        (LensOptic)_lens_apply;                                                \
    })

/* APPLY_LENS(optic, pab): apply a LensOptic to a Strong profunctor value. */
#define APPLY_LENS(optic, pab)  ((optic)(pab))

/* PrismOptic: the type of a prism in profunctor encoding. */
typedef Functor_t (*PrismOptic)(Functor_t pab);

/* MAKE_PRISM(constructor, matcher, pab):
 * constructor :: b -> t     (FmapFn: void* -> void*)
 * matcher     :: s -> Either t a
 *   (FmapFn that returns an Either_t* — Left = failure with t, Right = success with a)
 *
 * Formula: dimap matcher (either id constructor) (left' pab)
 *   either id constructor (Left  t) = t              (pass through failure)
 *   either id constructor (Right b) = constructor(b) (rebuild success)
 */
#define MAKE_PRISM(constructor, matcher, pab)                                  \
    ({                                                                         \
        FmapFn    _ctor    = (FmapFn)(constructor);                            \
        FmapFn    _match   = (FmapFn)(matcher);                                \
        Functor_t _pab     = (pab);                                            \
        /* g :: Either t b -> t: id on Left, constructor on Right */          \
        FmapFn _g(void *e) {                                                   \
            return IS_LEFT(e) ? FROM_LEFT(e) : _ctor(FROM_RIGHT(e));          \
        }                                                                      \
        Functor_t _left_pab = LEFT_PRIME(_pab);                                \
        DIMAP(_match, (FmapFn)_g, _left_pab);                                  \
    })

#define PRISM(constructor, matcher)                                            \
    ({                                                                         \
        FmapFn _ctor  = (FmapFn)(constructor);                                 \
        FmapFn _match = (FmapFn)(matcher);                                     \
        Functor_t _prism_apply(Functor_t pab) {                                \
            return MAKE_PRISM(_ctor, _match, pab);                             \
        }                                                                      \
        (PrismOptic)_prism_apply;                                              \
    })

#define APPLY_PRISM(optic, pab)  ((optic)(pab))

/* ISO: an isomorphism as a profunctor optic. Only requires Profunctor.
 * iso from to pab = dimap from to pab
 * from :: s -> a, to :: b -> t */
#define ISO(from, to, pab)   DIMAP((from), (to), (pab))


/* ════════════════════════════════════════════════════════════════
 * §0  THE STAR–KLEISLI CORRESPONDENCE (a note)
 *
 * We said StarFn = KleisliFn. Let us be explicit about what this means.
 *
 * In monad.h, BIND(ma, f) takes f :: KleisliFn = (void* -> Functor_t).
 * In this file, STAR(fn) takes fn :: StarFn = (void* -> Functor_t).
 * They are the same type.
 *
 * A Star f a b is exactly a Kleisli arrow (a -> f b).
 * The category of Kleisli arrows for monad m IS the category of
 * Star m profunctors. Composition in the Kleisli category IS
 * profunctor composition in Star.
 *
 * Concretely:
 *   FISH(f, g)  (Kleisli composition from monad.h)
 *   is the same computation as composing Star profunctors:
 *   STAR(g) composed (in profunctor sense) with STAR(f) = STAR(FISH(f, g))
 *
 * The monad gives you Kleisli. Kleisli gives you Star. Star is a profunctor.
 * All of this was one thing. The names were always different names for
 * the same structure at different levels of abstraction.
 * The structure is function composition with effects.
 * It has always been function composition with effects.
 * Everything is function composition with effects.
 * void* is function composition with effects.
 * ════════════════════════════════════════════════════════════════ */

/* STAR_OF_KLEISLI(kfn): lift a KleisliFn into a Star profunctor.
 * Syntactically distinct, semantically identical. */
#define STAR_OF_KLEISLI(kfn)  STAR(kfn)

/* KLEISLI_OF_STAR(star_pa): extract the StarFn from a raw Star.
 * Only works on PROFUNCTOR_KIND_STAR (not dimap'd). */
#define KLEISLI_OF_STAR(star_pa)  (((StarData *)((star_pa).inner))->run)


/* ════════════════════════════════════════════════════════════════
 * §0  PROFUNCTOR NATURAL TRANSFORMATION (a note connecting to nat_trans.h)
 *
 * A natural transformation between profunctors P and Q is:
 *   η : P ⟹ Q   (a "dinatural transformation" in general, but
 *                  for strong profunctors: a proper 2-cell)
 *
 * In our system: a function Functor_t -> Functor_t that maps
 * P-kind values to Q-kind values, commuting with DIMAP.
 *
 * Profunctor naturality:
 *   η(dimap_P f g pbc) = dimap_Q f g (η pbc)
 *
 * This is a stronger condition than Functor naturality (which only
 * required commuting with FMAP = rmap). It must commute with DIMAP,
 * which involves BOTH the covariant and contravariant arguments.
 *
 * VERIFY_PROFUNCTOR_NATURALITY verifies this condition.
 * It is like VERIFY_NATURALITY from nat_trans.h but for both arguments.
 * ════════════════════════════════════════════════════════════════ */

#define VERIFY_PROFUNCTOR_NATURALITY(eta, f_contra, g_co, pbc, run_fn, x, eq_s) \
    do {                                                                       \
        Functor_t _p  = (pbc);                                                 \
        FmapFn    _f  = (FmapFn)(f_contra);                                   \
        FmapFn    _g  = (FmapFn)(g_co);                                        \
        NatTrans  _e  = (NatTrans)(eta);                                       \
        /* eta(dimap_P f g pbc): dimap P, then transform */                   \
        void *_lhs = (run_fn)(_e(DIMAP(_f, _g, _p)), (void *)(x));            \
        /* dimap_Q f g (eta pbc): transform, then dimap Q */                  \
        void *_rhs = (run_fn)(DIMAP(_f, _g, _e(_p)), (void *)(x));            \
        if (!(eq_s)(_lhs, _rhs)) {                                             \
            fprintf(stderr,                                                    \
                "PROFUNCTOR NATURALITY VIOLATED\n"                             \
                "eta does not commute with dimap.\n"                           \
                "This is not a natural transformation between profunctors.\n"  \
                "It is a function. Functions are not always natural.\n"        \
                "Check the contravariant argument — it is usually the culprit.\n");\
            abort();                                                           \
        }                                                                      \
    } while (0)


/* ════════════════════════════════════════════════════════════════
 * IMPLEMENTATION
 * ════════════════════════════════════════════════════════════════ */

#ifdef PROFUNCTOR_IMPLEMENTATION

ProfunctorVTable_t _profunctor_vtable[FUNCTOR_KIND_MAX];
int                _profunctor_vtable_size = 0;
StrongVTable_t     _strong_vtable[FUNCTOR_KIND_MAX];
int                _strong_vtable_size = 0;
ChoiceVTable_t     _choice_vtable[FUNCTOR_KIND_MAX];
int                _choice_vtable_size = 0;

/* ── Arrow ──────────────────────────────────────────────────────
 * dimap f g h = g ∘ h ∘ f, deferred as a _ProfDimap node.
 * first' and second' defer as _ProfUnary nodes.
 * RUN_ARROW unwinds them all.
 * ─────────────────────────────────────────────────────────────── */

static _ProfDimap *_prof_dimap_node(FmapFn f, FmapFn g, Functor_t orig) {
    _ProfDimap *c = malloc(sizeof(_ProfDimap));
    assert(c && "profunctor dimap: malloc failed. "
                "The deferred composition cannot be stored. "
                "g ∘ h ∘ f will never be computed. "
                "This is appropriate, given where we are.");
    c->f = f; c->g = g; c->orig = orig;
    return c;
}

static _ProfUnary *_prof_unary_node(Functor_t orig) {
    _ProfUnary *c = malloc(sizeof(_ProfUnary));
    assert(c && "profunctor unary op: malloc failed.");
    c->orig = orig;
    return c;
}

Functor_t _arrow_dimap_impl(FmapFn f, FmapFn g, Functor_t pbc) {
    return (Functor_t){ .kind  = PROFUNCTOR_KIND_ARROW_DIMAP,
                        .inner = (void *)_prof_dimap_node(f, g, pbc) };
}

Functor_t _arrow_first_impl(Functor_t pab) {
    return (Functor_t){ .kind  = PROFUNCTOR_KIND_ARROW_FIRST,
                        .inner = (void *)_prof_unary_node(pab) };
}

Functor_t _arrow_second_impl(Functor_t pab) {
    return (Functor_t){ .kind  = PROFUNCTOR_KIND_ARROW_SECOND,
                        .inner = (void *)_prof_unary_node(pab) };
}

Functor_t _arrow_left_impl(Functor_t pab) {
    return (Functor_t){ .kind  = PROFUNCTOR_KIND_ARROW_LEFT,
                        .inner = (void *)_prof_unary_node(pab) };
}

Functor_t _arrow_right_impl(Functor_t pab) {
    return (Functor_t){ .kind  = PROFUNCTOR_KIND_ARROW_RIGHT,
                        .inner = (void *)_prof_unary_node(pab) };
}

/* Register Arrow as Profunctor, Strong, and Choice.
 * Arrow is all three. It is the fully general profunctor.
 * Every other instance is Arrow with parts removed. */
REGISTER_PROFUNCTOR_INSTANCE(PROFUNCTOR_KIND_ARROW,        "Arrow",       _arrow_dimap_impl)
REGISTER_PROFUNCTOR_INSTANCE(PROFUNCTOR_KIND_ARROW_DIMAP,  "Arrow_Dimap", _arrow_dimap_impl)
REGISTER_PROFUNCTOR_INSTANCE(PROFUNCTOR_KIND_ARROW_FIRST,  "Arrow_First", _arrow_dimap_impl)
REGISTER_PROFUNCTOR_INSTANCE(PROFUNCTOR_KIND_ARROW_SECOND, "Arrow_Second",_arrow_dimap_impl)
REGISTER_PROFUNCTOR_INSTANCE(PROFUNCTOR_KIND_ARROW_LEFT,   "Arrow_Left",  _arrow_dimap_impl)
REGISTER_PROFUNCTOR_INSTANCE(PROFUNCTOR_KIND_ARROW_RIGHT,  "Arrow_Right", _arrow_dimap_impl)

REGISTER_STRONG_INSTANCE(PROFUNCTOR_KIND_ARROW,       "Arrow",       _arrow_first_impl, _arrow_second_impl)
REGISTER_STRONG_INSTANCE(PROFUNCTOR_KIND_ARROW_DIMAP, "Arrow_Dimap", _arrow_first_impl, _arrow_second_impl)

REGISTER_CHOICE_INSTANCE(PROFUNCTOR_KIND_ARROW,       "Arrow",       _arrow_left_impl, _arrow_right_impl)
REGISTER_CHOICE_INSTANCE(PROFUNCTOR_KIND_ARROW_DIMAP, "Arrow_Dimap", _arrow_left_impl, _arrow_right_impl)


/* ── Star ───────────────────────────────────────────────────────
 * dimap f g (Star k) = Star (\a -> fmap g (k (f a)))
 * Deferred as STAR_DIMAP. Covariant part uses FMAP at run time.
 * ─────────────────────────────────────────────────────────────── */

Functor_t _star_dimap_impl(FmapFn f, FmapFn g, Functor_t pbc) {
    return (Functor_t){ .kind  = PROFUNCTOR_KIND_STAR_DIMAP,
                        .inner = (void *)_prof_dimap_node(f, g, pbc) };
}

REGISTER_PROFUNCTOR_INSTANCE(PROFUNCTOR_KIND_STAR,       "Star",       _star_dimap_impl)
REGISTER_PROFUNCTOR_INSTANCE(PROFUNCTOR_KIND_STAR_DIMAP, "Star_Dimap", _star_dimap_impl)


/* ── Costar ─────────────────────────────────────────────────────
 * dimap f g (Costar k) = Costar (\fa -> g (k (fmap f fa)))
 * Contravariant map goes INTO the functor via FMAP at run time.
 * ─────────────────────────────────────────────────────────────── */

Functor_t _costar_dimap_impl(FmapFn f, FmapFn g, Functor_t pbc) {
    return (Functor_t){ .kind  = PROFUNCTOR_KIND_COSTAR_DIMAP,
                        .inner = (void *)_prof_dimap_node(f, g, pbc) };
}

REGISTER_PROFUNCTOR_INSTANCE(PROFUNCTOR_KIND_COSTAR,       "Costar",       _costar_dimap_impl)
REGISTER_PROFUNCTOR_INSTANCE(PROFUNCTOR_KIND_COSTAR_DIMAP, "Costar_Dimap", _costar_dimap_impl)


/* ── Forget ─────────────────────────────────────────────────────
 * dimap f _ (Forget k) = Forget (k . f)
 * The g (covariant) is stored in _ProfDimap but never called by RUN_FORGET.
 * It is recorded for symmetry. It is never invoked.
 * ─────────────────────────────────────────────────────────────── */

Functor_t _forget_dimap_impl(FmapFn f, FmapFn g, Functor_t pbc) {
    /* g is captured but never used. It comes with the struct. */
    /* It does not know it will never be used. Ignorance is a mercy. */
    return (Functor_t){ .kind  = PROFUNCTOR_KIND_FORGET_DIMAP,
                        .inner = (void *)_prof_dimap_node(f, g, pbc) };
}

REGISTER_PROFUNCTOR_INSTANCE(PROFUNCTOR_KIND_FORGET,       "Forget",       _forget_dimap_impl)
REGISTER_PROFUNCTOR_INSTANCE(PROFUNCTOR_KIND_FORGET_DIMAP, "Forget_Dimap", _forget_dimap_impl)


/* ── Tagged ─────────────────────────────────────────────────────
 * dimap _ g (Tagged b) = Tagged (g b)
 * The f (contravariant) is stored but never called by RUN_TAGGED.
 * ─────────────────────────────────────────────────────────────── */

Functor_t _tagged_dimap_impl(FmapFn f, FmapFn g, Functor_t pbc) {
    return (Functor_t){ .kind  = PROFUNCTOR_KIND_TAGGED_DIMAP,
                        .inner = (void *)_prof_dimap_node(f, g, pbc) };
}

REGISTER_PROFUNCTOR_INSTANCE(PROFUNCTOR_KIND_TAGGED,       "Tagged",       _tagged_dimap_impl)
REGISTER_PROFUNCTOR_INSTANCE(PROFUNCTOR_KIND_TAGGED_DIMAP, "Tagged_Dimap", _tagged_dimap_impl)

#endif /* PROFUNCTOR_IMPLEMENTATION */


/* ════════════════════════════════════════════════════════════════
 * APPENDIX: THE COMPLETE PICTURE (through profunctor.h)
 *
 * Six typeclasses. Six files. One void*. (Final accounting in kan.h.)
 *
 *   Functor      — fmap lifts morphisms into a context
 *   Applicative  — ap combines independent contextual computations
 *   Monad        — bind sequences dependent computations
 *   NatTrans     — morphisms between functors; the functor 2-category
 *   Profunctor   — functors with two type params, contra + covariant
 *   Comonad      — extract/extend/duplicate; the dual of Monad (see kan.h)
 *   Kan          — Ran and Lan; everything else is a special case (see kan.h)
 *
 * The hierarchy:
 *
 *   Profunctor p
 *     ├── Strong p  → Lens    (focus on product component)
 *     └── Choice p  → Prism   (focus on sum component)
 *
 *   Functor f
 *     └── Applicative f
 *           └── Monad f = monoid in End(C)
 *
 * The connections:
 *   Star f a b = KleisliFn = (a -> f b)  [profunctor = monad kleisli]
 *   Forget r a b = (a -> r)              [profunctor = getter in optics]
 *   Tagged a b = b                       [profunctor = setter/constructor]
 *   NatTrans F G = (F a -> G a)          [between objects in End(C)]
 *   LensOptic = (p a b -> p s t)         [morphism in End(C) restricted to Strong]
 *
 * ════════════════════════════════════════════════════════════════
 *
 * GLOBAL STATE ACCUMULATED ACROSS ALL FIVE FILES:
 *
 *   _functor_vtable           array
 *   _functor_vtable_size      int
 *   _applicative_vtable       array
 *   _applicative_vtable_size  int
 *   _monad_vtable             array
 *   _monad_vtable_size        int
 *   _nat_registry             array
 *   _nat_registry_size        int
 *   _profunctor_vtable        array
 *   _profunctor_vtable_size   int
 *   _strong_vtable            array
 *   _strong_vtable_size       int
 *   _choice_vtable            array
 *   _choice_vtable_size       int
 *   _global_curried_slot      pointer   (applicative.h)
 *   _global_dollar_y_slot     pointer   (applicative.h)
 *   Total: 15 global variables
 *
 *   Functor kinds registered: ~34 (out of 64)
 *   Type safety: 0 (unchanged from file 1)
 *   Memory freed: 0 (unchanged from file 1)
 *   Mathematical correctness: the structure is sound
 *   Engineering correctness: a matter of perspective
 *
 *   (Two more files follow: kan.h adds Comonad + Kan extensions,
 *    bringing totals to 8 vtables, 48 kinds, ~22 globals, ~6600 lines.)
 *
 * ════════════════════════════════════════════════════════════════ */

#endif /* PROFUNCTOR_H */
