<div align="center">

# Lib

### Bibliothèque C++ privée — mémoire, console & overlays

![C++](https://img.shields.io/badge/C%2B%2B-11%2B-00599C?style=for-the-badge&logo=c%2B%2B&logoColor=white)
![Status](https://img.shields.io/badge/status-actif-success?style=for-the-badge)
![Access](https://img.shields.io/badge/access-private-red?style=for-the-badge&logo=git&logoColor=white)
![Platform](https://img.shields.io/badge/platform-windows-0078D6?style=for-the-badge&logo=windows&logoColor=white)

</div>

---

## Vue d'ensemble

Bibliothèque modulaire regroupant des composants bas niveau réutilisables :
gestion mémoire **interne** et **externe**, interface **console** interactive,
et système d'**overlays** graphiques.

Chaque domaine vit sur sa propre branche pour itérer en isolation,
puis converge sur `main` une fois validé.

---

## Architecture par branches

```
                          ┌──────────────┐
                          │     main     │   stable · intégration
                          └──────┬───────┘
                                 │
        ┌──────────────┬─────────┴─────────┬──────────────┐
        ▼              ▼                   ▼              ▼
   ┌─────────┐   ┌────────────┐    ┌──────────────┐   ┌─────────┐
   │ console │   │ memInternal│    │ memExternal  │   │ overlay │
   └─────────┘   └────────────┘    └──────────────┘   └─────────┘
```

| Branche | Rôle | Périmètre |
|---|---|---|
| **`main`** | Tronc stable | Intégration des fonctionnalités validées |
| **`console`** | Interface CLI | Commandes utilisateur · formatage · I/O console |
| **`memoryInternal`** | Mémoire interne | Allocation, buffers, optimisations cache |
| **`memoryExternal`** | Mémoire externe | Sérialisation, fichiers, I/O persistante |
| **`overlay`** | Couches visuelles | Rendu, superposition, profiling / debug |

---

## Détails des modules

<details>
<summary><b>console</b> — interface en ligne de commande</summary>

- Gestion des commandes utilisateur
- Affichage et formatage de données
- Interface interactive textuelle

</details>

<details>
<summary><b>memoryInternal</b> — gestion mémoire interne</summary>

- Allocation / désallocation optimisée
- Gestion des buffers internes
- Optimisations de cache

</details>

<details>
<summary><b>memoryExternal</b> — mémoire externe & persistance</summary>

- Sérialisation / désérialisation
- Gestion des fichiers
- Buffers externes et I/O

</details>

<details>
<summary><b>overlay</b> — overlays & rendu</summary>

- Superposition de couches
- Rendu et visualisation
- Outils de profiling / debug

</details>

---

## Stack technique

| | |
|---|---|
| **Langage** | C++ 11+ |
| **Type** | Bibliothèque réutilisable |
| **Cible** | Windows |

---

## Utilisation

Chaque branche est **autonome** : tu peux la cloner et l'utiliser isolément,
ou la fusionner dans `main` une fois validée.

```bash
git clone -b <branche> <url-du-repo>
```

---

<div align="center">

### Confidentialité

Dépôt **privé** — accès restreint aux collaborateurs autorisés.

</div>
