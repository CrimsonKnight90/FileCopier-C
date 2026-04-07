#pragma once
#include <string>
#include <map>
#include <vector>

namespace FileCopier {

enum class Lang { ES, EN, FR, DE, PT };

class Language {
public:
    static Language& Instance();

    void   SetLanguage(Lang lang);
    Lang   Current() const { return m_current; }
    const  std::wstring& Get(const std::wstring& key) const;

    // Lista de idiomas disponibles para la UI
    static std::vector<std::pair<Lang, std::wstring>> Available();

private:
    Language();
    void LoadES();
    void LoadEN();

    Lang m_current = Lang::ES;
    std::map<std::wstring, std::wstring> m_strings;
    std::wstring m_missing;
};

// Macro conveniente
#define TR(key) FileCopier::Language::Instance().Get(L##key).c_str()
#define TRW(key) FileCopier::Language::Instance().Get(L##key)

} // namespace FileCopier
