// REF: https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/windows-audio-architecture
//      *DirectSound IS DEPRECATED.
// REF: https://gist.github.com/kevinmoran/2e673695058c7bc32fb5172848900db5
// REF: https://learn.microsoft.com/en-us/cpp/cpp/how-to-create-and-use-ccomptr-and-ccomqiptr-instances?view=msvc-170
//      https://stackoverflow.com/questions/6844943/what-is-the-use-of-ccomptr-over-ccomqiptr-in-com
#include "pch.h"
#include "dllmain.hpp"


// ----- Constants -----
// NOTE: CLSID                      -> Class ID.
//       IID                        -> Interface ID.
//       COM Object                 -> COM server.
//       CoCreateInstance()         -> Create the COM object. (Allocate memory.) 
//       IUnknown->QueryInterface() -> Get the specified interface. (If the COM object has multiple interfaces.)
// NOTE: Bps           : bytes per second.
//       bps           : bits per second.
// NOTE: Audio Standard: 44.1 [kHz], 16 [bits]
//       Video Standard: 48   [kHz], 24 [bits]
const static TCHAR NAME_MAPFILE[] = TEXT("Local\\WASAPI_STREAMER");
const static CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const static IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
const static IID IID_IAudioClient = __uuidof(IAudioClient3);
const static IID IID_IAudioRenderClient = __uuidof(IAudioRenderClient);
const static DWORD AUDIOCLIENT_FLAGS = (
  AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
  AUDCLNT_STREAMFLAGS_NOPERSIST |
  // AUDCLNT_STREAMFLAGS_RATEADJUST |
  // AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY |
  0
);
const static WAVEFORMATEX WFEX_INIT = {};
static Mutex MTX;

// ----- Variables -----
static HANDLE hMapFile = nullptr;
static HANDLE hEventMemoryReleased = nullptr;
static HANDLE hThreadStreaming = nullptr;
static bool isStreaming = false;
static WASAPIPCMStreamerEventHandler eventMemoryReleased = nullptr;
static AUDCLNT_SHAREMODE mode;
static WAVEFORMATEX wfex = WFEX_INIT;
static CComPtr<IAudioClient3> client;
static LPBYTE buf = nullptr;
static UINT32 sizeBuf = 0;

// ----- Functions (private) -----
static void SetWFEX(const WORD &nChannels, const WORD &nBitsPerSample, const DWORD &nSamplesPerSec) {
  // NOTE: If wFormatTag is WAVE_FORMAT_PCM, cbSize must be zero.
  // REF: https://learn.microsoft.com/ja-jp/windows/win32/api/mmeapi/ns-mmeapi-waveformatex
  wfex.wFormatTag = WAVE_FORMAT_PCM;
  wfex.nChannels = nChannels;
  wfex.wBitsPerSample = nBitsPerSample;
  wfex.nSamplesPerSec = nSamplesPerSec;
  wfex.nBlockAlign = (nChannels * (nBitsPerSample / 8));
  wfex.nAvgBytesPerSec = (wfex.nBlockAlign * nSamplesPerSec);
  wfex.cbSize = 0;
  if (_DEBUG) {
    // Output format to console.
    std::cerr << "[DEBUG #DEFAULT DEVICE FORMAT]" << std::endl;
    // std::cerr << "           FORMAT: " << "0b" << std::bitset<16>(format.wFormatTag) << std::endl;
    std::cerr << "         CHANNELS: " << wfex.nChannels << std::endl;
    std::cerr << "  BITS PER SAMPLE: " << wfex.wBitsPerSample << std::endl;
    std::cerr << "      BLOCK ALIGN: " << wfex.nBlockAlign << std::endl;
    std::cerr << "  SAMPLES PER SEC: " << wfex.nSamplesPerSec << std::endl;
    std::cerr << "    BYTES PER SEC: " << wfex.nAvgBytesPerSec << std::endl;
    // std::cerr << "             SIZE: " << format.cbSize << std::endl;
  }

  return;
}

static HRESULT GetDeviceDefault(CComPtr<IMMDevice> &device) {
  HRESULT hr;
  CComPtr<IMMDeviceEnumerator> devices;

  // Get available device list interface.
  // hr = CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL, IID_IMMDeviceEnumerator, (LPVOID*)&devices);
  hr = devices.CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL);
  if (FAILED(hr)) {
    if (_DEBUG) {
      std::cerr << "ERROR: IMMDeviceEnumerator.CoCreateInstance() [HR: 0x" << std::hex << hr << "]" << std::endl;
    }

    return hr;
  }

  // Get default device interface.
  hr = devices->GetDefaultAudioEndpoint(EDataFlow::eRender, ERole::eConsole, &device);
  if (FAILED(hr)) {
    if (_DEBUG) {
      std::cerr << "ERROR: IMMDeviceEnumerator.GetDefaultAudioEndpoint() [HR: 0x" << std::hex << hr << "]" << std::endl;
    }

    return hr;
  }

  return S_OK;
}

static HRESULT GetAudioClientDefault(CComPtr<IAudioClient3>& client) {
  HRESULT hr;
  CComPtr<IMMDevice> device;

  // Get default device.
  hr = GetDeviceDefault(device);
  if (FAILED(hr)) {
    return hr;
  }

  // Get AudioClient interface.
  hr = device->Activate(IID_IAudioClient, CLSCTX_ALL, nullptr, (LPVOID*)&client);
  if (FAILED(hr)) {
    if (_DEBUG) {
      std::cerr << "ERROR: IMMDevice.Activate() [HR: 0x" << std::hex << hr << "]" << std::endl;
    }

    return hr;
  }

  return S_OK;
}

static HRESULT IinitializeAudioClient(CComPtr<IAudioClient3>& client, AUDCLNT_SHAREMODE mode) {
  const static REFERENCE_TIME DEVICEPERIOD_SEC = 1000 * 10000;  // NOTE: The unit is 100 [sec].

  HRESULT hr;
  REFERENCE_TIME hnsBufferDuration;
  REFERENCE_TIME hnsPeriodcity;

  // Get device period.
  hr = client->GetDevicePeriod(&hnsBufferDuration, nullptr);
  if (FAILED(hr)) {
    if (_DEBUG) {
      std::cerr << "ERROR: IAudioClient.GetDevicePeriod() [HR: 0x" << std::hex << hr << "]" << std::endl;
    }

    return hr;
  }

  // Set hnsPeriodcity
  switch (mode) {
  case AUDCLNT_SHAREMODE_EXCLUSIVE:
    hnsBufferDuration = DEVICEPERIOD_SEC;

    // NOTE: hnsPeriodcity must be the same as hnsBufferDuration if AUDCLNT_STREAMFLAGS_EVENTCALLBACK is set.
    hnsPeriodcity = hnsBufferDuration;
    break;

  case AUDCLNT_SHAREMODE_SHARED:
  default:
    hnsPeriodcity = 0;
    break;
  }

  // // Check the format support state. 
  // hr = client->IsFormatSupported(mode, &wfex, nullptr);
  // if (FAILED(hr)) {
  //   if (_DEBUG) {
  //     std::cerr << "ERROR: IAudioClient.IsFormatSupported() [HR: 0x" << std::hex << hr << "]" << std::endl;
  //   }

  //   return hr;
  // }

  // NOTE: If AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED is returned, buffer size is determined automatically.
  // REF: https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient-initialize
  hr = client->Initialize(mode, AUDIOCLIENT_FLAGS, hnsBufferDuration, hnsPeriodcity, &wfex, nullptr);
  if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) {
    UINT32 numBufSamples;

    // Get number of buffer samples.
    hr = client->GetBufferSize(&numBufSamples);
    if (FAILED(hr)) {
      if (_DEBUG) {
        std::cerr << "ERROR: IAudioClient.GetBufferSize() [HR: 0x" << std::hex << hr << "]" << std::endl;
      }

      return hr;
    }

    // Recompute buffer duration.
    hnsBufferDuration = (REFERENCE_TIME)(DEVICEPERIOD_SEC * ((double)numBufSamples / wfex.nSamplesPerSec));
    hnsPeriodcity = hnsBufferDuration;

    // Initialize AudioClient.
    hr = client->Initialize(mode, AUDIOCLIENT_FLAGS, hnsBufferDuration, hnsPeriodcity, &wfex, nullptr);
    if (FAILED(hr)) {
      if (_DEBUG) {
        std::cerr << "ERROR: IAudioClient.Initialize() [HR: 0x" << std::hex << hr << "]" << std::endl;
      }

      return hr;
    }
  }
  else if (FAILED(hr)) {
    if (_DEBUG) {
      std::cerr << "ERROR: IAudioClient.Initialize() [HR: 0x" << std::hex << hr << "]" << std::endl;
    }

    return hr;
  }

  return S_OK;
}

static HRESULT Initialize_(
  AUDCLNT_SHAREMODE mode,
  WORD nChannels,
  WORD nBitsPerSample,
  DWORD nSamplesPerSec
)
{
  HRESULT hr;

  // Check arguments.
  switch (mode) {
  case AUDCLNT_SHAREMODE_EXCLUSIVE:
  case AUDCLNT_SHAREMODE_SHARED:
    break;

  default:
    return E_FAIL;
  }

  if ((nChannels < 1) || (nChannels > 2)) {
    return E_FAIL;
  }

  if ((nBitsPerSample < 1) || ((nBitsPerSample % 8) != 0)) {
    return E_FAIL;
  }

  if ((nSamplesPerSec < 1) || false) {
    return E_FAIL;
  }

  // Uninitialize.
  if (client != nullptr) {
    Uninitialize();
  }

  // Set share mode.
  ::mode = mode;

  // Set WFEX
  SetWFEX(nChannels, nBitsPerSample, nSamplesPerSec);

  // Ge AudioClient interface.
  hr = GetAudioClientDefault(client);
  if (FAILED(hr)) {
    return hr;
  }

  // Create event.
  hEventMemoryReleased = CreateEvent(NULL, FALSE, FALSE, NULL);
  if (hEventMemoryReleased == NULL) {
    if (_DEBUG) {
      std::cerr << "ERROR: CreateEvent() [HR: 0x" << std::hex << hr << "]" << std::endl;
    }

    return E_FAIL;
  }

  // Initialize AudioClient.
  hr = IinitializeAudioClient(client, mode);
  if (FAILED(hr)) {
    return hr;
  }

  // Set event.
  hr = client->SetEventHandle(hEventMemoryReleased);
  if (FAILED(hr)) {
    if (_DEBUG) {
      std::cerr << "ERROR: IAudioClient.SetEventHandle() [HR: 0x" << std::hex << hr << "]" << std::endl;
    }

    return hr;
  }

  return S_OK;
}

static HRESULT StartStreaming_()
{
  HRESULT hr;
  CComPtr<IAudioRenderClient> renderer;
  UINT32 numBufSamples;
  UINT32 numBufSamplesUsed;
  UINT32 numBufSamplesAvailable;
  HANDLE hMMCSS;

  // Get number of buffer samples.
  hr = client->GetBufferSize(&numBufSamples);
  if (FAILED(hr)) {
    if (_DEBUG) {
      std::cerr << "ERROR: IAudioClient.GetBufferSize() [HR: 0x" << std::hex << hr << "]" << std::endl;
    }

    return hr;
  }

  // Get AudioRenderClient interface.
  hr = client->GetService(IID_IAudioRenderClient, (LPVOID*)&renderer);
  if (FAILED(hr)) {
    if (_DEBUG) {
      std::cerr << "ERROR: IAudioClient.GetService() [HR: 0x" << std::hex << hr << "]" << std::endl;
    }

    return hr;
  }

  // Clear buffer.
  {
    // Get buffer.
    hr = renderer->GetBuffer(numBufSamples, &buf);
    if (FAILED(hr)) {
      if (_DEBUG) {
        std::cerr << "ERROR: IAudioRenderClient.GetBuffer() [HR: 0x" << std::hex << hr << "]" << std::endl;
      }

      return hr;
    }

    // Clear buffer.
    ZeroMemory(buf, (numBufSamples * wfex.nBlockAlign));

    // Release buffer.
    renderer->ReleaseBuffer(numBufSamples, 0);
  }

  // Start playing.
  hr = client->Start();
  if (FAILED(hr)) {
    if (_DEBUG) {
      std::cerr << "ERROR: IAudioClient.Start() [HR: 0x" << std::hex << hr << "]" << std::endl;
    }

    return hr;
  }

  // Change thread priority.
  // REF: https://learn.microsoft.com/en-us/windows/win32/procthread/multimedia-class-scheduler-service
  // REF: https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/low-latency-audio
  {
    DWORD taskIndex = 0;
    
    hMMCSS = AvSetMmThreadCharacteristicsA("Pro Audio", &taskIndex);
    if (hMMCSS == NULL) {
      if (_DEBUG) {
        std::cerr << "ERROR: AvSetMmThreadCharacteristicsA()" << std::endl;
      }
    }
  }

  //
  while (isStreaming) {
    // Wait for .
    if (WaitForSingleObject(hEventMemoryReleased, 2000) != WAIT_OBJECT_0) {
      if (_DEBUG) {
        std::cerr << "ERROR: WaitForSingleObject()" << std::endl;
      }

      hr = E_FAIL;

      break;
    }

    if (mode == AUDCLNT_SHAREMODE_EXCLUSIVE) {
      numBufSamplesAvailable = numBufSamples;
    }
    else {
      // Get number of buffer samples.
      hr = client->GetCurrentPadding(&numBufSamplesUsed);
      if (FAILED(hr)) {
        if (_DEBUG) {
          std::cerr << "ERROR: IAudioClient.GetCurrentPadding() [HR: 0x" << std::hex << hr << "]" << std::endl;
        }

        break;
      }

      // Compute number of available buffer samples.
      // REF: https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient-getcurrentpadding
      numBufSamplesAvailable = (numBufSamples - numBufSamplesUsed);
    }

    // Get buffer.
    hr = renderer->GetBuffer(numBufSamplesAvailable, &buf);
    if (FAILED(hr)) {
      if (_DEBUG) {
        std::cerr << "ERROR: IAudioRenderClient.GetBuffer() [HR: 0x" << std::hex << hr << "]" << std::endl;
      }

      break;
    }

    // Copy to buffer.
    {
      bool isCritical = (MTX.Lock(100) == WAIT_OBJECT_0);

      sizeBuf = (numBufSamplesAvailable * wfex.nBlockAlign);
      if ((eventMemoryReleased != nullptr) && isCritical) {
        eventMemoryReleased();
      }
      else {
        ZeroMemory(buf, sizeBuf);
      }
      buf = nullptr;
      sizeBuf = 0;

      if (isCritical) {
        MTX.Unlock();
      }
    }

    // Release buffer.
    hr = renderer->ReleaseBuffer(numBufSamplesAvailable, 0);
    if (FAILED(hr)) {
      if (_DEBUG) {
        std::cerr << "ERROR: IAudioRenderClient.ReleaseBuffer() [HR: 0x" << std::hex << hr << "]" << std::endl;
      }

      break;
    }
  }

  // Stop playing.
  client->Stop();

  // Reset thread priority.
  if (hMMCSS != NULL) {
    AvRevertMmThreadCharacteristics(hMMCSS);
  }

  return hr;
}

static DWORD CALLBACK StartStreaming(LPVOID param) {
  HRESULT hr;

  hr = StartStreaming_();
  if (FAILED(hr)) {
    isStreaming = false;
  }

  return 0;
}

static void StopStreaming() {
  isStreaming = false;
  if (hThreadStreaming != NULL) {
    WaitForSingleObject(hThreadStreaming, INFINITE);

    CloseHandle(hThreadStreaming);
    hThreadStreaming = NULL;

    if (_DEBUG) {
      std::cerr << "  : Streaming thread has been stopped." << std::endl;
    }
  }

  return;
}

// ----- Functions (public) -----
HRESULT __stdcall Initialize(
  AUDCLNT_SHAREMODE mode,
  WORD nChannels,
  WORD nBitsPerSample,
  DWORD nSamplesPerSec
)
{
  HRESULT hr;
  MutexLockGuard mtx(&MTX);

  hr = Initialize_(mode, nChannels, nBitsPerSample, nSamplesPerSec);
  if (FAILED(hr)) {
    Uninitialize();
  }

  return hr;
}

void __stdcall Uninitialize() {
  MutexLockGuard mtx(&MTX);

  if (_DEBUG) {
    std::cerr << "[DEBUG #UNINITIALIZE]" << std::endl;
  }

  // Stop streaming.
  StopStreaming();

  // Release.
  if (client != nullptr) {
    client->SetEventHandle(nullptr);

    if (hEventMemoryReleased) {
      CloseHandle(hEventMemoryReleased);
      hEventMemoryReleased = NULL;
    }

    client.Release();

    if (_DEBUG) {
      std::cerr << "  : AudioClient has been released." << std::endl;
    }
  }

  wfex = WFEX_INIT;
  mode = AUDCLNT_SHAREMODE_SHARED;

  return;
}

bool __stdcall Start(WASAPIPCMStreamerEventHandler ehMemoryReleased) {
  MutexLockGuard mtx(&MTX);

  if (_DEBUG) {
    std::cerr << "[DEBUG #START]" << std::endl;
  }
  
  // Stop streaming.
  StopStreaming();

  // Start streaming.
  // REF: https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createthread
  if (client != nullptr) {
    DWORD idThread;

    isStreaming = true;
    eventMemoryReleased = ehMemoryReleased;
    hThreadStreaming = CreateThread(NULL, 0, StartStreaming, NULL, 0, &idThread);
    if (hThreadStreaming == NULL) {
      isStreaming = false;
    }
  }

  return isStreaming;
}

void __stdcall Stop() {
  MutexLockGuard mtx(&MTX);

  if (_DEBUG) {
    std::cerr << "[DEBUG #STOP]" << std::endl;
  }

  // Stop streaming.
  StopStreaming();

  return;
}

bool __stdcall GetIsStreaming() {
  MutexLockGuard mtx(&MTX);

  return isStreaming;
}

// NOTE: THIS FUNCTION MUST BE CALLED FROM eventMemoryReleased.
UINT32 __stdcall GetBufferSize() {
  MutexLockGuard mtx(&MTX);

  return sizeBuf;
}

// NOTE: THIS FUNCTION MUST BE CALLED FROM eventMemoryReleased.
bool __stdcall CopyToBuffer(BYTE* data, UINT32 length) {
  MutexLockGuard mtx(&MTX);

  if ((sizeBuf == 0) || (length != sizeBuf)) {
    return false;
  }

  // Copy.
  std::memcpy(buf, data, sizeBuf);
  
  return true;
}

// ----- Entrypoint -----
BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
  // REF: https://learn.microsoft.com/en-us/windows/win32/dlls/dllmain
  switch (ul_reason_for_call) {
  case DLL_PROCESS_ATTACH:
    // // Initialize COM.
    // hr = CoInitializeEx(nullptr, COINIT_SPEED_OVER_MEMORY);
    // if (FAILED(hr)) {
    // }
    break;

  case DLL_THREAD_ATTACH:
  case DLL_THREAD_DETACH:
    break;

  case DLL_PROCESS_DETACH:
    // NOTE: If calling CComPtr<T>.Release() in this block will throw an exception. (When loaded from c#.)
    //       MUST BE RELEASE COM OBJECT BEFORE CALLING CoUninitialize().
    // REF: https://stackoverflow.com/questions/3288264/exception-during-destruction-of-ccomptr
    // Uninitialize();

    // // Uninitialize COM.
    // CoUninitialize();
    break;
  }

  return TRUE;
}
