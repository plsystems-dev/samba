# AVX2 field batching

This implementation accelerates the existing public `mul3`, `mul4` and `sqr4`
helpers used by Curve25519/Ed25519. Four independent operations occupy AVX2's
four unsigned 64-bit lanes. The public five-limb representation, curve
algorithms, point encodings and signature acceptance rules are preserved.

Inputs are split into alternating 26/25-bit digits for unsigned 32×32→64
multiplication. The header documents the loose-input bounds, weighted
convolution and carry reduction. Multiplication sums wrapped terms before
applying the factor of 19, reducing temporary vectors and register pressure.
Vector transposes pack and unpack the first four limbs. Signing and private-key
derivation retain
the original scalar operations because the vector helpers do not clear stack
scratch. AVX-512 dispatch is unchanged.

`FD_F25519_AVX2` defaults to `FD_HAS_AVX` in the reference backend. Setting it
to `0` selects the original scalar batching. SHA-512 remains AVX2-accelerated
in both benchmark variants.

## Iris measurements, 28 September 2026

Base: Harmonic `eed6c122a9756fc1f6e5021a636a3d6961a76952`.
Host: AMD EPYC 7443, performance governor. Compilers: GCC 13.3.0 with
`linux_gcc_zen3` and Clang 18.1.3 with `linux_clang_zen3`.
Tests used previously idle isolated CPU 3; its SMT sibling was already offline.
The validator continued running passively, with no service or system changes.

Five paired GCC rounds alternated variant order. The additional Clang comparison
rotated and reversed the order of GCC scalar, Clang scalar and Clang AVX2 builds
over five rounds. Each round verified 25,000
messages of each size, rotating through 64 deterministic public keys/messages.
Key generation, signing and warmup were outside the measured interval.

| Message bytes | GCC scalar, µs/verify | GCC AVX2, µs/verify | Throughput increase |
| ---: | ---: | ---: | ---: |
| 128 | 43.616 | 39.287 | 11.02% |
| 512 | 43.944 | 39.618 | 10.92% |
| 1232 | 44.823 | 40.522 | 10.61% |

Clang provides another measured opportunity with the same code:

| Message bytes | GCC scalar control, µs/verify | Clang AVX2, µs/verify | Throughput increase |
| ---: | ---: | ---: | ---: |
| 128 | 43.647 | 36.913 | 18.24% |
| 512 | 43.950 | 37.279 | 17.90% |
| 1232 | 44.816 | 38.105 | 17.61% |

Against Clang's own scalar baseline, the AVX2 gain is 21.4–22.1%. Clang's scalar
baseline is slower than GCC's: changing compilers alone does not explain the
improvement. Existing CPU profiles already use `-O3 -march=znver3 -mtune=znver3`;
stack protection and frame pointers were retained.

All 15 size/round comparisons in each comparison set favored AVX2. See
[benchmark-results.json](benchmark-results.json) for the samples. These are
single-core crypto measurements, not measured validator throughput or voting
improvements. The source works with the existing GCC build. The Clang result
does not establish that a full validator built with Clang is faster; that
requires separate integration and workload validation. The candidate was not
installed into either validator.

## Optimization experiments

The initial AVX2 implementation improved GCC verification throughput by 3.0–3.5%.
Eight subsequent source variants were checked against the complete verification
API on Iris, using two timed runs per variant and bracketing control runs:

| Variant | Decision |
| --- | --- |
| Scalar three-operation multiplication batches | Slower; rejected |
| Forced inlining of vector multiply/square helpers | Slower; rejected |
| Group wrapped multiplication terms before multiplying by 19 | Faster; retained |
| Transpose-based input/output packing | Faster; retained |
| Both preceding changes together | Fastest GCC variant; retained |
| Keep partial point doubling in vector lanes | Slower; rejected |
| Packed point doubling plus both retained changes | Slower; rejected |
| Group wrapped squaring terms on top of the retained changes | Slower; rejected |

Only the retained changes are in this implementation. Their arithmetic
coefficients, accumulator bounds and memory accesses were independently reviewed.
The final retained implementation then underwent the five-round measurements
above, rather than relying on the shorter screening runs.

## Checks completed on Iris

- 20,657 field-input sets compared against Fiat, including loose upper bounds,
 carry patterns, lane independence, in-place aliases and tight output bounds.
- Existing Ed25519 tests, including Wycheproof, CCTV and batch cases;
 signature-malleability, X25519 and Ristretto tests, under both GCC and Clang.
- 768 complete-verification cases with identical input/result digests and
 error-code histograms between scalar and AVX2 builds.
- All tests above under Clang 18.1.3 with AddressSanitizer and
 UndefinedBehaviorSanitizer, configured to halt on an error.
- GCC and Clang optimized assembly inspection: the signing call graph contains
 no new AVX2 field-helper calls.

The tests build the crypto libraries and test executables only. No full
validator integration or live validator benchmark was performed.

## Reproduce in a disposable source checkout

```sh
printf '%s\n' 'CPPFLAGS+=-DFD_F25519_AVX2=0' > config/extra/with-avx2-scalar.mk
make -j2 MACHINE=linux_gcc_zen3 FD_NODEPS=1 \
  BUILDDIR=linux/gcc/zen3-scalar EXTRAS=avx2-scalar bench_ed25519_avx2
make -j2 MACHINE=linux_gcc_zen3 FD_NODEPS=1 \
  BUILDDIR=linux/gcc/zen3-avx2 bench_ed25519_avx2 test_f25519_avx2 \
  test_ed25519 test_ed25519_signature_malleability test_x25519 test_ristretto255
make -j2 MACHINE=linux_clang_zen3 FD_NODEPS=1 \
  BUILDDIR=linux/clang/zen3-avx2 bench_ed25519_avx2
make -j2 MACHINE=linux_clang_zen3 FD_NODEPS=1 \
  BUILDDIR=linux/clang/zen3-scalar EXTRAS=avx2-scalar bench_ed25519_avx2
```

Run both benchmark executables on the same available CPU with `--tile-cpus f`
and `--iterations 25000`. Compare their `DIFF` lines before their `BENCH` lines.
For example, on the tested host:

```sh
taskset -c 3 nice -n 10 build/linux/gcc/zen3-avx2/unit-test/bench_ed25519_avx2 \
  --tile-cpus f --iterations 25000
```

The floating tile option preserves the external CPU affinity and nice level.
The benchmark uses synthetic keys only. For sanitizer builds use
`MACHINE=linux_clang_zen3 EXTRAS="asan ubsan"` and a separate build directory.
