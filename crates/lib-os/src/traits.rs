//! # traits
//!
//! Trait `OsAdapter` — interfaz portable de operaciones de SO.
//!
//! ## Principio de diseño
//!
//! Todo lo que depende del sistema operativo pasa por este trait.
//! `lib-core` no tiene `#[cfg(windows)]` ni `#[cfg(unix)]` en ningún lugar.
//!
//! Las operaciones aquí documentadas tienen semántica bien definida
//! en ambas plataformas, aunque la implementación difiera.

use std::path::Path;
use lib_core::error::Result;

/// Información sobre el tipo de dispositivo de almacenamiento.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DriveKind {
    /// Disco de estado sólido (SATA SSD o NVMe).
    Ssd,
    /// Disco duro mecánico.
    Hdd,
    /// Unidad de red (NAS, SMB, NFS).
    Network,
    /// No se pudo determinar (fallback conservador = HDD).
    Unknown,
}

impl DriveKind {
    /// Retorna `true` si el dispositivo soporta paralelismo de I/O eficiente.
    pub fn supports_parallel_io(&self) -> bool {
        matches!(self, DriveKind::Ssd)
    }

    /// Retorna `true` si conviene serializar escrituras (evitar seek penalty).
    pub fn prefers_sequential(&self) -> bool {
        matches!(self, DriveKind::Hdd | DriveKind::Network | DriveKind::Unknown)
    }
}

/// Estrategia de copia recomendada para un par origen/destino.
#[derive(Debug, Clone, Copy)]
pub struct CopyStrategy {
    /// Tipo del dispositivo origen.
    pub source_kind: DriveKind,
    /// Tipo del dispositivo destino.
    pub dest_kind:   DriveKind,
    /// Número de workers recomendado para el enjambre.
    pub recommended_swarm_concurrency: usize,
    /// Tamaño de bloque recomendado en bytes.
    pub recommended_block_size: usize,
}

impl CopyStrategy {
    /// Calcula la estrategia óptima según origen y destino.
    pub fn compute(source_kind: DriveKind, dest_kind: DriveKind) -> Self {
        // Heurísticas basadas en benchmarks empíricos:
        //
        // HDD→HDD: monohilo, bloques grandes (reduce seeks en origen Y destino)
        // SSD→HDD: ráfagas moderadas (HDD es el cuello de botella)
        // HDD→SSD: monohilo (HDD es el cuello de botella)
        // SSD→SSD: máximo paralelismo
        // *→Network: conservador (latencia de red, no saturar buffer)

        let (concurrency, block_size) = match (source_kind, dest_kind) {
            (DriveKind::Hdd,     DriveKind::Hdd)     => (1,   8 * 1024 * 1024),
            (DriveKind::Ssd,     DriveKind::Hdd)     => (4,   8 * 1024 * 1024),
            (DriveKind::Hdd,     DriveKind::Ssd)     => (1,   4 * 1024 * 1024),
            (DriveKind::Ssd,     DriveKind::Ssd)     => (128, 4 * 1024 * 1024),
            (_,                  DriveKind::Network)  => (8,  16 * 1024 * 1024),
            (DriveKind::Network, _)                   => (16,  4 * 1024 * 1024),
            (DriveKind::Unknown, _) | (_, DriveKind::Unknown) => (4, 4 * 1024 * 1024),
        };

        Self {
            source_kind,
            dest_kind,
            recommended_swarm_concurrency: concurrency,
            recommended_block_size:        block_size,
        }
    }
}

/// Interfaz portable de operaciones dependientes del SO.
///
/// Implementada por `WindowsAdapter` (Win32) y `UnixAdapter` (POSIX).
pub trait OsAdapter: Send + Sync {
    // ── Detección de hardware ─────────────────────────────────────────────────

    /// Detecta el tipo de unidad que contiene `path`.
    fn detect_drive_kind(&self, path: &Path) -> DriveKind;

    /// Calcula la estrategia óptima para una operación origen→destino.
    fn compute_strategy(&self, source: &Path, dest: &Path) -> CopyStrategy {
        let source_kind = self.detect_drive_kind(source);
        let dest_kind   = self.detect_drive_kind(dest);
        CopyStrategy::compute(source_kind, dest_kind)
    }

    // ── Preallocación ─────────────────────────────────────────────────────────

    /// Pre-alloca espacio en disco para `path` de `size` bytes.
    ///
    /// ## Semántica
    ///
    /// - **Windows**: usa `SetFileInformationByHandle` con `FileAllocationInfo`.
    ///   Extiende el archivo sin escribir ceros, reduciendo fragmentación.
    /// - **Linux**: usa `posix_fallocate()`. No escribe datos, solo reserva bloques.
    ///
    /// Si el sistema de archivos no soporta preallocación (FAT32, exFAT),
    /// este método retorna `Ok(())` sin error (degradación suave).
    fn preallocate(&self, path: &Path, size: u64) -> Result<()>;

    // ── Permisos y metadatos ──────────────────────────────────────────────────

    /// Copia los permisos y timestamps del origen al destino.
    ///
    /// En Windows: copia ACLs, atributos (hidden, read-only, etc.) y timestamps.
    /// En Unix:    copia `mode`, `uid`, `gid` y `atime`/`mtime`.
    fn copy_metadata(&self, source: &Path, dest: &Path) -> Result<()>;

    // ── Nombre de la plataforma (para logging) ────────────────────────────────
    fn platform_name(&self) -> &'static str;
}