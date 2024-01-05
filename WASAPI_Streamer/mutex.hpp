#pragma once

#include <windows.h>


// ----- Types -----
// REF: https://qiita.com/hidetaka0/items/59002521130450093dbf
class Mutex
{
private:
  // ----- Properties -----
  HANDLE hMutex;

public:
  // ----- Constructors -----
  Mutex()
  {
    this->hMutex = CreateMutex(NULL, FALSE, NULL);

    return;
  }

  ~Mutex()
  {
    CloseHandle(this->hMutex);

    return;
  }

  // ----- Methods -----
  DWORD Lock(DWORD dwMilliseconds = INFINITE)
  {
    return WaitForSingleObject(this->hMutex, dwMilliseconds);
  }

  BOOL Unlock()
  {
    return ReleaseMutex(this->hMutex);
  }
};

class MutexLockGuard
{
private:
  // ----- Properties -----
  Mutex* hMutex = nullptr;

public:
  // ----- Constructors -----
  MutexLockGuard(Mutex* mutex)
  {
    if (mutex != nullptr) {
      this->hMutex = mutex;

      this->hMutex->Lock();
    }

    return;
  }

  ~MutexLockGuard()
  {
    if (this->hMutex != nullptr) {
      this->hMutex->Unlock();
    }

    return;
  }
};
