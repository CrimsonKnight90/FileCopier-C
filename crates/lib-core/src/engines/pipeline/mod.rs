//! # pipeline
//!
//! Componentes del pipeline de copia para archivos grandes.
//!
//! ## Arquitectura del pipeline
//!
//! ```text
//! ┌──────────────┐  crossbeam::channel  ┌──────────────┐
//! │  BlockReader │ ───── Block ────────► │  BlockWriter │
//! │  (OS thread) │   (backpressure)      │  (OS thread) │
//! └──────────────┘                       └──────────────┘
//!        │                                      │
//!        └── HasherDispatch (inline)            └── HasherDispatch (inline)
//!             (hash del origen)                      (hash del destino)
//! ```
//!
//! ## Backpressure
//!
//! El canal crossbeam tiene capacidad fija (`config.channel_capacity`).
//! Si el escritor no puede mantener el ritmo del lector (por ejemplo,
//! destino HDD vs origen NVMe), el lector se bloquea en `send()`.
//! Esto evita cargar el archivo completo en RAM.
//!
//! ## Bloques
//!
//! Cada `Block` es un `Vec<u8>` de tamaño `config.block_size_bytes`.
//! El último bloque puede ser menor. El fin del stream se señala
//! cerrando el canal (el sender se hace drop), no con un bloque sentinel.

pub mod reader;
pub mod writer;

pub use reader::BlockReader;
pub use writer::BlockWriter;

/// Un bloque de datos leído del origen, listo para ser escrito y hasheado.
///
/// El campo `data` es un `Vec<u8>` reutilizable. En versiones futuras
/// podría ser reemplazado por un buffer pool para evitar allocaciones.
#[derive(Debug)]
pub struct Block {
    /// Datos del bloque.
    pub data: Vec<u8>,

    /// Offset dentro del archivo (útil para escritura directa y checksums parciales).
    pub offset: u64,

    /// Número de secuencia del bloque (0-indexed). Útil para diagnóstico.
    pub sequence: u64,
}

impl Block {
    pub fn new(data: Vec<u8>, offset: u64, sequence: u64) -> Self {
        Self { data, offset, sequence }
    }

    pub fn len(&self) -> usize {
        self.data.len()
    }

    pub fn is_empty(&self) -> bool {
        self.data.is_empty()
    }
}