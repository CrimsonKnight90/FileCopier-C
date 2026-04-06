# FileCopier

High-performance file copy utility for Windows, inspired by Robocopy and FastCopy.

## Features

- **Multi-threaded copying** — configurable thread count (4–64)
- **Overlapped (async) I/O** — read and write in parallel using `FILE_FLAG_OVERLAPPED`
- **No-buffering mode** — bypasses OS cache with `FILE_FLAG_NO_BUFFERING` for maximum NVMe throughput
- **Long path support** — handles paths beyond MAX_PATH using `\\?\` prefix
- **Pause / Resume** — per-file and global pause with resume from exact byte offset
- **Auto-retry** — configurable retries per file with delay
- **Resume across sessions** — partial progress saved to JSON, restored on next run
- **Real-time progress** — per-file progress bars, speed (MB/s), ETA
- **Error log** — exportable error log with Retry / Skip / Skip All / Abort choices
- **Qt 6 UI** — modern, cross-platform interface with dark mode support

---

## Requirements

| Tool | Version |
|------|---------|
| Windows | 10 / 11 (x64) |
| Qt | 6.5+ |
| CMake | 3.20+ |
| Compiler | MSVC 2022 or MinGW-w64 14+ |

---

## Building

### 1. Clone and configure

```bat
git clone https://github.com/you/FileCopier.git
cd FileCopier
mkdir build && cd build
```

### 2. Configure with CMake

**MSVC:**
```bat
cmake .. -G "Visual Studio 17 2022" -A x64 ^
         -DCMAKE_PREFIX_PATH="C:/Qt/6.7.0/msvc2022_64" ^
         -DCMAKE_BUILD_TYPE=Release
```

**MinGW:**
```bat
cmake .. -G "MinGW Makefiles" ^
         -DCMAKE_PREFIX_PATH="C:/Qt/6.7.0/mingw_64" ^
         -DCMAKE_BUILD_TYPE=Release
```

### 3. Build

```bat
cmake --build . --config Release --parallel
```

The executable will be at `build/Release/FileCopier.exe`.  
`windeployqt` runs automatically post-build and copies the required Qt DLLs.

---

## Project Structure

```
FileCopier/
├── include/                  # Public headers
│   ├── FileCopyEngine.h      # Copy engine API + CopyOptions
│   ├── ThreadPool.h          # Generic thread pool (template Enqueue<>)
│   ├── DirectoryScanner.h    # Recursive directory scanner
│   ├── BufferManager.h       # VirtualAlloc-aligned buffer
│   ├── Jobs.h                # JobQueue, JobState, ResumeManager
│   ├── Utils.h               # Logger, ConfigManager, ErrorHandler
│   └── EventBus.h            # Typed event bus (motor → UI)
│
├── src/
│   ├── core/
│   │   ├── FileCopyEngine.cpp    # Overlapped I/O, buffered fallback, verify
│   │   ├── DirectoryScanner.cpp  # FindFirstFileW recursive scan
│   │   ├── ThreadPool.cpp        # Workers, pause/resume, WaitAll
│   │   └── BufferManager.cpp     # VirtualAlloc / VirtualFree
│   │
│   ├── jobs/
│   │   ├── JobQueue.cpp      # Thread-safe job queue
│   │   ├── JobState.cpp      # Per-job state tracking
│   │   └── ResumeManager.cpp # JSON persistence (no external deps)
│   │
│   ├── ui/
│   │   ├── MainWindow.cpp    # Main Qt window + CopyWorker QThread
│   │   ├── ProgressPanel.cpp # Per-file progress rows + global bar
│   │   └── ErrorDialog.cpp   # Error modal + error log widget
│   │
│   ├── utils/
│   │   ├── Logger.cpp        # Thread-safe wofstream logger
│   │   ├── ConfigManager.cpp # INI load/save
│   │   ├── ErrorHandler.cpp  # Win32 error → action decision
│   │   └── EventBus.cpp      # Event emit/subscribe
│   │
│   └── main.cpp              # WinMain entry point
│
├── resources/                # Icons, .rc file
├── filecopier.ini            # Default configuration
├── CMakeLists.txt
└── README.md
```

---

## Configuration (`filecopier.ini`)

| Key | Default | Description |
|-----|---------|-------------|
| `threadCount` | `8` | Parallel copy threads |
| `bufferSizeKB` | `1024` | I/O buffer size (KB) |
| `maxRetries` | `3` | Retries per file on error |
| `retryDelayMs` | `500` | Delay between retries (ms) |
| `verifyAfterCopy` | `0` | Compare file sizes after copy |
| `skipOnError` | `1` | Skip file after max retries |
| `useNoBuffering` | `1` | Bypass OS cache |
| `useOverlapped` | `1` | Async I/O |
| `logPath` | `filecopier.log` | Log file location |
| `statePath` | `filecopier_state.json` | Resume state file |

---

## Architecture Overview

```
┌─────────────────────────────────────────────┐
│                  Qt UI Layer                │
│  MainWindow · ProgressPanel · ErrorDialog   │
└────────────────────┬────────────────────────┘
                     │ QMetaObject::invokeMethod
                     │ (Qt::QueuedConnection)
┌────────────────────▼────────────────────────┐
│               EventBus (singleton)          │
│  OnFileStarted · OnProgress · OnError · ... │
└────────────────────┬────────────────────────┘
                     │ Emit()
┌────────────────────▼────────────────────────┐
│           Job Manager Layer                 │
│       JobQueue · JobState · ResumeManager   │
└────────────────────┬────────────────────────┘
                     │
┌────────────────────▼────────────────────────┐
│              Core Engine Layer              │
│  FileCopyEngine (Overlapped I/O)            │
│  ThreadPool (N workers)                     │
│  DirectoryScanner (FindFirstFileW)          │
│  BufferManager (VirtualAlloc)               │
└─────────────────────────────────────────────┘
```

---

## Performance Tips

- **NVMe → NVMe**: set `threadCount=32`, `bufferSizeKB=4096`, `useNoBuffering=1`
- **HDD → HDD**: set `threadCount=4`, `bufferSizeKB=1024` (more threads hurt HDDs)
- **Network share**: set `useNoBuffering=0`, `threadCount=8`
- **Many small files**: reduce `bufferSizeKB=256`, increase `threadCount`
