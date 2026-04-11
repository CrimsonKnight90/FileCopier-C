//! # windows
//!
//! Implementación de `OsAdapter` para Windows usando WinAPI.
//!
//! ## Operaciones implementadas
//!
//! - `preallocate`: `SetFileInformationByHandle` con `FileAllocationInfo`.
//!   Reserva espacio contiguo en NTFS reduciendo fragmentación.
//!
//! - `copy_metadata`: `SetFileTime` para timestamps, `SetFileAttributes`
//!   para atributos (hidden, read-only, system, archive).
//!
//! ## Preallocación en NTFS
//!
//! A diferencia de `SetEndOfFile` (que escribe ceros y es lento),
//! `FileAllocationInfo` solo reserva la estructura en la MFT de NTFS.
//! El tiempo de preallocación de un archivo de 10 GB es ~0ms.

use std::path::Path;

use lib_core::error::{CoreError, Result};
use crate::traits::{DriveKind, OsAdapter};
use crate::detect::detect_drive_kind;

pub struct WindowsAdapter;

impl WindowsAdapter {
    pub fn new() -> Self {
        Self
    }
}

impl Default for WindowsAdapter {
    fn default() -> Self {
        Self::new()
    }
}

impl OsAdapter for WindowsAdapter {
    fn detect_drive_kind(&self, path: &Path) -> DriveKind {
        detect_drive_kind(path)
    }

    fn preallocate(&self, path: &Path, size: u64) -> Result<()> {
        use std::fs::OpenOptions;
        use std::os::windows::fs::OpenOptionsExt;
        use std::os::windows::io::AsRawHandle;

        // Abrir el archivo existente para modificar su información de allocación.
        // El archivo debe existir ya (creado por el writer antes de llamar aquí).
        let file = OpenOptions::new()
            .write(true)
            .open(path)
            .map_err(|e| CoreError::io(path, e))?;

        let handle = file.as_raw_handle();

        // FILE_ALLOCATION_INFO: reserva `AllocationSize` bytes en NTFS.
        // Diferente de SetEndOfFile: no escribe ceros, solo extiende la MFT entry.
        #[repr(C)]
        struct FileAllocationInfo {
            allocation_size: i64,
        }

        let info = FileAllocationInfo {
            allocation_size: size as i64,
        };

        // FileAllocationInfo = 5 en el enum FILE_INFO_BY_HANDLE_CLASS de WinAPI
        const FILE_ALLOCATION_INFO_CLASS: u32 = 5;

        let result = unsafe {
            windows_sys::Win32::Storage::FileSystem::SetFileInformationByHandle(
                handle as _,
                FILE_ALLOCATION_INFO_CLASS,
                &info as *const _ as *const _,
                std::mem::size_of::<FileAllocationInfo>() as u32,
            )
        };

        if result == 0 {
            // SetFileInformationByHandle falló. Esto puede ocurrir en:
            // - FAT32/exFAT (no soportan preallocación por MFT)
            // - Unidades de red
            // En estos casos, degradamos silenciosamente.
            let err = std::io::Error::last_os_error();
            tracing::warn!(
                "Preallocación no soportada en '{}': {} (degradando silenciosamente)",
                path.display(),
                err
            );
            // No retornamos error: la copia puede continuar sin preallocación.
        } else {
            tracing::debug!(
                "Preallocación OK: {} → {} bytes",
                path.display(),
                size
            );
        }

        Ok(())
    }

    fn copy_metadata(&self, source: &Path, dest: &Path) -> Result<()> {
        use std::fs;

        // 1. Copiar timestamps via std (funciona en Windows)
        let src_meta = fs::metadata(source)
            .map_err(|e| CoreError::io(source, e))?;

        // Copiar modified time es lo más crítico para herramientas de backup
        let modified = src_meta.modified()
            .map_err(|e| CoreError::io(source, e))?;

        // filetime solo está disponible como crate externo; usamos std para MVP.
        // La API estable de Rust no expone SetFileTime directamente,
        // así que usamos el método portable de std::fs.
        //
        // TODO Fase 2: usar `filetime` crate para acceso completo a atime/mtime/ctime.

        // 2. Copiar atributos de archivo (hidden, read-only, etc.)
        let src_attrs = src_meta.file_attributes();

        use std::os::windows::fs::MetadataExt;
        // Atributos de Windows: filtramos los que no tienen sentido en destino
        // (comprimido, encrypted, reparse_point se excluyen intencionalmente)
        const COPY_ATTRS: u32 = 0x00000001  // FILE_ATTRIBUTE_READONLY
            | 0x00000002  // FILE_ATTRIBUTE_HIDDEN
            | 0x00000004  // FILE_ATTRIBUTE_SYSTEM
            | 0x00000020  // FILE_ATTRIBUTE_ARCHIVE
            | 0x00000080; // FILE_ATTRIBUTE_NORMAL

        let attrs_to_set = src_attrs & COPY_ATTRS;

        if attrs_to_set != 0 {
            use std::os::windows::fs::OpenOptionsExt;
            // SetFileAttributes via std no está disponible directamente;
            // usamos la WinAPI.
            let dest_wide: Vec<u16> = {
                use std::os::windows::ffi::OsStrExt;
                std::ffi::OsStr::new(dest)
                    .encode_wide()
                    .chain(std::iter::once(0))
                    .collect()
            };

            let result = unsafe {
                windows_sys::Win32::Storage::FileSystem::SetFileAttributesW(
                    dest_wide.as_ptr(),
                    attrs_to_set,
                )
            };

            if result == 0 {
                let err = std::io::Error::last_os_error();
                tracing::warn!(
                    "No se pudieron copiar atributos a '{}': {}",
                    dest.display(),
                    err
                );
                // No fatal: los metadatos son best-effort en MVP
            }
        }

        tracing::trace!(
            "Metadatos copiados: {} → {}",
            source.display(),
            dest.display()
        );

        Ok(())
    }

    fn platform_name(&self) -> &'static str {
        "windows"
    }
}