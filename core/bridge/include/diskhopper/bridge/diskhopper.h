#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handles: Swift treats each as UnsafeMutableRawPointer. */
typedef void DhEngine;
typedef void DhScan;
typedef void DhCleanResult;

/* Bucket selectors (imported into Swift as Int32 constants). */
#define DhBucketSafe 0
#define DhBucketReview 1
#define DhBucketProtected 2

/* Engine holds the home directory used by the classifier. */
DhEngine* dh_engine_create(const char* home_dir);
void dh_engine_destroy(DhEngine* engine);

/* Scan + classify a path. Returns a handle; NULL if the engine was NULL.
   On failure dh_scan_error() returns the reason. Call dh_scan_destroy(). */
DhScan* dh_scan(DhEngine* engine, const char* path, int threads);
const char* dh_scan_error(const DhScan* scan);
void dh_scan_destroy(DhScan* scan);

const char* dh_scan_root(const DhScan* scan);
uint64_t dh_scan_apparent(const DhScan* scan);
uint64_t dh_scan_allocated(const DhScan* scan);
uint64_t dh_scan_files(const DhScan* scan);
uint64_t dh_scan_dirs(const DhScan* scan);
uint64_t dh_scan_symlinks(const DhScan* scan);
uint64_t dh_scan_errors(const DhScan* scan);

uint64_t dh_scan_bucket_bytes(const DhScan* scan, int bucket);

/* Cleanable entries discovered for a bucket (Safe or Review). */
size_t dh_scan_cleanable_count(const DhScan* scan, int bucket);
const char* dh_scan_cleanable_path(const DhScan* scan, int bucket,
                                   size_t index);
const char* dh_scan_cleanable_rule_id(const DhScan* scan, int bucket,
                                      size_t index);
const char* dh_scan_cleanable_rule_name(const DhScan* scan, int bucket,
                                        size_t index);
uint64_t dh_scan_cleanable_size(const DhScan* scan, int bucket,
                                size_t index);

/* Execute cleanup on a scanned snapshot.
   include_safe / include_review select buckets.
   permanent_safe: SAFE items are permanently deleted (requires the Time
   Machine gate) instead of being moved to Trash.
   audit_path: append an audit log to this file ("" disables).
   Call dh_clean_destroy() on the result. */
DhCleanResult* dh_clean(DhScan* scan, int include_safe, int include_review,
                        int permanent_safe, const char* audit_path);
size_t dh_clean_planned(const DhCleanResult* result);
size_t dh_clean_succeeded(const DhCleanResult* result);
size_t dh_clean_failed(const DhCleanResult* result);
uint64_t dh_clean_bytes_freed(const DhCleanResult* result);
uint64_t dh_clean_bytes_failed(const DhCleanResult* result);
const char* dh_clean_first_error(const DhCleanResult* result);
void dh_clean_destroy(DhCleanResult* result);

int dh_time_machine_available(void);

#ifdef __cplusplus
}
#endif