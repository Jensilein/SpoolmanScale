#include "services/worker_heap.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include "hardware/sd_logger.h"
#include "services/backend.h"
#include "services/backend_api.h"

uint32_t workerFloorBackend() {
  return backendJobMinHeapBoth(backendBaseUrl(), backendFdbBaseUrl());
}

uint32_t workerFloorTls() {
  return (uint32_t)BACKEND_JOB_MIN_HEAP_TLS;
}

bool workerHeapReady(const char* who, uint32_t floor_bytes, uint32_t stack_bytes,
                     LogGate* gate) {
  const uint32_t free_bytes = (uint32_t)ESP.getFreeHeap();
  const uint32_t largest = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  if (workerHeapAllows(free_bytes, largest, floor_bytes, stack_bytes)) {
    // The refusals have stopped: the next episode gets a line of its own.
    logGateReset(gate);
    return true;
  }
  if (who && gate && logGateDue(gate, millis(), BACKEND_JOB_REFUSAL_LOG_MS)) {
    logSDf("%s: postponed, heap %u of %u, largest block %u for a %u stack (%u more since the last line)",
           who, (unsigned)free_bytes, (unsigned)floor_bytes, (unsigned)largest,
           (unsigned)stack_bytes, (unsigned)logGateTakeSuppressed(gate));
  }
  return false;
}
