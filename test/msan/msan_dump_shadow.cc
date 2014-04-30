// RUN: %clangxx_msan -m64 -O0 -g %s -o %t && %t >%t.out 2>&1
// RUN: FileCheck %s < %t.out

// RUN: %clangxx_msan -fsanitize-memory-track-origins -m64 -O0 -g %s -o %t && %t >%t.out 2>&1
// RUN: FileCheck %s < %t.out

// RUN: %clangxx_msan -fsanitize-memory-track-origins=2 -m64 -O0 -g %s -o %t && %t >%t.out 2>&1
// RUN: FileCheck %s < %t.out

#include <sanitizer/msan_interface.h>

int main(void) {
  char *p = new char[16];
  __msan_dump_shadow(p, 5);
  delete[] p;
  const char *q = "abc";
  __msan_dump_shadow(q, 3);
  return 0;
}

// CHECK: ff ff ff ff ff
// CHECK: 00 00 00