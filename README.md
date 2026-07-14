# Memory.hpp - Bibliothèque de Manipulation de Mémoire Externe

Une bibliothèque C++ moderne pour lire/écrire la mémoire d'un processus externe sur Windows, avec support du pattern scanning, reverse engineering et parsing PE.

## 📋 Table des matières

- [Migration depuis les versions antérieures](#migration-depuis-les-versions-antérieures)
- [Caractéristiques](#caractéristiques)
- [Prérequis](#prérequis)
- [Installation](#installation)
- [API Référence](#api-référence)
- [Exemples](#exemples)
- [Cache multi-TTL (`cm`)](#cache-multi-ttl-cm)
- [Architecture Interne](#architecture-interne)
- [Thread-safety](#thread-safety)

---

## 🔀 Migration depuis les versions antérieures

Cette version corrige plusieurs bugs latents et resserre l'API. Les changements observables :

| Avant | Maintenant | Pourquoi |
|---|---|---|
| `memory.processId`, `memory.processHandle` publics | `memory.Pid()`, `memory.Handle()`, `memory.IsAttached()` | Empêche l'écriture externe qui bypasse `Detach()` et laisse la classe dans un état incohérent. |
| `Attach()` détache d'abord, puis ouvre — si l'ouverture échoue on a perdu l'ancien handle | `AttachEx()` est **atomique** : le nouveau handle est ouvert avant que l'ancien soit fermé, et toute erreur laisse l'attachement précédent en place | Handle leak + état zombie éliminés. |
| `Attach()` retourne `bool` et printf directement | `AttachEx()` retourne `AttachResult` (`Ok` / `NtApiUnresolved` / `ProcessNotFound` / `OpenFailed`) ; `Attach()` reste et renvoie `bool` pour compat | Diagnostiquer un échec sans parser stdout. |
| Tous les `printf` internes | Callback global via `Memory::SetLogger(fn)` — silencieux par défaut | Une lib low-level ne pollue plus `stdout`. |
| `Read<T>()` : impossible de distinguer « lecture ratée » et « valeur zéro » | Ajout de `TryRead<T>()` qui retourne `std::optional<T>` ; `Read<T>()` conservé pour la commodité | Détection d'erreur sans sentinelle. |
| `ReadString(addr, size)` avec `vector<char>(size, '\0')` — OOB si aucun terminateur | `vector<char>(size + 1, '\0')` + `strnlen` explicite | Fin de la lecture au-delà du buffer. |
| `ReadRaw(addr, const void* buffer, size)` | `ReadRaw(addr, void* buffer, size)` | Un `const_cast` sur un pointeur `const` réel écrivait sur de la mémoire read-only et crashait. |
| `ParseIdaPattern` consommait `?????` comme un seul wildcard | `?` ou `??` valent un wildcard ; `???`+ est rejeté | Un pattern mal saisi ne se réaligne plus silencieusement. |
| `FindSig(mod, {bytes})` et `BytesToIda({bytes})` : `0x00` = wildcard implicite | **STRICT** : chaque octet est littéral. Utilise la surcharge avec `mask` pour des wildcards | `0x00` est un opcode/operand légitime — matches faussement positifs éliminés. |
| `ReadChunkedFallback` remplissait les chunks ratés avec des zéros silencieux | Chunks ratés remplis avec `0xCC` (int3, improbable en data comme en code) ; retourne `ReadResult { Empty, Partial, Full }` | Les scans downstream ne matchent plus des « faux zéros ». |
| Aucun cache — chaque `PatternScan` re-lisait tout le module (20+ MiB) | Cache module/section par base ; `memory.ClearScanCache()` pour invalider | Coût O(n) sur le premier scan, O(pattern) sur tous les suivants. |
| `CreateSigIDA` | `CreateSigIda` (nouvelle convention) ; `CreateSigIDA` reste comme alias | Cohérence de nommage. |
| C++11 accepté, pas d'assert x64 | **C++17 requis**, `static_assert(sizeof(void*) == 8)` | `std::optional`, `inline static`, structured bindings ; les offsets PEB et `IMAGE_NT_HEADERS64` sont x64-only, autant l'expliciter au compile-time. |

---

## 🎯 Caractéristiques

✅ **Accès direct à la mémoire externe** via APIs NT natives
✅ **Pattern scanning** avec signatures IDA
✅ **Recherche de chaînes** dans les sections PE
✅ **Reverse engineering** : xrefs, function walking
✅ **Génération automatique de signatures** uniques
✅ **Support PE64** (DOS, NT headers, sections)
✅ **Parsing du PEB** pour énumération de modules
✅ **Lectures chunked** avec statut de couverture (`Full` / `Partial` / `Empty`)
✅ **Cache module/section** partagé entre `FindStringA`, `FindRipXrefTo`, `PatternScan`
✅ **Logger injectable** — silencieux par défaut

---

## 📦 Prérequis

- **OS** : Windows 7+
- **Compilateur** : MSVC 2017+ ou Clang (C++17)
- **C++** : **C++17** ou supérieur (`std::optional`, `inline static`, structured bindings)
- **Cible** : **x64 uniquement** (contrôlé par `static_assert`)
- **Permissions** : Accès administrateur ou `SeDebugPrivilege`

### Headers requis

```cpp
#include <Windows.h>
#include <winternl.h>
#include <optional>
#include <unordered_map>
#include <vector>
#include <string>
```

---

## 🚀 Installation

### Méthode 1 : Header-only

```cpp
#include "memory.hpp"

memory.Attach(L"target.exe");
```

### Méthode 2 : Intégration dans un projet

```bash
git clone https://github.com/Storm3416/Lib
# Copier memory.hpp dans votre projet
```

---

## 📚 API Référence

### Logger

#### `static void Memory::SetLogger(LogFn fn)`

Enregistre un callback pour toutes les traces internes (`[memory]`, `[xref]`, `[sig]`). Par défaut, aucun output — parfait pour une lib embarquée. Le callback reçoit une chaîne C nulle-terminée déjà formatée.

```cpp
Memory::SetLogger([](const char* msg) {
    std::fputs(msg, stderr);
});
// Ou brancher sur spdlog, votre Console::debug, un log file, etc.
```

---

### Gestion du processus

#### `AttachResult AttachEx(const wchar_t* processName)`

Version détaillée. Atomique : si la localisation ou l'ouverture du nouveau processus échoue, l'attachement précédent est préservé.

```cpp
enum class AttachResult {
    Ok,
    NtApiUnresolved,   // GetModuleHandle("ntdll.dll") ou GetProcAddress ratés
    ProcessNotFound,   // NtQuerySystemInformation n'a pas trouvé le nom
    OpenFailed,        // NtOpenProcess a échoué (voir logger pour NTSTATUS)
};

switch (memory.AttachEx(L"notepad.exe")) {
case AttachResult::Ok:              /* continuer */         break;
case AttachResult::ProcessNotFound: /* rerun l'énum */      break;
case AttachResult::OpenFailed:      /* demander SeDebug */  break;
case AttachResult::NtApiUnresolved: /* Windows trop vieux */break;
}
```

#### `bool Attach(const wchar_t* processName)`

Raccourci : retourne `true` ssi `AttachEx(...) == AttachResult::Ok`.

```cpp
if (memory.Attach(L"notepad.exe")) {
    // ...
}
```

#### `void Detach()`

Ferme le handle et vide le cache de scan.

#### `std::uintptr_t Pid() const`

PID du processus attaché, `0` sinon.

#### `void* Handle() const`

Handle brut du processus, `nullptr` sinon. Ne pas fermer directement — utiliser `Detach()`.

#### `bool IsAttached() const`

Sucre pour `Handle() != nullptr`.

---

### Modules et Sections

#### `std::uintptr_t GetModuleAddress(const wchar_t* moduleName)`

```cpp
auto kernelBase = memory.GetModuleAddress(L"kernel32.dll");
```

#### `ModuleInfo GetModuleInfo(const wchar_t* moduleName)`

```cpp
auto mod = memory.GetModuleInfo(L"kernel32.dll");
if (mod.valid()) {
    printf("Base: %p, Size: %zu\n", (void*)mod.base, mod.size);
}
```

#### `SectionInfo GetSection(const wchar_t*, const char*)`

Localise une section PE (`.text`, `.rdata`, `.data`, etc.).

```cpp
auto text = memory.GetSection(L"kernel32.dll", ".text");
```

---

### Recherche et Pattern Scanning

#### `std::uintptr_t FindStringA(const wchar_t*, const char*)`

Cherche une chaîne ASCII dans `.rdata`.

```cpp
auto addr = memory.FindStringA(L"kernel32.dll", "LoadLibraryA");
```

#### `std::uintptr_t PatternScan(const wchar_t*, const std::string& idaPattern)`

Pattern IDA-style avec wildcards `?` ou `??`.

```cpp
auto func = memory.PatternScan(L"kernel32.dll", "55 8B EC 48 83 EC ??");
```

**Format des patterns** :

- `55` = byte exact `0x55`
- `?` ou `??` = un octet wildcard
- Espaces = séparateurs
- `???`+ = **erreur** (pas silencieusement collapsé, contrairement aux anciennes versions)

#### `std::uintptr_t FindSig(const wchar_t*, const std::string&)`

Alias de `PatternScan()`.

#### `std::uintptr_t FindSig(const wchar_t*, std::initializer_list<uint8_t> bytes, const std::string& mask)`

Version avec mask explicite pour les wildcards.

```cpp
auto addr = memory.FindSig(L"kernel32.dll",
    {0x55, 0x8B, 0xEC, 0x48, 0x83, 0xEC},
    "??????");
```

#### `std::uintptr_t FindSig(const wchar_t*, std::initializer_list<uint8_t> bytes)` — **STRICT**

⚠️ **Breaking change** : cette surcharge sans `mask` traite désormais **chaque octet littéralement**. `0x00` n'est plus un wildcard implicite.

```cpp
// Ancien comportement (buggé) : {0x55, 0x00} matchait "55 ??"
// Nouveau comportement       : {0x55, 0x00} matche "55 00" strictement
auto addr = memory.FindSig(L"kernel32.dll", {0x55, 0x8B, 0xEC});
// Pour des wildcards, passer un mask explicite :
auto addr2 = memory.FindSig(L"kernel32.dll", {0x55, 0x8B, 0xEC}, "xx?");
```

---

### Reverse Engineering

#### `std::uintptr_t FindRipXrefTo(const wchar_t*, std::uintptr_t targetVa)`

Cherche une instruction RIP-relative (`LEA` / `MOV`) pointant vers `targetVa`.

#### `std::uintptr_t WalkBackToFunctionStart(const wchar_t*, std::uintptr_t xrefVa, std::size_t maxBack = 0x2000)`

Remonte à la première paire de `CC CC` (padding int3 entre fonctions).

#### `std::uintptr_t FindByStringXref(const wchar_t*, const char* needle, bool walkBackToFnStart = true)`

Workflow complet : chaîne → xref → fonction.

```cpp
auto fn = memory.FindByStringXref(L"app.dll", "error occurred", true);
```

---

### Lecture / Écriture

#### `template<typename T> T Read(std::uintptr_t address)`

Ignore l'échec — retourne `T{}` en cas d'erreur (**indistinguable** d'une lecture réussie qui vaut zéro).

#### `template<typename T> std::optional<T> TryRead(std::uintptr_t address)` — **NEW**

Distingue échec et zéro.

```cpp
if (auto pid = memory.TryRead<std::int32_t>(addr)) {
    use(*pid);
} else {
    // lecture ratée
}
```

#### `bool ReadRaw(std::uintptr_t address, void* buffer, size_t size)`

Signature `void*` — ne jamais passer un `const char*` d'un string littéral.

#### `std::string ReadString(std::uintptr_t address, size_t size = 32)`

Length-safe. Le buffer interne est de taille `size + 1`, et un `strnlen(buffer, size)` borne la construction du `std::string`.

#### `template<typename T> bool Write(std::uintptr_t address, const T& value)`

#### `bool WriteRaw(std::uintptr_t address, const void* buffer, size_t size)`

---

### Résolution d'Adresses

#### `std::uintptr_t ResolveRel32(std::uintptr_t instrAddr, int dispOffset = 3, int instrLen = 7)`

Utilise `TryRead` en interne — retourne `0` proprement si l'instruction est illisible.

#### `std::uintptr_t PatternScanRel32(const wchar_t*, const std::string&, int dispOffset = 3, int instrLen = 7)`

`PatternScan()` + `ResolveRel32()`.

---

### Génération de Signatures

#### `std::uintptr_t CreateSigIda(std::uintptr_t address, const wchar_t* moduleName, std::size_t maxLen = 64)`

Génère une signature IDA unique en wildcardant automatiquement :

- Déplacements 32-bit RIP-relatifs
- Cibles de `CALL` / `JMP` (`E8` / `E9`)
- `Jcc rel32` (`0F 8x`)
- Décodeurs SSE (`0F 10..17`, `28`, `29`, `6E`, `6F`, `7E`, `7F` avec préfixes `66`/`F2`/`F3`)

L'alias `CreateSigIDA` (majuscules d'origine) reste dispo pour ne pas casser les callers existants.

#### `static std::string BytesToIda(bytes, mask)`

Convertit des octets en pattern IDA avec mask.

#### `static std::string BytesToIda(bytes)` — **STRICT**

⚠️ **Breaking change** : plus d'auto-wildcarding des `0x00`. Chaque octet est littéral.

---

### Cache de scan

#### `void ClearScanCache() const`

Vide le cache module/section utilisé par `FindStringA` / `FindRipXrefTo` / `PatternScan`. À appeler si la cible se réécrit (self-modifying code, injection, hot-patch), sinon `Detach()` s'en charge.

---

## 💡 Exemples

### Exemple 1 : Lire/Écrire simple

```cpp
#include "memory.hpp"

int main() {
    Memory::SetLogger([](const char* msg) { std::fputs(msg, stderr); });

    if (!memory.Attach(L"notepad.exe")) {
        return 1;
    }

    if (auto v = memory.TryRead<int>(0x140000000)) {
        printf("Lu: 0x%X\n", *v);
    }
    memory.Write(0x140000000, 0xDEADBEEF);

    memory.Detach();
}
```

---

### Exemple 2 : Trouver une fonction via une chaîne

```cpp
memory.Attach(L"myapp.exe");
auto fn = memory.FindByStringXref(L"myapp.dll", "error occurred", true);
if (fn) {
    memory.CreateSigIda(fn, L"myapp.dll", 64);
}
```

---

### Exemple 3 : Pattern scanning avec logger

```cpp
Memory::SetLogger([](const char* m){ std::fputs(m, stdout); });
memory.Attach(L"game.exe");

auto func = memory.PatternScan(L"game.dll", "55 8B EC 48 83 EC ??");
if (func) {
    std::vector<std::uint8_t> buf(32);
    memory.ReadRaw(func, buf.data(), buf.size());
}
```

---

### Exemple 4 : Attachement atomique et diagnostics

```cpp
switch (memory.AttachEx(L"target.exe")) {
case AttachResult::Ok:              break;
case AttachResult::NtApiUnresolved: std::fputs("ntdll KO\n", stderr); return 1;
case AttachResult::ProcessNotFound: std::fputs("cible absente\n", stderr); return 2;
case AttachResult::OpenFailed:      std::fputs("droit debug requis\n", stderr); return 3;
}
```

Si `target.exe` disparaît mais qu'on tente de re-attacher à `other.exe` avec échec, l'ancien handle reste utilisable.

---

### Exemple 5 : Cache multi-TTL

```cpp
#include "memoryCache.h"

cm cache(memory);
auto hp = cache.fast<int>(hpAddr);        // 10us  TTL
auto mp = cache.medium<int>(mpAddr);      // 500ms TTL
auto pool = cache.slow<uintptr_t>(poolAddr); // 15s TTL

cache.evict_stale_medium(std::chrono::seconds(2));
```

---

## 🧩 Cache multi-TTL (`cm`)

`memoryCache.h` fournit deux briques :

- **`safety::cached<T>`** : conteneur `atomic<shared_ptr<const T>>` pour publier une valeur immuable lue par plusieurs threads (thread-safe).
- **`cm`** : cache par-adresse à 4 étages (`direct`, `fast`, `medium`, `slow`, plus l'alias `debit`) qui délègue à `Memory::TryRead` — un read raté ne pollue plus le cache avec des faux zéros.

⚠️ `cm` n'est **pas thread-safe** (les `unordered_map` internes ne le sont pas). Wrap-le dans un mutex ou donne un `cm` par thread.

---

## 🏗️ Architecture Interne

### Structures

```cpp
struct ModuleInfo   { std::uintptr_t base; std::size_t size; bool valid() const; };
struct SectionInfo  { std::uintptr_t base; std::size_t size; bool valid() const; };
enum class AttachResult { Ok, NtApiUnresolved, ProcessNotFound, OpenFailed };
enum class ReadResult   { Empty, Partial, Full };
```

### APIs Natives (ntdll.dll)

- `NtReadVirtualMemory` — lecture externe
- `NtWriteVirtualMemory` — écriture externe
- `NtOpenProcess` — handle du processus
- `NtClose` — fermeture
- `NtQuerySystemInformation` — énumération des processus
- `NtQueryInformationProcess` — accès au PEB

### Parsing PE

- **PEB walking** via `InLoadOrderModuleList` (Ldr+0x10)
- **DOS header** puis **IMAGE_NT_HEADERS64**
- **Section table** parcourue linéairement

### Pattern scanning

- **Cache module/section** : chaque section lue une fois via `ReadChunkedFallback`, réutilisée pour les scans suivants
- **Chunks 1 MiB** : chunks ratés remplis avec `0xCC`
- **Couverture** exposée : `ReadResult::Full` / `Partial` / `Empty`
- **Wildcards stricts** : `?` ou `??`, sinon erreur

---

## 🔒 Thread-safety

| Composant | Statut |
|---|---|
| `Memory::Attach` / `AttachEx` | À faire depuis un seul thread (typiquement le main) avant de lâcher les workers. |
| `ResolveNtApis` (interne) | Idempotent — la race de résolution est bénigne (chaque racer écrit les six mêmes pointeurs ntdll), mais serialize le premier `Attach` par sécurité. |
| `Memory::Read` / `TryRead` / `Write` / `PatternScan` | OK depuis plusieurs threads une fois attaché, tant que **`ClearScanCache`, `Detach` et `AttachEx` ne sont pas en cours**. Le cache de scan est protégé par `mutable` mais pas par un mutex. |
| `safety::cached<T>` | Thread-safe (atomic shared_ptr, ordering acquire/release). |
| `cm` | **Non** thread-safe — mutex externe ou un `cm` par thread. |

---

## ⚠️ Notes de Sécurité

⚠️ **Cette bibliothèque permet l'accès direct à la mémoire d'autres processus.**

Usages légitimes :
- ✅ Debugging et profiling
- ✅ Security research
- ✅ Analyse de logiciels malveillants
- ✅ Reverse engineering pédagogique

---

## 📄 Licence

À définir selon vos besoins (MIT, GPL, etc.)

---

## 👤 Auteur

**Storm3416** — [GitHub](https://github.com/Storm3416)

---

**Dernière mise à jour** : 2026-07-14
