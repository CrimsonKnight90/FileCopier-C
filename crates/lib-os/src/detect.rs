//! # detect
//!
//! Detección de tipo de hardware de almacenamiento.
//!
//! ## Windows — cómo funciona la detección SSD/HDD
//!
//! El sistema operativo expone `IOCTL_STORAGE_QUERY_PROPERTY` con
//! `StorageDeviceSeekPenaltyProperty`. El resultado es un struct
//! `DEVICE_SEEK_PENALTY_DESCRIPTOR` con un campo `IncursSeekPenalty`:
//!
//!   - `FALSE (0)` → no hay seek penalty → **SSD / NVMe**
//!   - `TRUE  (1)` → hay seek penalty    → **HDD mecánico**
//!
//! ## Flujo en Windows
//!
//! ```text
//! Path "C:\Users\..." → letra "C:" → "\\.\C:" → CreateFileW
//!   → DeviceIoControl(IOCTL_STORAGE_QUERY_PROPERTY)
//!   → DEVICE_SEEK_PENALTY_DESCRIPTOR.IncursSeekPenalty
//!   → false → SSD  /  true → HDD
//! ```

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
    let root = match extract_drive_root(path) {
        Some(r) => r,
        None    => return DriveKind::Network,
    };

    let root_wide: Vec<u16> = {
        use std::ffi::OsStr;
        use std::os::windows::ffi::OsStrExt;
        OsStr::new(&root)
            .encode_wide()
            .chain(std::iter::once(0))
            .collect()
    };

    let drive_type = unsafe {
        windows_sys::Win32::Storage::FileSystem::GetDriveTypeW(root_wide.as_ptr())
    };

    match drive_type {
        4 => DriveKind::Network,
        3 => {
            // DRIVE_FIXED: consultar IOCTL para distinguir SSD de HDD
            let letter = &root[..2]; // "C:"
            detect_seek_penalty(letter).unwrap_or(DriveKind::Hdd)
        }
        2 => DriveKind::Hdd, // DRIVE_REMOVABLE
        _ => DriveKind::Unknown,
    }
}

/// Consulta IOCTL_STORAGE_QUERY_PROPERTY para determinar SSD vs HDD.
/// `drive_letter` debe ser "C:" (sin barra final).
#[cfg(windows)]
fn detect_seek_penalty(drive_letter: &str) -> Option<DriveKind> {
    use windows_sys::Win32::Foundation::{CloseHandle, INVALID_HANDLE_VALUE};
    use windows_sys::Win32::Storage::FileSystem::{
        CreateFileW, FILE_SHARE_READ, FILE_SHARE_WRITE, OPEN_EXISTING,
    };
    use windows_sys::Win32::System::IO::DeviceIoControl;

    // Abrir "\\.\C:" con acceso 0 (solo consulta de propiedades)
    let volume_path = format!("\\\\.\\{drive_letter}");
    let volume_wide: Vec<u16> = {
        use std::ffi::OsStr;
        use std::os::windows::ffi::OsStrExt;
        OsStr::new(&volume_path)
            .encode_wide()
            .chain(std::iter::once(0))
            .collect()
    };

    let handle = unsafe {
        CreateFileW(
            volume_wide.as_ptr(),
            0,                                  // dwDesiredAccess = 0 (consulta)
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            std::ptr::null(),
            OPEN_EXISTING,
            0,
            0,
        )
    };

    if handle == INVALID_HANDLE_VALUE {
        tracing::debug!(
            "CreateFileW falló para '{}': {}",
            volume_path,
            std::io::Error::last_os_error()
        );
        return None;
    }

    let result = query_seek_penalty(handle);
    unsafe { CloseHandle(handle) };
    result
}

#[cfg(windows)]
fn query_seek_penalty(handle: windows_sys::Win32::Foundation::HANDLE) -> Option<DriveKind> {
    use windows_sys::Win32::System::IO::DeviceIoControl;

    // IOCTL_STORAGE_QUERY_PROPERTY = 0x002D1400
    const IOCTL_STORAGE_QUERY_PROPERTY: u32 = 0x002D1400;

    // StorageDeviceSeekPenaltyProperty = 7
    const STORAGE_DEVICE_SEEK_PENALTY_PROPERTY: i32 = 7;
    const PROPERTY_STANDARD_QUERY: i32 = 0;

    #[repr(C)]
    struct StoragePropertyQuery {
        property_id:           i32,
        query_type:            i32,
        additional_parameters: [u8; 1],
    }

    #[repr(C)]
    struct DeviceSeekPenaltyDescriptor {
        version:             u32,
        size:                u32,
        incurs_seek_penalty: u8, // 0 = SSD, 1 = HDD
    }

    let query = StoragePropertyQuery {
        property_id:           STORAGE_DEVICE_SEEK_PENALTY_PROPERTY,
        query_type:            PROPERTY_STANDARD_QUERY,
        additional_parameters: [0u8],
    };

    let mut descriptor = DeviceSeekPenaltyDescriptor {
        version: 0,
        size:    0,
        incurs_seek_penalty: 0,
    };

    let mut bytes_returned: u32 = 0;

    let ok = unsafe {
        DeviceIoControl(
            handle,
            IOCTL_STORAGE_QUERY_PROPERTY,
            &query as *const _ as *const _,
            std::mem::size_of::<StoragePropertyQuery>() as u32,
            &mut descriptor as *mut _ as *mut _,
            std::mem::size_of::<DeviceSeekPenaltyDescriptor>() as u32,
            &mut bytes_returned,
            std::ptr::null_mut(),
        )
    };

    if ok == 0 {
        tracing::debug!(
            "DeviceIoControl SEEK_PENALTY falló: {}",
            std::io::Error::last_os_error()
        );
        return None;
    }

    let kind = if descriptor.incurs_seek_penalty == 0 {
        DriveKind::Ssd
    } else {
        DriveKind::Hdd
    };

    tracing::debug!(
        "seek_penalty={} → {:?}",
        descriptor.incurs_seek_penalty,
        kind
    );

    Some(kind)
}

/// Extrae la raíz de la unidad sin canonicalize().
/// "C:\Users\foo" → Some("C:\\")
/// "\\server\x"   → None
#[cfg(windows)]
fn extract_drive_root(path: &Path) -> Option<String> {
    let s = path.to_string_lossy();

    if s.starts_with("\\\\") || s.starts_with("//") {
        return None;
    }

    let bytes = s.as_bytes();
    if bytes.len() >= 2 && bytes[1] == b':' {
        let letter = bytes[0] as char;
        if letter.is_ascii_alphabetic() {
            return Some(format!("{}:\\", letter.to_ascii_uppercase()));
        }
    }

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
    let mounts   = std::fs::read_to_string("/proc/mounts").ok()?;
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

    let dev_name = Path::new(best_dev).file_name()?.to_str()?;
    Some(strip_partition_number(dev_name).to_string())
}

#[cfg(unix)]
fn strip_partition_number(dev: &str) -> &str {
    if dev.contains('p') {
        if let Some(pos) = dev.rfind('p') {
            if dev[pos + 1..].chars().all(|c| c.is_ascii_digit()) {
                return &dev[..pos];
            }
        }
    }
    let end = dev.trim_end_matches(|c: char| c.is_ascii_digit());
    if end.len() < dev.len() { end } else { dev }
}

#[cfg(unix)]
fn read_rotational(dev_name: &str) -> DriveKind {
    match std::fs::read_to_string(format!("/sys/block/{dev_name}/queue/rotational")) {
        Ok(s) => match s.trim() {
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
        let kind = detect_drive_kind(Path::new("."));
        println!("DriveKind para '.': {kind:?}");
    }

    #[cfg(windows)]
    #[test]
    fn extract_drive_root_standard() {
        assert_eq!(
            extract_drive_root(Path::new(r"C:\Users\herna\Documents")),
            Some("C:\\".to_string())
        );
    }

    #[cfg(windows)]
    #[test]
    fn extract_drive_root_forward_slashes() {
        assert_eq!(
            extract_drive_root(Path::new("D:/Games")),
            Some("D:\\".to_string())
        );
    }

    #[cfg(windows)]
    #[test]
    fn extract_drive_root_unc_is_none() {
        assert_eq!(
            extract_drive_root(Path::new(r"\\server\share\file")),
            None
        );
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
