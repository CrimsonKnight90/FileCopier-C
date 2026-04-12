//! # detect
//!
//! Detección de tipo de hardware de almacenamiento.
//!
//! ## Estrategia por plataforma
//!
//! - **Windows**: `GetDriveTypeW` para clasificar el tipo de unidad,
//!   seguido de detección de seek penalty via IOCTL (Fase 2).
//!   La letra de unidad se extrae directamente del path sin `canonicalize()`
//!   para evitar fallos cuando el path de destino aún no existe.
//!
//! - **Linux**: Lectura de `/sys/block/<dev>/queue/rotational`.
//!   `0` → SSD, `1` → HDD.
//!
//! ## Fallback
//!
//! Si la detección falla (permisos, VM, unidad de red), retorna
//! `DriveKind::Unknown` que se trata conservadoramente como HDD.

use std::path::Path;

use crate::traits::DriveKind;

/// Detecta el tipo de unidad para `path` en la plataforma actual.
pub fn detect_drive_kind(path: &Path) -> DriveKind {
    #[cfg(windows)]
    return windows_detect(path);

    #[cfg(unix)]
    return linux_detect(path);

    #[cfg(not(any(windows, unix)))]
    {
        let _ = path;
        DriveKind::Unknown
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Windows
// ─────────────────────────────────────────────────────────────────────────────

#[cfg(windows)]
fn windows_detect(path: &Path) -> DriveKind {
    // Extraer la raíz de la unidad (ej: "C:\") directamente del path,
    // sin llamar a canonicalize() que falla si el path no existe todavía.
    let root = match extract_drive_root(path) {
        Some(r) => r,
        None    => return DriveKind::Unknown,
    };

    let root_wide: Vec<u16> = {
        use std::ffi::OsStr;
        use std::os::windows::ffi::OsStrExt;
        OsStr::new(&root)
            .encode_wide()
            .chain(std::iter::once(0))
            .collect()
    };

    // GetDriveTypeW retorna constantes definidas en la WinAPI:
    //   0 = DRIVE_UNKNOWN
    //   1 = DRIVE_NO_ROOT_DIR
    //   2 = DRIVE_REMOVABLE
    //   3 = DRIVE_FIXED        ← disco local (HDD o SSD)
    //   4 = DRIVE_REMOTE       ← unidad de red
    //   5 = DRIVE_CDROM
    //   6 = DRIVE_RAMDISK
    let drive_type = unsafe {
        windows_sys::Win32::Storage::FileSystem::GetDriveTypeW(root_wide.as_ptr())
    };

    match drive_type {
        4 => DriveKind::Network,   // DRIVE_REMOTE
        5 => DriveKind::Unknown,   // DRIVE_CDROM — no relevante para copia
        3 => {
            // DRIVE_FIXED: puede ser HDD o SSD.
            // Intentamos detectar seek penalty. Si falla, conservador = Hdd.
            detect_seek_penalty_windows(&root).unwrap_or(DriveKind::Hdd)
        }
        2 => DriveKind::Hdd,       // DRIVE_REMOVABLE: USB, SD — tratar como HDD
        _ => DriveKind::Unknown,
    }
}

/// Extrae la raíz de la unidad de un path de Windows sin llamar a canonicalize.
///
/// Ejemplos:
///   `C:\Users\foo\bar`  → `"C:\\"`
///   `C:/Users/foo`      → `"C:\\"`
///   `\\server\share\x`  → `None` (UNC paths → Unknown, se tratan como Network)
///   `relative\path`     → `None`
#[cfg(windows)]
fn extract_drive_root(path: &Path) -> Option<String> {
    let s = path.to_string_lossy();

    // UNC paths (\\server\share) → tratar como red
    if s.starts_with("\\\\") || s.starts_with("//") {
        return None;
    }

    // Buscar el patrón "X:" al inicio (letra de unidad)
    let bytes = s.as_bytes();
    if bytes.len() >= 2 && bytes[1] == b':' {
        let letter = bytes[0] as char;
        if letter.is_ascii_alphabetic() {
            return Some(format!("{}:\\", letter.to_ascii_uppercase()));
        }
    }

    None
}

/// Detecta si una unidad local tiene seek penalty (HDD) o no (SSD).
///
/// Usa `IOCTL_STORAGE_QUERY_PROPERTY` con `StorageDeviceSeekPenaltyProperty`.
/// Si `IncursSeekPenalty == FALSE` → SSD.
///
/// Por ahora retorna `None` (fallback a HDD) hasta la implementación
/// completa en Fase 2 con el crate `windows` o llamadas directas a DeviceIoControl.
#[cfg(windows)]
fn detect_seek_penalty_windows(_volume_root: &str) -> Option<DriveKind> {
    // TODO Fase 2: implementar IOCTL_STORAGE_QUERY_PROPERTY completo.
    //
    // El esquema es:
    // 1. Abrir "\\.\PhysicalDrive0" con CreateFileW (GENERIC_READ, FILE_SHARE_READ|WRITE)
    // 2. Preparar STORAGE_PROPERTY_QUERY { PropertyId = StorageDeviceSeekPenaltyProperty }
    // 3. DeviceIoControl con IOCTL_STORAGE_QUERY_PROPERTY
    // 4. Leer DEVICE_SEEK_PENALTY_DESCRIPTOR { IncursSeekPenalty }
    //    FALSE → SSD, TRUE → HDD
    //
    // Por ahora retornamos None → el caller usa Hdd como fallback conservador.
    None
}

// ─────────────────────────────────────────────────────────────────────────────
// Linux / Unix
// ─────────────────────────────────────────────────────────────────────────────

#[cfg(unix)]
fn linux_detect(path: &Path) -> DriveKind {
    match find_block_device(path) {
        Some(dev) => read_rotational(&dev),
        None      => DriveKind::Unknown,
    }
}

#[cfg(unix)]
fn find_block_device(path: &Path) -> Option<String> {
    use std::fs;

    let mounts   = fs::read_to_string("/proc/mounts").ok()?;
    let abs_path = path.canonicalize().ok()?;
    let abs_str  = abs_path.to_string_lossy();

    let mut best_mount = "";
    let mut best_dev   = "";

    for line in mounts.lines() {
        let mut parts  = line.split_whitespace();
        let device     = parts.next()?;
        let mountpoint = parts.next()?;

        if abs_str.starts_with(mountpoint) && mountpoint.len() > best_mount.len() {
            best_mount = mountpoint;
            best_dev   = device;
        }
    }

    if best_dev.is_empty() {
        return None;
    }

    let dev_name = std::path::Path::new(best_dev).file_name()?.to_str()?;
    let base     = strip_partition_number(dev_name);
    Some(base.to_string())
}

#[cfg(unix)]
fn strip_partition_number(dev: &str) -> &str {
    // nvme0n1p1 → nvme0n1
    if dev.contains('p') {
        if let Some(pos) = dev.rfind('p') {
            let suffix = &dev[pos + 1..];
            if suffix.chars().all(|c| c.is_ascii_digit()) {
                return &dev[..pos];
            }
        }
    }
    // sda1 → sda
    let end = dev.trim_end_matches(|c: char| c.is_ascii_digit());
    if end.len() < dev.len() { end } else { dev }
}

#[cfg(unix)]
fn read_rotational(dev_name: &str) -> DriveKind {
    let path = format!("/sys/block/{dev_name}/queue/rotational");
    match std::fs::read_to_string(&path) {
        Ok(content) => match content.trim() {
            "0" => DriveKind::Ssd,
            "1" => DriveKind::Hdd,
            _   => DriveKind::Unknown,
        },
        Err(_) => DriveKind::Unknown,
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Tests
// ─────────────────────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn detect_current_dir_does_not_panic() {
        let kind = detect_drive_kind(std::path::Path::new("."));
        println!("DriveKind para '.': {kind:?}");
        // En CI puede ser Unknown. No fallamos por eso.
    }

    #[cfg(windows)]
    #[test]
    fn extract_drive_root_standard_path() {
        let p = std::path::Path::new(r"C:\Users\herna\Documents");
        assert_eq!(extract_drive_root(p), Some("C:\\".to_string()));
    }

    #[cfg(windows)]
    #[test]
    fn extract_drive_root_forward_slashes() {
        let p = std::path::Path::new("D:/Games/Steam");
        assert_eq!(extract_drive_root(p), Some("D:\\".to_string()));
    }

    #[cfg(windows)]
    #[test]
    fn extract_drive_root_unc_returns_none() {
        let p = std::path::Path::new(r"\\server\share\file.txt");
        assert_eq!(extract_drive_root(p), None);
    }

    #[cfg(windows)]
    #[test]
    fn extract_drive_root_relative_returns_none() {
        let p = std::path::Path::new("relative\\path");
        assert_eq!(extract_drive_root(p), None);
    }

    #[cfg(unix)]
    #[test]
    fn strip_partition_sda() {
        assert_eq!(strip_partition_number("sda1"), "sda");
        assert_eq!(strip_partition_number("sda"),  "sda");
    }

    #[cfg(unix)]
    #[test]
    fn strip_partition_nvme() {
        assert_eq!(strip_partition_number("nvme0n1p1"), "nvme0n1");
        assert_eq!(strip_partition_number("nvme0n1"),   "nvme0n1");
    }
}
