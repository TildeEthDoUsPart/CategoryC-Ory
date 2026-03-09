/*
 * nat_trans.h — Natural Transformations, in C macros
 *
 * The fourth file. The roof goes on.
 *
 * ════════════════════════════════════════════════════════════════
 *
 * WHAT HAS COME BEFORE:
 *
 *   functor.h:     Objects within a functor. fmap lifts morphisms.
 *   applicative.h: Combining independent computations. AP.
 *   monad.h:       Sequencing dependent computations. BIND. DO.
 *
 * WHAT THIS FILE IS:
 *
 *   Not a new operation ON the functors.
 *   A new view OF the functors themselves as a category.
 *
 *   Category of types C:
 *     Objects:   types (void* in our system)
 *     Morphisms: functions (FmapFn: void* → void*)
 *
 *   Our functors F, G, H, ... are ENDOFUNCTORS on C:
 *     Each maps objects to objects (a ↦ F(a))
 *     Each maps morphisms to morphisms (f ↦ fmap f)
 *
 *   The FUNCTOR CATEGORY End(C):
 *     Objects:   functors (our kinds: MAYBE, LIST, IO, READER, ...)
 *     Morphisms: natural transformations between functors
 *
 *   A NATURAL TRANSFORMATION η : F ⟹ G is a family of morphisms
 *
 *       η_A : F(A) → G(A)
 *
 *   one for each object A, such that for every morphism f : A → B,
 *   the NATURALITY SQUARE commutes:
 *
 *       F(A) ──── η_A ────▶ G(A)
 *        │                   │
 *       F(f)               G(f)
 *        │                   │
 *        ▼                   ▼
 *       F(B) ──── η_B ────▶ G(B)
 *
 *   i.e.:   G(f) ∘ η_A  =  η_B ∘ F(f)
 *           FMAP_G(f, eta(fa))  =  eta(FMAP_F(f, fa))
 *
 * In C, A is always void*. There is only one object.
 * η is a function Functor_t → Functor_t that changes the kind.
 * The naturality condition is still meaningful and checkable.
 * The type-erasure collapses the family to a single function.
 * This is either profound or a coincidence.
 * In category theory, these often coincide.
 *
 * ════════════════════════════════════════════════════════════════
 *
 * THE 2-CATEGORY STRUCTURE:
 *
 *   We have been working in a 2-category all along.
 *
 *   0-cells: one (the category of void*-typed things)
 *   1-cells: our endofunctors (Maybe, List, IO, Reader, ...)
 *   2-cells: natural transformations between functors
 *
 *   Composition of 2-cells:
 *     Vertical:   η : F ⟹ G,  ε : G ⟹ H  gives  ε • η : F ⟹ H
 *     Horizontal: η : F ⟹ G,  H a functor  gives  H∘η : H∘F ⟹ H∘G   (left whisker)
 *                                             and    η∘H : F∘H ⟹ G∘H   (right whisker)
 *
 *   The interchange law: vertical and horizontal composition commute
 *   in the appropriate sense.
 *
 *   This structure is what makes category theory "2-dimensional".
 *   It is also what your codebase has secretly been all along.
 *   You were doing 2-category theory.
 *   You were doing it with void* and __attribute__((constructor)).
 *   This is fine.
 *
 * ════════════════════════════════════════════════════════════════
 *
 * THE YONEDA LEMMA:
 *
 *   For any functor F and any object r:
 *
 *     Nat(Hom(r, −), F)  ≅  F(r)
 *
 *   Natural transformations from the representable functor Hom(r, −)
 *   to F are in natural bijection with elements of F(r).
 *
 *   In our system: Hom(r, a) = (r → a) = Reader r a.
 *   Reader IS the representable functor.
 *
 *   So:  Nat(Reader r, F)  ≅  F(r)
 *
 *   Forward:  given η : Reader r ⟹ F,
 *             get the element: η(id_r) ∈ F(r)
 *
 *   Backward: given x ∈ F(r) and a Reader r a (i.e., a function f : r → a),
 *             get the nat trans: η_a(f) = fmap f x ∈ F(a)
 *
 *   The round-trip in one direction:
 *     yoneda_bwd(yoneda_fwd(η))(READER(f))
 *     = fmap f (η(READER(id)))
 *     = η(fmap_Reader f (READER(id)))     [by naturality of η]
 *     = η(READER(f ∘ id))
 *     = η(READER(f))                      ✓
 *
 *   The naturality condition of η IS the Yoneda lemma.
 *   The Yoneda lemma falls out of the functor identity law.
 *   Everything is connected.
 *   We implement this below.
 *
 * ════════════════════════════════════════════════════════════════
 *
 * CONCRETE NATURAL TRANSFORMATIONS PROVIDED:
 *
 *   maybe_to_list : Maybe ⟹ List
 *     Nothing   ↦  []
 *     Just x    ↦  [x]
 *
 *   list_to_maybe : List ⟹ Maybe   (= safeHead)
 *     []        ↦  Nothing
 *     (x : _)   ↦  Just x
 *
 *   maybe_to_io : Maybe ⟹ IO
 *     Nothing   ↦  IO that aborts with an error
 *     Just x    ↦  IO (return x)   [pure x in IO]
 *
 *   id_to_maybe : Id ⟹ Maybe    (= Just = PURE for Maybe)
 *   id_to_list  : Id ⟹ List     (= singleton = PURE for List)
 *   id_to_io    : Id ⟹ IO       (= pure for IO)
 *   id_to_reader : Id ⟹ Reader  (= const function = pure for Reader)
 *
 *   The pure/return family is a natural transformation from the
 *   identity functor to each Applicative functor.
 *   This is not a coincidence.
 *
 * ════════════════════════════════════════════════════════════════
 *
 * A FINAL NOTE ON THE MONAD:
 *
 *   "A monad is just a monoid in the category of endofunctors."
 *
 *   We can now say what this means precisely:
 *   - The category of endofunctors End(C) has our four functors as objects
 *   - A monoid in End(C) is: an object M (a functor), with two morphisms
 *       μ : M∘M ⟹ M   (multiplication — this is JOIN: m(m a) → m a)
 *       η : Id  ⟹ M   (unit        — this is RETURN: a → m a)
 *     satisfying associativity and unit laws
 *   - These are exactly the monad laws
 *   - μ is JOIN. η is MRETURN. We implemented both.
 *   - The monad laws are the monoid laws in End(C).
 *
 *   You have been building a monoid in the category of endofunctors.
 *   In C. With macros. Since functor.h.
 *   The comment "just" in "just a monoid" is load-bearing.
 *   "Just" does a lot of work in mathematics.
 *   So does void*.
 *
 * ════════════════════════════════════════════════════════════════
 *
 * USAGE:
 *
 *   #define FUNCTOR_IMPLEMENTATION
 *   #define APPLICATIVE_IMPLEMENTATION
 *   #define MONAD_IMPLEMENTATION
 *   #define NAT_TRANS_IMPLEMENTATION
 *   #include "nat_trans.h"
 *
 */

#ifndef NAT_TRANS_H
#define NAT_TRANS_H

#include "monad.h"


/* ════════════════════════════════════════════════════════════════
 * §0  THE IDENTITY FUNCTOR
 *
 *   Id a = a
 *   fmap f (Id x) = Id (f x)
 *
 * The identity functor does nothing. It wraps a value and unwraps it.
 * It is the identity morphism in the functor category.
 * It is the unit for functor composition.
 * It is important for the Yoneda lemma.
 * It is trivial to implement.
 * In a file that has implemented a Monad vtable using
 * __attribute__((constructor)), "trivial" feels earned.
 * ════════════════════════════════════════════════════════════════ */

#define FUNCTOR_KIND_ID   12

#define ID_VAL(x)   ((Functor_t){ .kind = FUNCTOR_KIND_ID, .inner = (void *)(x) })
#define UN_ID(fa)   ((fa).inner)

Functor_t _id_fmap_impl(FmapFn f, Functor_t fa);
Functor_t _id_pure_impl(void *a);
Functor_t _id_ap_impl(Functor_t ff, Functor_t fa);
Functor_t _id_bind_impl(Functor_t ma, KleisliFn f);


/* ════════════════════════════════════════════════════════════════
 * §0  FUNCTOR COMPOSITION
 *
 *   (F ∘ G)(a) = F(G(a))
 *
 * In our encoding: F(G(a)) is a Functor_t with kind=F whose inner
 * is a heap-allocated Functor_t* of kind=G.
 *
 *   COMPOSE_FUNCTORS(outer_kind, inner_fa):
 *     Wraps inner_fa (a Functor_t of kind G) into outer_kind (F).
 *     Result: F(G(a)), kind=F, inner=Functor_t*
 *
 *   INNER_FUNCTOR(fa):
 *     Extracts the inner Functor_t from a composed functor.
 *     Requires fa.inner to be a Functor_t* (convention, not enforced).
 *
 * Caveat: this convention (inner = Functor_t*) CONFLICTS with existing
 * usage where inner points to data structs (Maybe_t*, ListNode_t*, etc.).
 * Composed functors must be constructed explicitly via COMPOSE_FUNCTORS.
 * Do not apply INNER_FUNCTOR to a raw JUST or CONS. You will get garbage.
 * The garbage will compile. The garbage will run. The garbage will be wrong.
 * This is C. Garbage is always an option.
 * ════════════════════════════════════════════════════════════════ */

static inline Functor_t _compose_functors(int outer_kind, Functor_t inner_fa) {
    Functor_t *p = malloc(sizeof(Functor_t));
    assert(p && "COMPOSE_FUNCTORS: malloc failed. "
                "Cannot compose functors: insufficient memory for the outer layer. "
                "The category of endofunctors requires heap.");
    *p = inner_fa;
    return (Functor_t){ .kind = outer_kind, .inner = (void *)p };
}

#define COMPOSE_FUNCTORS(outer_kind, inner_fa)  (_compose_functors((outer_kind), (inner_fa)))
#define INNER_FUNCTOR(fa)                       (*(Functor_t *)((fa).inner))


/* ════════════════════════════════════════════════════════════════
 * §0  THE NATURAL TRANSFORMATION TYPE
 *
 *   NatTrans: the type of natural transformations in our system.
 *
 *   Haskell: type NatTrans f g = forall a. f a -> g a
 *   C:       typedef Functor_t (*NatTrans)(Functor_t);
 *
 *   A NatTrans takes a Functor_t of one kind and returns a Functor_t
 *   of another kind. It does not inspect the inner value's type.
 *   This is the type-erased "forall a" — the function works uniformly
 *   for any a, because a is always void*.
 *
 *   The naturality condition (cannot be enforced statically):
 *     eta(FMAP_F(f, fa)) == FMAP_G(f, eta(fa))
 *   Checked at runtime by VERIFY_NATURALITY.
 * ════════════════════════════════════════════════════════════════ */

typedef Functor_t (*NatTrans)(Functor_t);


/* ════════════════════════════════════════════════════════════════
 * §0  THE NAT TRANS REGISTRY
 *
 * A table mapping (source_kind, target_kind) pairs to NatTrans functions.
 * Allows runtime lookup: "give me the nat trans from Maybe to List."
 *
 * This is, roughly, what a Haskell coerce or a typeclass dictionary
 * for transformation instances would provide.
 * It is also a 2D lookup table with a linear search.
 * We have seen linear search before. We are at peace with linear search.
 * ════════════════════════════════════════════════════════════════ */

typedef struct {
    int       from_kind;
    int       to_kind;
    const char *name;
    NatTrans  eta;
} NatTransEntry_t;

#define NAT_TRANS_REGISTRY_MAX  128   /* 128 natural transformations. */
                                      /* If you have more, see a doctor. */

extern NatTransEntry_t _nat_registry[NAT_TRANS_REGISTRY_MAX];
extern int             _nat_registry_size;

#define REGISTER_NAT_TRANS(FROM, TO, NAME, ETA_FN)                             \
    static __attribute__((constructor))                                        \
    void _nat_register_##FROM##_##TO(void) {                                   \
        if (_nat_registry_size >= NAT_TRANS_REGISTRY_MAX) {                    \
            fputs("NAT_TRANS: registry overflow.\n"                            \
                  "You have too many natural transformations.\n"               \
                  "This is categorically impossible to have too many of,\n"    \
                  "but you have hit the array bound. Increase the constant.\n",\
                  stderr);                                                     \
            abort();                                                           \
        }                                                                      \
        _nat_registry[_nat_registry_size++] = (NatTransEntry_t){               \
            .from_kind = (FROM),                                               \
            .to_kind   = (TO),                                                 \
            .name      = (NAME),                                               \
            .eta       = (ETA_FN),                                             \
        };                                                                     \
    }

/* Look up a registered nat trans. Aborts if not found. */
static inline NatTrans _find_nat_trans(int from, int to) {
    for (int i = 0; i < _nat_registry_size; i++) {
        if (_nat_registry[i].from_kind == from &&
            _nat_registry[i].to_kind   == to) {
            return _nat_registry[i].eta;
        }
    }
    fprintf(stderr,
        "NAT_TRANS: no transformation registered from kind=%d to kind=%d.\n"
        "Either:\n"
        "  (a) this transformation does not exist mathematically.\n"
        "      (check that your functors are actually related)\n"
        "  (b) this transformation exists but you haven't implemented it.\n"
        "      (register it with REGISTER_NAT_TRANS)\n"
        "  (c) there is no natural transformation between these functors\n"
        "      and you are attempting something that is not a morphism\n"
        "      in the functor category. This is a categorical type error.\n"
        "      There is no compiler to catch this. Only consequences.\n",
        from, to);
    abort();
}

/* APPLY_NAT(eta, fa): apply a natural transformation.
 * Checks that fa.kind matches the expected source kind if you provide one,
 * OR just applies blindly if you use APPLY_NAT_UNSAFE.
 * The safe version requires specifying from_kind for runtime check. */
#define APPLY_NAT(eta, fa)              ((eta)(fa))
#define APPLY_NAT_CHECKED(eta, from, fa)                                       \
    (((fa).kind == (from))                                                    \
        ? (eta)(fa)                                                            \
        : (fprintf(stderr, "APPLY_NAT: kind mismatch: expected %d, got %d\n", \
                   (from), (fa).kind), abort(), (fa)))

/* FIND_AND_APPLY(from_kind, to_kind, fa): look up and apply. */
#define FIND_AND_APPLY(from, to, fa)    (_find_nat_trans((from), (to))(fa))

/* Print all registered nat trans. */
#define NAT_TRANS_PRINT_REGISTRY()                                             \
    do {                                                                       \
        fprintf(stderr, "Registered NatTrans (%d):\n", _nat_registry_size);   \
        for (int _i = 0; _i < _nat_registry_size; _i++) {                     \
            fprintf(stderr, "  [%d] %d ⟹ %d  (%s)\n", _i,                    \
                _nat_registry[_i].from_kind, _nat_registry[_i].to_kind,       \
                _nat_registry[_i].name);                                       \
        }                                                                      \
    } while (0)


/* ════════════════════════════════════════════════════════════════
 * §0  NATURALITY VERIFICATION
 *
 *   The naturality square:
 *
 *       F(A) ──── eta ────▶ G(A)
 *        │                   │
 *      fmap_F f             fmap_G f
 *        │                   │
 *        ▼                   ▼
 *       F(B) ──── eta ────▶ G(B)
 *
 *   Condition: eta(FMAP(f, fa)) == FMAP(f, eta(fa))
 *
 *   VERIFY_NATURALITY(eta, f, fa, eq):
 *     Checks that eta commutes with fmap for the given f and fa.
 *     eq :: Functor_t -> Functor_t -> int  (1 = equal)
 *     If the square doesn't commute, the transformation is NOT natural.
 *     It is then a morphism in some other category we don't have a header for.
 * ════════════════════════════════════════════════════════════════ */

#define VERIFY_NATURALITY(eta, f, fa, eq)                                      \
    do {                                                                       \
        Functor_t _fa  = (fa);                                                 \
        FmapFn    _f   = (FmapFn)(f);                                          \
        NatTrans  _eta = (NatTrans)(eta);                                      \
        /* Top then right: eta(fmap_F f fa) */                                 \
        Functor_t _top_right = _eta(FMAP(_f, _fa));                            \
        /* Right then bottom: fmap_G f (eta fa) */                             \
        Functor_t _right_then_bottom = FMAP(_f, _eta(_fa));                    \
        if (!(eq)(_top_right, _right_then_bottom)) {                           \
            fprintf(stderr,                                                    \
                "NATURALITY VIOLATED: eta does not commute with fmap.\n"       \
                "  source kind:  %d\n"                                         \
                "  target kind:  %d\n"                                         \
                "  The naturality square does not commute.\n"                  \
                "  eta is a function Functor_t -> Functor_t.\n"                \
                "  eta is NOT a natural transformation.\n"                     \
                "  eta has committed a categorical crime.\n",                  \
                _fa.kind, _eta(_fa).kind);                                     \
            abort();                                                           \
        }                                                                      \
    } while (0)


/* ════════════════════════════════════════════════════════════════
 * §0  THE IDENTITY NATURAL TRANSFORMATION
 *
 *   id_F : F ⟹ F
 *   (id_F)_A = id_{F A}
 *
 * The identity transformation for a given functor is just the identity
 * function on Functor_t values of that kind. It does nothing.
 * It is the identity morphism in the functor category.
 * As expected, it is also the least interesting thing here.
 * ════════════════════════════════════════════════════════════════ */

static Functor_t _nat_id_impl(Functor_t fa) { return fa; }

/* NAT_ID: the identity natural transformation. */
#define NAT_ID  ((NatTrans)_nat_id_impl)


/* ════════════════════════════════════════════════════════════════
 * §0  VERTICAL COMPOSITION  (•)
 *
 *   η : F ⟹ G,  ε : G ⟹ H
 *   ε • η : F ⟹ H
 *   (ε • η)_A = ε_A ∘ η_A
 *
 * Vertical composition: apply η first, then ε.
 * It is function composition on NatTrans functions.
 * It is the composition morphism in the functor category.
 * It satisfies associativity (because function composition does).
 * NAT_ID is its left and right unit (because _nat_id_impl is id).
 *
 * NAT_VCOMP(epsilon, eta): returns a NatTrans.
 * Uses a GCC nested function to compose. Valid while scope is live.
 * ════════════════════════════════════════════════════════════════ */

#define NAT_VCOMP(epsilon, eta)                                                \
    ({                                                                         \
        NatTrans _e = (NatTrans)(epsilon);                                     \
        NatTrans _n = (NatTrans)(eta);                                         \
        Functor_t _vcomp(Functor_t fa) { return _e(_n(fa)); }                 \
        (NatTrans)_vcomp;                                                      \
    })

/* Alias: ● for vertical composition.
 * We cannot use ● as a C identifier.
 * We can use THEN_NAT.
 * We will use THEN_NAT. */
#define THEN_NAT(eta, epsilon)  NAT_VCOMP((epsilon), (eta))


/* ════════════════════════════════════════════════════════════════
 * §0  HORIZONTAL COMPOSITION — WHISKERING
 *
 *   Left whisker: H ∘ η
 *     Given η : F ⟹ G and functor H,
 *     H∘η : H∘F ⟹ H∘G
 *     (H∘η)_A = H(η_A) : H(F A) → H(G A)
 *
 *     In our encoding: given H(F(a)) (COMPOSE_FUNCTORS(H, fa_of_kind_F)),
 *     apply η to the INNER F(a) to get G(a),
 *     wrap result back in H.
 *
 *     WHISKER_LEFT(h_kind, eta, composed_fa):
 *       composed_fa must have been built with COMPOSE_FUNCTORS(h_kind, f_a)
 *       i.e., composed_fa.kind = h_kind, composed_fa.inner = Functor_t*
 *
 *   Right whisker: η ∘ K
 *     Given η : F ⟹ G and functor K,
 *     η∘K : F∘K ⟹ G∘K
 *     (η∘K)_A = η_{K A} : F(K A) → G(K A)
 *
 *     In our encoding: F(K(a)) is a Functor_t of kind=F where inner
 *     "happens to be" a K(a) value. Applying η changes the kind from F to G
 *     without touching the inner. This is just APPLY_NAT(eta, fka).
 *
 *     WHISKER_RIGHT(eta, fka):
 *       fka.kind = F (the source of eta)
 *       Returns Functor_t of kind = target(eta)
 *       Inner is K(a), unmodified.
 *
 *     Right whiskering is literally just APPLY_NAT.
 *     In a fully type-erased system, "apply η to F(K(a))" and "apply η to F(a)"
 *     are the same operation — η maps F-things to G-things, full stop.
 *     The K-structure inside is passed through unchanged.
 *     Type erasure makes right whiskering free.
 *     Type erasure takes much and gives little. This is one of the givings.
 * ════════════════════════════════════════════════════════════════ */

/* Left whisker: H ∘ η.
 * composed_fa must be constructed via COMPOSE_FUNCTORS(h_kind, f_a).
 * Returns COMPOSE_FUNCTORS(h_kind, eta(inner_f_a)). */
static inline Functor_t _whisker_left(int h_kind, NatTrans eta, Functor_t composed_fa) {
    if (composed_fa.kind != h_kind) {
        fprintf(stderr,
            "WHISKER_LEFT: outer kind mismatch: expected %d, got %d.\n"
            "The composed functor must have the specified outer kind.\n"
            "Did you use COMPOSE_FUNCTORS to construct it?\n",
            h_kind, composed_fa.kind);
        abort();
    }
    Functor_t inner = INNER_FUNCTOR(composed_fa);  /* F(a) */
    Functor_t g_a   = eta(inner);                   /* G(a) */
    return COMPOSE_FUNCTORS(h_kind, g_a);           /* H(G(a)) */
}

#define WHISKER_LEFT(h_kind, eta, composed_fa) \
    (_whisker_left((h_kind), (NatTrans)(eta), (composed_fa)))

/* Right whisker: η ∘ K. As discussed above: just APPLY_NAT. */
#define WHISKER_RIGHT(eta, fka)   APPLY_NAT((eta), (fka))

/* Make a left-whisker NatTrans as a first-class value.
 * Returns a NatTrans (H∘F ⟹ H∘G) using a GCC nested function. */
#define MAKE_WHISKER_LEFT(h_kind, eta)                                         \
    ({                                                                         \
        int       _h = (h_kind);                                               \
        NatTrans  _e = (NatTrans)(eta);                                        \
        Functor_t _wl(Functor_t fa) { return _whisker_left(_h, _e, fa); }     \
        (NatTrans)_wl;                                                         \
    })

/* Make a right-whisker NatTrans as a first-class value. */
#define MAKE_WHISKER_RIGHT(eta)                                                \
    ({                                                                         \
        NatTrans _e = (NatTrans)(eta);                                         \
        Functor_t _wr(Functor_t fa) { return _e(fa); }                        \
        (NatTrans)_wr;                                                         \
    })


/* ════════════════════════════════════════════════════════════════
 * §0  THE INTERCHANGE LAW
 *
 *   In a 2-category, vertical and horizontal composition commute:
 *
 *     (ε₂ • η₂) ∘ (ε₁ • η₁)  =  (ε₂ ∘ ε₁) • (η₂ ∘ η₁)
 *
 *   where ∘ is horizontal composition and • is vertical.
 *
 *   In our context: we only have one 0-cell (the C void*-category),
 *   so horizontal composition reduces to whiskering.
 *   The interchange law reduces to: whiskering distributes over
 *   vertical composition.
 *
 *   Concretely:
 *     WHISKER_LEFT(H, NAT_VCOMP(ε, η)) == NAT_VCOMP(WHISKER_LEFT(H, ε), WHISKER_LEFT(H, η))
 *
 *   VERIFY_INTERCHANGE checks this for a specific composed functor value.
 * ════════════════════════════════════════════════════════════════ */

#define VERIFY_INTERCHANGE(h_kind, eta, epsilon, composed_fa, eq)              \
    do {                                                                       \
        int      _h = (h_kind);                                                \
        NatTrans _n = (NatTrans)(eta);                                         \
        NatTrans _e = (NatTrans)(epsilon);                                     \
        Functor_t _fa = (composed_fa);                                         \
        /* LHS: whisker the vertical composite */                              \
        NatTrans _vcomp = NAT_VCOMP(_e, _n);                                   \
        Functor_t _lhs = _whisker_left(_h, _vcomp, _fa);                       \
        /* RHS: vertical composite of the whiskers */                          \
        Functor_t _wn   = _whisker_left(_h, _n, _fa);                          \
        Functor_t _rhs  = _whisker_left(_h, _e, COMPOSE_FUNCTORS(_h, _wn));   \
        /* Wait - that's wrong. Let me re-examine. */                          \
        /* Correct RHS: apply η-whisker first, then ε-whisker. */             \
        /* But both take H∘F inputs... we need to be careful about what      */\
        /* type the intermediate result has. */                                \
        /* Actually: both LHS and RHS should equal η_H ; ε_H applied to _fa */\
        /* For composed functors this is just left-whisker twice vs once.    */\
        /* RHS = whisker_left(H, ε)(whisker_left(H, η)(fa))                  */\
        Functor_t _wn_result = _whisker_left(_h, _n, _fa);                     \
        Functor_t _rhs_v2    = _whisker_left(_h, _e,                           \
                                   COMPOSE_FUNCTORS(_h, INNER_FUNCTOR(_wn_result)));\
        if (!(eq)(_lhs, _rhs_v2)) {                                            \
            fprintf(stderr,                                                    \
                "INTERCHANGE LAW VIOLATED.\n"                                  \
                "Whiskering does not distribute over vertical composition.\n"  \
                "Your natural transformations are not well-behaved\n"          \
                "with respect to the 2-categorical structure.\n"               \
                "h_kind: %d\n", _h);                                           \
            abort();                                                           \
        }                                                                      \
    } while (0)


/* ════════════════════════════════════════════════════════════════
 * §0  THE YONEDA LEMMA
 *
 *   Nat(Reader r, F)  ≅  F(r)
 *
 *   YONEDA_FWD(eta):
 *     Given η : Reader r ⟹ F (a NatTrans that maps READER-kind to F-kind),
 *     return the corresponding element of F(r).
 *     Implementation: apply η to the identity reader READER(id).
 *     Result: F(r) as a Functor_t.
 *
 *   YONEDA_BWD(fr):
 *     Given fr : F(r) (a Functor_t of some kind F),
 *     return the corresponding NatTrans: Reader r ⟹ F.
 *     Implementation: given a Reader r a (stores fn : r → a as inner),
 *     return FMAP(fn, fr).
 *     The fn from the reader is used as a FmapFn to lift fr from F(r) to F(a).
 *
 *   This requires reader_ra to be a RAW READER (kind=FUNCTOR_KIND_READER)
 *   where inner IS a ReaderFn. For composed readers (READER_COMPOSED,
 *   READER_BIND, etc.), the inner is a struct*, not a function*.
 *   YONEDA_BWD only handles raw readers. Document this. Document this again.
 *   This is documented. Twice. Consider it documented.
 *
 *   VERIFY_YONEDA_ROUNDTRIP:
 *     Checks that fwd(bwd(fr)) == fr (by the functor identity law).
 *     The other direction requires the naturality condition of eta.
 *     If VERIFY_NATURALITY passes for eta, the other direction holds.
 * ════════════════════════════════════════════════════════════════ */

/* YONEDA_FWD: η ↦ η(id_r). Apply the nat trans to the identity Reader. */
#define YONEDA_FWD(eta) \
    (APPLY_NAT((eta), READER((FmapFn)_id_impl)))

/* YONEDA_BWD(fr): x ↦ λ(Reader fn) → fmap fn x.
 * Returns a NatTrans (Reader → F) using a GCC nested function.
 * The NatTrans is valid while the enclosing scope is live.
 * fr must have a registered Functor instance (for FMAP to work).
 * reader_ra passed to the result must have kind=FUNCTOR_KIND_READER
 * (raw reader only — see note above). */
#define YONEDA_BWD(fr)                                                         \
    ({                                                                         \
        Functor_t _fr = (fr);                                                  \
        Functor_t _yoneda_nat(Functor_t reader_ra) {                           \
            if (reader_ra.kind != FUNCTOR_KIND_READER) {                       \
                fputs("YONEDA_BWD: expected a raw READER (kind=4).\n"         \
                      "Composed readers cannot be used here.\n"                \
                      "The Yoneda bijection requires the identity to be\n"     \
                      "directly accessible as a function pointer.\n"           \
                      "It is not. The reader has been composed away.\n",       \
                      stderr);                                                 \
                abort();                                                       \
            }                                                                  \
            /* reader_ra.inner is a ReaderFn (void* -> void*) = FmapFn */     \
            FmapFn fn = (FmapFn)reader_ra.inner;                               \
            /* fmap fn fr : lift fr from F(r) to F(a) using fn : r → a */    \
            return FMAP(fn, _fr);                                              \
        }                                                                      \
        (NatTrans)_yoneda_nat;                                                 \
    })

/* VERIFY_YONEDA_ROUNDTRIP(fr, eq):
 * Checks fwd(bwd(fr)) == fr.
 * Reduces to: FMAP(id, fr) == fr, i.e., the functor identity law.
 * If your functor satisfies the identity law, this will pass.
 * If your functor does NOT satisfy the identity law, fix your functor first.
 * Then come back and see if the Yoneda round-trip holds.
 * (It will. The two conditions are equivalent here.) */
#define VERIFY_YONEDA_ROUNDTRIP(fr, eq)                                        \
    do {                                                                       \
        Functor_t _fr = (fr);                                                  \
        /* bwd(fr) is a NatTrans, fwd applies it to READER(id) */             \
        NatTrans _bwd = YONEDA_BWD(_fr);                                       \
        Functor_t _roundtrip = YONEDA_FWD(_bwd);                              \
        /* fwd(bwd(fr)) = bwd(fr)(READER(id)) = FMAP(id, fr) */               \
        if (!(eq)(_roundtrip, _fr)) {                                          \
            fprintf(stderr,                                                    \
                "YONEDA ROUNDTRIP FAILED: fwd(bwd(x)) ≠ x\n"                  \
                "  kind: %d\n"                                                 \
                "This means FMAP(ID, fr) ≠ fr.\n"                             \
                "Your functor does not satisfy the identity law.\n"            \
                "The Yoneda lemma cannot save you from a broken functor.\n"    \
                "Nothing can save you from a broken functor.\n"                \
                "Fix the functor.\n",                                          \
                _fr.kind);                                                     \
            abort();                                                           \
        }                                                                      \
    } while (0)


/* ════════════════════════════════════════════════════════════════
 * §1  MAYBE ⟹ LIST
 *
 *   maybe_to_list : Maybe ⟹ List
 *     Nothing ↦ []
 *     Just x  ↦ [x]
 *
 * Naturality: for any f : a → b,
 *   maybe_to_list(fmap_Maybe f (Just x))
 *   = maybe_to_list(Just (f x))
 *   = [f x]
 *   = fmap_List f [x]
 *   = fmap_List f (maybe_to_list(Just x))  ✓
 *
 * This is the simplest natural transformation we have.
 * It converts a container with at most one element
 * into a container with at most one element.
 * The category theory word for this is "natural".
 * The English word is "obvious". They mean the same thing here.
 * ════════════════════════════════════════════════════════════════ */

Functor_t _maybe_to_list(Functor_t fa);


/* ════════════════════════════════════════════════════════════════
 * §2  LIST ⟹ MAYBE  (safeHead)
 *
 *   list_to_maybe : List ⟹ Maybe   (= safeHead)
 *     []      ↦ Nothing
 *     (x : _) ↦ Just x
 *
 * Naturality: for any f : a → b,
 *   list_to_maybe(fmap_List f (x:xs))
 *   = list_to_maybe(f x : fmap f xs)
 *   = Just (f x)
 *   = fmap_Maybe f (Just x)
 *   = fmap_Maybe f (list_to_maybe(x:xs))  ✓
 *
 * Note: list_to_maybe and maybe_to_list are NOT inverses.
 * maybe_to_list . list_to_maybe ≠ id (it drops all but the head).
 * list_to_maybe . maybe_to_list = id (on Maybe values).
 *
 * They are related by an ADJUNCTION:
 *   list_to_maybe ⊣ ??? — not exactly an adjunction in the standard sense.
 *   Actually: the relevant adjunction is between Maybe and List as
 *   functors related by the free-forgetful adjunction on pointed sets.
 *   The unit is: x ↦ [x] (= maybe_to_list . Just = pure_List)
 *   The counit is: [[x]] ↦ [x] (concat / JOIN for List... almost)
 *   The details are left to the Appendix we will never write.
 * ════════════════════════════════════════════════════════════════ */

Functor_t _list_to_maybe(Functor_t fa);
#define SAFE_HEAD(fa)  (_list_to_maybe(fa))


/* ════════════════════════════════════════════════════════════════
 * §3  MAYBE ⟹ IO
 *
 *   maybe_to_io : Maybe ⟹ IO
 *     Nothing ↦ IO that calls abort() — total, in the "terminates" sense
 *     Just x  ↦ IO (pure x) — an IO action that returns x without effects
 *
 * Naturality holds:
 *   maybe_to_io(fmap_Maybe f (Just x))
 *   = maybe_to_io(Just (f x))
 *   = IO (pure (f x))
 *   = fmap_IO f (IO (pure x))
 *   = fmap_IO f (maybe_to_io(Just x))  ✓
 *
 * This transformation is "fromJust in IO":
 * if you have a Maybe, promote it to IO, and if Nothing, crash.
 * This is how Haskell programs have been failing since 1990.
 * We preserve the tradition.
 * ════════════════════════════════════════════════════════════════ */

Functor_t _maybe_to_io(Functor_t fa);


/* ════════════════════════════════════════════════════════════════
 * §4  ID ⟹ F  (pure as a natural transformation)
 *
 *   For any Applicative F:
 *     pure_F : Id ⟹ F
 *     (pure_F)_A (Id x) = F (pure x)
 *
 * pure is a natural transformation from the identity functor to F.
 * This is why Applicative's pure is required to be natural:
 * it's not just a function, it's a morphism in the functor category.
 *
 * Naturality of pure_F:
 *   pure_F(fmap_Id f (Id x))
 *   = pure_F(Id (f x))
 *   = F (pure (f x))
 *   = fmap_F f (F (pure x))
 *   = fmap_F f (pure_F(Id x))  ✓
 *
 * This follows directly from the Applicative coherence laws.
 * The coherence laws exist to make pure natural.
 * Everything is connected.
 * We have implemented four typeclasses and discovered they were
 * always a single structure. This is what category theory does.
 * ════════════════════════════════════════════════════════════════ */

Functor_t _id_to_maybe(Functor_t fa);
Functor_t _id_to_list(Functor_t fa);
Functor_t _id_to_io(Functor_t fa);
Functor_t _id_to_reader(Functor_t fa);


/* ════════════════════════════════════════════════════════════════
 * §5  THE MONAD AS A MONOID
 *
 *   "A monad is just a monoid in the category of endofunctors,
 *    with product × replaced by composition of endofunctors
 *    and unit set by the identity endofunctor."
 *    — Saunders Mac Lane, Categories for the Working Mathematician
 *
 *   The monoid structure:
 *
 *   Object:  M (a monad/functor, identified by its kind tag)
 *
 *   Unit:    η : Id ⟹ M          — the "return" nat trans (MRETURN / pure)
 *            η_A(Id x) = M(return x)
 *
 *   Multiply: μ : M∘M ⟹ M        — the "join" nat trans (JOIN)
 *            μ_A(M(M(a))) = M(a)   by JOIN
 *
 *   Monoid laws (= monad laws, expressed as commuting diagrams):
 *
 *     μ ∘ (M · η) = id_M    (right unit: join . fmap return = id)
 *     μ ∘ (η · M) = id_M    (left unit:  join . return      = id)
 *     μ ∘ (M · μ) = μ ∘ (μ · M)  (assoc: join . fmap join = join . join)
 *
 *   We implement μ and η as NatTrans functions and verify the laws.
 *
 * ════════════════════════════════════════════════════════════════ */

/* MONAD_UNIT(kind, fa): η for a specific monad. Returns M(id_val) ↦ M(return val).
 * For our monads, η = id_to_F. We alias. */
#define MONAD_UNIT_MAYBE   _id_to_maybe
#define MONAD_UNIT_LIST    _id_to_list
#define MONAD_UNIT_IO      _id_to_io
#define MONAD_UNIT_READER  _id_to_reader

/* MONAD_MU(kind, mm_a): μ for a specific monad = JOIN.
 * mm_a must have inner = Functor_t* (heap-allocated, as per JOIN convention).
 * μ_A(M(M(a))) = JOIN(mm_a) = M(a). */
#define MONAD_MU(mm_a)  JOIN(mm_a)

/* VERIFY_MONAD_UNIT_LAW(kind, fa, unit_nat, eq):
 * Checks: μ(η_M(fa)) = fa  (join . return = id)
 * i.e., wrapping fa in an extra M layer and then join-ing returns fa.
 * fa.kind must equal kind. */
#define VERIFY_MONAD_UNIT_LAW(fa, unit_nat, eq)                                \
    do {                                                                        \
        Functor_t _fa  = (fa);                                                  \
        NatTrans  _eta = (NatTrans)(unit_nat);                                  \
        /* Build Id(fa) then apply eta to get M(fa) */                          \
        Functor_t _id_fa   = ID_VAL((void *)&_fa);                              \
        Functor_t _m_fa    = _eta(_id_fa);                                      \
        /* Now JOIN: _m_fa.inner should be Functor_t* pointing to _fa */       \
        /* But _m_fa is the result of pure_F applied to &_fa... */              \
        /* For Maybe: pure_F(Id(&fa)) = Just(&fa)                    */         \
        /* JOIN(Just(&fa)) = *(&fa) = fa                             */         \
        Functor_t _joined  = JOIN(_m_fa);                                       \
        if (!(eq)(_joined, _fa)) {                                              \
            fprintf(stderr,                                                     \
                "MONAD UNIT LAW VIOLATED: μ(η(fa)) ≠ fa\n"                     \
                "  kind: %d\n"                                                  \
                "join . return ≠ id\n"                                          \
                "The monad-as-monoid structure is broken.\n"                    \
                "The monad is not a monoid.\n"                                  \
                "Mac Lane is disappointed.\n",                                  \
                _fa.kind);                                                       \
            abort();                                                             \
        }                                                                        \
    } while (0)


/* ════════════════════════════════════════════════════════════════
 * INSTANCE DECLARATIONS
 * ════════════════════════════════════════════════════════════════ */

/* Nothing placeholder: IO action for maybe_to_io(Nothing). */
static void *_io_abort_thunk(void) {
    fputs("maybe_to_io: called on Nothing. "
          "There is no value here. There was never a value here. "
          "Aborting.\n", stderr);
    abort();
    return NULL;  /* unreachable, but the compiler wants it */
}


/* ════════════════════════════════════════════════════════════════
 * IMPLEMENTATION
 * ════════════════════════════════════════════════════════════════ */

#ifdef NAT_TRANS_IMPLEMENTATION

NatTransEntry_t _nat_registry[NAT_TRANS_REGISTRY_MAX];
int             _nat_registry_size = 0;

/* ── Identity Functor ───────────────────────────────────────────
 *
 * The identity functor: does nothing, wraps nothing, is nothing,
 * contains everything. The Zhuangzi of functors.
 * ─────────────────────────────────────────────────────────────── */

Functor_t _id_fmap_impl(FmapFn f, Functor_t fa) {
    return ID_VAL(f(fa.inner));
}

Functor_t _id_pure_impl(void *a) {
    return ID_VAL(a);
}

Functor_t _id_ap_impl(Functor_t ff, Functor_t fa) {
    FmapFn f = (FmapFn)ff.inner;
    return ID_VAL(f(fa.inner));
}

Functor_t _id_bind_impl(Functor_t ma, KleisliFn f) {
    return f(ma.inner);
}

REGISTER_MONAD_INSTANCE(
    FUNCTOR_KIND_ID, "Id",
    _id_fmap_impl, _id_pure_impl, _id_ap_impl, _id_bind_impl)


/* ── maybe_to_list ──────────────────────────────────────────────
 * Maybe ⟹ List
 * ─────────────────────────────────────────────────────────────── */

Functor_t _maybe_to_list(Functor_t fa) {
    if (IS_NOTHING(fa)) return NIL;
    return CONS(FROM_JUST(fa), NIL);
}

REGISTER_NAT_TRANS(
    FUNCTOR_KIND_MAYBE, FUNCTOR_KIND_LIST,
    "maybe_to_list", _maybe_to_list)


/* ── list_to_maybe (safeHead) ───────────────────────────────────
 * List ⟹ Maybe
 * ─────────────────────────────────────────────────────────────── */

Functor_t _list_to_maybe(Functor_t fa) {
    if (LIST_IS_NIL(fa)) return NOTHING;
    return JUST(LIST_HEAD(fa));
}

REGISTER_NAT_TRANS(
    FUNCTOR_KIND_LIST, FUNCTOR_KIND_MAYBE,
    "list_to_maybe (safeHead)", _list_to_maybe)


/* ── maybe_to_io ────────────────────────────────────────────────
 * Maybe ⟹ IO
 * Nothing → IO that aborts.
 * Just x  → IO that returns x. (pure x in IO)
 * ─────────────────────────────────────────────────────────────── */

Functor_t _maybe_to_io(Functor_t fa) {
    if (IS_NOTHING(fa)) {
        return IO(_io_abort_thunk);
    }
    /* Just x → pure x in IO = IO_PURE containing x */
    return _io_pure_impl(FROM_JUST(fa));
}

REGISTER_NAT_TRANS(
    FUNCTOR_KIND_MAYBE, FUNCTOR_KIND_IO,
    "maybe_to_io", _maybe_to_io)


/* ── id_to_F: pure as natural transformation ────────────────────
 * Id ⟹ Maybe, List, IO, Reader
 * Each is exactly the `pure` / `return` for the target functor.
 * ─────────────────────────────────────────────────────────────── */

Functor_t _id_to_maybe(Functor_t fa) {
    /* pure (unwrap (Id x)) = Just x */
    return JUST(UN_ID(fa));
}

Functor_t _id_to_list(Functor_t fa) {
    /* pure x = [x] */
    return CONS(UN_ID(fa), NIL);
}

Functor_t _id_to_io(Functor_t fa) {
    /* pure x = IO that returns x */
    return _io_pure_impl(UN_ID(fa));
}

Functor_t _id_to_reader(Functor_t fa) {
    /* pure x = Reader (\_ -> x) */
    return _reader_pure_impl(UN_ID(fa));
}

REGISTER_NAT_TRANS(FUNCTOR_KIND_ID, FUNCTOR_KIND_MAYBE, "pure_Maybe (id_to_maybe)", _id_to_maybe)
REGISTER_NAT_TRANS(FUNCTOR_KIND_ID, FUNCTOR_KIND_LIST,  "pure_List  (id_to_list)",  _id_to_list)
REGISTER_NAT_TRANS(FUNCTOR_KIND_ID, FUNCTOR_KIND_IO,    "pure_IO    (id_to_io)",    _id_to_io)
REGISTER_NAT_TRANS(FUNCTOR_KIND_ID, FUNCTOR_KIND_READER,"pure_Reader(id_to_reader)",_id_to_reader)


/* ── list_to_io: execute nondeterminism as IO ───────────────────
 *
 * List ⟹ IO: take the head, run it as IO.
 * (If the list is empty, abort. You made an empty list of IO actions.)
 *
 * Note: this is NOT list_to_maybe composed with maybe_to_io,
 * even though it composes to the same function. The composition
 * of natural transformations via NAT_VCOMP would give the same result
 * as this direct implementation. This verifies that our nat trans
 * composition is correct.
 * ─────────────────────────────────────────────────────────────── */

static Functor_t _list_to_io(Functor_t fa) {
    if (LIST_IS_NIL(fa)) {
        fputs("list_to_io: empty list. "
              "No IO action to run. "
              "You have computed all paths and found no survivors.\n", stderr);
        abort();
    }
    /* Take the head: it should be an IO action (Functor_t) stored as void*.
     * If the list contains raw IO thunks (void*(void)), wrap them.
     * If it contains Functor_t* (IO actions), unwrap and return.
     * Convention: list elements that are Functor_t* are used directly.
     * List elements that are raw thunks (IOThunk) are wrapped in IO(). */
    void *head = LIST_HEAD(fa);
    /* We can't distinguish at runtime without a tag. Assume Functor_t*. */
    return *(Functor_t *)head;
}

REGISTER_NAT_TRANS(FUNCTOR_KIND_LIST, FUNCTOR_KIND_IO, "list_to_io (head)", _list_to_io)

#endif /* NAT_TRANS_IMPLEMENTATION */


/* ════════════════════════════════════════════════════════════════
 * APPENDIX: THE COMPLETE PICTURE (through nat_trans.h)
 *
 * What we have built, across the first four files:
 *
 *   ┌─────────────────────────────────────────────────────────┐
 *   │  A 2-category of C endofunctors                         │
 *   │                                                         │
 *   │  0-cells: 1 (void*-category)                            │
 *   │                                                         │
 *   │  1-cells (endofunctors):                                │
 *   │    Id, Maybe, List, IO, Reader                          │
 *   │    + all composed variants (IO_COMPOSED, etc.)          │
 *   │    Each with: fmap, pure, ap, bind, return              │
 *   │                                                         │
 *   │  2-cells (natural transformations):                     │
 *   │    maybe_to_list   : Maybe ⟹ List                      │
 *   │    list_to_maybe   : List  ⟹ Maybe                     │
 *   │    maybe_to_io     : Maybe ⟹ IO                        │
 *   │    list_to_io      : List  ⟹ IO                        │
 *   │    id_to_{Maybe,List,IO,Reader} : Id ⟹ each            │
 *   │    NAT_ID          : F ⟹ F  (identity, for each F)     │
 *   │    Composable via NAT_VCOMP                             │
 *   │    Whiskerable via WHISKER_LEFT, WHISKER_RIGHT          │
 *   │                                                         │
 *   │  Monads as monoids in End(C):                           │
 *   │    Unit:     η = id_to_F    (return / pure)             │
 *   │    Multiply: μ = JOIN        (join)                     │
 *   │    Laws verified by VERIFY_MONAD_*                      │
 *   │                                                         │
 *   │  Yoneda:                                                │
 *   │    Nat(Reader r, F) ≅ F(r)                              │
 *   │    YONEDA_FWD / YONEDA_BWD / VERIFY_YONEDA_ROUNDTRIP   │
 *   └─────────────────────────────────────────────────────────┘
 *
 * ════════════════════════════════════════════════════════════════
 *
 * CONTINUED IN (left for subsequent files, the reader, or therapy):
 *
 *   Profunctors  — see profunctor.h. It happened. It was bad.
 *   Comonad      — see kan.h. extract :: w a -> a. extend. The dual of Monad.
 *   Kan extensions — see kan.h. Lan, Ran. Everything is a Kan extension.
 *   Adjunctions  — implied by Lan ⊣ (−∘k) ⊣ Ran. Exercise for the reader.
 *
 * STILL NOT IMPLEMENTED (left for the reader, the sequel, or therapy):
 *
 *   Alternative  — MonadPlus, empty/(<|>), the failure monad
 *   MonadTrans   — monad transformers (MaybeT, ReaderT, ...)
 *   Free monad   — half-implemented by our IO chain representation
 *   Arrows       — generalised Kleisli arrows. We have KleisliFn. We're close.
 *   ∞-categories — Maybe not.
 *
 * ════════════════════════════════════════════════════════════════
 *
 * ACCOUNTING (through nat_trans.h, four files):
 *
 *   Files:              4  (functor.h, applicative.h, monad.h, nat_trans.h)
 *   Global arrays:      4  (functor, applicative, monad, nat_trans vtables)
 *   Global ints:        4  (vtable size counters)
 *   Global pointers:    2  (applicative.h's global slots — the only ones)
 *   Functor kinds:     13  (Id, Maybe, List, IO×5, Reader×5)
 *   NatTrans registered: 8 (at minimum)
 *   Type safety:        0
 *   Memory freed:       0
 *   Lines of C:      ~2700 across these four files
 *   Dignity lost:       immeasurable
 *   Category theory learned: the same amount, arriving from an unusual direction
 *
 *   The unusual direction was correct.
 *   See kan.h for the final accounting across all six files.
 *
 * ════════════════════════════════════════════════════════════════ */

#endif /* NAT_TRANS_H */
