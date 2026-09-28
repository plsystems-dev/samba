# AVX2 field batching

This implementation accelerates the existing public `mul3`, `mul4` and `sqr4`
helpers used by Curve25519/Ed25519. Four independent operations occupy AVX2's
four unsigned 64-bit lanes. The public five-limb representation, curve
algorithms, point encodings and signature acceptance rules are preserved.

Inputs are split into alternating 26/25-bit digits for unsigned 32×32→64
multiplication. The header documents the loose-input bounds, weighted
convolution and carry reduction. Signing and private-key derivation retain
the original scalar operations because the vector helpers do not clear stack
scratch. AVX-512 dispatch is unchanged.

`FD_F25519_AVX2` defaults to `FD_HAS_AVX` in the reference backend. Setting it
to `0` selects the original scalar batching. SHA-512 remains AVX2-accelerated
in both benchmark variants.

## Iris measurements, 28 September 2026

Base: Harmonic `eed6c122a9756fc1f6e5021a636a3d6961a76952`.
Host: AMD EPYC 7443, GCC 13.3.0, `linux_gcc_zen3`, performance governor.
Tests used previously idle isolated CPU 3; its SMT sibling was already offline.
The validator continued running passively, with no service or system changes.

Five paired rounds alternated variant order. Each round verified 25,000
messages of each size, rotating through 64 deterministic public keys/messages.
Key generation, signing and warmup were outside the measured interval.

| Message bytes | Scalar median, µs/verify | AVX2 median, µs/verify | Throughput increase |
| ---: | ---: | ---: | ---: |
| 128 | 43.657 | 42.374 | 3.03% |
| 512 | 43.993 | 42.526 | 3.45% |
| 1232 | 44.850 | 43.359 | 3.44% |

All 15 size/round comparisons favored AVX2. See
[benchmark-results.json](benchmark-results.json) for the samples. These are
single-core crypto measurements, not measured validator throughput or voting
improvements. The candidate was not installed into either validator.

## Checks completed on Iris

- 20,657 field-input sets compared against Fiat, including loose upper bounds,
 carry patterns, lane independence, in-place aliases and tight output bounds.
- Existing Ed25519 tests, including Wycheproof, CCTV and batch cases;
 signature-malleability, X25519 and Ristretto tests.
- 768 complete-verification cases with identical input/result digests and
 error-code histograms between scalar and AVX2 builds.
- All tests above under Clang 18.1.3 with AddressSanitizer and
 UndefinedBehaviorSanitizer, configured to halt on an error.
- Optimized assembly inspection: the signing call graph contains no new AVX2
 field-helper calls.

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
