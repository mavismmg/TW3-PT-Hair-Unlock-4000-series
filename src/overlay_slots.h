#pragma once

#include "protected_pointer.h"

#include <Windows.h>

namespace single_overlay::slots {

inline bool ImageSlot(HMODULE image, void** slot) noexcept {
  MEMORY_BASIC_INFORMATION memory{};
  const auto address = reinterpret_cast<uintptr_t>(slot);
  return image != nullptr && slot != nullptr &&
         (address % alignof(void*)) == 0 &&
         VirtualQuery(slot, &memory, sizeof(memory)) == sizeof(memory) &&
         memory.State == MEM_COMMIT && memory.Type == MEM_IMAGE &&
         memory.AllocationBase == image &&
         !(memory.Protect & (PAGE_GUARD | PAGE_NOACCESS | PAGE_EXECUTE |
                            PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                            PAGE_EXECUTE_WRITECOPY));
}

inline bool ImageEntry(HMODULE image, void* entry) noexcept {
  MEMORY_BASIC_INFORMATION memory{};
  return image != nullptr && entry != nullptr &&
         VirtualQuery(entry, &memory, sizeof(memory)) == sizeof(memory) &&
         memory.Type == MEM_IMAGE && memory.State == MEM_COMMIT &&
         memory.AllocationBase == image &&
         !(memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
         (memory.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                            PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
}

inline bool Exchange(void** slot, void* expected, void* replacement,
                     DWORD restore) noexcept {
  auto make_writable = [&]() {
    DWORD ignored = 0;
    DWORD observed = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &ignored) ||
        !protected_pointer::QueryProtection(reinterpret_cast<uintptr_t>(slot),
                                            observed, sizeof(void*))) {
      return false;
    }
    return observed == PAGE_READWRITE || observed == PAGE_WRITECOPY;
  };
  auto restore_protection = [&]() {
    return protected_pointer::RestoreProtectionWithRetry(
        slot, sizeof(void*), restore, &VirtualProtect);
  };

  if (!make_writable()) {
    restore_protection();
    return false;
  }
  void* observed = InterlockedCompareExchangePointer(
      reinterpret_cast<void* volatile*>(slot), replacement, expected);
  if (observed != expected) {
    restore_protection();
    return false;
  }
  if (restore_protection() &&
      protected_pointer::ReadPointer(reinterpret_cast<uintptr_t>(slot)) ==
          replacement) {
    return true;
  }
  if (make_writable()) {
    InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(slot),
                                      expected, replacement);
  }
  restore_protection();
  return false;
}

}  // namespace single_overlay::slots
