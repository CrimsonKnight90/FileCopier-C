//! Implementación de `ChecksumAlgorithm` usando XxHash3 (64-bit).
//!
//! ## Cuándo usar XxHash
//!
//! XxHash3 es la opción cuando se prioriza **velocidad pura** sobre
//! seguridad criptográfica. Es ~2× más rápido que blake3 pero no es
//! criptográficamente seguro (susceptible a ataques de colisión deliberada).
//!
//! **Casos de uso**: verificación de integridad en redes de confianza,
//! deduplicación interna, donde el adversario no puede manipular los datos.
//!
//! **No usar para**: checksums expuestos al usuario final como garantía
//! de seguridad, o en contextos donde los datos vienen de fuentes no confiables.

use std::hash::Hasher;
use super::ChecksumAlgorithm;
use twox_hash::XxHash3_64;

/// Hasher incremental basado en XxHash3 (64-bit).
pub struct XxHasher {
    hasher: XxHash3_64,
}

impl XxHasher {
    pub fn new() -> Self {
        Self {
            hasher: XxHash3_64::default(),
        }
    }
}

impl Default for XxHasher {
    fn default() -> Self {
        Self::new()
    }
}

impl ChecksumAlgorithm for XxHasher {
    #[inline]
    fn update(&mut self, data: &[u8]) {
        // XxHash3 usa SIMD internamente en x86_64.
        // La implementación es no-criptográfica pero extremadamente rápida.
        self.hasher.write(data);
    }

    fn finalize(self: Box<Self>) -> String {
        let hash = self.hasher.finish();
        // Formato: 16 hex chars para el u64
        format!("{hash:016x}")
    }

    fn name(&self) -> &'static str {
        "xxhash3-64"
    }
}