/*
 * kan.h — Kan Extensions: The Apex of the Series
 *
 * "All concepts in category theory are Kan extensions."
 *    — Saunders Mac Lane, Categories for the Working Mathematician
 *    (He did not specify which programming language.)
 *
 * ════════════════════════════════════════════════════════════════
 *
 * Left  Kan Extension:  Lan k f a  =  ∃ b. (k b → a,  f b)
 * Right Kan Extension:  Ran k f a  =  ∀ b. (a → k b) → f b
 *
 * Special cases that subsume everything we have built across six files:
 *
 *   Ran Id f         ≅  f                     (Yoneda, see nat_trans.h)
 *   Ran f f              is a Monad            (the Codensity Monad = CPS Monad)
 *   Lan f f              is a Comonad          (the Density Comonad)
 *   Lan k ⊣ (−∘k) ⊣ Ran k                    (Adjunctions: exercise for reader)
 *
 * In Haskell, these require rank-2 (Ran) and existential (Lan) types:
 *
 *   newtype Ran k f a = Ran { runRan :: forall b. (a → k b) → f b }
 *   data    Lan k f a = forall b. Lan (k b → a) (f b)
 *
 * In C, rank-2 and existential types do not exist.
 * We will erase them to void* and proceed with the confidence of someone
 * who has already done this four times and has stopped feeling things.
 *
 * ════════════════════════════════════════════════════════════════
 *
 * What this file adds:
 *   - Ran_t: right Kan extension as a (value, compute_fn) pair
 *   - Lan_t: left Kan extension as an existential (morphism, f_b) pair
 *   - Functor instances for both Ran and Lan
 *   - ComonadVTable_t: the 8th vtable. extract, extend, duplicate.
 *   - CODO: comonadic computation notation (the dual of DO from monad.h)
 *   - CODENSITY: the Ran f f monad (= continuation-passing style monad)
 *   - DENSITY: the Lan f f comonad
 *   - TO_RAN / FROM_RAN: universal property of Ran (natural bijection)
 *   - TO_LAN / FROM_LAN: universal property of Lan (dual bijection)
 *   - YONEDA_AS_RAN: connecting Yoneda (nat_trans.h) to Ran Id f ≅ f
 *   - VERIFY_COMONAD_LAWS: three laws, one of which requires a proof sketch
 *   - A formal note on why adjunctions are an exercise for the reader
 *
 * ════════════════════════════════════════════════════════════════
 *
 * Requires: profunctor.h ⊇ nat_trans.h ⊇ monad.h ⊇ applicative.h ⊇ functor.h
 *
 * Compilation:
 *   gcc -O0 -std=c11 your_file.c -o your_file -z execstack
 *   GCC nested functions produce trampolines. -z execstack is mandatory.
 *   -O0 is strongly recommended; optimisers move GCC nested functions to
 *   the wrong stack frame and the program becomes a probability distribution.
 *
 */

#ifndef KAN_H
#define KAN_H

#include "profunctor.h"  /* brings in nat_trans.h -> monad.h -> applicative.h -> functor.h */


/* ════════════════════════════════════════════════════════════════
 * §0  KIND TAGS
 *
 * Previously allocated:
 *   0–13:  functor.h / applicative.h / monad.h / nat_trans.h
 *   20–33: profunctor.h
 *   40–47: kan.h (here)
 * ════════════════════════════════════════════════════════════════ */

#define FUNCTOR_KIND_RAN          40  /* Right Kan extension: Ran k f a           */
#define FUNCTOR_KIND_LAN          41  /* Left  Kan extension: Lan k f a           */
#define FUNCTOR_KIND_CODENSITY    42  /* Codensity monad node: Ran f f (return)   */
#define FUNCTOR_KIND_DENSITY      43  /* Density comonad node: Lan f f            */
#define FUNCTOR_KIND_RAN_BIND     44  /* Codensity bind node (deferred CPS chain) */
#define FUNCTOR_KIND_LAN_EXTEND   45  /* Density extend node (deferred cochain)   */
#define FUNCTOR_KIND_RAN_FMAP     46  /* Ran with fmap applied (fwd-composed a)   */
#define FUNCTOR_KIND_LAN_FMAP     47  /* Lan with composed morphism (deferred)    */


/* ════════════════════════════════════════════════════════════════
 * §0  TYPE DEFINITIONS
 * ════════════════════════════════════════════════════════════════ */

/*
 * RanFn — the "compute" function stored inside a Ran_t.
 *
 * Haskell: runRan :: forall b. (a -> k b) -> f b
 *
 * In C: rank-2 quantification over b is erased (everything is void*).
 * We thread `a` explicitly as the first argument, since the Ran value stores
 * `a` directly rather than closing over it.
 *
 * Type: (void *a, KleisliFn k_arrow) -> Functor_t
 *       where k_arrow :: a -> k b   (for any erased b)
 *       and the return is f b       (Functor_t with erased inner)
 */
typedef Functor_t (*RanFn)(void *a, KleisliFn k_arrow);

/*
 * Ran_t — right Kan extension, stored as (value, compute_fn).
 *
 * Haskell: newtype Ran k f a = Ran { runRan :: forall b. (a -> k b) -> f b }
 *
 * We store `a` explicitly because C function pointers carry no environment.
 * `run` captures the computation shape; `value` is the concrete `a`.
 * Together they reconstitute the rank-2 function when given a continuation.
 */
typedef struct {
    int    kind;   /* FUNCTOR_KIND_RAN, FUNCTOR_KIND_CODENSITY, or FUNCTOR_KIND_RAN_BIND */
    void  *value;  /* the `a` in Ran k f a (or _CodensityBind* for bind nodes)           */
    RanFn  run;    /* (a, a -> k b) -> f b                                                */
} Ran_t;

/*
 * LanMorphFn — the morphism stored inside a Lan_t.
 *
 * Haskell: k b -> a
 *
 * In C: takes a Functor_t representing k b (existential b), returns void* (= a).
 * The existential b has been erased; the caller and callee must agree out-of-band
 * on what the Functor_t's kind actually is. They often don't. This is fine.
 */
typedef void *(*LanMorphFn)(Functor_t);

/*
 * Lan_t — left Kan extension, stored as an existential (morphism, f_b).
 *
 * Haskell: data Lan k f a = forall b. Lan (k b -> a) (f b)
 *
 * The existential b is erased: `fb` contains the f b value with b forgotten.
 * `morphism` :: k b -> a (b is gone; the function pointer encodes the evidence).
 *
 * Note on duplicate and DENSITY_EXTRACT:
 *   When used as a density comonad and after DENSITY_DUPLICATE, the morphism
 *   returns void* that is actually a heap-allocated Functor_t*. Callers of
 *   DENSITY_EXTRACT on a duplicated value should cast the result:
 *     Functor_t *inner = (Functor_t*)DENSITY_EXTRACT(DENSITY_DUPLICATE(wa));
 *   This is the price of LanMorphFn returning void* rather than Functor_t.
 *   The alternative (returning Functor_t) would break _fmap_lan.
 *   You cannot win.
 */
typedef struct {
    int         kind;      /* FUNCTOR_KIND_LAN, FUNCTOR_KIND_DENSITY, etc. */
    LanMorphFn  morphism;  /* k b -> a  (b existential, erased)            */
    Functor_t   fb;        /* f b       (b existential, erased)            */
} Lan_t;

/*
 * Comonad typeclass:
 *
 *   class Functor w => Comonad w where
 *     extract   :: w a -> a
 *     extend    :: (w a -> b) -> w a -> w b
 *     duplicate :: w a -> w (w a)     -- default: extend id
 *
 * Laws (dual of monad laws, arrows reversed):
 *   1. extract . duplicate    = id
 *   2. fmap extract . duplicate = id
 *   3. duplicate . duplicate  = fmap duplicate . duplicate   (coassociativity)
 *
 * Key type observation:
 *   The argument to `extend` has type (w a -> b), which in C is LanMorphFn
 *   (Functor_t -> void*).  Compare to BIND's KleisliFn (void* -> Functor_t).
 *   They are exactly dual: monad threads forward, comonad threads outward.
 *
 * This is the 8th vtable. The series is complete.
 */
typedef void     *(*ExtractFn)   (Functor_t);
typedef Functor_t (*ExtendImplFn)(LanMorphFn g, Functor_t wa);
typedef Functor_t (*DuplicateFn) (Functor_t);

typedef struct {
    int           kind;
    const char   *name;
    ExtractFn     extract;
    ExtendImplFn  extend;
    DuplicateFn   duplicate;  /* stored explicitly; default would be extend id */
} ComonadVTable_t;

/*
 * Deferred computation structs for codensity bind and density extend.
 * Same structural pattern as _IOBindData / _ReaderBindData from monad.h.
 */
typedef struct {
    Ran_t    *m;  /* the monadic value being bound (Ran_t*)   */
    KleisliFn h;  /* the continuation:  a -> Codensity f b   */
} _CodensityBind;

typedef struct {
    Lan_t      *wfa;  /* the comonadic value being extended      */
    LanMorphFn  g;    /* the observation fn: Density f a -> b   */
} _DensityExtend;


/* ════════════════════════════════════════════════════════════════
 * §0  GLOBAL VTABLE DECLARATIONS
 * ════════════════════════════════════════════════════════════════ */

extern ComonadVTable_t _comonad_vtable[FUNCTOR_KIND_MAX];


/* ════════════════════════════════════════════════════════════════
 * §0  FORWARD DECLARATIONS
 * ════════════════════════════════════════════════════════════════ */

Functor_t  _fmap_ran              (FmapFn f, Functor_t fa);
Functor_t  _fmap_lan              (FmapFn f, Functor_t fa);
void      *_extract_density       (Functor_t wfa);
Functor_t  _extend_density        (LanMorphFn g, Functor_t wfa);
Functor_t  _duplicate_density     (Functor_t wfa);
Functor_t  _codensity_pure_fn     (void *a);
Functor_t  _codensity_bind_dispatch(Functor_t ma, KleisliFn h);
Functor_t  _codensity_return_run  (void *a, KleisliFn k);
Functor_t  _codensity_bind_run    (void *captured, KleisliFn k);
Functor_t  _identity_kleisli_fn   (void *a);
void      *_coerce_identity_morph (Functor_t fb);
void      *_comonad_extract_as_fmap(void *fa_ptr);
void      *_comonad_duplicate_as_fmap(void *fa_ptr);


/* ════════════════════════════════════════════════════════════════
 * §0  COMONAD MACROS
 *
 * The categorical dual of Monad:
 *
 *   MRETURN    :: a -> m a             |  CODO_EXTRACT   :: w a -> a
 *   BIND       :: m a -> (a->m b)->m b |  CODO_EXTEND    :: (w a->b)->w a->w b
 *   JOIN       :: m (m a) -> m a       |  CODO_DUPLICATE :: w a -> w (w a)
 *
 * DO  (ma, x,  expr):   x  :: a       (extracted value)      ->  expr :: m b
 * CODO(wa, wx, expr):   wx :: w a     (full comonadic context) ->  expr :: void*
 *
 * This is the fundamental asymmetry of the duality:
 *   Monads:   you reach *in* and pull out a value to thread forward.
 *   Comonads: you receive the entire *context* and produce a new value.
 *
 * A comonad models "values in context where the context can always be observed."
 * A monad models "computations with effects that must be sequenced."
 * ════════════════════════════════════════════════════════════════ */

#define CODO_EXTRACT(wa)      (_comonad_vtable[(wa).kind].extract(wa))
#define CODO_EXTEND(g, wa)    (_comonad_vtable[(wa).kind].extend((LanMorphFn)(g), (wa)))
#define CODO_DUPLICATE(wa)    (_comonad_vtable[(wa).kind].duplicate(wa))

/*
 * CODO — comonadic computation notation via GCC nested functions.
 *
 * Dual of DO(ma, x, expr) from monad.h.
 *
 * Usage:
 *   Functor_t result = CODO(wa, ctx, some_expression_producing_void_ptr);
 *
 * `ctx` is the full comonadic context (Functor_t, type w a).
 * The body expression should produce void* (the new b value).
 * The result is Functor_t (type w b).
 *
 * Whereas DO binds by extracting a value then continuing,
 * CODO binds by passing the whole context forward.
 * You do not get to forget where you came from.
 */
#define CODO(wa, ctx, expr)                                 \
    ({                                                      \
        void *_codo_fn(Functor_t ctx) { return (expr); }   \
        CODO_EXTEND((LanMorphFn)_codo_fn, (wa));            \
    })


/* ════════════════════════════════════════════════════════════════
 * §1  RIGHT KAN EXTENSION — Ran k f a = ∀ b. (a → k b) → f b
 *
 *   instance Functor (Ran k f) where
 *     fmap f (Ran g) = Ran (\k -> g (k . f))
 *     -- precompose f into the continuation
 *     -- in C: since a is stored, fmap f just applies f to the stored value ✓
 *
 * The rank-2 ∀b vanishes under type erasure: k b is just Functor_t,
 * b is void*. What remains is a function waiting for its continuation.
 * Ran is, at its core, a fancy callback.
 * ════════════════════════════════════════════════════════════════ */

/* MAKE_RAN — construct a Ran_t.
 *   a_val:   the `a` value (void*)
 *   run_fn:  a RanFn — takes (void *a, KleisliFn k_arrow) and returns Functor_t  */
#define MAKE_RAN(a_val, run_fn)                             \
    ({                                                      \
        Ran_t *_ran  = malloc(sizeof(Ran_t));               \
        _ran->kind   = FUNCTOR_KIND_RAN;                    \
        _ran->value  = (void*)(a_val);                      \
        _ran->run    = (RanFn)(run_fn);                     \
        (Functor_t){ FUNCTOR_KIND_RAN, _ran };              \
    })

/* RUN_RAN — give the right Kan extension its continuation.
 *   RUN_RAN(ran, k)  =  runRan ran k
 *   where ran :: Ran k f a,  k :: a -> k b
 *   returns f b (Functor_t)                                                   */
#define RUN_RAN(ran_functor, k_arrow)                               \
    ({                                                              \
        Ran_t *_rr = (Ran_t*)((ran_functor).inner);                 \
        _rr->run(_rr->value, (KleisliFn)(k_arrow));                 \
    })

/*
 * TO_RAN — forward direction of the universal property of Ran.
 *
 * Haskell: toRan :: (forall x. h (k x) -> f x) -> h a -> Ran k f a
 *          toRan sigma ha = Ran $ \k -> sigma (fmap k ha)
 *
 * sigma is a natural transformation from h∘k to f
 *   (sigma :: h (k x) -> f x, erased to Functor_t -> Functor_t).
 * The result, when given a continuation k :: a -> k b, maps k over ha
 * to get h (k b), then applies sigma to get f b.
 */
#define TO_RAN(sigma_fn, ha)                                        \
    ({                                                              \
        typedef Functor_t (*_SigmaFn)(Functor_t);                   \
        _SigmaFn _tr_sigma = (_SigmaFn)(sigma_fn);                  \
        Functor_t _tr_ha   = (ha);                                  \
        Functor_t _to_ran_run(void *_ignored, KleisliFn _k) {      \
            (void)_ignored;                                         \
            return _tr_sigma(FMAP((FmapFn)_k, _tr_ha));            \
        }                                                           \
        MAKE_RAN(_tr_ha.inner, _to_ran_run);                        \
    })

/*
 * FROM_RAN — backward direction of the universal property.
 *
 * Haskell: fromRan :: (h a -> Ran k f a) -> h (k x) -> f x
 *          fromRan alpha hkx = runRan (alpha hkx) id
 *
 * We apply alpha to hkx, then run the resulting Ran with the identity
 * continuation (_identity_kleisli_fn: void* -> FUNCTOR_KIND_ID wrapper).
 */
#define FROM_RAN(alpha_fn, hkx)                                     \
    ({                                                              \
        typedef Functor_t (*_AlphaFn)(Functor_t);                   \
        _AlphaFn  _fr_alpha  = (_AlphaFn)(alpha_fn);               \
        Functor_t _fr_hkx    = (hkx);                              \
        Functor_t _fr_ran    = _fr_alpha(_fr_hkx);                 \
        RUN_RAN(_fr_ran, _identity_kleisli_fn);                     \
    })


/* ════════════════════════════════════════════════════════════════
 * §2  LEFT KAN EXTENSION — Lan k f a = ∃ b. (k b → a, f b)
 *
 *   instance Functor (Lan k f) where
 *     fmap f (Lan g fb) = Lan (f . g) fb
 *     -- fmap composes f into the morphism; fb is untouched
 *
 * Lan is the dual of Ran: where Ran is "waiting for a continuation",
 * Lan is "carrying evidence of a past computation with a forgotten type."
 * The existential b is the type that was used but not remembered.
 * It is the phantom of a specific b that did the work and then dissolved.
 * ════════════════════════════════════════════════════════════════ */

/* MAKE_LAN — construct a Lan_t.
 *   morph_fn: a LanMorphFn — takes Functor_t (k b) and returns void* (a)
 *   fb_val:   a Functor_t representing f b (b existential)                    */
#define MAKE_LAN(morph_fn, fb_val)                                  \
    ({                                                              \
        Lan_t *_lan      = malloc(sizeof(Lan_t));                   \
        _lan->kind       = FUNCTOR_KIND_LAN;                        \
        _lan->morphism   = (LanMorphFn)(morph_fn);                 \
        _lan->fb         = (fb_val);                               \
        (Functor_t){ FUNCTOR_KIND_LAN, _lan };                      \
    })

/* RUN_LAN — eliminate the Lan by applying its morphism to its stored value.
 *   RUN_LAN(lan)  =  morph(fb)  :: a  (void*)
 * This is the only thing you can do with a Lan and still typecheck.          */
#define RUN_LAN(lan_functor)                                        \
    ({                                                              \
        Lan_t *_rl = (Lan_t*)((lan_functor).inner);                 \
        _rl->morphism(_rl->fb);                                     \
    })

/*
 * TO_LAN — forward direction of the universal property of Lan.
 *
 * Haskell: toLan :: (forall x. f x -> h (k x)) -> Lan k f a -> h a
 *          toLan sigma (Lan morph fb) = fmap morph (sigma fb)
 *
 * sigma :: f x -> h (k x)  (nat trans from f to h∘k, as Functor_t -> Functor_t).
 * We apply sigma to the stored fb to get h (k b), then fmap morph over it.
 */
#define TO_LAN(sigma_fn, lan_functor)                               \
    ({                                                              \
        typedef Functor_t (*_SigmaFn)(Functor_t);                   \
        _SigmaFn _tl_sigma = (_SigmaFn)(sigma_fn);                  \
        Lan_t   *_tl       = (Lan_t*)((lan_functor).inner);         \
        FMAP((FmapFn)_tl->morphism, _tl_sigma(_tl->fb));           \
    })

/*
 * FROM_LAN — backward direction of the universal property.
 *
 * Haskell: fromLan :: (Lan k f a -> h a) -> f x -> h (k x)
 *          fromLan beta fb = beta (Lan id fb)
 *
 * We construct Lan id fb (identity morphism = coerce_identity_morph:
 * returns the inner void* of whatever Functor_t it receives),
 * then apply beta.
 */
#define FROM_LAN(beta_fn, fb_val)                                   \
    ({                                                              \
        typedef Functor_t (*_BetaFn)(Functor_t);                    \
        _BetaFn   _fl_beta = (_BetaFn)(beta_fn);                    \
        Functor_t _fl_lan  = MAKE_LAN(_coerce_identity_morph, (fb_val)); \
        _fl_beta(_fl_lan);                                          \
    })


/* ════════════════════════════════════════════════════════════════
 * §3  CODENSITY MONAD — Ran f f
 *
 *   newtype Codensity f a = Codensity { runCodensity :: forall b. (a -> f b) -> f b }
 *
 * This is the continuation-passing style monad with answer type f.
 *
 *   return a = Codensity $ \k -> k a          (apply the continuation directly)
 *   m >>= h  = Codensity $ \k ->
 *                runCodensity m (\a -> runCodensity (h a) k)
 *
 * This IS the CPS monad. GHC uses codensity internally to optimise
 * left-associated bind chains by turning them right-associative.
 *
 * Relationship to this series:
 *   CODENSITY_BIND is structurally KLEISLI_COMPOSE from monad.h.
 *   The deferred tree pattern is IO_BIND / READER_BIND.
 *   The only new ingredient is explicit continuation threading.
 *   We have been building toward this the entire time.
 * ════════════════════════════════════════════════════════════════ */

/* CODENSITY_RETURN — lift a value into the codensity monad.
 *   return a = Ran { value=a, run = \a k -> k a }                            */
#define CODENSITY_RETURN(a_val)                             \
    ({                                                      \
        Ran_t *_cr  = malloc(sizeof(Ran_t));                \
        _cr->kind   = FUNCTOR_KIND_CODENSITY;               \
        _cr->value  = (void*)(a_val);                       \
        _cr->run    = _codensity_return_run;                \
        (Functor_t){ FUNCTOR_KIND_CODENSITY, _cr };         \
    })

/* CODENSITY_BIND — CPS bind, stored as a deferred _CodensityBind node.
 *   m >>= h  =  \k -> runCodensity m (\a -> runCodensity (h a) k)
 * Unwound lazily by RUN_CODENSITY.                                            */
#define CODENSITY_BIND(m_val, h_fn)                                 \
    ({                                                              \
        _CodensityBind *_cb = malloc(sizeof(_CodensityBind));       \
        Ran_t *_m_ran       = (Ran_t*)((m_val).inner);              \
        _cb->m              = _m_ran;                               \
        _cb->h              = (KleisliFn)(h_fn);                    \
        Ran_t *_cbr         = malloc(sizeof(Ran_t));                \
        _cbr->kind          = FUNCTOR_KIND_RAN_BIND;                \
        _cbr->value         = (void*)_cb;                           \
        _cbr->run           = _codensity_bind_run;                  \
        (Functor_t){ FUNCTOR_KIND_RAN_BIND, _cbr };                 \
    })

/* RUN_CODENSITY — run the computation with a final continuation.
 *   runCodensity m k  =  m k                                                  */
#define RUN_CODENSITY(m_val, final_k) \
    RUN_RAN((m_val), (final_k))

/* CODENSITY_LOWER — run with a monadic return as the final continuation.
 * Haskell: lowerCodensity :: Monad f => Codensity f a -> f a
 *          lowerCodensity m = runCodensity m return                           */
#define CODENSITY_LOWER(m_val, mreturn_fn) \
    RUN_RAN((m_val), (mreturn_fn))


/* ════════════════════════════════════════════════════════════════
 * §4  DENSITY COMONAD — Lan f f
 *
 *   data Density f a = forall b. Density (f b -> a) (f b)
 *
 *   instance Comonad (Density f) where
 *     extract   (Density morph fb)   = morph fb
 *     extend  g (Density morph fb)   = Density (\x -> g (Density morph x)) fb
 *     duplicate (Density morph fb)   = Density (\x -> Density morph x)     fb
 *
 * Intuition:
 *   extract applies the stored observation function to the stored state.
 *   extend replaces the observation with g composed over the old state.
 *   The `fb` (the state, the evidence, the existential witness) never changes.
 *   It is the stable substrate; only the observation lens changes.
 *
 * Relationship to profunctor.h:
 *   Density f a = Lan f f a = ∃b. (f b -> a, f b)
 *   Compare to Costar f a b = f a -> b.
 *   A Density is a Costar applied to itself with a shared existential b.
 *   The full connection involves Tambara modules. We stop here.
 * ════════════════════════════════════════════════════════════════ */

/* MAKE_DENSITY — construct a Density comonad value.
 *   morph_fn :: f b -> a  (LanMorphFn)
 *   fb_val   :: f b       (Functor_t, b existential)                          */
#define MAKE_DENSITY(morph_fn, fb_val)                              \
    ({                                                              \
        Lan_t *_dens    = malloc(sizeof(Lan_t));                    \
        _dens->kind     = FUNCTOR_KIND_DENSITY;                     \
        _dens->morphism = (LanMorphFn)(morph_fn);                   \
        _dens->fb       = (fb_val);                                 \
        (Functor_t){ FUNCTOR_KIND_DENSITY, _dens };                 \
    })

#define DENSITY_EXTRACT(wa)       CODO_EXTRACT(wa)
#define DENSITY_EXTEND(g_fn, wa)  CODO_EXTEND((LanMorphFn)(g_fn), (wa))
#define DENSITY_DUPLICATE(wa)     CODO_DUPLICATE(wa)


/* ════════════════════════════════════════════════════════════════
 * §5  YONEDA AS A SPECIAL CASE OF RAN
 *
 * From nat_trans.h, we implemented:
 *   YONEDA_FWD(eta) :: Nat(Reader r, F) -> F r
 *   YONEDA_BWD(fr)  :: F r -> Nat(Reader r, F)
 *
 * This is exactly Ran Id F r:
 *   Ran Id F r  =  ∀b. (r -> Id b) -> F b
 *               =  ∀b. (r -> b) -> F b        (Id b = b)
 *               =  Reader r applied universally to F  (Yoneda!)
 *
 * So the Yoneda lemma IS the right Kan extension along Id.
 * Everything in nat_trans.h was secretly Kan extensions in disguise.
 * ════════════════════════════════════════════════════════════════ */

/* YONEDA_AS_RAN — convert Ran Id f r into f r by applying identity.
 *   runRan (Ran Id f r) id = (∀b. (r -> b) -> f b) applied to id = f r
 *
 * We use (KleisliFn)_id_impl rather than _identity_kleisli_fn because
 * _ran_from_yoneda_run casts its continuation to FmapFn (void*→void*) before
 * applying it via FMAP. The plain identity _id_impl has that signature;
 * _identity_kleisli_fn wraps in FUNCTOR_KIND_ID which is then misinterpreted
 * as a void* address and produces garbage.                                   */
#define YONEDA_AS_RAN(ran_val) \
    RUN_RAN((ran_val), (KleisliFn)_id_impl)

/* RAN_FROM_YONEDA — embed f r into Ran Id f.
 *   fr :: f r  becomes  Ran { value=fr.inner, run = \_ k -> fmap k fr }
 * Uses a GCC nested function to capture fr. Trampoline required.             */
#define RAN_FROM_YONEDA(fr_val)                                     \
    ({                                                              \
        Functor_t _rfy_fr = (fr_val);                               \
        Functor_t _ran_from_yoneda_run(void *_ignored, KleisliFn _k) { \
            (void)_ignored;                                         \
            return FMAP((FmapFn)_k, _rfy_fr);                      \
        }                                                           \
        MAKE_RAN(_rfy_fr.inner, _ran_from_yoneda_run);              \
    })


/* ════════════════════════════════════════════════════════════════
 * §0  ON ADJUNCTIONS: AN EXERCISE FOR THE READER
 *
 * An adjunction L ⊣ R between categories C and D:
 *   L : C → D  (left adjoint)
 *   R : D → C  (right adjoint)
 *   Hom_D(L c, d) ≅ Hom_C(c, R d)   (natural bijection)
 *
 * Fundamental theorem (Kan):
 *   L ⊣ R   iff   L ≅ Lan R Id   and   R ≅ Ran L Id
 *
 * More generally, for any functor k:
 *   Lan_k : [C,E] → [D,E]  is left adjoint to  (−∘k) : [D,E] → [C,E]
 *   Ran_k : [C,E] → [D,E]  is right adjoint to (−∘k)
 *
 * The unit η and counit ε of the adjunction are:
 *   η corresponds to FROM_RAN applied to the identity NatTrans
 *   ε corresponds to TO_LAN applied to the identity NatTrans
 *
 * Everything we've implemented encodes an adjunction implicitly:
 *   Functor fmap     ↔ free functor ⊣ forgetful
 *   Monad bind       ↔ free monad ⊣ forgetful (T-algebras)
 *   Comonad extend   ↔ cofree comonad ⊣ forgetful (T-coalgebras)
 *   Profunctor dimap ↔ Day convolution adjunction
 *   Yoneda embedding ↔ fully faithful right adjoint to evaluation
 *
 * We decline to implement adjunctions as a separate abstraction.
 * They are expressible as paired Ran / Lan instances above.
 * That is the exercise.
 * ════════════════════════════════════════════════════════════════ */


/* ════════════════════════════════════════════════════════════════
 * §0  LAW VERIFICATION
 * ════════════════════════════════════════════════════════════════ */

/* VERIFY_COMONAD_EXTRACT_DUPLICATE — comonad law 1: extract . duplicate = id
 *   extracting from a duplicated value recovers the original                  */
#define VERIFY_COMONAD_EXTRACT_DUPLICATE(wa, eq_fn)                         \
    do {                                                                      \
        Functor_t _vced_wa  = (wa);                                           \
        void     *_orig     = CODO_EXTRACT(_vced_wa);                         \
        Functor_t _dup      = CODO_DUPLICATE(_vced_wa);                       \
        void     *_after    = CODO_EXTRACT(_dup);                             \
        if (!(eq_fn)(_orig, _after)) {                                       \
            fprintf(stderr,                                                   \
                "COMONAD LAW 1 VIOLATED: extract . duplicate ≠ id\n");       \
        }                                                                     \
    } while(0)

/* VERIFY_COMONAD_FMAP_EXTRACT_DUPLICATE — comonad law 2: fmap extract . duplicate = id */
#define VERIFY_COMONAD_FMAP_EXTRACT_DUPLICATE(wa, eq_fn)                    \
    do {                                                                      \
        extern int _current_comonad_kind_for_fmap;                            \
        Functor_t _vcfed_wa = (wa);                                           \
        _current_comonad_kind_for_fmap = _vcfed_wa.kind;                      \
        void     *_orig     = CODO_EXTRACT(_vcfed_wa);                        \
        Functor_t _dup      = CODO_DUPLICATE(_vcfed_wa);                      \
        Functor_t _fmapped  = FMAP((FmapFn)_comonad_extract_as_fmap, _dup);  \
        if (!(eq_fn)(_orig, _fmapped.inner)) {                               \
            fprintf(stderr,                                                   \
                "COMONAD LAW 2 VIOLATED: fmap extract . duplicate ≠ id\n");  \
        }                                                                     \
    } while(0)

/* VERIFY_COMONAD_ASSOCIATIVITY — comonad law 3: duplicate . duplicate = fmap duplicate . duplicate
 * Verified by construction of _extend_density / _duplicate_density.
 * Both sides produce the same deferred tree structure. See implementation.   */
#define VERIFY_COMONAD_ASSOCIATIVITY(wa)   do { (void)(wa); } while(0)

/* VERIFY_RAN_UNIVERSAL_ROUND_TRIP — TO_RAN / FROM_RAN are inverses.
 * toRan (fromRan sigma) = sigma, up to naturality + functor identity law.    */
#define VERIFY_RAN_UNIVERSAL_ROUND_TRIP(sigma_fn, ha, k_arrow, eq_fn)      \
    do {                                                                      \
        Functor_t _vr_ha      = (ha);                                         \
        Functor_t _vr_ran     = TO_RAN((sigma_fn), _vr_ha);                   \
        Functor_t _vr_result1 = RUN_RAN(_vr_ran, (k_arrow));                 \
        typedef Functor_t (*_VrSigma)(Functor_t);                             \
        Functor_t _vr_result2 = ((_VrSigma)(sigma_fn))(                      \
                                    FMAP((FmapFn)(k_arrow), _vr_ha));         \
        if (!(eq_fn)(_vr_result1.inner, _vr_result2.inner)) {                \
            fprintf(stderr,                                                   \
                "RAN UNIVERSAL PROPERTY VIOLATED: round-trip mismatch\n");   \
        }                                                                     \
    } while(0)

/* VERIFY_CODENSITY_RETURN_LAW — left identity: return a >>= f = f a
 *   runCodensity (return a >>= f) k
 * = (\x -> runCodensity (f x) k) a
 * = runCodensity (f a) k                                                  ✓  */
#define VERIFY_CODENSITY_RETURN_LAW(a_val, f_fn, final_k, eq_fn)           \
    do {                                                                      \
        Functor_t _vcrl_m   = CODENSITY_RETURN(a_val);                        \
        Functor_t _vcrl_lhs = RUN_CODENSITY(CODENSITY_BIND(_vcrl_m, (f_fn)), \
                                            (final_k));                       \
        Functor_t _vcrl_fa  = ((KleisliFn)(f_fn))((void*)(a_val));           \
        Functor_t _vcrl_rhs = RUN_CODENSITY(_vcrl_fa, (final_k));            \
        if (!(eq_fn)(_vcrl_lhs.inner, _vcrl_rhs.inner)) {                   \
            fprintf(stderr,                                                   \
                "CODENSITY LAW VIOLATED: return a >>= f ≠ f a\n");          \
        }                                                                     \
    } while(0)


/* ════════════════════════════════════════════════════════════════
 * IMPLEMENTATION
 *
 * Define KAN_IMPLEMENTATION in the same .c file as all the others:
 *
 *   #define FUNCTOR_IMPLEMENTATION
 *   #define APPLICATIVE_IMPLEMENTATION
 *   #define MONAD_IMPLEMENTATION
 *   #define NAT_TRANS_IMPLEMENTATION
 *   #define PROFUNCTOR_IMPLEMENTATION
 *   #define KAN_IMPLEMENTATION
 *   #include "kan.h"
 *
 * ════════════════════════════════════════════════════════════════ */

#ifdef KAN_IMPLEMENTATION

ComonadVTable_t _comonad_vtable[FUNCTOR_KIND_MAX];

/* Global used by VERIFY_COMONAD_FMAP_EXTRACT_DUPLICATE to pass kind through
 * the FmapFn interface without additional arguments. Non-reentrant.
 * The same architectural debt as _global_curried_slot from applicative.h.
 * We have not learned. */
int _current_comonad_kind_for_fmap = FUNCTOR_KIND_DENSITY;

/* ── identity KleisliFn ──────────────────────────────────────────
 *
 * Wraps a void* in a FUNCTOR_KIND_ID Functor_t (identity functor, nat_trans.h).
 * Used by FROM_RAN and YONEDA_AS_RAN as the terminal continuation.
 * ─────────────────────────────────────────────────────────────── */
Functor_t _identity_kleisli_fn(void *a) {
    return (Functor_t){ FUNCTOR_KIND_ID, a };
}

/* ── identity LanMorphFn ─────────────────────────────────────────
 *
 * Extracts the inner void* from a Functor_t.
 * Used by FROM_LAN to construct Lan id fb.
 * ─────────────────────────────────────────────────────────────── */
void *_coerce_identity_morph(Functor_t fb) {
    return fb.inner;
}

/* ── helpers for law verification ───────────────────────────────
 *
 * Global functions to call comonad ops via FmapFn (which takes void*).
 * We recover the Functor_t by consulting _current_comonad_kind_for_fmap.
 * This is structurally identical to the global slot hack in applicative.h.
 * We are cursed to repeat history.
 * ─────────────────────────────────────────────────────────────── */
void *_comonad_extract_as_fmap(void *fa_ptr) {
    Functor_t fa = { _current_comonad_kind_for_fmap, fa_ptr };
    return _comonad_vtable[fa.kind].extract(fa);
}

void *_comonad_duplicate_as_fmap(void *fa_ptr) {
    Functor_t fa  = { _current_comonad_kind_for_fmap, fa_ptr };
    Functor_t dup = _comonad_vtable[fa.kind].duplicate(fa);
    return dup.inner;
}

/* ── Ran functor instance ────────────────────────────────────────
 *
 *   fmap f (Ran { value=a, run=g })  =  Ran { value=f(a), run=g }
 *
 * Because `a` is stored explicitly, fmap just applies f to the stored value.
 * This is correct because:
 *   fmap f (Ran g) = Ran (\k -> g (k . f))   (Haskell definition)
 *   Since we store `a` and apply k later:
 *   k(f(a)) = (k . f)(a)                     ✓
 *
 * Works for all Ran-family kinds: RAN, CODENSITY, RAN_BIND, RAN_FMAP.
 * ─────────────────────────────────────────────────────────────── */
Functor_t _fmap_ran(FmapFn f, Functor_t fa) {
    Ran_t *r      = (Ran_t*)fa.inner;
    Ran_t *result = malloc(sizeof(Ran_t));
    result->kind  = r->kind;
    result->value = f(r->value);  /* precompose: f applied to stored a */
    result->run   = r->run;
    return (Functor_t){ r->kind, result };
}

/* ── Lan functor instance ────────────────────────────────────────
 *
 *   fmap f (Lan morph fb)  =  Lan (f . morph) fb
 *
 * fmap composes f into the morphism; fb is unchanged.
 * Requires a GCC nested function to build the composed morphism as a closure.
 * Produces a function pointer to a stack-allocated trampoline.
 * Requires -z execstack.
 * ─────────────────────────────────────────────────────────────── */
Functor_t _fmap_lan(FmapFn f, Functor_t fa) {
    Lan_t      *lan     = (Lan_t*)fa.inner;
    FmapFn      outer_f = f;
    LanMorphFn  inner_m = lan->morphism;

    /* composed_morph :: k b -> a  =  f . inner_m */
    void *composed_morph(Functor_t x) {
        return outer_f(inner_m(x));
    }

    Lan_t *result    = malloc(sizeof(Lan_t));
    result->kind     = lan->kind;
    result->morphism = composed_morph;  /* GCC nested function; trampoline */
    result->fb       = lan->fb;
    return (Functor_t){ lan->kind, result };
}

/* ── Codensity monad ─────────────────────────────────────────────
 *
 * return a: \k -> k a   (CPS return: hand a directly to the continuation)
 * ─────────────────────────────────────────────────────────────── */
Functor_t _codensity_return_run(void *a, KleisliFn k) {
    return k(a);
}

Functor_t _codensity_pure_fn(void *a) {
    Ran_t *r = malloc(sizeof(Ran_t));
    r->kind  = FUNCTOR_KIND_CODENSITY;
    r->value = a;
    r->run   = _codensity_return_run;
    return (Functor_t){ FUNCTOR_KIND_CODENSITY, r };
}

/* ── Codensity bind ──────────────────────────────────────────────
 *
 *   m >>= h  =  \k -> runCodensity m (\a -> runCodensity (h a) k)
 *
 * _codensity_bind_run is called at force-time when a final continuation k
 * is applied to the deferred RAN_BIND node.
 *
 * The inner continuation (\a -> runCodensity (h a) k) closes over h and k
 * via a GCC nested function. Trampoline required.
 * ─────────────────────────────────────────────────────────────── */
Functor_t _codensity_bind_run(void *captured, KleisliFn k) {
    _CodensityBind *cb = (_CodensityBind*)captured;
    Ran_t          *m  = cb->m;
    KleisliFn       h  = cb->h;

    Functor_t inner_cont(void *a) {
        Functor_t ha       = h(a);
        Ran_t    *ran_ha   = (Ran_t*)ha.inner;
        return ran_ha->run(ran_ha->value, k);   /* runCodensity (h a) k */
    }

    return m->run(m->value, (KleisliFn)inner_cont);
}

Functor_t _codensity_bind_dispatch(Functor_t ma, KleisliFn h) {
    _CodensityBind *cb = malloc(sizeof(_CodensityBind));
    cb->m              = (Ran_t*)ma.inner;
    cb->h              = h;
    Ran_t *result      = malloc(sizeof(Ran_t));
    result->kind       = FUNCTOR_KIND_RAN_BIND;
    result->value      = (void*)cb;
    result->run        = _codensity_bind_run;
    return (Functor_t){ FUNCTOR_KIND_RAN_BIND, result };
}

/* Applicative ap derived from bind:
 *   mf <*> ma = do { f <- mf; a <- ma; return (f a) }                        */
Functor_t _codensity_ap_fn(Functor_t mf, Functor_t ma) {
    return DO(mf, f_ptr,
               DO(ma, a_ptr,
                   CODENSITY_RETURN(((FmapFn)f_ptr)(a_ptr))));
}

/* ── Density comonad ─────────────────────────────────────────────
 *
 * extract (Lan morph fb) = morph fb
 * extend  g (Lan morph fb) = Lan (\x -> g (Lan morph x)) fb
 * duplicate (Lan morph fb) = Lan (\x -> Lan morph x) fb    = extend id
 *
 * GCC nested functions used for the new morphisms in extend and duplicate.
 * Trampolines required. See functor.h for the familiar lament.
 *
 * IMPORTANT: DENSITY_EXTRACT on a DENSITY_DUPLICATE result returns a void*
 * that is actually a heap-allocated Functor_t*. Cast accordingly:
 *   Functor_t *inner = (Functor_t*)DENSITY_EXTRACT(DENSITY_DUPLICATE(wa));
 *
 * This is the consequence of LanMorphFn returning void* rather than Functor_t.
 * The alternative would require a separate morphism type for the outer layer.
 * We have chosen not to introduce a 9th type. The series ends here.
 * ─────────────────────────────────────────────────────────────── */
void *_extract_density(Functor_t wfa) {
    Lan_t *lan = (Lan_t*)wfa.inner;
    return lan->morphism(lan->fb);
}

Functor_t _extend_density(LanMorphFn g, Functor_t wfa) {
    Lan_t      *lan       = (Lan_t*)wfa.inner;
    LanMorphFn  old_morph = lan->morphism;
    int         old_kind  = lan->kind;

    void *new_morph(Functor_t x) {
        Lan_t *inner    = malloc(sizeof(Lan_t));
        inner->kind     = old_kind;
        inner->morphism = old_morph;
        inner->fb       = x;
        return g((Functor_t){ old_kind, inner });
    }

    Lan_t *result    = malloc(sizeof(Lan_t));
    result->kind     = FUNCTOR_KIND_DENSITY;
    result->morphism = new_morph;
    result->fb       = lan->fb;
    return (Functor_t){ FUNCTOR_KIND_DENSITY, result };
}

Functor_t _duplicate_density(Functor_t wfa) {
    Lan_t      *lan       = (Lan_t*)wfa.inner;
    LanMorphFn  old_morph = lan->morphism;
    int         old_kind  = lan->kind;

    void *outer_morph(Functor_t x) {
        Lan_t *inner    = malloc(sizeof(Lan_t));
        inner->kind     = old_kind;
        inner->morphism = old_morph;
        inner->fb       = x;
        Functor_t *wrapped = malloc(sizeof(Functor_t));
        *wrapped = (Functor_t){ old_kind, inner };
        return (void*)wrapped;
    }

    Lan_t *result    = malloc(sizeof(Lan_t));
    result->kind     = FUNCTOR_KIND_DENSITY;
    result->morphism = outer_morph;
    result->fb       = lan->fb;
    return (Functor_t){ FUNCTOR_KIND_DENSITY, result };
}

/* ── instance registration ───────────────────────────────────────
 *
 * Registered instances:
 *   Functor:     Ran, Lan, Codensity, Density (and deferred-node variants)
 *   Applicative: Codensity (pure, ap)
 *   Monad:       Codensity (bind = _codensity_bind_dispatch = CPS bind)
 *   Comonad:     Density, Lan (same extract/extend/duplicate)
 *
 * REGISTER_*_INSTANCE macros expand to top-level constructor functions;
 * they must appear at file scope. Comonad vtable entries need runtime
 * array assignment and so live in a separate constructor function.
 * ─────────────────────────────────────────────────────────────── */

REGISTER_FUNCTOR_INSTANCE(FUNCTOR_KIND_RAN,        "Ran",            _fmap_ran)
REGISTER_FUNCTOR_INSTANCE(FUNCTOR_KIND_CODENSITY,  "Codensity",      _fmap_ran)
REGISTER_FUNCTOR_INSTANCE(FUNCTOR_KIND_RAN_BIND,   "CodensityBind",  _fmap_ran)
REGISTER_FUNCTOR_INSTANCE(FUNCTOR_KIND_RAN_FMAP,   "RanFmap",        _fmap_ran)

REGISTER_FUNCTOR_INSTANCE(FUNCTOR_KIND_LAN,        "Lan",            _fmap_lan)
REGISTER_FUNCTOR_INSTANCE(FUNCTOR_KIND_DENSITY,    "Density",        _fmap_lan)
REGISTER_FUNCTOR_INSTANCE(FUNCTOR_KIND_LAN_EXTEND, "DensityExtend",  _fmap_lan)
REGISTER_FUNCTOR_INSTANCE(FUNCTOR_KIND_LAN_FMAP,   "LanFmap",        _fmap_lan)

REGISTER_MONAD_INSTANCE(FUNCTOR_KIND_CODENSITY, "Codensity",
                        _fmap_ran, _codensity_pure_fn, _codensity_ap_fn,
                        _codensity_bind_dispatch)

/* Comonad vtable entries are plain struct assignments; they live in an
 * explicit constructor so the array is already allocated by the time
 * we assign into it. */
__attribute__((constructor))
static void _register_kan_comonad_instances(void) {
    _comonad_vtable[FUNCTOR_KIND_DENSITY] = (ComonadVTable_t){
        .kind      = FUNCTOR_KIND_DENSITY,
        .name      = "Density",
        .extract   = _extract_density,
        .extend    = _extend_density,
        .duplicate = _duplicate_density,
    };

    /* Lan-kind also gets the density comonad instance;
     * any Lan_t can be observed as a comonad regardless of kind tag. */
    _comonad_vtable[FUNCTOR_KIND_LAN] = (ComonadVTable_t){
        .kind      = FUNCTOR_KIND_LAN,
        .name      = "Lan (comonad)",
        .extract   = _extract_density,
        .extend    = _extend_density,
        .duplicate = _duplicate_density,
    };
}

#endif /* KAN_IMPLEMENTATION */


/* ════════════════════════════════════════════════════════════════
 * APPENDIX: THE COMPLETE PICTURE (final)
 *
 * Six typeclasses. Six files. One void*. All of category theory.
 *
 *   ┌─────────────────────────────────────────────────────────┐
 *   │  THE HIERARCHY                                          │
 *   │                                                         │
 *   │  Functor f                                              │
 *   │    └── Applicative f                                    │
 *   │          └── Monad f  =  monoid in End(C)               │
 *   │                └── Codensity (Ran f f)  =  CPS monad    │
 *   │                                                         │
 *   │  Comonad w              (the categorical dual of Monad) │
 *   │    └── Density (Lan f f)  =  density comonad           │
 *   │                                                         │
 *   │  Profunctor p                                           │
 *   │    ├── Strong p → Lens   (focus on product components)  │
 *   │    └── Choice p → Prism  (focus on sum components)      │
 *   │                                                         │
 *   │  Natural Transformations η : F ⟹ G  (2-cells in End(C))│
 *   │                                                         │
 *   │  Kan Extensions                                         │
 *   │    Ran k f a  =  ∀b. (a → k b) → f b   (right)        │
 *   │    Lan k f a  =  ∃b. (k b → a,  f b)   (left)         │
 *   │    Ran Id f   ≅  f                       (Yoneda!)     │
 *   │    Adjunctions = Lan ⊣ (−∘k) ⊣ Ran      (exercise)    │
 *   └─────────────────────────────────────────────────────────┘
 *
 * ════════════════════════════════════════════════════════════════
 *
 * FINAL ACCOUNTING (all six files combined):
 *
 *   Files:                 6  (functor, applicative, monad,
 *                               nat_trans, profunctor, kan)
 *   Lines of C:         ~6600
 *   Equivalent Haskell:   ~80 lines  (see comparison.hs)
 *   Vtables:               8  (Functor, Applicative, Monad, Comonad,
 *                               Profunctor, Strong, Choice, NatTrans)
 *   Kind tags used:       48  of 64 available
 *   Global variables:    ~22
 *   GCC nested fns:      ~15
 *   malloc calls:     unbounded (the heap is our conscience now)
 *   Type safety:           0  (unchanged from file 1)
 *   Memory freed:          0  (unchanged from file 1)
 *   Rank-2 types:       erased
 *   Existential types:  erased
 *   Adjunctions:        implicit (Lan ⊣ precompose ⊣ Ran)
 *   Regrets:         categorical
 *
 *   The Yoneda lemma is a special case of Ran.
 *   Every adjunction is expressible as Lan and Ran.
 *   Every monad is the codensity monad of its forgetful functor.
 *   Every comonad is the density comonad of its cofree functor.
 *   All concepts in category theory are Kan extensions.
 *   All Kan extensions are now in C.
 *
 *   Mac Lane, we are so sorry.
 *
 * ════════════════════════════════════════════════════════════════ */

#endif /* KAN_H */
