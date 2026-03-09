/*
 * example.c — A tour of the six files
 *
 * Demonstrates each typeclass in order of increasing architectural regret.
 * Every example works. Nothing is freed. The heap grows.
 *
 * Compilation:
 *   gcc -O0 -std=c11 example.c -o example -z execstack
 *
 * The -z execstack flag is required because DO, CODO, KLEISLI_COMPOSE,
 * YONEDA_BWD, LENS, PRISM, and the Codensity/Density implementations all
 * use GCC nested functions, which produce stack-allocated trampolines.
 */

#define FUNCTOR_IMPLEMENTATION
#define APPLICATIVE_IMPLEMENTATION
#define MONAD_IMPLEMENTATION
#define NAT_TRANS_IMPLEMENTATION
#define PROFUNCTOR_IMPLEMENTATION
#define KAN_IMPLEMENTATION
#include "kan.h"

#include <stdio.h>
#include <string.h>


/* ════════════════════════════════════════════════════════════════
 * Utilities
 * ════════════════════════════════════════════════════════════════ */

static void *double_int(void *x) { return BOX_INT(UNBOX_INT(x) * 2); }
static void *inc_int(void *x)    { return BOX_INT(UNBOX_INT(x) + 1); }
static void *neg_int(void *x)    { return BOX_INT(-UNBOX_INT(x));    }

static void print_maybe_int(Functor_t fa, const char *label) {
    printf("  %-20s = ", label);
    if (IS_NOTHING(fa)) printf("Nothing\n");
    else printf("Just %d\n", UNBOX_INT(FROM_JUST(fa)));
}

static void print_list_int(Functor_t fa, const char *label) {
    printf("  %-20s = [", label);
    Functor_t cur = fa;
    int first = 1;
    while (!LIST_IS_NIL(cur)) {
        if (!first) printf(", ");
        printf("%d", UNBOX_INT(LIST_HEAD(cur)));
        cur = LIST_TAIL(cur);
        first = 0;
    }
    printf("]\n");
}


/* ════════════════════════════════════════════════════════════════
 * §1  FUNCTOR — fmap
 *
 * In Haskell:   fmap (*2) (Just 21)   = Just 42
 *               fmap (*2) Nothing      = Nothing
 *               fmap (*2) [1,2,3]      = [2,4,6]
 * ════════════════════════════════════════════════════════════════ */

static void demo_functor(void) {
    printf("── §1 Functor ────────────────────────────────────────────────\n");

    Functor_t just21   = JUST(BOX_INT(21));
    Functor_t nothing  = NOTHING;
    Functor_t list123  = LIST(BOX_INT(1), BOX_INT(2), BOX_INT(3));

    print_maybe_int(FMAP(double_int, just21),  "fmap (*2) (Just 21)");
    print_maybe_int(FMAP(double_int, nothing), "fmap (*2) Nothing");
    print_list_int (FMAP(double_int, list123), "fmap (*2) [1,2,3]");

    /* Functor composition law:  fmap f . fmap g  =  fmap (f . g) */
    Functor_t lhs = FMAP(double_int, FMAP(inc_int, just21));  /* fmap (*2) . fmap (+1) */
    Functor_t rhs = FMAP_COMPOSE(double_int, inc_int, just21);/* fmap ((*2).(+1))      */
    printf("  %-20s = (Just %d, Just %d) — law holds: %s\n",
           "composition law",
           UNBOX_INT(FROM_JUST(lhs)),
           UNBOX_INT(FROM_JUST(rhs)),
           UNBOX_INT(FROM_JUST(lhs)) == UNBOX_INT(FROM_JUST(rhs)) ? "yes" : "NO");
    printf("\n");
}


/* ════════════════════════════════════════════════════════════════
 * §2  APPLICATIVE — pure and (<*>)
 *
 * In Haskell:   pure 42                    = Just 42
 *               Just (*2) <*> Just 21      = Just 42
 *               [(*2), (+10)] <*> [1,2,3]  = [2,4,6,11,12,13]
 * ════════════════════════════════════════════════════════════════ */

static void demo_applicative(void) {
    printf("── §2 Applicative ────────────────────────────────────────────\n");

    /* pure 42 :: Maybe Int */
    Functor_t p = PURE(FUNCTOR_KIND_MAYBE, BOX_INT(42));
    print_maybe_int(p, "pure 42 :: Maybe");

    /* Just (*2) <*> Just 21 */
    Functor_t ff = JUST(BOX_PTR(double_int));
    Functor_t fa = JUST(BOX_INT(21));
    print_maybe_int(AP(ff, fa), "Just (*2) <*> Just 21");

    /* Nothing <*> Just 21 */
    print_maybe_int(AP(NOTHING, fa), "Nothing <*> Just 21");

    /* List applicative: Cartesian product of functions × values */
    Functor_t fns  = LIST(BOX_PTR(double_int), BOX_PTR(inc_int));
    Functor_t vals = LIST(BOX_INT(10), BOX_INT(20), BOX_INT(30));
    print_list_int(AP(fns, vals), "[(*2),(+1)] <*> [10,20,30]");
    printf("\n");
}


/* ════════════════════════════════════════════════════════════════
 * §3  MONAD — bind and do-notation
 *
 * In Haskell:
 *   safeRecip :: Int -> Maybe Int
 *   safeRecip 0 = Nothing
 *   safeRecip n = Just (100 `div` n)
 *
 *   do { x <- Just 5; safeRecip x }   = Just 20
 *   do { x <- Just 0; safeRecip x }   = Nothing
 *   do { x <- [1,2,3]; [x, x*10] }    = [1,10,2,20,3,30]
 * ════════════════════════════════════════════════════════════════ */

static Functor_t safe_recip(void *x) {
    int n = UNBOX_INT(x);
    if (n == 0) return NOTHING;
    return JUST(BOX_INT(100 / n));
}

static Functor_t list_with_double(void *x) {
    int n = UNBOX_INT(x);
    return LIST(BOX_INT(n), BOX_INT(n * 10));
}

static void demo_monad(void) {
    printf("── §3 Monad ──────────────────────────────────────────────────\n");

    /* Maybe monad: DO desugars to BIND, GCC nested function closes over scope */
    Functor_t r1 = DO(JUST(BOX_INT(5)), x, safe_recip(x));
    Functor_t r2 = DO(JUST(BOX_INT(0)), x, safe_recip(x));
    print_maybe_int(r1, "Just 5  >>= safeRecip");
    print_maybe_int(r2, "Just 0  >>= safeRecip");

    /* List monad: concatMap */
    Functor_t xs = LIST(BOX_INT(1), BOX_INT(2), BOX_INT(3));
    Functor_t r3 = DO(xs, x, list_with_double(x));
    print_list_int(r3, "[1,2,3] >>= \\x->[x,x*10]");

    /* Kleisli composition: (>=>) */
    KleisliFn inc_then_safe = KLEISLI_COMPOSE(
        (KleisliFn)safe_recip,
        (KleisliFn)/* inc wrapped in kleisli */ NULL
    );
    /* manual kleisli: safe_recip . inc_int */
    Functor_t r4 = DO(DO(JUST(BOX_INT(4)), y, JUST(inc_int(y))), z, safe_recip(z));
    print_maybe_int(r4, "Just 4 >>= (+1) >>= safeRecip");

    (void)inc_then_safe; /* suppress unused warning on the kleisli attempt */
    printf("\n");
}


/* ════════════════════════════════════════════════════════════════
 * §4  NATURAL TRANSFORMATION — η : F ⟹ G
 *
 * In Haskell:
 *   maybeToList :: Maybe a -> [a]
 *   maybeToList Nothing  = []
 *   maybeToList (Just x) = [x]
 *
 *   listToMaybe :: [a] -> Maybe a
 *   listToMaybe []    = Nothing
 *   listToMaybe (x:_) = Just x
 *
 * Yoneda:  Nat(Reader r, F) ≅ F(r)
 * ════════════════════════════════════════════════════════════════ */

static void demo_nat_trans(void) {
    printf("── §4 Natural Transformations ────────────────────────────────\n");

    /* Lookup the registered maybe_to_list nat trans */
    NatTrans eta_m2l = _find_nat_trans(FUNCTOR_KIND_MAYBE, FUNCTOR_KIND_LIST);

    Functor_t just42  = JUST(BOX_INT(42));
    Functor_t nothing = NOTHING;
    Functor_t list123 = LIST(BOX_INT(1), BOX_INT(2), BOX_INT(3));

    print_list_int(APPLY_NAT(eta_m2l, just42),  "maybeToList (Just 42)");
    print_list_int(APPLY_NAT(eta_m2l, nothing), "maybeToList Nothing");

    NatTrans eta_l2m = _find_nat_trans(FUNCTOR_KIND_LIST, FUNCTOR_KIND_MAYBE);
    print_maybe_int(APPLY_NAT(eta_l2m, list123), "listToMaybe [1,2,3]");
    print_maybe_int(APPLY_NAT(eta_l2m, NIL),     "listToMaybe []");

    /* Vertical composition: list_to_maybe ∘ maybe_to_list */
    NatTrans eta_roundtrip = NAT_VCOMP(eta_l2m, eta_m2l);
    print_maybe_int(APPLY_NAT(eta_roundtrip, just42),  "(l2m . m2l) (Just 42)");
    print_maybe_int(APPLY_NAT(eta_roundtrip, nothing), "(l2m . m2l) Nothing");

    /* Yoneda: Nat(Reader r, F) ≅ F(r) */
    /* YONEDA_FWD extracts an F(r) from the nat trans η applied to id_r */
    Functor_t fr = JUST(BOX_INT(99));
    NatTrans  bwd = YONEDA_BWD(fr);   /* F(r) -> Nat(Reader r, F)          */
    Functor_t fwd = YONEDA_FWD(bwd);  /* Nat(Reader r, F) -> F(r) = Just 99 */
    print_maybe_int(fwd, "Yoneda round-trip");
    printf("\n");
}


/* ════════════════════════════════════════════════════════════════
 * §5  PROFUNCTOR — dimap, lenses, prisms
 *
 * In Haskell (lens library style):
 *   _fst :: Lens' (a, b) a
 *   _fst f (a, b) = fmap (\a' -> (a', b)) (f a)
 *
 *   over _fst (+1) (10, 20) = (11, 20)
 *   view _fst      (10, 20) = 10
 * ════════════════════════════════════════════════════════════════ */

static void *get_fst(void *pair) { return ((Pair_t*)pair)->fst; }
static void *set_fst(void *pair) {
    /* pair here is actually a Pair_t* where fst has been updated */
    return pair;
}
/* For a proper lens we need setter :: (new_a, original_s) -> s */
static void *lens_set_fst(void *new_pair_ptr) {
    return new_pair_ptr;  /* the Pair_t is already constructed by MAKE_LENS */
}

static void demo_profunctor(void) {
    printf("── §5 Profunctor / Optics ────────────────────────────────────\n");

    /* Build a pair */
    Pair_t *p = malloc(sizeof(Pair_t));
    p->fst = BOX_INT(10);
    p->snd = BOX_INT(20);

    /* Arrow profunctor: dimap pre post arrow */
    /* dimap neg (*2) (+1) :: Int -> Int = (*2) . (+1) . neg */
    Functor_t arr   = ARROW((FmapFn)inc_int);
    Functor_t dimapped = DIMAP(neg_int, double_int, arr);
    printf("  %-20s = %d  (dimap neg (*2) (+1) applied to 5)\n",
           "dimap neg (*2) (+1) 5",
           UNBOX_INT(RUN_ARROW(dimapped, BOX_INT(5))));
    /* Expected: double_int(inc_int(neg_int(5))) = double_int(inc_int(-5)) = double_int(-4) = -8 */

    /* Star profunctor = Kleisli arrow */
    /* Star safe_recip :: Star Maybe Int Int */
    Functor_t star = STAR((StarFn)safe_recip);
    Functor_t star_dimapped = DIMAP(inc_int, ID, star);
    /* dimap (+1) id safe_recip :: Int -> Maybe Int = safe_recip . (+1) */
    Functor_t result = RUN_STAR(star_dimapped, BOX_INT(4));  /* safe_recip(5) = Just 20 */
    print_maybe_int(result, "dimap (+1) id safeRecip 4");

    /* Forget profunctor = getter */
    Functor_t forget = FORGET((FmapFn)get_fst);
    printf("  %-20s = %d\n",
           "view _fst (10,20)",
           UNBOX_INT(RUN_FORGET(forget, (void*)p)));

    /* Lens via MAKE_LENS:  MAKE_LENS(getter, setter, pab) */
    /* We use the Arrow profunctor to "over": apply a function to fst */
    Functor_t arr_double = ARROW((FmapFn)double_int);
    Functor_t lens_result = MAKE_LENS(get_fst, lens_set_fst, arr_double);
    Pair_t *updated = (Pair_t*)RUN_ARROW(lens_result, (void*)p);
    printf("  %-20s = (%d, %d)\n",
           "over _fst (*2) (10,20)",
           UNBOX_INT(updated->fst),
           UNBOX_INT((void*)p->snd == (void*)p->snd ? p->snd : p->snd));
    printf("\n");
}


/* ════════════════════════════════════════════════════════════════
 * §6  KAN EXTENSIONS — Codensity (CPS monad) and Density (comonad)
 *
 * In Haskell:
 *   -- Codensity = CPS monad
 *   type Codensity f a = forall b. (a -> f b) -> f b
 *   return a = \k -> k a
 *   m >>= h  = \k -> m (\a -> h a k)
 *
 *   -- Density = Lan f f
 *   data Density f a = forall b. Density (f b -> a) (f b)
 *   extract (Density f x) = f x
 * ════════════════════════════════════════════════════════════════ */

static Functor_t kleisli_safe_recip(void *x) { return safe_recip(x); }

static void demo_kan(void) {
    printf("── §6 Kan Extensions ─────────────────────────────────────────\n");

    /* Codensity monad: CPS-lifted computation.
     *
     * CODENSITY_RETURN(x) = \k -> k(x).
     * The same deferred value can produce different results depending on
     * which final continuation you supply — this is the point.
     *
     * RUN_CODENSITY(cm, _just_impl) = Just(4)
     * RUN_CODENSITY(cm, safe_recip) = safe_recip(4) = Just(25)
     *
     * Note: CODENSITY_BIND requires its kleisli argument to return a Codensity
     * value (not a plain Maybe), so we demonstrate the deferred-continuation
     * semantics through RUN_CODENSITY instead. */
    Functor_t cm = CODENSITY_RETURN(BOX_INT(4));
    Functor_t as_maybe  = RUN_CODENSITY(cm, (KleisliFn)_just_impl);
    Functor_t via_recip = RUN_CODENSITY(cm, (KleisliFn)kleisli_safe_recip);
    print_maybe_int(as_maybe,  "codensity return 4");
    print_maybe_int(via_recip, "codensity: safeRecip 4");

    /* Yoneda as Ran Id: RAN_FROM_YONEDA / YONEDA_AS_RAN round-trip.
     * YONEDA_AS_RAN(RAN_FROM_YONEDA(fr)) = fmap id fr = fr.
     * back has the same kind as original (Maybe), not Id-wrapped. */
    Functor_t original = JUST(BOX_INT(77));
    Functor_t as_ran   = RAN_FROM_YONEDA(original);
    Functor_t back     = YONEDA_AS_RAN(as_ran);
    print_maybe_int(back, "Yoneda via Ran");

    /* Density comonad: extract observes the stored value. */
    /* Density wraps (morphism :: f b -> a, stored f b). */
    /* We use FUNCTOR_KIND_MAYBE as our f, and get_fst as morphism. */
    /* morphism extracts the first element of a Pair inside a Maybe. */
    static Pair_t pair_for_density;
    pair_for_density.fst = BOX_INT(55);
    pair_for_density.snd = BOX_INT(99);

    /* LanMorphFn :: Functor_t -> void*
     * Our morphism: extract the int from a Just (Pair_t*) */
    void *density_morph(Functor_t fb) {
        if (IS_NOTHING(fb)) return BOX_INT(0);
        Pair_t *pr = (Pair_t*)FROM_JUST(fb);
        return pr->fst;
    }
    Functor_t stored_fb = JUST(BOX_PTR(&pair_for_density));
    Functor_t wa = MAKE_DENSITY(density_morph, stored_fb);

    printf("  %-20s = %d  (extract from Density)\n",
           "density extract",
           UNBOX_INT(DENSITY_EXTRACT(wa)));

    /* DENSITY_EXTEND is not demonstrated here.
     *
     * GCC nested functions produce trampolines on the enclosing stack frame.
     * _extend_density() creates a nested function and RETURNS its address —
     * but returning invalidates the stack frame, making the stored function
     * pointer dangling. Calling it afterwards is UB and segfaults in practice.
     *
     * Proper Lan/Density extend requires heap-allocated closures (libffi, or
     * a manual closure struct). For a SIGBOVIK submission, this note is funnier
     * than a working implementation would be. The Haskell version is 1 line. */

    printf("\n");
}


/* ════════════════════════════════════════════════════════════════
 * §0  IO MONAD — side effects, deferred then executed
 *
 * In Haskell:
 *   do { putStrLn "hello"
 *      ; x <- return 42
 *      ; putStrLn (show x) }
 * ════════════════════════════════════════════════════════════════ */

static void *io_hello(void) {
    printf("  [IO] hello from a thunk\n");
    return BOX_INT(42);
}

static void demo_io(void) {
    printf("── §0 IO Monad ───────────────────────────────────────────────\n");

    /* Build a deferred IO action and fmap over it before running */
    Functor_t action = IO(io_hello);
    Functor_t mapped = FMAP(double_int, action);   /* deferred: not yet run */

    printf("  (IO action built, not yet executed)\n");
    void *result = RUN_IO(mapped);                 /* now it runs */
    printf("  [IO] result after fmap (*2): %d\n", UNBOX_INT(result));

    /* IO bind: sequence two actions */
    Functor_t io_ret = DO(action, x,
                          MRETURN(FUNCTOR_KIND_IO,
                                  BOX_INT(UNBOX_INT(x) + 1000)));
    void *r2 = RUN_IO(io_ret);
    printf("  [IO] bind result (+1000): %d\n", UNBOX_INT(r2));
    printf("\n");
}


/* ════════════════════════════════════════════════════════════════
 * §0  READER MONAD — read-only environment
 *
 * In Haskell:
 *   greet :: Reader String String
 *   greet = do { name <- ask; return ("hello, " ++ name) }
 *   runReader greet "world"  = "hello, world"
 * ════════════════════════════════════════════════════════════════ */

static void *prepend_hello(void *env) {
    static char buf[128];
    snprintf(buf, sizeof(buf), "hello, %s", (char*)env);
    return (void*)buf;
}

static void demo_reader(void) {
    printf("── §0 Reader Monad ───────────────────────────────────────────\n");

    Functor_t greet  = READER(prepend_hello);
    Functor_t greet2 = FMAP(/* uppercase first char via identity for demo */ ID, greet);

    printf("  %-20s = %s\n", "runReader greet",
           (char*)RUN_READER(greet, "world"));

    /* Reader bind: use the environment in a derived computation */
    Functor_t greet_len = DO(greet, msg,
                              MRETURN(FUNCTOR_KIND_READER,
                                      BOX_INT((int)strlen((char*)msg))));
    printf("  %-20s = %d\n", "length of greeting",
           UNBOX_INT(RUN_READER(greet_len, "SIGBOVIK")));

    (void)greet2;
    printf("\n");
}


/* ════════════════════════════════════════════════════════════════
 * main
 * ════════════════════════════════════════════════════════════════ */

int main(void) {
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  Six Haskell typeclasses implemented in C macros             ║\n");
    printf("║  Presented without dignity, but with mathematical rigour     ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");

    demo_functor();
    demo_applicative();
    demo_monad();
    demo_nat_trans();
    demo_profunctor();
    demo_kan();
    demo_io();
    demo_reader();

    printf("══════════════════════════════════════════════════════════════\n");
    printf("All demos completed.\n");
    printf("Memory freed: 0 bytes. The heap contains everything we built.\n");
    printf("Type safety: none. It compiled. That's the bar.\n");
    return 0;
}
