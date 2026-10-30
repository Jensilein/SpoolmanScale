#pragma once

#include <stdint.h>

// ============================================================
//  WHEN A BACKGROUND WORKER MAY START: THE RULES
//
//  A worker that talks to a server runs on a task of its own (backend_job,
//  web_jobs, tag_probe_job, dried_batch, ams_weights, filament_db, the label
//  jobs, the TLS warm-up and the update check). Its stack comes out of the
//  internal heap the moment the task is created, so the start is refused
//  while too little is free. That used to be 60000 bytes at ten places,
//  written when a TLS handshake was taken to need some 40 kB of internal
//  heap. Since Core 3 mbedTLS allocates in PSRAM
//  (CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC in platformio.ini), so what a request
//  takes from the internal heap is its stack and its connection.
//
//  The floors below come from Eifel-Joe's measurements on a Waveshare 7B
//  (24./25.10.2026): about 22 to 24 kB per background request, encrypted or
//  not, a start at 50000 leaving some 28 kB while it runs. Plain http needs
//  no handshake at all, hence the lower floor. On the WT32 they are on test:
//  the list job logs the free heap at its start and the lowest since boot.
//
//  Only the address decides, the one thing known before the task exists.
//  A worker that may talk to two servers (BamBuddy with Spoolman behind it)
//  takes the higher floor of the two.
//
//  The free sum is not enough on its own: the stack has to fit in one piece,
//  so the largest free block must hold it as well.
//
//  The rules live here without Arduino, the heap or the log, so that
//  test/test_backend_job_rules.py can hold them. The workers ask them through
//  services/worker_heap.h.
// ============================================================

#define BACKEND_JOB_MIN_HEAP_TLS   50000   // https: stack and socket, TLS lives in PSRAM
#define BACKEND_JOB_MIN_HEAP_PLAIN 40000   // http: stack plus plain sockets

// A refused start is asked for again on every pass of the loop; its log
// line is written at most this often.
#define BACKEND_JOB_REFUSAL_LOG_MS 60000

// The free internal heap a worker needs before it may start, for a server
// at this base URL. "https://" in any case of letters, an empty address or
// none: the TLS floor, because nothing says it is plain. Anything else is
// http, a bare address without scheme included (backendComposeBase reads it
// that way).
uint32_t backendJobMinHeap(const char* base_url);

// The higher of the two floors, for a worker that may talk to either address.
uint32_t backendJobMinHeapBoth(const char* first, const char* second);

// True when a task with a stack of stack_bytes may start: at least floor_bytes
// free in all, and the largest free block big enough for the stack.
bool workerHeapAllows(uint32_t free_bytes, uint32_t largest_block,
                      uint32_t floor_bytes, uint32_t stack_bytes);

// Says whether a line that can repeat on every pass may be written now: the
// first time, then once per interval. The refused calls are counted so the
// line can say how many it stands for. Zero-initialise it (LogGate g{}); one
// gate for one line, used from one task.
struct LogGate {
  bool     armed;       // a line has been written, last_ms is valid
  uint32_t last_ms;     // when it was written
  uint32_t suppressed;  // calls refused since then
};

// True for the first call and then once interval_ms has passed since the
// last line, counted modulo 2^32 so a wrapping millis() does not stall it.
// Every other call counts as suppressed. No gate: always due.
bool logGateDue(LogGate* g, uint32_t now_ms, uint32_t interval_ms);

// The calls refused since the last line, which it also clears. 0 for no gate.
uint32_t logGateTakeSuppressed(LogGate* g);

// Makes the next call due again and forgets the count, for when the refusals
// have stopped and the next episode should start with a line of its own.
void logGateReset(LogGate* g);
