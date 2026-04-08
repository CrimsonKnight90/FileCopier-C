#pragma once
#include <QString>
#include <string>
#include <map>
#include <vector>

namespace FileCopier {

enum class Lang { ES, EN };

class Language {
public:
    static Language& Instance();

    void    SetLanguage(Lang lang);
    Lang    Current() const { return m_current; }
    QString Get(const QString& key) const;

    static std::vector<std::pair<Lang, QString>> Available();

private:
    Language();
    void LoadES();
    void LoadEN();

    Lang m_current = Lang::ES;
    std::map<QString, QString> m_strings;
};

// Macro: devuelve QString, compatible con todos los widgets Qt
#define TR(key) FileCopier::Language::Instance().Get(QString(key))

} // namespace FileCopier
