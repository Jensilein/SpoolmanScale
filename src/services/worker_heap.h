#pragma once

#include <stdint.h>

#include "services/backend_job_rules.h"

// ============================================================
//  WORKER START: THE HEAP CHECK EVERY BACKGROUND TASK ASKS
//
//  Reads the internal heap and its largest free block and applies the rules
//  of backend_job_rules.h. A refusal is logged at most once per
//  BACKEND_JOB_REFUSAL_LOG_MS through the caller's gate, because a refused
//  start is asked for again on every pass of the loop.
// ============================================================

// The floor for a worker that talks to the active backend: the higher of the
// backend's address and the filament database's, which differ only for
// BamBuddy with Spoolman behind it.
uint32_t workerFloorBackend();

// The floor for a worker that talks to GitHub, always https.
uint32_t workerFloorTls();

// True when a task with this stack may start now. `who` names the worker in
// the log line; nullptr or no gate keeps a refusal silent.
bool workerHeapReady(const char* who, uint32_t floor_bytes, uint32_t stack_bytes,
                     LogGate* gate);
