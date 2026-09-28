#include "fd_ed25519.h"
#include <signal.h>
#include <stddef.h>

typedef void (*mul_fn_t)( ulong out[8], ulong const scalar[4] );
extern void edwards25519_scalarmulbase    ( ulong out[8], ulong const scalar[4] );
extern void edwards25519_scalarmulbase_alt( ulong out[8], ulong const scalar[4] );

typedef struct {
  ulong abi_mask;
  ulong scratch_or;
  ulong nonzero_words;
  ulong rsp_before;
  ulong rsp_after;
} report_t;

extern void fd_test_signing_stack( mul_fn_t, ulong out[8], ulong const scalar[4], report_t * );
FD_STATIC_ASSERT( sizeof(report_t)==40UL, assembly_report_size );
FD_STATIC_ASSERT( offsetof(report_t,rsp_after)==32UL, assembly_report_offset );

/* Public test scalars and affine points, independently calculated with
   src/wiredancer/py/ref_ed25519.py.  The second scalar is the clamped hash
   of RFC 8032 test 1's seed; its compressed output is that test's public key. */
static struct {
  char const * name;
  ulong scalar[4];
  ulong point[8];
} const fixtures[] = {
  { "one",
    { 0x0000000000000001UL, 0x0000000000000000UL, 0x0000000000000000UL, 0x0000000000000000UL },
    { 0xc9562d608f25d51aUL, 0x692cc7609525a7b2UL, 0xc0a4e231fdd6dc5cUL, 0x216936d3cd6e53feUL,
      0x6666666666666658UL, 0x6666666666666666UL, 0x6666666666666666UL, 0x6666666666666666UL } },
  { "rfc8032_test1",
    { 0xcb33284f86837c30UL, 0x3c010ac0f12e7a42UL, 0xa3c080d96827fffdUL, 0x4fe94d9006f020a5UL },
    { 0xb12786bd777645ceUL, 0xc513d47253187c24UL, 0x2297e08d60d0f620UL, 0x55d0e09a2b9d3429UL,
      0xb70ab18201985ad7UL, 0x3a0764c9d3fe4bd5UL, 0x2523a6daf372e10eUL, 0x1a5107f7681a02afUL } }
};

int
main( int argc, char ** argv ) {
  fd_boot( &argc, &argv );
  FD_TEST( argc==1 );
  struct { char const * name; mul_fn_t fn; } const backends[] = {
#if defined(__ADX__) && defined(__BMI2__)
    { "adx", edwards25519_scalarmulbase },
#endif
    { "alt", edwards25519_scalarmulbase_alt }
  };

  sigset_t all, old;
  FD_TEST( !sigfillset( &all ) );
  for( ulong f=0UL; f<sizeof(backends)/sizeof(backends[0]); f++ ) {
    for( ulong k=0UL; k<sizeof(fixtures)/sizeof(fixtures[0]); k++ ) {
      struct { ulong before[2]; ulong point[8]; ulong after[2]; } out;
      report_t report = {0};
      fd_memset( &out, 0xa5, sizeof(out) );

      /* A delivered signal could overwrite dead stack before the probe's
         first instruction.  Inspect in assembly before returning to C. */
      FD_TEST( !sigprocmask( SIG_BLOCK, &all, &old ) );
      fd_test_signing_stack( backends[f].fn, out.point, fixtures[k].scalar, &report );
      FD_TEST( !sigprocmask( SIG_SETMASK, &old, NULL ) );

      FD_LOG_NOTICE(( "%s %s: scratch_or=%016lx nonzero_words=%lu abi_mask=%lu",
                      backends[f].name, fixtures[k].name,
                      report.scratch_or, report.nonzero_words, report.abi_mask ));
      FD_TEST( !memcmp( out.point, fixtures[k].point, sizeof(out.point) ) );
      for( ulong i=0UL; i<2UL; i++ ) {
        FD_TEST( out.before[i]==0xa5a5a5a5a5a5a5a5UL );
        FD_TEST( out.after [i]==0xa5a5a5a5a5a5a5a5UL );
      }
      FD_TEST( report.abi_mask==0UL );
      FD_TEST( report.rsp_before==report.rsp_after );
      FD_TEST( report.scratch_or==0UL );
      FD_TEST( report.nonzero_words==0UL );
    }
  }
  FD_LOG_NOTICE(( "pass" ));
  fd_halt();
  return 0;
}
