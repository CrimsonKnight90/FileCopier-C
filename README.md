# FileCopier-Rust

Motor de copia profesional de alto rendimiento escrito en Rust, con arquitectura
modular, motor dual (bloques grandes + enjambre), hashing paralelo,
pausa/reanudar, checkpointing e interfaz Tauri.

---

## Arquitectura del workspace

```
FileCopier-Rust/
├── Cargo.toml          # Workspace raíz — define members y dependencias compartidas
│
├── lib-core/           # Motor principal (sin dependencias de SO)
│   └── src/
│       ├── engine/     # Motor dual: bloques grandes y enjambre
│       ├── pipeline/   # Lector → Canal → Hasher → Escritor
│       ├── hash/       # Trait ChecksumAlgorithm + blake3 / xxhash / sha2
│       ├── checkpoint/ # Estado de pausa, reanudar y persistencia en disco
│       └── telemetry/  # Métricas diferenciadas (MB/s vs archivos/s)
│
├── lib-os/             # Abstracciones del sistema operativo
│   └── src/
│       ├── traits.rs   # Interfaz portable (OsAdapter)
│       └── windows/    # Implementación Win32 (preallocación, rename atómico)
│
├── app-cli/            # CLI (Fase 1 — MVP)
│   └── src/
│       └── main.rs     # Interfaz de línea de comandos con clap
│
└── app-gui/            # GUI Tauri (Fase 2)
    ├── src-tauri/
    └── src/
```

---

## Motor Dual

### Motor de Bloques Grandes (≥ umbral, default 16 MB)

```
┌────────┐   crossbeam   ┌────────┐   rayon   ┌────────┐
│ Reader │ ────────────► │ Buffer │ ─────────► │ Hasher │
└────────┘    canal      └────────┘  paralelo  └────────┘
                                                    │
                                               ┌────▼───┐
                                               │ Writer │
                                               └────────┘
```

- Bloques de 4 MB (configurable).
- Backpressure real: canal crossbeam con capacidad limitada.
- Hashing paralelo con rayon sobre cada bloque.

### Motor de Enjambre (< umbral)

- Tareas asíncronas independientes con `tokio::spawn`.
- Concurrencia limitada por `tokio::sync::Semaphore` (default 128).
- Optimizado para IOPS: latencia mínima, apertura rápida de archivos.

---

## Resiliencia

| Mecanismo | Descripción |
|---|---|
| `.partial` | Archivo destino escrito con extensión `.partial` hasta completarse |
| Rename atómico | Rename final solo si hash verificado (opt-in con `--verify`) |
| Checkpoint JSON | Estado persistido en disco: permite reanudar tras desconexión |
| `AtomicBool` | Pausa limpia: el escritor vacía buffer antes de suspender |

---

## Heurísticas de Hardware

| Escenario | Estrategia |
|---|---|
| HDD → HDD | Monohilo, sin paralelismo |
| SSD → HDD | Lectura por ráfagas + buffer grande |
| SSD → SSD/NVMe | Paralelismo total |

---

## Fases de desarrollo

| Fase | Contenido |
|---|---|
| **Fase 1** (actual) | CLI + motor dual + hashing + `.partial` + checkpoint |
| **Fase 2** | GUI Tauri + heurísticas dinámicas + control de ancho de banda |
| **Fase 3** | VSS (Volume Shadow Copy), integración profunda con SO |

---

## Compilación rápida

```bash
# Debug (compilación rápida)
cargo build

# Release optimizado
cargo build --release

# Ejecutar CLI
cargo run -p app-cli -- --help

# Tests de toda la workspace
cargo test --workspace

# Benchmarks
cargo bench -p lib-core
```

---

## Requisitos

- Rust 1.78+ (stable)
- Windows: MSVC toolchain (`x86_64-pc-windows-msvc`)
- Linux: `x86_64-unknown-linux-gnu` (para CI/CD)

---

## Configuración CLI (Fase 1)

```
filecopier [OPTIONS] <ORIGEN> <DESTINO>

Options:
  --verify              Habilita hashing y verificación post-copia (blake3)
  --hasher <ALGO>       Algoritmo: blake3 (default), xxhash, sha2
  --block-size <MB>     Tamaño de bloque en MB (default: 4)
  --threshold <MB>      Umbral triage bloques/enjambre en MB (default: 16)
  --swarm-limit <N>     Máx. tareas concurrentes en enjambre (default: 128)
  --resume              Intenta reanudar desde checkpoint existente
  -h, --help            Muestra ayuda
  -V, --version         Muestra versión
```