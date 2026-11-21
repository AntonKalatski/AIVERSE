// MinGW-w64 lacks the Windows SDK's EventToken.h, which WebView2.h includes (MSVC builds never see this folder).
#pragma once
#include <stdint.h>
#ifndef __EVENTREGISTRATIONTOKEN_DEFINED
#define __EVENTREGISTRATIONTOKEN_DEFINED
typedef struct EventRegistrationToken { int64_t value; } EventRegistrationToken;
#endif
