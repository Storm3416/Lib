# Memory.hpp - Bibliothèque de Manipulation de Mémoire Externe

Une bibliothèque C++ moderne pour lire/écrire la mémoire d'un processus externe sur Windows, avec support du pattern scanning, reverse engineering et parsing PE.

## 📋 Table des matières

- [Caractéristiques](#caractéristiques)
- [Prérequis](#prérequis)
- [Installation](#installation)
- [API Référence](#api-référence)
- [Exemples](#exemples)
- [Architecture Interne](#architecture-interne)

---

## 🎯 Caractéristiques

✅ **Accès direct à la mémoire externe** via APIs NT natives
✅ **Pattern scanning** avec signatures IDA
✅ **Recherche de chaînes** dans les sections PE
✅ **Reverse engineering** : xrefs, function walking
✅ **Génération automatique de signatures** uniques
✅ **Support PE32/PE64** (DOS, NT headers, sections)
✅ **Parsing du PEB** pour énumération de modules
✅ **Lectures chunked** (fallback pour grandes allocations)

---

## 📦 Prérequis

- **OS** : Windows (XP+)
- **Compilateur** : MSVC 2015+ ou Clang
- **C++** : C++11 ou supérieur
- **Permissions** : Accès administrateur ou SeDebugPrivilege

### Headers requis
```cpp
#include <Windows.h>
#include <winternl.h>
#include <vector>
#include <string>
```

---

## 🚀 Installation

### Méthode 1 : Header-only
```cpp
#include "memory.hpp"

// Instance globale prête à l'emploi
memory.Attach(L"target.exe");
```

### Méthode 2 : Intégration dans un projet
```bash
git clone https://github.com/Storm3416/Lib
# Copier memory.hpp dans votre projet
```

---

## 📚 API Référence

### Gestion du processus

#### `bool Attach(const wchar_t* processName)`
Connecte à un processus par son nom.
```cpp
if (memory.Attach(L"notepad.exe")) {
    printf("Connecté au processus\n");
}
```
**Retour** : `true` si succès, `false` sinon

---

#### `void Detach()`
Déconnecte du processus et libère les ressources.
```cpp
memory.Detach();
```

---

### Modules et Sections

#### `std::uintptr_t GetModuleAddress(const wchar_t* moduleName)`
Obtient l'adresse de base d'un module chargé.
```cpp
auto kernelBase = memory.GetModuleAddress(L"kernel32.dll");
printf("kernel32.dll base: %p\n", (void*)kernelBase);
```

---

#### `ModuleInfo GetModuleInfo(const wchar_t* moduleName)`
Obtient l'adresse et la taille d'un module.
```cpp
auto mod = memory.GetModuleInfo(L"kernel32.dll");
if (mod.valid()) {
    printf("Base: %p, Size: %zu\n", (void*)mod.base, mod.size);
}
```

---

#### `SectionInfo GetSection(const wchar_t* ModuleName, const char* SectionName)`
Localise une section PE (`.text`, `.rdata`, `.data`, etc).
```cpp
auto textSection = memory.GetSection(L"kernel32.dll", ".text");
if (textSection.valid()) {
    printf(".text: %p - %p\n", (void*)textSection.base, 
           (void*)(textSection.base + textSection.size));
}
```

---

### Recherche et Pattern Scanning

#### `std::uintptr_t FindStringA(const wchar_t* ModuleName, const char* Needle)`
Cherche une chaîne ASCII dans la section `.rdata`.
```cpp
auto addr = memory.FindStringA(L"kernel32.dll", "LoadLibraryA");
if (addr) {
    printf("Chaîne trouvée à: %p\n", (void*)addr);
}
```

---

#### `std::uintptr_t PatternScan(const wchar_t* moduleName, const std::string& ida_pattern)`
Scanner un pattern avec format IDA (hex + wildcards).
```cpp
// Chercher : push rbp; mov rbp, rsp
auto func = memory.PatternScan(L"kernel32.dll", "55 8B EC 48 83 EC ??");

// Ou avec std::string pour le module
auto func2 = memory.PatternScan("kernel32.dll", "55 8B EC");
```

**Format des patterns** :
- `55` = byte exact 0x55
- `??` = wildcard (n'importe quel byte)
- Espaces = séparateurs

---

#### `std::uintptr_t FindSig(const wchar_t* moduleName, const std::string& ida_pattern)`
Alias de `PatternScan()`.
```cpp
auto addr = memory.FindSig(L"kernel32.dll", "E8 ?? ?? ?? ??");  // CALL
```

---

#### `std::uintptr_t FindSig(const wchar_t* moduleName, std::initializer_list<std::uint8_t> bytes, const std::string& mask)`
Cherche un pattern avec liste d'octets et mask.
```cpp
auto addr = memory.FindSig(L"kernel32.dll",
    {0x55, 0x8B, 0xEC, 0x48, 0x83, 0xEC},
    "??????");
```

---

#### `std::uintptr_t FindSig(const wchar_t* moduleName, std::initializer_list<std::uint8_t> bytes)`
Cherche un pattern d'octets (auto-masking des 0x00).
```cpp
auto addr = memory.FindSig(L"kernel32.dll", {0x55, 0x8B, 0xEC});
```

---

### Reverse Engineering

#### `std::uintptr_t FindRipXrefTo(const wchar_t* ModuleName, std::uintptr_t TargetVa)`
Trouve une instruction RIP-relative pointant vers une adresse.
```cpp
auto strAddr = memory.FindStringA(L"app.dll", "error");
auto xref = memory.FindRipXrefTo(L"app.dll", strAddr);
// Retourne l'adresse de l'instruction qui référence strAddr
```

---

#### `std::uintptr_t WalkBackToFunctionStart(const wchar_t* ModuleName, std::uintptr_t XrefVa, std::size_t MaxBack)`
Remonte à la fonction contenant une adresse en cherchant les marqueurs `0xCC`.
```cpp
auto funcStart = memory.WalkBackToFunctionStart(L"app.dll", 0x140001234, 0x2000);
printf("Fonction trouvée à: %p\n", (void*)funcStart);
```
**Paramètres** :
- `XrefVa` : adresse où commencer le recherche
- `MaxBack` : distance maximale à remonter (défaut: 0x2000)

---

#### `std::uintptr_t FindByStringXref(const wchar_t* ModuleName, const char* Needle, bool WalkBackToFnStart)`
Workflow complet : cherche une chaîne → trouve la xref → remonte à la fonction.
```cpp
// Cherche "error occurred", trouve la xref, remonte à la fonction
auto funcAddr = memory.FindByStringXref(L"app.dll", "error occurred", true);

// Sans remontée à la fonction
auto xrefAddr = memory.FindByStringXref(L"app.dll", "error occurred", false);
```

---

### Lecture/Écriture de Mémoire

#### `template <typename T> T Read(const std::uintptr_t address)`
Lit une valeur générique.
```cpp
int value = memory.Read<int>(0x140000000);
float pi = memory.Read<float>(0x140001000);

struct MyStruct { int x; float y; };
MyStruct data = memory.Read<MyStruct>(0x140002000);
```

---

#### `bool ReadRaw(const std::uintptr_t address, const void* buffer, size_t size)`
Lit des octets bruts.
```cpp
std::vector<uint8_t> buffer(256);
if (memory.ReadRaw(0x140000000, buffer.data(), 256)) {
    printf("Lecture OK\n");
}
```

---

#### `std::string ReadString(std::uintptr_t address, size_t size)`
Lit une chaîne de caractères (null-terminated).
```cpp
std::string text = memory.ReadString(0x140001000, 64);
printf("Texte: %s\n", text.c_str());
```

---

#### `template <typename T> bool Write(const std::uintptr_t address, const T& value)`
Écrit une valeur générique.
```cpp
memory.Write(0x140000000, 0x12345678);
memory.Write(0x140000004, 3.14159f);
```
**Retour** : `true` si succès, `false` sinon

---

#### `bool WriteRaw(const std::uintptr_t address, const void* buffer, size_t size)`
Écrit des octets bruts.
```cpp
uint8_t patch[] = {0x90, 0x90, 0x90};  // NOPs
memory.WriteRaw(0x140000000, patch, sizeof(patch));
```

---

### Résolution d'Adresses

#### `std::uintptr_t ResolveRel32(std::uintptr_t instr_addr, int disp_offset, int instr_len)`
Résout un déplacement relatif 32-bit (RIP-relative).
```cpp
// Instruction RIP-relative : lea rax, [rel msg]
// À l'adresse 0x140001000, déplacement à offset 3, longueur 7
auto resolved = memory.ResolveRel32(0x140001000, 3, 7);
printf("Adresse résolue: %p\n", (void*)resolved);
```

---

#### `std::uintptr_t PatternScanRel32(const wchar_t* moduleName, const std::string& ida_pattern, int disp_offset, int instr_len)`
Combine `PatternScan()` + `ResolveRel32()`.
```cpp
// Trouve "E8 ?? ?? ?? ??" et résout la cible du CALL
auto callTarget = memory.PatternScanRel32(L"kernel32.dll", "E8 ?? ?? ?? ??");
```

---

### Génération de Signatures

#### `std::uintptr_t CreateSigIDA(std::uintptr_t address, const wchar_t* moduleName, std::size_t MaxLen)`
Génère automatiquement une signature unique pour une adresse.
```cpp
// Crée une signature de max 64 bytes pour l'adresse donnée
memory.CreateSigIDA(0x140001000, L"kernel32.dll", 64);
// Affiche : [sig] 0x140001000  ->  "55 8B EC 48 83 EC ?? ??"
```

Automatiquement :
- Remplace les déplacements 32-bit par `??`
- Remplace les destinations CALL/JMP par `??`
- Remplace les décodeurs SSE par `??`

---

#### `static std::string BytesToIda(const std::vector<std::uint8_t>& bytes, const std::string& mask)`
Convertit des octets en pattern IDA avec mask personnalisé.
```cpp
std::vector<uint8_t> code = {0x55, 0x8B, 0xEC, 0x00, 0x00};
std::string sig = memory.BytesToIda(code, "??????  ");
// Résultat : "55 8B EC ? ?"
```

---

#### `static std::string BytesToIda(const std::vector<std::uint8_t>& bytes)`
Convertit des octets en pattern IDA (auto-masking des 0x00).
```cpp
std::vector<uint8_t> code = {0x55, 0x8B, 0xEC, 0x00, 0x00};
std::string sig = memory.BytesToIda(code);
// Résultat : "55 8B EC ? ?"
```

---

## 💡 Exemples

### Exemple 1 : Lire/Écrire simple
```cpp
#include "memory.hpp"

int main() {
    if (!memory.Attach(L"notepad.exe")) {
        printf("Erreur: impossible de se connecter\n");
        return 1;
    }

    // Lire une valeur
    int value = memory.Read<int>(0x140000000);
    printf("Valeur lue: 0x%X\n", value);

    // Écrire une valeur
    memory.Write(0x140000000, 0xDEADBEEF);
    printf("Valeur écrite\n");

    memory.Detach();
    return 0;
}
```

---

### Exemple 2 : Trouver une fonction via une chaîne
```cpp
#include "memory.hpp"

int main() {
    memory.Attach(L"myapp.exe");

    // Cherche "error occurred" → trouve xref → remonte à la fonction
    std::uintptr_t funcAddr = memory.FindByStringXref(
        L"myapp.dll", 
        "error occurred", 
        true  // remontée à la fonction
    );

    if (funcAddr) {
        printf("Fonction trouvée à: %p\n", (void*)funcAddr);
        
        // Créer une signature pour cette fonction
        memory.CreateSigIDA(funcAddr, L"myapp.dll", 64);
    }

    memory.Detach();
    return 0;
}
```

---

### Exemple 3 : Pattern scanning
```cpp
#include "memory.hpp"

int main() {
    memory.Attach(L"game.exe");

    // Chercher une séquence d'instructions
    // push rbp
    // mov rbp, rsp
    // sub rsp, ??
    auto func = memory.PatternScan(L"game.dll", "55 8B EC 48 83 EC ??");

    if (func) {
        printf("Fonction trouvée à: %p\n", (void*)func);
        
        // Lire les 32 premiers bytes
        std::vector<uint8_t> buf(32);
        memory.ReadRaw(func, buf.data(), 32);
    }

    memory.Detach();
    return 0;
}
```

---

### Exemple 4 : Inspection PE
```cpp
#include "memory.hpp"

int main() {
    memory.Attach(L"target.exe");

    // Obtenir les infos du module
    auto modInfo = memory.GetModuleInfo(L"kernel32.dll");
    printf("kernel32.dll base: %p\n", (void*)modInfo.base);
    printf("kernel32.dll size: %zu bytes\n", modInfo.size);

    // Inspecter les sections
    auto textSection = memory.GetSection(L"kernel32.dll", ".text");
    auto rdataSection = memory.GetSection(L"kernel32.dll", ".rdata");
    auto dataSection = memory.GetSection(L"kernel32.dll", ".data");

    printf(".text : %p - %p\n", (void*)textSection.base,
           (void*)(textSection.base + textSection.size));
    printf(".rdata: %p - %p\n", (void*)rdataSection.base,
           (void*)(rdataSection.base + rdataSection.size));
    printf(".data : %p - %p\n", (void*)dataSection.base,
           (void*)(dataSection.base + dataSection.size));

    memory.Detach();
    return 0;
}
```

---

### Exemple 5 : Génération automatique de signatures
```cpp
#include "memory.hpp"

int main() {
    memory.Attach(L"target.exe");

    // Supposons qu'on a trouvé une fonction intéressante
    std::uintptr_t funcAddr = memory.PatternScan(L"target.dll", "E8 ?? ?? ?? ??");

    if (funcAddr) {
        // Générer une signature unique
        printf("\nGénération de signature pour %p...\n", (void*)funcAddr);
        memory.CreateSigIDA(funcAddr, L"target.dll", 64);
        
        // Affiche quelque chose comme:
        // [sig] 0x140001234  ->  "55 8B EC 48 83 EC ?? ?? 48 89 ??"
    }

    memory.Detach();
    return 0;
}
```

---

### Exemple 6 : Initializer list FindSig
```cpp
#include "memory.hpp"

int main() {
    memory.Attach(L"target.exe");

    // Chercher avec initializer_list
    auto addr1 = memory.FindSig(L"target.dll", 
        {0x55, 0x8B, 0xEC, 0x48, 0x83, 0xEC},
        "??????");

    // Auto-masking (0x00 = wildcard)
    auto addr2 = memory.FindSig(L"target.dll",
        {0x55, 0x8B, 0xEC, 0x00, 0x00, 0x48});

    printf("Adresse 1: %p\n", (void*)addr1);
    printf("Adresse 2: %p\n", (void*)addr2);

    memory.Detach();
    return 0;
}
```

---

## 🏗️ Architecture Interne

### Structures
```cpp
struct ModuleInfo {
    std::uintptr_t base;   // Adresse de base du module
    std::size_t size;      // Taille en bytes
    bool valid() const;    // Vérifiez si les données sont valides
};

struct SectionInfo {
    std::uintptr_t base;   // Adresse de base de la section
    std::size_t size;      // Taille en bytes
    bool valid() const;    // Vérifiez si les données sont valides
};
```

### APIs Natives (ntdll.dll)
- `NtReadVirtualMemory` : Lire la mémoire externe
- `NtWriteVirtualMemory` : Écrire la mémoire externe
- `NtOpenProcess` : Ouvrir un handle de processus
- `NtClose` : Fermer un handle
- `NtQuerySystemInformation` : Énumérer les processus
- `NtQueryInformationProcess` : Obtenir les infos du processus (PEB)

### Parsing PE
- **PEB Walking** : Énumération des modules via Process Environment Block
- **DOS Header** : Localisation de l'en-tête NT
- **Section Headers** : Récupération des offsets et tailles des sections

### Pattern Scanning
- **IDA Format** : Support des patterns hex classiques (55 8B EC ??)
- **Chunked Reading** : Lecture par chunks de 1MB en fallback
- **Masking** : Wildcards (0x00 ou ??) pour l'adaptation

---

## ⚠️ Notes de Sécurité

⚠️ **Cette bibliothèque permet l'accès direct à la mémoire d'autres processus.**

Usages légitimes :
- ✅ Debugging et profiling
- ✅ Security research
- ✅ Analyse de logiciels malveillants

Usages problématiques :
- ❌ Injection/hooking malveillant
- ❌ Contournement de protections
- ❌ Modification de jeux vidéo

---

## 📄 Licence

À définir selon vos besoins (MIT, GPL, etc.)

---

## 👤 Auteur

**Storm3416** - [GitHub](https://github.com/Storm3416)

---

## 📞 Support

Pour les bugs, feature requests ou questions :
- Créer une issue sur GitHub
- Consulter la documentation des APIs NT

---

**Dernière mise à jour** : 2026-07-01
