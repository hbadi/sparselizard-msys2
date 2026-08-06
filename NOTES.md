# Notes de travail — sparselizard sous MSYS2/MinGW

Journal de la session du 2026-08-06. Tout ce qui est présenté ici comme un fait a été **mesuré**,
pas supposé. Ce qui reste une hypothèse est signalé comme tel.

> Ce fichier est dans un dépôt **public**. Il ne contient que des chemins d'atelier et des
> constats techniques, rien de sensible — mais garder ça en tête avant d'y ajouter quoi que ce soit.

---

## 1. Où on en est

**Acquis, mesuré :**

- `mingw-w64-sparselizard` construit sur **les quatre saveurs** — mingw64, ucrt64, clang64,
  clangarm64 — à partir de l'amont plus six patchs.
- `petsc-mumps` construit sur les quatre, dans les **trois** modèles de parallélisme
  (`dso` séquentiel, `dto` OpenMP, `dmo` MPI avec ScaLAPACK).
- La **chaîne complète** est prouvée sur les deux environnements gcc : une application consommant
  le paquet par `find_package` résout **à travers MUMPS**, valeur `1.30634e-10`.

**Non résolu :**

- La chaîne **clang/flang bloque dans le solve** — 22 minutes sur un problème qui prend 3 secondes
  en gcc. Ce n'est pas ARM : `clang64` est du x86_64 et bloque aussi. Discriminant préparé mais
  non joué, voir §5.

**Non fait :**

- La soumission à `msys2/MINGW-packages`. Prévu : une **issue avant toute PR**, le PKGBUILD amont
  portant une ligne `# Contributor` (Oleg A. Khlybov) et pas de mainteneur actif.

---

## 2. La disposition MSYS2, à connaître avant tout

**PETSc livre douze variantes**, nommées `<scalaire><parallélisme><opt>` :

- scalaire — `s` réel simple, `d` réel double, `c` complexe simple, `z` complexe double ;
- parallélisme — `m` MPI, `s` séquentiel (MPIUNI), `t` OpenMP ;
- `o` — optimisé.

Chacune a son arbre complet :

```
en-têtes     /mingw64/include/petsc/<v>/     + racine commune /mingw64/include/petsc/
import lib   /mingw64/lib/petsc/<v>/libpetsc.dll.a
statique     /mingw64/lib/petsc/<v>/libpetsc.a
DLL          /mingw64/bin/libpetsc-<v>.dll
pkg-config   /mingw64/lib/pkgconfig/petsc-<v>.pc
```

SLEPc et MUMPS suivent **le même schéma de suffixes**, ce qui rend l'appariement mécanique :
`petsc-dto` va avec `slepc-dto` et `mumps-dto`.

**Variante retenue : `dto`.** Choix contraint par clangarm64 — le PKGBUILD amont fait

```sh
builds='dso dto zso zto sso sto cso cto'
[[ ${CARCH} == aarch64 ]] || builds+=' dmo zmo smo cmo'
```

Ce sont les variantes **MPI** qui manquent sur ARM. `dto` existe partout ; `dmo` aurait fait
tomber clangarm64.

---

## 3. Les pièges

### 3.1 Emballage

**CRLF dans le PKGBUILD.** Les quatre saveurs meurent sur
`==> ERROR: PKGBUILD contains CRLF characters and cannot be sourced`. Le dépôt est pourtant en LF
pur : ce sont les **runners Windows de GitHub** qui font leur `checkout` avec `core.autocrlf=true`.
Correctif : `.gitattributes` avec `* text=auto eol=lf`. **Invisible en construction locale**, le
fichier ne transitant par aucun `checkout`.

**Les patchs vont à côté du PKGBUILD, pas dans un sous-répertoire.** `makepkg` résout les entrées
non-URL de `source=()` contre le répertoire du PKGBUILD. Un `patches/` donne
`0001-....patch was not found in the build directory and is not a URL`.

**Ne pas paraphraser les dépendances dans le workflow.** Nommer `parmetis` dans `pacboy` casse
clangarm64, où il n'existe pas. Le PKGBUILD porte déjà la logique par architecture : réduire
`pacboy` au strict nécessaire (`cc:p`, `fc:p`, `cmake:p`, `ninja:p`, `pkgconf:p`) et laisser
`makepkg --syncdeps` résoudre le reste.

**Un PKGBUILD amont a des fichiers compagnons.** Celui de petsc référence huit fichiers locaux
dans son `source=()` — un harnais Tcl et quatre patchs. Copier le seul PKGBUILD donne
`petsc.tcl was not found`.

**Ninja ne lit pas `MAKEFLAGS`.** `cmake --build` sans `-j` applique le défaut de ninja, cœurs + 2,
en ignorant ce que le packageur a demandé. Sur les runners : 4 cœurs, `makepkg.conf` demandant
`-j5`, ninja en faisant 6. Extraire le nombre et le passer :

```bash
_jobs=$(printf '%s' "${MAKEFLAGS}" | sed -nE 's/.*-j[[:space:]]*([0-9]+).*/\1/p')
cmake --build "build-${MSYSTEM}" ${_jobs:+-j "${_jobs}"}
```

**Un remplaçant de paquet a besoin de `provides` et `conflicts`.** `petsc-mumps` seul ne
satisferait pas une dépendance sur `petsc`. La convention pacman, employée chez MINGW-packages :

```bash
provides=("${MINGW_PACKAGE_PREFIX}-petsc=${pkgver}")
conflicts=("${MINGW_PACKAGE_PREFIX}-petsc")
```

Garder `_realname=petsc` pour que les noms de bibliothèques, la disposition des variantes et les
`.pc` restent identiques. Le paquet amont est un *split package* : renommer **les deux** moitiés.

### 3.2 Source amont

**Le tag `v.2022.05` est inexploitable.** Il est **65 commits derrière** master et antérieur au
build CMake — il porte encore l'ancien schéma `cMake/Setup*.cmake`. Testé : **aucun** des correctifs
ne s'y applique. Épingler un commit à la place, avec `pkgver=2022.05.r65`.

### 3.3 Compilation

**gcc 16 compile en C++20 par défaut.** Le retrait du `using namespace std;` global de
`hierarchicalformfunctioncontainer.h` semblait purement MSVC — la collision `sl::integral` avec le
concept `std::integral` demande C++20. Il est en réalité **requis** : sans lui, toute application
appelant `integral()` échoue sur `reference to 'integral' is ambiguous`.

**Le stub MPIUNI est à un chemin doublé** : `/mingw64/include/petsc/petsc/mpiuni/mpi.h`, dans la
racine commune et non dans l'arbre de la variante.

**`msmpi` possède `/mingw64/include/mpi.h`.** `slmpi.cpp` fait un `#include <mpi.h>` nu. Si
`/mingw64/include` passe avant le répertoire MPIUNI, on compile contre MS-MPI en liant un PETSc
MPIUNI : ça compile proprement et ça ne marche pas. D'où `NO_DEFAULT_PATH` sur la recherche et
`target_include_directories(... BEFORE ...)`.

**PETSc est configuré `--with-cxx=0`.** `PETSC_FUNCTION_NAME_CXX` et `PETSC_CXX_RESTRICT` sont
absents de `petscconf.h`. `cMake/FindPETSc.cmake` les fournit en repli. Ce n'est pas une
particularité MSYS2 : c'est le choix amont.

**`if(WIN32)` est vrai sous MinGW.** Le build MinGW générait une feuille de propriétés **Visual
Studio** nommant des `.dll.a`. Guarder sur `if(MSVC)`.

**Le module pkg-config s'appelle `petsc-dto`, pas `PETSc`.** Rien ne répond au nom générique.

### 3.4 Solveur

**Le repli est silencieux par conception**, donc **la valeur ne prouve rien**. `capacitance-computing`
sort `1.30634e-10` que MUMPS ait servi ou que le LU de PETSc ait pris le relais. Seul
`universe::getsolvertype()` distingue, et il faut le lire **après** le solve — la résolution se
fait à la première factorisation.

**MUMPS séquentiel embarque son propre faux MPI** (`/mingw64/include/mumps/mpi_seq/mpi.h`,
65 symboles `MPI_*` exportés) et PETSc embarque MPIUNI. **Pas de collision** : PETSc renomme tout
en `Petsc_MPI_*` (`#define MPI_Init Petsc_MPI_Init`). En revanche, **ne pas transmettre** le
répertoire de stub de MUMPS à PETSc — même piège que `msmpi`.

**ScaLAPACK n'est exigé que pour le MUMPS parallèle.** `dso` et `dto` configurent sans. Le message
`Package mumps requested but dependency scalapack not requested` n'apparaît que sur `dmo`, parce
que le MUMPS parallèle s'en sert pour la factorisation du nœud racine.

**Ne pas passer `--with-shared-libraries=1`.** Ce serait une **régression** : le paquet livre déjà
une DLL **et** un `.a`, upstream construisant PETSc statique puis liant la DLL lui-même
(`ld -shared --whole-archive libpetsc.a --out-implib libpetsc.dll.a`). Pour `dto` :
`libpetsc.a` 40,8 Mo et `libpetsc.dll.a` 14,4 Mo. L'option ferait perdre le statique.

### 3.5 Infrastructure

**GitHub Actions peut tomber.** Le 2026-08-06 : `major_outage`, jobs mourant sur
`Failed to resolve action download info / Internal Server Error / Service Unavailable / Bad Gateway`
au stade « Getting action download info », avant toute exécution de code. `gh run cancel` répondant
`HTTP 500` **tout en prenant effet**. Ne pas conclure d'un échec sans avoir lu la cause.

**`actions/checkout` et `actions/upload-artifact` en v4 ciblent Node 20**, désormais déprécié. v7
déclare `node24`. `msys2/setup-msys2@v2` est déjà conforme.

---

## 4. Les astuces

**La matrice à quatre saveurs trouve ce que le local ne peut pas.** Trois échecs de la journée —
CRLF, patchs mal placés, `-j` fantôme — étaient **impossibles** à voir en construction locale. Le
dépôt s'est payé trois fois en une heure et demie.

**Un test doit savoir échouer.** Le contrôle négatif de `tests/mumps-check` a été fait avant de
s'y fier : contre un PETSc sans MUMPS, il rend `solver used : petsc`, valeur juste, et **échoue**.
Un test incapable d'échouer n'aurait rien valu.

**Vérifier ce qui est réellement entré**, pas seulement que le build est vert :

```bash
grep -o 'PETSC_HAVE_PACKAGES.*' ${MINGW_PREFIX}/include/petsc/dto/petscconf.h
```

**Les `.pc` portent déjà la bonne réponse par saveur**, runtime Fortran compris — `clang64` et
`clangarm64` utilisent flang et `-lflang_rt.runtime` là où gcc utilise `-lgfortran -lquadmath`.
Passer par pkg-config plutôt que par une découverte maison est ce qui fait marcher une seule
recette sur les quatre.

**`paths-ignore` pour garder la liste des runs lisible** — sans quoi toute poussée sur
`experiments/` relance les quatre saveurs du paquet pour rien.

**Isoler la variante par réécriture, pas par option de recette.** Les recettes sont destinées à
l'amont ; un bouton ajouté pour une expérience y serait du bruit. Le workflow fait le `sed`.

---

## 5. Le blocage clang, non résolu

```
MINGW64  (gcc)    3 s      solver used : mumps    PASS
UCRT64   (gcc)    3 s      solver used : mumps    PASS
CLANG64  (flang)  22 min   bloque dans le solve
CLANGARM64        bloque de même
```

Le journal s'arrête après le chargement du maillage : **ça bloque dans le solve**.
Ce n'est **pas** l'architecture — `clang64` est du x86_64.

Piste, relevée sur la chaîne gcc :

```
libmumps-dto.dll  ->  libgfortran-5, libgomp-1, libmetis, libscotch
libpetsc-dto.dll  ->  libgfortran-5, libgomp-1
```

Les deux partagent **le même** runtime OpenMP. Deux runtimes OpenMP dans un processus est une
cause classique de blocage, et la chaîne clang passe par flang et LLVM.

**Discriminant préparé, non joué** : la variante `dso` ne lie **aucun** OpenMP
(`libgfortran-5` seul). Relancer la chaîne sur clang avec `PETSC_VARIANT=dso` tranche :

```bash
gh workflow run end-to-end-mumps.yml -f variant=dso
```

- ça résout en 3 s → le coupable est OpenMP, probablement contournable ;
- ça bloque encore → c'est MUMPS sous flang, et la proposition amont devrait se limiter aux
  environnements gcc.

**Sans effet sur le paquet sparselizard** : le PETSc officiel n'ayant pas MUMPS, le repli joue et
rien ne bloque. Mais **cela conditionne la proposition amont** — proposer MUMPS pour les quatre
environnements livrerait un blocage sur deux d'entre eux.

---

## 6. Argumentaire pour l'issue amont

À porter chez `msys2/MINGW-packages`, une fois le §5 tranché :

- **Le changement est minuscule** : une fonction produisant trois drapeaux, appelée dans les trois
  branches d'un `case` existant, plus deux dépendances. Rien n'est retiré ni modifié pour les
  variantes actuelles.
- **L'appariement est mécanique** : MUMPS livre les mêmes douze variantes sous les mêmes noms.
  Les deux paquets semblent avoir été conçus par la même personne, avec le même schéma.
- **Si les variantes séquentielles sont pauvres, ce n'est pas une décision contre MUMPS** :
  upstream pose `--with-metis=1 --with-parmetis=1` sur la **seule** branche MPI du `case`. Le
  paquet porte donc déjà l'asymétrie, en sens inverse.
- **Preuve à l'appui** : construit sur les quatre environnements, et une application réelle résout
  effectivement à travers MUMPS sur les deux chaînes gcc.

**Réserve honnête à porter** : seules les variantes **réelles double** ont été mesurées. Les huit
autres utilisent le même mécanisme et les mêmes `mumps-<v>` correspondants, mais n'ont pas été
testées.

---

## 7. Correctifs sparselizard portés en patchs

Six, dans `mingw-w64-sparselizard/`, appliqués sur le commit amont `0b826d88` :

| patch | objet |
| --- | --- |
| 0001 | découverte d'un PETSc/SLEPc existant au lieu du schéma `~/SLlibs` |
| 0002 | retrait du `using namespace std` d'un en-tête public |
| 0003 | `install(EXPORT)` et `sparselizardConfig.cmake` |
| 0004 | repli silencieux sur le LU de PETSc quand le solveur demandé n'existe pas |
| 0005 | découverte d'un PETSc livrant une bibliothèque par variante |
| 0006 | artefacts Visual Studio émis pour MSVC et non pour Windows au sens large |

Les 0002, 0004, 0005 et 0006 sont des correctifs amont à part entière, indépendants de MSYS2.
