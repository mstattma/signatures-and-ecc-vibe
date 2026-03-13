#!/usr/bin/env python3
"""
UOV (Oil and Vinegar) Security Estimator

Estimates the security level of UOV parameters (q, v, o) against
the main known attacks. Based on the analysis from:
- Beullens et al., "Oil and Vinegar: Modern Parameters and Implementations" (2023)
- Kipnis & Shamir, "Cryptanalysis of the Oil & Vinegar Signature Scheme" (1998)
- Thomae & Wolf, "Solving underdetermined systems of multivariate quadratic equations..." (2012)
- Beullens, "Improved Cryptanalysis of UOV and Rainbow" (Eurocrypt 2021)
- Furue et al., "A New Security Analysis Against MAYO and QR-UOV" (2024)

The attacks considered:
1. Direct algebraic attack (solving the MQ system via Gröbner basis / XL)
2. Kipnis-Shamir (KS) attack (recovering the Oil subspace)
3. Intersection attack (Beullens 2021 improvement)
4. Reconciliation attack
5. Forgery via birthday / collision on the hash
"""

import math
from dataclasses import dataclass
from typing import List, Tuple


@dataclass
class UOVParams:
    """UOV parameter set."""
    q: int       # Field size (16 or 256)
    v: int       # Number of vinegar variables
    o: int       # Number of oil variables (= number of equations)
    name: str = ""

    @property
    def n(self) -> int:
        return self.v + self.o

    @property
    def sig_bytes(self) -> int:
        """Signature size in bytes = n elements + 16-byte salt."""
        if self.q == 16:
            return (self.n + 1) // 2 + 16  # 4 bits per element
        elif self.q == 256:
            return self.n + 16  # 8 bits per element
        else:
            return math.ceil(self.n * math.log2(self.q) / 8) + 16

    @property
    def sig_bits(self) -> int:
        return self.sig_bytes * 8

    @property
    def pk_bytes_classic(self) -> int:
        """Uncompressed public key size (classic variant)."""
        # PK = m * n*(n+1)/2 field elements
        n_tri = self.n * (self.n + 1) // 2
        if self.q == 16:
            return self.o * n_tri // 2  # 4 bits per element, o_byte = o/2
        else:
            return self.o * n_tri

    @property
    def pk_bytes_pkc(self) -> int:
        """Compressed public key size (pkc variant)."""
        # cpk = seed(16) + o * o*(o+1)/2 field elements (P3 part)
        n_tri_o = self.o * (self.o + 1) // 2
        if self.q == 16:
            return 16 + self.o * n_tri_o // 2
        else:
            return 16 + self.o * n_tri_o

    @property
    def recovered_hash_bits(self) -> int:
        """Bits of hash digest recoverable from signature via P(s)."""
        return self.o * int(math.log2(self.q))


def log2_binomial(n: int, k: int) -> float:
    """Compute log2(C(n,k)) using Stirling or exact."""
    if k < 0 or k > n:
        return -float('inf')
    if k == 0 or k == n:
        return 0.0
    # Use log-gamma for large values
    result = (math.lgamma(n + 1) - math.lgamma(k + 1) - math.lgamma(n - k + 1)) / math.log(2)
    return result


def direct_attack_complexity(q: int, n: int, m: int) -> float:
    """
    Estimate complexity of direct algebraic attack on a system of
    m quadratic equations in n variables over GF(q).

    Uses the Thomae-Wolf / hybrid approach estimate:
    The best strategy is to guess k variables, reducing to (n-k) variables,
    then solve the remaining system. The complexity is approximately:

        min_k { q^k * C(n-k+d_reg, d_reg)^omega }

    where d_reg is the degree of regularity and omega ~ 2 (for sparse linear algebra).

    For a random MQ system with m equations in n' = n-k variables:
        d_reg ≈ (sum of (deg_i - 1) for i=1..m) / (n' - m) + 1 for overdetermined
        For m ≈ n', d_reg is approximately q-dependent.

    We use the simplified Bardet et al. estimate for semi-regular systems.
    """
    log2_q = math.log2(q)
    omega = 2  # Linear algebra exponent (conservative; Strassen gives ~2.37)

    best = float('inf')
    for k in range(0, n):
        n_prime = n - k
        if n_prime <= m:
            # Overdetermined or square: use degree of regularity
            # For a random quadratic system with m >= n', d_reg is small
            # Approximate: solving cost is roughly q^k * poly(n')
            # For square systems (m = n'), Gröbner basis has complexity
            # roughly O(n'^(2*omega)) which for small n' is fast
            if n_prime <= 2:
                cost = k * log2_q  # Almost free to solve
            else:
                # Use Lazard's bound: d_reg for square systems
                # d_reg ~ (m*(2-1) - n' + 2) = m - n' + 2 for m >= n'
                d_reg = m - n_prime + 2
                if d_reg < 2:
                    d_reg = 2
                log2_matrices = omega * log2_binomial(n_prime + d_reg, d_reg)
                cost = k * log2_q + log2_matrices
        else:
            # Underdetermined: n' > m
            # Degree of regularity for random quadratic in n' vars, m eqs:
            # Use Bardet et al. estimate
            # For underdetermined (n' > m), d_reg grows roughly as:
            # d_reg ≈ 2 + floor((m-1)/(n'-m)) when n' > m
            # But more precisely, we use the generating function approach
            excess = n_prime - m
            if excess >= m:
                # Very underdetermined: just guessing is cheaper
                cost = k * log2_q + 2 * m * log2_q  # rough lower bound
            else:
                d_reg = 2
                # Increase d_reg until the number of monomials of degree d_reg
                # exceeds the number of equations contributed at that degree
                while True:
                    n_mono = log2_binomial(n_prime + d_reg, d_reg)
                    n_eqs_approx = log2_binomial(n_prime + d_reg - 2, d_reg - 2) + math.log2(m)
                    if n_eqs_approx >= n_mono - 1:
                        break
                    d_reg += 1
                    if d_reg > 100:
                        break

                log2_matrices = omega * log2_binomial(n_prime + d_reg, d_reg)
                cost = k * log2_q + log2_matrices

        if cost < best:
            best = cost

    return best


def kipnis_shamir_attack(q: int, v: int, o: int) -> float:
    """
    Kipnis-Shamir attack complexity.

    The KS attack finds the Oil subspace by solving a MinRank problem.
    The key equation: find a linear combination of the public matrices
    P_1, ..., P_m that has rank at most v (instead of v+o).

    Complexity using the Minors modeling (Faugère et al.):
        ~ q^((o+1-v)*(o-v)/2) * poly(n,o)  when o < v

    For v >= o (which is required for UOV security):
        The attack essentially requires solving an MQ system in
        o*(o+1)/2 unknowns and o*(v-o+1) equations.

    Simplified estimate from Beullens (2021):
        The KS attack has complexity approximately:
        C_KS = q^max(0, ceil((o-1)/2) - floor((v-o)/2)) * poly(n)

    More precisely, the MinRank problem via kernel modeling gives:
        cost ≈ binomial(n, o+1)^omega * q^max(0, o+1-v)
    """
    log2_q = math.log2(q)

    # Kipnis-Shamir via Support Minors modeling (Bardet et al. 2020)
    # For UOV with v >= o, the complexity is very high
    # The attack reduces to finding a vector in the Oil space
    # by solving a MinRank instance of target rank o in (o x n) matrices

    # Using Beullens' improved analysis:
    # The relevant parameter is v - o (the "gap")
    gap = v - o

    if gap < 0:
        return 0  # Insecure if v < o

    # Support Minors Modeling (Bardet et al.):
    # For target rank r in (m x n) matrices, with m >= n >= r:
    # Cost ~ binomial(n, r+1)^omega for the simplest variant
    # For UOV specifically, Beullens shows the cost is approximately:
    # The number of unknowns is O(o^2) and the system is determined
    # when v is sufficiently larger than o.

    # Simplified: KS attack complexity ~ q^max(0, (o-v+1)*(o-v)/2)
    # when v >= o, this exponent is <= 0, so the polynomial part dominates
    # The polynomial cost is roughly O(o^(2*omega)) ~ O(o^4)

    # More careful analysis following the NIST UOV spec:
    # The MinRank problem has parameters:
    #   matrix size: n x n (but structured)
    #   target rank: v (finding rank-v submatrix)
    #   number of matrices: o

    # For v >= 2*o: KS attack is essentially exponential in o
    # For v >= o: still exponential but with smaller exponent

    # Using the Kipnis-Shamir-Courtois analysis:
    # Find O such that P_i * O = 0 for the oil subspace
    # This requires solving m*n equations in n*o unknowns
    # after accounting for the structure: o*(o+1)/2 unknowns, v*o equations

    n_unknowns = o * (o + 1) // 2
    n_equations = v * o

    if n_equations >= n_unknowns:
        # Overdetermined: solve as an MQ system
        # Complexity: essentially polynomial if very overdetermined
        # But these are quadratic equations, not linear
        # Use the same Gröbner/XL estimate
        return direct_attack_complexity(q, n_unknowns, n_equations)
    else:
        # Underdetermined: need to guess some variables
        deficit = n_unknowns - n_equations
        return deficit * log2_q + direct_attack_complexity(q, n_equations, n_equations)


def intersection_attack(q: int, v: int, o: int) -> float:
    """
    Beullens' intersection attack (Eurocrypt 2021).

    This attack exploits that two random subspaces of dimension o in
    GF(q)^n intersect non-trivially when 2*o > n.

    For UOV with v >= o (so n >= 2*o), the intersection is trivial
    and this attack doesn't apply directly.

    When v < 2*o, the expected intersection dimension is 2*o - n = 2*o - v - o = o - v,
    which is negative when v > o. So for v >= o, this attack gives no advantage.

    However, Beullens' reconciliation attack still applies:
    Given two independent OV instances, find consistent solutions.
    The complexity depends on q and the parameters.

    For v >= o: the reconciliation attack has complexity at least q^(v-o).
    """
    log2_q = math.log2(q)
    gap = v - o

    if gap <= 0:
        return 0  # Trivially broken

    # The intersection/reconciliation attack:
    # Complexity ~ q^(v - o) for the core step
    # Plus polynomial overhead
    base_cost = gap * log2_q

    # But there's a polynomial-time component too
    poly_cost = 3 * math.log2(max(v + o, 2))  # O(n^3) linear algebra

    return base_cost + poly_cost


def collision_forgery(q: int, o: int) -> float:
    """
    Birthday/collision attack on the hash-and-sign paradigm.

    If the hash outputs o elements of GF(q), a collision can be found
    in O(q^(o/2)) work (birthday bound).

    For q=256, o=44: 256^22 = 2^176 -- very high.
    For q=16, o=64: 16^32 = 2^128 -- matches AES-128.
    """
    log2_q = math.log2(q)
    return o * log2_q / 2


def guess_and_solve_attack(q: int, v: int, o: int) -> float:
    """
    Guess-and-determine: guess all vinegar variables, get a linear
    system in the oil variables. Always works but costs q^v to enumerate.

    More clever: guess only enough vinegar variables to make the system
    solvable, then use algebraic techniques.

    Actually, the standard signing approach IS this: pick random vinegar
    values and solve the linear system. An attacker without the trapdoor
    must solve the quadratic system.

    This attack is subsumed by the direct attack estimate.
    """
    return v * math.log2(q)


def estimate_security(params: UOVParams) -> dict:
    """
    Estimate security of UOV parameters against all known attacks.

    Returns a dict with the complexity (in log2 of operations) for each attack.
    """
    q, v, o = params.q, params.v, params.o
    n = v + o

    results = {}

    results['direct_attack'] = direct_attack_complexity(q, n, o)
    results['kipnis_shamir'] = kipnis_shamir_attack(q, v, o)
    results['intersection'] = intersection_attack(q, v, o)
    results['collision_forgery'] = collision_forgery(q, o)
    results['brute_force_vinegar'] = guess_and_solve_attack(q, v, o)

    results['security_level'] = min(results.values())
    results['bottleneck'] = min(results, key=results.get)

    return results


def search_params(target_security: float, q: int = 256,
                  v_o_ratio_range: Tuple[float, float] = (1.5, 3.0),
                  max_sig_bits: int = 1200) -> List[Tuple[UOVParams, dict]]:
    """
    Search for UOV parameters that achieve at least target_security bits
    of security with minimal signature size.
    """
    candidates = []

    for o in range(8, 80):
        for v_ratio_10 in range(15, 31):  # v/o ratio from 1.5 to 3.0
            v_ratio = v_ratio_10 / 10.0
            v = int(round(o * v_ratio))
            if v < o:
                continue

            params = UOVParams(q=q, v=v, o=o,
                             name=f"GF({q}),n={v+o},v={v},o={o}")

            if params.sig_bits > max_sig_bits:
                continue

            security = estimate_security(params)

            if security['security_level'] >= target_security:
                candidates.append((params, security))

    # Also try GF(16)
    for o in range(16, 120):
        for v_ratio_10 in range(15, 31):
            v_ratio = v_ratio_10 / 10.0
            v = int(round(o * v_ratio))
            if v < o:
                continue

            params = UOVParams(q=16, v=v, o=o,
                             name=f"GF(16),n={v+o},v={v},o={o}")

            if params.sig_bits > max_sig_bits:
                continue

            security = estimate_security(params)

            if security['security_level'] >= target_security:
                candidates.append((params, security))

    # Sort by signature size
    candidates.sort(key=lambda x: x[0].sig_bits)

    return candidates


def print_results(params: UOVParams, security: dict):
    """Pretty-print security estimation results."""
    print(f"\n{'='*70}")
    print(f"  {params.name or f'UOV(GF({params.q}), v={params.v}, o={params.o})'}")
    print(f"{'='*70}")
    print(f"  n = {params.n} (v={params.v}, o={params.o}), q = {params.q}")
    print(f"  Signature:  {params.sig_bytes} bytes = {params.sig_bits} bits")
    print(f"  Public key: {params.pk_bytes_classic:,} bytes (classic)")
    print(f"              {params.pk_bytes_pkc:,} bytes (compressed)")
    print(f"  Recovered hash: {params.recovered_hash_bits} bits from P(s)")
    print(f"  ---")
    for attack, bits in security.items():
        if attack in ('security_level', 'bottleneck'):
            continue
        marker = " <-- BOTTLENECK" if attack == security['bottleneck'] else ""
        print(f"  {attack:30s}: {bits:8.1f} bits{marker}")
    print(f"  ---")
    print(f"  OVERALL SECURITY: {security['security_level']:.1f} bits")
    print(f"  Bottleneck: {security['bottleneck']}")


def main():
    print("=" * 70)
    print("  UOV Security Estimator")
    print("  Known attacks: Direct (Gröbner/XL), Kipnis-Shamir,")
    print("  Intersection/Reconciliation, Collision forgery")
    print("=" * 70)

    # First, verify against known NIST Level 1 parameters
    print("\n\n### NIST Level 1 Reference Parameters ###")

    nist_params = [
        UOVParams(q=16, v=96, o=64, name="uov-Is (NIST Level 1, GF(16))"),
        UOVParams(q=256, v=68, o=44, name="uov-Ip (NIST Level 1, GF(256))"),
        UOVParams(q=256, v=112, o=72, name="uov-III (NIST Level 3, GF(256))"),
        UOVParams(q=256, v=148, o=96, name="uov-V (NIST Level 5, GF(256))"),
    ]

    for p in nist_params:
        sec = estimate_security(p)
        print_results(p, sec)

    # Search for 80-bit security parameters
    print("\n\n" + "=" * 70)
    print("  SEARCH: Minimum signature size for 80-bit security")
    print("=" * 70)

    candidates_80 = search_params(80, q=256, max_sig_bits=900)
    candidates_80_gf16 = search_params(80, q=16, max_sig_bits=900)
    all_80 = candidates_80 + candidates_80_gf16
    all_80.sort(key=lambda x: x[0].sig_bits)

    print(f"\nTop 10 smallest signatures with >= 80-bit security:")
    print(f"{'Params':40s} {'Sig(B)':>7s} {'Sig(bits)':>10s} {'Security':>10s} {'Bottleneck':>25s} {'Hash(bits)':>10s}")
    print("-" * 110)
    seen_sizes = set()
    count = 0
    for params, sec in all_80:
        key = (params.q, params.sig_bytes)
        if key in seen_sizes:
            continue
        seen_sizes.add(key)
        print(f"  GF({params.q:3d}) v={params.v:3d} o={params.o:3d} n={params.n:3d}"
              f"  {params.sig_bytes:5d}   {params.sig_bits:8d}   {sec['security_level']:8.1f}   {sec['bottleneck']:>25s}   {params.recovered_hash_bits:8d}")
        count += 1
        if count >= 15:
            break

    # Search for 100-bit security parameters
    print("\n\n" + "=" * 70)
    print("  SEARCH: Minimum signature size for 100-bit security")
    print("=" * 70)

    candidates_100 = search_params(100, q=256, max_sig_bits=1100)
    candidates_100_gf16 = search_params(100, q=16, max_sig_bits=1100)
    all_100 = candidates_100 + candidates_100_gf16
    all_100.sort(key=lambda x: x[0].sig_bits)

    print(f"\nTop 10 smallest signatures with >= 100-bit security:")
    print(f"{'Params':40s} {'Sig(B)':>7s} {'Sig(bits)':>10s} {'Security':>10s} {'Bottleneck':>25s} {'Hash(bits)':>10s}")
    print("-" * 110)
    seen_sizes = set()
    count = 0
    for params, sec in all_100:
        key = (params.q, params.sig_bytes)
        if key in seen_sizes:
            continue
        seen_sizes.add(key)
        print(f"  GF({params.q:3d}) v={params.v:3d} o={params.o:3d} n={params.n:3d}"
              f"  {params.sig_bytes:5d}   {params.sig_bits:8d}   {sec['security_level']:8.1f}   {sec['bottleneck']:>25s}   {params.recovered_hash_bits:8d}")
        count += 1
        if count >= 15:
            break

    # Print detailed analysis for the best candidates
    print("\n\n### Detailed Analysis: Best candidates ###")

    # Best 80-bit
    if all_80:
        best_80 = all_80[0]
        print_results(best_80[0], best_80[1])

    # Best 100-bit
    if all_100:
        best_100 = all_100[0]
        print_results(best_100[0], best_100[1])

    # Summary table
    print("\n\n" + "=" * 70)
    print("  SUMMARY: Recommended custom parameters for stego channel")
    print("=" * 70)
    print(f"\n  Channel budget after outer ECC: ~500 bits")
    print(f"  Message recovery: YES (digest recovered from P(s))")
    print(f"  No separate digest field needed!")
    print()
    print(f"  {'Target':>10s} {'Best params':>30s} {'Sig bits':>10s} {'Hash bits':>10s} {'PK (cpk)':>12s}")
    print(f"  {'-'*10} {'-'*30} {'-'*10} {'-'*10} {'-'*12}")

    for target, candidates in [(80, all_80), (100, all_100)]:
        if candidates:
            p, s = candidates[0]
            desc = f"GF({p.q}) v={p.v} o={p.o} n={p.n}"
            print(f"  {target:>7d}-bit"
                  f"  {desc:>30s}"
                  f"  {p.sig_bits:>8d}"
                  f"  {p.recovered_hash_bits:>8d}"
                  f"  {p.pk_bytes_pkc:>10,d}")


if __name__ == "__main__":
    main()
