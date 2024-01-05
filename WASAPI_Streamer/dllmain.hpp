#pragma once

#include <cstdint>
#include <cstdlib>
#include <bitset>
#include <iostream>

#include <atlbase.h>
#include <audioclient.h>
#include <avrt.h>
#include <minwindef.h>
#include <mmdeviceapi.h>
#include <processthreadsapi.h>
#include <windows.h>
#include <winnt.h>

#include "mutex.hpp"

// NOTE: Avoid name-mangling with `extern "C"`.
//       However, name-mangling due to calling-convention specifications is unavoidable.
//       (Export functions through the module-definition file.)
// NOTE: The calling-convention of Win32-API and C# is StdCall.
// NOTE: MDF -> Module Definition File.
// REF: https://learn.microsoft.com/en-us/cpp/build/exporting-from-a-dll-using-declspec-dllexport?view=msvc-170
#define USE_MDF

#ifdef USE_MDF
#define DllExport
#else
#define DllExport __declspec(dllexport)
#endif  // USE_MDF


// ----- Types -----
typedef void(__stdcall* WASAPIPCMStreamerEventHandler)();

// ----- Functions -----
extern "C" {
  HRESULT __stdcall Initialize(
    AUDCLNT_SHAREMODE mode,
    WORD nChannels,
    WORD nBitsPerSample,
    DWORD nSamplesPerSec
  );
  void __stdcall Uninitialize();
  bool __stdcall Start(WASAPIPCMStreamerEventHandler ehMemoryReleased);
  void __stdcall Stop();
  bool __stdcall GetIsStreaming();
  // NOTE: THIS FUNCTION MUST BE CALLED FROM eventMemoryReleased.
  UINT32 __stdcall GetBufferSize();
  // NOTE: THIS FUNCTION MUST BE CALLED FROM eventMemoryReleased.
  bool __stdcall CopyToBuffer(BYTE* data, UINT32 length);
}
