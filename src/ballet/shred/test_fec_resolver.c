#include "fd_shredder.h"
#include "fd_shred.h"
#include "fd_fec_set.h"


/* An entry batch of 64 entries with 20 transactions per entry takes up
   about 256 kB, for about 200B/txn, which seems reasonable.  We'll do a
   10 MB entry batch, which is about 50k transactions. */
#define PERF_TEST_SZ (10UL*1024UL*1024UL)
uchar perf_test_entry_batch[ PERF_TEST_SZ ];

uchar fec_set_memory[ 9UL*2048UL * (FD_REEDSOL_DATA_SHREDS_MAX+FD_REEDSOL_PARITY_SHREDS_MAX) ];

uchar resolver_mem[ 1024UL*1024UL ] __attribute__((aligned(FD_FEC_RESOLVER_ALIGN)));

/* First 32B of what Solana calls the private key is what we call the
   private key, second 23B are what we call the public key. */
FD_IMPORT_BINARY( test_private_key, "src/ballet/shred/fixtures/demo-shreds.key"  );

FD_IMPORT_BINARY( test_bin,         "src/ballet/shred/fixtures/demo-shreds.bin"  );

static int
sets_eq( fd_fec_set_t const * a, fd_fec_set_t const * b ) {
  if( (a==NULL) ^ (b==NULL) ) return 0;

  if( a->data_shred_cnt   != b->data_shred_cnt   ) return 0;
  if( a->parity_shred_cnt != b->parity_shred_cnt ) return 0;

  for( ulong i=0UL; i<a->data_shred_cnt; i++ ) if( !fd_memeq( a->data_shreds[i], b->data_shreds[i], FD_SHRED_MIN_SZ ) ) {
    FD_LOG_NOTICE(( "data shred %lu not equal", i ));
    FD_LOG_HEXDUMP_NOTICE(( "a:", a->data_shreds[i], FD_SHRED_MIN_SZ ));
    FD_LOG_HEXDUMP_NOTICE(( "b:", b->data_shreds[i], FD_SHRED_MIN_SZ ));
    return 0;
  }
  for( ulong i=0UL; i<a->parity_shred_cnt; i++ ) if( !fd_memeq( a->parity_shreds[i], b->parity_shreds[i], FD_SHRED_MAX_SZ ) ) {
    FD_LOG_NOTICE(( "parity shred %lu not equal", i ));
    FD_LOG_HEXDUMP_NOTICE(( "a:", a->parity_shreds[i], FD_SHRED_MAX_SZ ));
    FD_LOG_HEXDUMP_NOTICE(( "b:", b->parity_shreds[i], FD_SHRED_MAX_SZ ));
    return 0;
  }

  return 1;
}

static inline uchar *
allocate_fec_set( fd_fec_set_t * set, uchar * ptr ) {
  for( ulong j=0UL; j<FD_REEDSOL_DATA_SHREDS_MAX;   j++ ) { set->data_shreds[   j ] = ptr;     ptr += 2048UL; }
  for( ulong j=0UL; j<FD_REEDSOL_PARITY_SHREDS_MAX; j++ ) { set->parity_shreds[ j ] = ptr;     ptr += 2048UL; }
  return ptr;
}

static void
test_one_batch( void ) {
  fd_shredder_t _shredder[ 1 ];
  FD_TEST( _shredder==fd_shredder_new( _shredder ) );
  fd_shredder_t * shredder = fd_shredder_join( _shredder );           FD_TEST( shredder );


  fd_entry_batch_meta_t meta[1];
  fd_memset( meta, 0, sizeof(fd_entry_batch_meta_t) );
  meta->block_complete = 1;

  FD_TEST( fd_shredder_init_batch( shredder, test_bin, test_bin_sz, meta ) );

  fd_fec_set_t _set[ 1 ];
  fd_fec_set_t out_sets[ 4UL ];
  uchar * ptr = fec_set_memory;
  ptr = allocate_fec_set( _set, ptr );

  for( ulong i=0UL; i<4UL; i++ )  ptr = allocate_fec_set( out_sets+i, ptr );

  for( ulong i=0UL; i<7UL; i++ ) {
    fd_fec_set_t * set = fd_shredder_next_fec_set( shredder, test_private_key, test_private_key+32UL, _set );

    fd_fec_resolver_t * resolver;
    resolver = fd_fec_resolver_join( fd_fec_resolver_new( resolver_mem, 4UL, 1UL, out_sets, test_private_key+32UL ) );
    for( ulong j=0UL; j<set->data_shred_cnt; j++ ) FD_TEST( !fd_fec_resolver_add_shred( resolver, fd_shred_parse( set->data_shreds[ j ], 2048UL ), 2048UL ) );
    FD_TEST( sets_eq( set, fd_fec_resolver_add_shred( resolver, fd_shred_parse( set->parity_shreds[ 0UL ], 2048UL ), 2048UL ) ) );

    resolver = fd_fec_resolver_join( fd_fec_resolver_new( resolver_mem, 4UL, 1UL, out_sets, test_private_key+32UL ) );
    for( ulong j=0UL; j<set->parity_shred_cnt-1UL; j++ ) FD_TEST( !fd_fec_resolver_add_shred( resolver, fd_shred_parse( set->parity_shreds[ j ], 2048UL ), 2048UL ) );
    for( ulong j=0UL; j<10UL;                      j++ ) FD_TEST( !fd_fec_resolver_add_shred( resolver, fd_shred_parse( set->parity_shreds[ 0 ], 2048UL ), 2048UL ) );
    FD_TEST( sets_eq( set, fd_fec_resolver_add_shred( resolver, fd_shred_parse( set->data_shreds[ 0UL ], 2048UL ), 2048UL ) ) );

    resolver = fd_fec_resolver_join( fd_fec_resolver_new( resolver_mem, 4UL, 1UL, out_sets, test_private_key+32UL ) );
    for( ulong j=1UL; j<set->parity_shred_cnt; j++ ) FD_TEST( !fd_fec_resolver_add_shred( resolver, fd_shred_parse( set->parity_shreds[ j ], 2048UL ), 2048UL ) );
    FD_TEST( sets_eq( set, fd_fec_resolver_add_shred( resolver, fd_shred_parse( set->parity_shreds[ 0UL ], 2048UL ), 2048UL ) ) );
  }
  FD_TEST( fd_shredder_fini_batch( shredder ) );
}


static void
test_interleaved( void ) {
  fd_shredder_t _shredder[ 1 ];
  FD_TEST( _shredder==fd_shredder_new( _shredder ) );
  fd_shredder_t * shredder = fd_shredder_join( _shredder );           FD_TEST( shredder );


  fd_entry_batch_meta_t meta[1];
  fd_memset( meta, 0, sizeof(fd_entry_batch_meta_t) );
  meta->block_complete = 1;

  FD_TEST( fd_shredder_init_batch( shredder, test_bin, test_bin_sz, meta ) );

  fd_fec_set_t _set[ 2 ];
  uchar * ptr = fec_set_memory;
  ptr = allocate_fec_set( _set+0, ptr );
  ptr = allocate_fec_set( _set+1, ptr );

  fd_fec_set_t out_sets[ 4UL ];
  for( ulong i=0UL; i<4UL; i++ ) ptr = allocate_fec_set( out_sets+i, ptr );


  fd_fec_set_t * set0 = fd_shredder_next_fec_set( shredder, test_private_key, test_private_key+32UL, _set     );
  fd_fec_set_t * set1 = fd_shredder_next_fec_set( shredder, test_private_key, test_private_key+32UL, _set+1UL );
  FD_TEST( fd_shredder_fini_batch( shredder ) );

  fd_fec_resolver_t * resolver;
  resolver = fd_fec_resolver_join( fd_fec_resolver_new( resolver_mem, 4UL, 1UL, out_sets, test_private_key+32UL ) );
  for( ulong j=0UL; j<set0->data_shred_cnt; j++ ) {
    FD_TEST( !fd_fec_resolver_add_shred( resolver, fd_shred_parse( set0->data_shreds[ j ], 2048UL ), 2048UL ) );
    FD_TEST( !fd_fec_resolver_add_shred( resolver, fd_shred_parse( set1->data_shreds[ j ], 2048UL ), 2048UL ) );
  }
  FD_TEST( sets_eq( set0, fd_fec_resolver_add_shred( resolver, fd_shred_parse( set0->parity_shreds[ 0UL ], 2048UL ), 2048UL ) ) );
  FD_TEST( sets_eq( set1, fd_fec_resolver_add_shred( resolver, fd_shred_parse( set1->parity_shreds[ 0UL ], 2048UL ), 2048UL ) ) );

  /*
  resolver = fd_fec_resolver_join( fd_fec_resolver_new( resolver_mem, 4UL, 1UL, out_sets, test_private_key+32UL ) );
  for( ulong j=0UL; j<set->parity_shred_cnt-1UL; j++ ) FD_TEST( !fd_fec_resolver_add_shred( resolver, fd_shred_parse( set->parity_shreds[ j ], 2048UL ), 2048UL ) );
  for( ulong j=0UL; j<10UL;                      j++ ) FD_TEST( !fd_fec_resolver_add_shred( resolver, fd_shred_parse( set->parity_shreds[ 0 ], 2048UL ), 2048UL ) );
  FD_TEST( sets_eq( set, fd_fec_resolver_add_shred( resolver, fd_shred_parse( set->data_shreds[ 0UL ], 2048UL ), 2048UL ) ) );

  resolver = fd_fec_resolver_join( fd_fec_resolver_new( resolver_mem, 4UL, 1UL, out_sets, test_private_key+32UL ) );
  for( ulong j=1UL; j<set->parity_shred_cnt; j++ ) FD_TEST( !fd_fec_resolver_add_shred( resolver, fd_shred_parse( set->parity_shreds[ j ], 2048UL ), 2048UL ) );
  FD_TEST( sets_eq( set, fd_fec_resolver_add_shred( resolver, fd_shred_parse( set->parity_shreds[ 0UL ], 2048UL ), 2048UL ) ) );
  */
}


static void
test_rolloff( void ) {
  fd_shredder_t _shredder[ 1 ];
  FD_TEST( _shredder==fd_shredder_new( _shredder ) );
  fd_shredder_t * shredder = fd_shredder_join( _shredder );           FD_TEST( shredder );


  fd_entry_batch_meta_t meta[1];
  fd_memset( meta, 0, sizeof(fd_entry_batch_meta_t) );
  meta->block_complete = 1;

  FD_TEST( fd_shredder_init_batch( shredder, test_bin, test_bin_sz, meta ) );

  fd_fec_set_t _set[ 3 ];
  uchar * ptr = fec_set_memory;
  ptr = allocate_fec_set( _set+0, ptr );
  ptr = allocate_fec_set( _set+1, ptr );
  ptr = allocate_fec_set( _set+2, ptr );

  fd_fec_set_t out_sets[ 2UL ];
  for( ulong i=0UL; i<2UL; i++ ) ptr = allocate_fec_set( out_sets+i, ptr );


  fd_fec_set_t * set0 = fd_shredder_next_fec_set( shredder, test_private_key, test_private_key+32UL, _set     );
  fd_fec_set_t * set1 = fd_shredder_next_fec_set( shredder, test_private_key, test_private_key+32UL, _set+1UL );
  fd_fec_set_t * set2 = fd_shredder_next_fec_set( shredder, test_private_key, test_private_key+32UL, _set+2UL );
  FD_TEST( fd_shredder_fini_batch( shredder ) );

  fd_fec_resolver_t * resolver;
  resolver = fd_fec_resolver_join( fd_fec_resolver_new( resolver_mem, 2UL, 1UL, out_sets, test_private_key+32UL ) );
  for( ulong j=0UL; j<set0->data_shred_cnt; j++ ) {
    FD_TEST( !fd_fec_resolver_add_shred( resolver, fd_shred_parse( set0->data_shreds[ j ], 2048UL ), 2048UL ) );
  }
  for( ulong j=0UL; j<set1->data_shred_cnt; j++ ) {
    FD_TEST( !fd_fec_resolver_add_shred( resolver, fd_shred_parse( set1->data_shreds[ j ], 2048UL ), 2048UL ) );
  }
  /* At this point we have set0, set1 */
  for( ulong j=0UL; j<set2->data_shred_cnt; j++ ) {
    FD_TEST( !fd_fec_resolver_add_shred( resolver, fd_shred_parse( set2->data_shreds[ j ], 2048UL ), 2048UL ) );
  }
  /* Now, set0 kicked out and added to done so it'll be ignored. set1
     and set2 are in progress. */
  FD_TEST( !fd_fec_resolver_add_shred( resolver, fd_shred_parse( set0->parity_shreds[ 0UL ], 2048UL ), 2048UL ) );
  FD_TEST( sets_eq( set1, fd_fec_resolver_add_shred( resolver, fd_shred_parse( set1->parity_shreds[ 0UL ], 2048UL ), 2048UL ) ) );
  FD_TEST( sets_eq( set2, fd_fec_resolver_add_shred( resolver, fd_shred_parse( set2->parity_shreds[ 0UL ], 2048UL ), 2048UL ) ) );

}

static void
perf_test( void ) {
  for( ulong i=0UL; i<PERF_TEST_SZ; i++ )  perf_test_entry_batch[ i ] = (uchar)i;

  fd_entry_batch_meta_t meta[1];
  fd_memset( meta, 0, sizeof(fd_entry_batch_meta_t) );
  meta->block_complete = 1;

  fd_shredder_t _shredder[ 1 ];
  FD_TEST( _shredder==fd_shredder_new( _shredder ) );
  fd_shredder_t * shredder = fd_shredder_join( _shredder );           FD_TEST( shredder );

  fd_fec_set_t _set[ 1 ];
  uchar * ptr = fec_set_memory;
  for( ulong j=0UL; j<FD_REEDSOL_DATA_SHREDS_MAX;   j++ ) { _set->data_shreds[   j ] = ptr;     ptr += 2048UL; }
  for( ulong j=0UL; j<FD_REEDSOL_PARITY_SHREDS_MAX; j++ ) { _set->parity_shreds[ j ] = ptr;     ptr += 2048UL; }

  ulong iterations = 100UL;
  long dt = -fd_log_wallclock();
  for( ulong iter=0UL; iter<iterations; iter++ ) {
    fd_shredder_init_batch( shredder, perf_test_entry_batch, PERF_TEST_SZ, meta );

    ulong sets_cnt = fd_shredder_count_fec_sets( PERF_TEST_SZ );
    for( ulong j=0UL; j<sets_cnt; j++ ) {
      fd_shredder_next_fec_set( shredder, test_private_key, test_private_key+32UL, _set );
    }
    fd_shredder_fini_batch( shredder );
  }
  dt += fd_log_wallclock();
  FD_LOG_NOTICE(( "%li ns/10 MB entry batch = %.3f Gbps", dt/(long)iterations, (double)(8UL * iterations * PERF_TEST_SZ)/(double)dt ));

}


int
main( int     argc,
      char ** argv ) {
  fd_boot( &argc, &argv );

  (void)perf_test;

  test_interleaved();
  test_one_batch();
  test_rolloff();


  FD_LOG_NOTICE(( "pass" ));
  fd_halt();
  return 0;
}
