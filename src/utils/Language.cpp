#include "../../include/Language.h"

namespace FileCopier {

Language& Language::Instance() {
    static Language inst;
    return inst;
}

Language::Language() {
    LoadES();
}

void Language::SetLanguage(Lang lang) {
    m_current = lang;
    m_strings.clear();
    switch (lang) {
        case Lang::EN: LoadEN(); break;
        default:       LoadES(); break;
    }
}

const std::wstring& Language::Get(const std::wstring& key) const {
    auto it = m_strings.find(key);
    if (it != m_strings.end()) return it->second;
    m_missing = L"[" + key + L"]";
    return m_missing;
}

std::vector<std::pair<Lang, std::wstring>> Language::Available() {
    return {
        { Lang::ES, L"Español"  },
        { Lang::EN, L"English"  },
    };
}

void Language::LoadES() {
    m_strings = {
        // Ventana principal
        { L"app.title",         L"FileCopier" },
        { L"lbl.source",        L"Origen:" },
        { L"lbl.dest",          L"Destino:" },
        { L"btn.browse",        L"..." },
        { L"btn.start",         L"Iniciar" },
        { L"btn.pause",         L"Pausar" },
        { L"btn.resume",        L"Reanudar" },
        { L"btn.cancel",        L"Cancelar" },
        { L"lbl.speed",         L"Velocidad:" },
        { L"lbl.eta",           L"Tiempo restante:" },
        { L"lbl.ready",         L"Listo" },
        { L"lbl.copying",       L"Copiando..." },
        { L"lbl.done",          L"Completado" },
        { L"btn.toggle.show",   L"▼" },
        { L"btn.toggle.hide",   L"▲" },

        // Pestañas
        { L"tab.copylist",      L"Lista de Copia" },
        { L"tab.errors",        L"Informe de Errores" },
        { L"tab.options",       L"Opciones" },

        // Lista de copia - cabeceras
        { L"col.source",        L"Fuente" },
        { L"col.size",          L"Tamaño" },
        { L"col.dest",          L"Destino" },
        { L"col.status",        L"Estado" },

        // Lista de copia - botones
        { L"btn.toTop",         L"⇈ Al inicio" },
        { L"btn.up",            L"↑ Subir" },
        { L"btn.down",          L"↓ Bajar" },
        { L"btn.toBottom",      L"⇊ Al final" },
        { L"btn.addFiles",      L"+ Añadir" },
        { L"btn.remove",        L"✕ Eliminar" },
        { L"btn.save",          L"💾 Guardar lista" },
        { L"btn.load",          L"📂 Cargar lista" },

        // Informe errores - cabeceras
        { L"col.time",          L"Hora" },
        { L"col.action",        L"Acción" },
        { L"col.target",        L"Destino" },
        { L"col.errtext",       L"Error" },

        // Informe errores - botones
        { L"btn.clearErrors",   L"🗑 Borrar informe" },
        { L"btn.saveErrors",    L"💾 Guardar informe" },

        // Opciones - secciones
        { L"opt.endCopy",       L"Fin de la copia" },
        { L"opt.noCloseErr",    L"No cerrar si hubo errores" },
        { L"opt.noClose",       L"No cerrar ventana" },
        { L"opt.close",         L"Cerrar ventana al terminar" },
        { L"opt.collision",     L"Colisiones de archivos" },
        { L"opt.col.ask",       L"Siempre preguntar" },
        { L"opt.col.cancel",    L"Cancelar copia entera" },
        { L"opt.col.skip",      L"Pasar (saltar)" },
        { L"opt.col.overwrite", L"Sobreescribir" },
        { L"opt.col.overwriteD",L"Sobreescribir si es diferente" },
        { L"opt.col.renameNew", L"Renombrar el archivo nuevo" },
        { L"opt.col.renameOld", L"Renombrar el archivo viejo" },
        { L"opt.copyErr",       L"Errores de copia" },
        { L"opt.err.ask",       L"Siempre preguntar" },
        { L"opt.err.cancel",    L"Cancelar la copia entera" },
        { L"opt.err.skip",      L"Pasar (saltar archivo)" },
        { L"opt.err.retryLog",  L"Reintentar una vez, luego mostrar en informe" },
        { L"opt.err.moveEnd",   L"Poner el archivo al final de la lista" },

        // Opciones - performance
        { L"opt.perf",          L"Rendimiento" },
        { L"opt.threads",       L"Hilos de copia:" },
        { L"opt.bufferKB",      L"Tamaño de buffer (KB):" },
        { L"opt.noBuffering",   L"I/O sin caché del sistema (FILE_FLAG_NO_BUFFERING)" },
        { L"opt.overlapped",    L"I/O asíncrono (overlapped)" },
        { L"opt.verify",        L"Verificar archivos tras copiar" },

        // Idioma
        { L"opt.language",      L"Idioma / Language" },

        // Mensajes
        { L"msg.selectFolders", L"Selecciona carpeta origen y destino." },
        { L"msg.done",          L"Completado: %1 ok, %2 con error de %3 total" },
        { L"msg.cancelled",     L"Cancelado por el usuario" },
        { L"status.scanning",   L"Escaneando archivos..." },
        { L"status.ready",      L"Listo" },
    };
}

void Language::LoadEN() {
    m_strings = {
        { L"app.title",         L"FileCopier" },
        { L"lbl.source",        L"Source:" },
        { L"lbl.dest",          L"Destination:" },
        { L"btn.browse",        L"..." },
        { L"btn.start",         L"Start" },
        { L"btn.pause",         L"Pause" },
        { L"btn.resume",        L"Resume" },
        { L"btn.cancel",        L"Cancel" },
        { L"lbl.speed",         L"Speed:" },
        { L"lbl.eta",           L"ETA:" },
        { L"lbl.ready",         L"Ready" },
        { L"lbl.copying",       L"Copying..." },
        { L"lbl.done",          L"Done" },
        { L"btn.toggle.show",   L"▼" },
        { L"btn.toggle.hide",   L"▲" },
        { L"tab.copylist",      L"Copy List" },
        { L"tab.errors",        L"Error Report" },
        { L"tab.options",       L"Options" },
        { L"col.source",        L"Source" },
        { L"col.size",          L"Size" },
        { L"col.dest",          L"Destination" },
        { L"col.status",        L"Status" },
        { L"btn.toTop",         L"⇈ To Top" },
        { L"btn.up",            L"↑ Up" },
        { L"btn.down",          L"↓ Down" },
        { L"btn.toBottom",      L"⇊ To Bottom" },
        { L"btn.addFiles",      L"+ Add Files" },
        { L"btn.remove",        L"✕ Remove" },
        { L"btn.save",          L"💾 Save List" },
        { L"btn.load",          L"📂 Load List" },
        { L"col.time",          L"Time" },
        { L"col.action",        L"Action" },
        { L"col.target",        L"Target" },
        { L"col.errtext",       L"Error" },
        { L"btn.clearErrors",   L"🗑 Clear Report" },
        { L"btn.saveErrors",    L"💾 Save Report" },
        { L"opt.endCopy",       L"End of copy" },
        { L"opt.noCloseErr",    L"Don't close if errors occurred" },
        { L"opt.noClose",       L"Don't close window" },
        { L"opt.close",         L"Close window when done" },
        { L"opt.collision",     L"File Collisions" },
        { L"opt.col.ask",       L"Always ask" },
        { L"opt.col.cancel",    L"Cancel entire copy" },
        { L"opt.col.skip",      L"Skip" },
        { L"opt.col.overwrite", L"Overwrite" },
        { L"opt.col.overwriteD",L"Overwrite if different" },
        { L"opt.col.renameNew", L"Rename new file" },
        { L"opt.col.renameOld", L"Rename old file" },
        { L"opt.copyErr",       L"Copy Errors" },
        { L"opt.err.ask",       L"Always ask" },
        { L"opt.err.cancel",    L"Cancel entire copy" },
        { L"opt.err.skip",      L"Skip file" },
        { L"opt.err.retryLog",  L"Retry once, then log error" },
        { L"opt.err.moveEnd",   L"Move file to end of list" },
        { L"opt.perf",          L"Performance" },
        { L"opt.threads",       L"Copy threads:" },
        { L"opt.bufferKB",      L"Buffer size (KB):" },
        { L"opt.noBuffering",   L"No-buffering I/O (FILE_FLAG_NO_BUFFERING)" },
        { L"opt.overlapped",    L"Overlapped async I/O" },
        { L"opt.verify",        L"Verify files after copy" },
        { L"opt.language",      L"Language / Idioma" },
        { L"msg.selectFolders", L"Please select source and destination folders." },
        { L"msg.done",          L"Done: %1 ok, %2 failed of %3 total" },
        { L"msg.cancelled",     L"Cancelled by user" },
        { L"status.scanning",   L"Scanning files..." },
        { L"status.ready",      L"Ready" },
    };
}

} // namespace FileCopier
