#include "../../include/BufferManager.h"
#include <stdexcept>

namespace FileCopier {

// ─────────────────────────────────────────────────────────────────────────────
BufferManager::BufferManager(size_t size) {
    Allocate(size);
}

BufferManager::~BufferManager() {
    Free();
}

// ─────────────────────────────────────────────────────────────────────────────
void BufferManager::Allocate(size_t size) {
    // VirtualAlloc garantiza alineación a página (4 KB) y es requisito para
    // FILE_FLAG_NO_BUFFERING (el buffer debe estar alineado al tamaño de sector).
    // Redondeamos al múltiplo de ALIGNMENT para asegurar compatibilidad con
    // sectores de 512 B, 4 KB y los requerimientos de NVMe.
    size_t aligned = (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);

    m_buffer = static_cast<BYTE*>(
        ::VirtualAlloc(nullptr, aligned, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)
    );
    if (!m_buffer)
        throw std::bad_alloc();

    m_size = aligned;
}

void BufferManager::Free() {
    if (m_buffer) {
        ::VirtualFree(m_buffer, 0, MEM_RELEASE);
        m_buffer = nullptr;
        m_size   = 0;
    }
}

bool BufferManager::Resize(size_t newSize) {
    Free();
    try {
        Allocate(newSize);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace FileCopier
