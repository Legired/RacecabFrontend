#pragma once
#include <string>
#include <vector>

#include "core/Settings.h"

// Sauvegarde et restauration de la configuration : <données>\backups\*.zip
// Contenu : config\ (réglages, profils, commandes, médias), db\library.sqlite (copie cohérente), themes\.
// Les médias sont facultatifs (volumineux). Le cache LaunchBox, les journaux et les sauvegardes ne sont jamais inclus.
namespace backup {

struct Info {
    fs::path file;
    std::string name;     // nom du fichier
    std::string date;     // « 2026-11-23 12:15 »
    uint64_t size = 0;
    bool automatic = false;
    std::string sortKey;  // date + nom, pour le tri
};

// Crée une sauvegarde. prefix : « sauvegarde », « auto » ou « avant-restauration ».
bool Create(const Paths& p, const std::string& prefix, bool includeMedia, std::string& outName, std::string& err);
std::vector<Info> List(const Paths& p);  // plus récente d'abord
// Sauvegarde automatique si la dernière date de plus de 20 h ; ne garde que les « keep » plus récentes.
void AutoIfDue(const Paths& p, int keep);

// Restauration en deux temps : la sauvegarde choisie est mise de côté, puis appliquée au prochain démarrage,
// avant l'ouverture de la base (rien n'est en cours d'utilisation). L'état actuel est d'abord sauvegardé.
bool ScheduleRestore(const Paths& p, const fs::path& zip, std::string& err);
bool ApplyPendingRestore(const Paths& p, std::string& message);  // à appeler au démarrage

}  // namespace backup
