#pragma once

#define fatal_sizeof(S)  char(*S##__LINE__)[sizeof(S)] = 1;
#define fatal_alignof(S) char(*S##__LINE__)[alignof(S)] = 1;
#define fatal_number(x)  char(*_##__LINE__)[x] = 1;