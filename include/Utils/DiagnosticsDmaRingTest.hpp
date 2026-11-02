#pragma once

// Only the dedicated offline executable compiles these controls.
#ifdef UNLEASHED_DIAGNOSTICS_DMA_RING_TEST
#include <cstdint>
namespace Diagnostics::DmaRingTest {
enum class Gate { None, WriterBeforeCommit, ReaderBeforeCopy, ReaderAfterCopy };
void Reset(); // Quiescent test process only; does not reset cumulative counters.
void SetClock(uint64_t tickMs);
void Arm(Gate gate);
bool Entered();
void Release();
}
#endif
