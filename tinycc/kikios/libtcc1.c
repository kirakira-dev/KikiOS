/* Minimal libtcc1 stubs for KikiOS */
/* ARM64 handles most operations natively, so we need very little here */

/* These might be needed for some edge cases */
void __clear_cache(void *beg, void *end) {
    /* ARM64 cache flush - might be needed for JIT but we don't use that */
    (void)beg; (void)end;
}
