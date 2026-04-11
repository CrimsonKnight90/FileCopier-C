//! # detect
//!
//! Detección de tipo de hardware de almacenamiento.
//!
//! ## Estrategia por plataforma
//!
//! - **Windows**: `DeviceIoControl` con `IOCTL_STORAGE_QUERY_PROPERTY`
//!   para consultar `StorageDeviceSeekPenaltyProperty`.
//!   Si `IncursSeekPenalty == FALSE` → SSD.
//!
//! - **Linux**: Lectura de `/sys/block/<dev>/queue/rotational`.
//!   `0` → SSD, `1` → HDD.
//!
//! ## Fallback
//!
//! Si la detección falla (permisos, VM, unidad de red), retorna
//! `DriveKind::Unknown` que se trata conservadoramente como HDD.
//! Esto evita que el motor asuma paralelismo en hardware que no lo soporta.

use std::path::Path;
use crate::traits::DriveKind;

/// Detecta el tipo de unidad para `path` en la plataforma actual.
pub fn detect_drive_kind(path: &Path) -> DriveKind {
    #[cfg(windows)]
    {
        windows_detect(path)
    }

    #[cfg(unix)]
    {
        linux_detect(path)
    }

    #[cfg(not(any(windows, unix)))]
    {
        let _ = path;
        DriveKind::Unknown
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Implementación Windows
// ─────────────────────────────────────────────────────────────────────────────

#[cfg(windows)]
fn windows_detect(path: &Path) -> DriveKind {
    // En Windows, necesitamos:
    // 1. Obtener la letra de unidad de `path` (ej: "C:")
    // 2. Abrir el volumen con CreateFile en modo de consulta
    // 3. Enviar IOCTL_STORAGE_QUERY_PROPERTY
    //
    // Por simplicidad en el MVP, usamos una heurística basada en el tipo
    // de sistema de archivos reportado por GetVolumeInformation.
    // La implementación completa con IOCTL viene en Fase 2.

    use std::ffi::OsStr;
    use std::os::windows::ffi::OsStrExt;

    // Extraer la raíz del volumen (ej: "C:\\")
    let root = match get_volume_root(path) {
        Some(r) => r,
        None    => return DriveKind::Unknown,
    };

    // Heurística temporal MVP: si es una unidad de red → Network
    // Si es local, asumimos SSD (conservador hacia el rendimiento).
    // TODO Fase 2: implementar IOCTL_STORAGE_QUERY_PROPERTY completo.
    let root_wide: Vec<u16> = OsStr::new(&root)
        .encode_wide()
        .chain(std::iter::once(0))
        .collect();

    // GetDriveTypeW: 4 = DRIVE_REMOTE
    let drive_type = unsafe {
        windows_sys::Win32::Storage::FileSystem::GetDriveTypeW(root_wide.as_ptr())
    };

    match drive_type {
        4 => DriveKind::Network,  // DRIVE_REMOTE
        _ => {
            // Intento real de detección SSD/HDD mediante seek penalty
            detect_seek_penalty(&root).unwrap_or(DriveKind::Unknown)
        }
    }
}

#[cfg(windows)]
fn get_volume_root(path: &Path) -> Option<String> {
    // Extraer "C:\" de un path cualquiera
    let abs = path.canonicalize().ok()?;
    let s = abs.to_string_lossy();
    // Los paths de Windows empiezan con "\\?\" en versión canonicalizada
    // Extraemos el prefijo de volumen
    let stripped = s.strip_prefix("\\\\?\\").unwrap_or(&s);
    let root = if stripped.len() >= 3 && &stripped[1..3] == ":\\" {
        format!("{}\\", &stripped[..2])
    } else {
        return None;
    };
    Some(root)
}

#[cfg(windows)]
fn detect_seek_penalty(volume_root: &str) -> Option<DriveKind> {
    // Implementación completa de IOCTL_STORAGE_QUERY_PROPERTY
    // Esta es la forma correcta de detectar SSD en Windows.
    //
    // Por ahora retornamos None para que el caller use el fallback.
    // TODO Fase 2: implementar con `windows` crate.
    let _ = volume_root;
    None
}

// ─────────────────────────────────────────────────────────────────────────────
// Implementación Linux
// ─────────────────────────────────────────────────────────────────────────────

#[cfg(unix)]
fn linux_detect(path: &Path) -> DriveKind {
    // Estrategia:
    // 1. Encontrar el dispositivo de bloque que monta `path` (leyendo /proc/mounts)
    // 2. Extraer el nombre base del dispositivo (ej: "sda" de "/dev/sda1")
    // 3. Leer /sys/block/<dev>/queue/rotational
    //    0 → no rotacional → SSD
    //    1 → rotacional    → HDD

    match find_block_device(path) {
        Some(dev) => read_rotational(&dev),
        None      => DriveKind::Unknown,
    }
}

#[cfg(unix)]
fn find_block_device(path: &Path) -> Option<String> {
    use std::fs;

    // Leer /proc/mounts y encontrar el punto de montaje más largo que
    // sea prefijo de `path`. El más largo es el más específico.
    let mounts = fs::read_to_string("/proc/mounts").ok()?;
    let abs_path = path.canonicalize().ok()?;
    let abs_str  = abs_path.to_string_lossy();

    let mut best_mount = "";
    let mut best_dev   = "";

    for line in mounts.lines() {
        let mut parts = line.split_whitespace();
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

    // Extraer nombre base del dispositivo (ej: "sda" de "/dev/sda1")
    let dev_name = std::path::Path::new(best_dev)
        .file_name()?
        .to_str()?;

    // Quitar número de partición: "sda1" → "sda", "nvme0n1p1" → "nvme0n1"
    let base = strip_partition_number(dev_name);
    Some(base.to_string())
}

#[cfg(unix)]
fn strip_partition_number(dev: &str) -> &str {
    // nvme devices: nvme0n1p1 → nvme0n1
    if dev.contains('p') {
        if let Some(pos) = dev.rfind('p') {
            let suffix = &dev[pos + 1..];
            if suffix.chars().all(|c| c.is_ascii_digit()) {
                return &dev[..pos];
            }
        }
    }
    // sd devices: sda1 → sda
    let end = dev.trim_end_matches(|c: char| c.is_ascii_digit());
    if end.len() < dev.len() { end } else { dev }
}

#[cfg(unix)]
fn read_rotational(dev_name: &str) -> DriveKind {
    let sysfs_path = format!("/sys/block/{dev_name}/queue/rotational");
    match std::fs::read_to_string(&sysfs_path) {
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
    fn test_detect_current_dir() {
        // Simplemente verificamos que no entra en pánico.
        // El tipo retornado depende del hardware del CI.
        let kind = detect_drive_kind(std::path::Path::new("."));
        println!("DriveKind detectado para '.': {kind:?}");
    }

    #[cfg(unix)]
    #[test]
    fn test_strip_partition() {
        assert_eq!(strip_partition_number("sda1"), "sda");
        assert_eq!(strip_partition_number("sda"),  "sda");
        assert_eq!(strip_partition_number("nvme0n1p1"), "nvme0n1");
        assert_eq!(strip_partition_number("nvme0n1"),   "nvme0n1");
    }
}