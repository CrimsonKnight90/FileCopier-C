#pragma once
#include <windows.h>
#include <cstddef>

namespace FileCopier {

// Gestiona un buffer alineado para I/O sin caché (FILE_FLAG_NO_BUFFERING).
// El buffer debe estar alineado a 512 bytes (tamaño de sector).
class BufferManager {
public:
    explicit BufferManager(size_t size);
    ~BufferManager();

    // No copiable
    BufferManager(const BufferManager&)            = delete;
    BufferManager& operator=(const BufferManager&) = delete;

    BYTE*  Data()      const { return m_buffer; }
    size_t Size()      const { return m_size;   }

    // Cambia el tamaño (libera y reasigna)
    bool Resize(size_t newSize);

private:
    void Allocate(size_t size);
    void Free();

    BYTE*  m_buffer = nullptr;
    size_t m_size   = 0;
    static constexpr size_t ALIGNMENT = 4096; // alineación típica NVMe
};

} // namespace FileCopier
