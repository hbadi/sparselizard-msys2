# Notes de travail — sparselizard sous MSYS2/MinGW

Journal de la session du 2026-08-06. Tout ce qui est présenté ici comme un fait a été **mesuré**,
pas supposé. Ce qui reste une hypothèse est signalé comme tel.

> Ce fichier est dans un dépôt **public**. Il ne contient que des chemins d'atelier et des
> constats techniques, rien de sensible — mais garder ça en tête avant d'y ajouter quoi que ce soit.

---

## 1. Où on en est

**Acquis, mesuré :**

- `mingw-w64-sparselizard` construit sur **les quatre saveurs** — mingw64, ucrt64, clang64,
  clangarm64 — à partir de l'amont plus huit patchs.
- `petsc-mumps` construit sur les quatre, dans les **trois** modèles de parallélisme
  (`dso` séquentiel, `dto` OpenMP, `dmo` MPI avec ScaLAPACK).
- La **chaîne complète** est prouvée sur les deux environnements gcc : une application consommant
  le paquet par `find_package` résout **à travers MUMPS**, valeur `1.30634e-10`.

- Le blocage clang est **imputé à OpenMP**, discriminant joué : en `dso`, qui ne lie aucun runtime
  OpenMP, **les quatre** saveurs résolvent à travers MUMPS en une centaine de millisecondes. Voir §5.

**Non résolu :**

- Le **mécanisme** du blocage. On sait ce qui le déclenche — OpenMP — pas ce qui se passe. Il reste
  donc entier en `dto`, la variante que le paquet sparselizard utilise.

**Engagé :**

- La soumission à `msys2/MINGW-packages` : l'**issue est ouverte**, [#30888][issue], et attend une
  réponse. La PR qu'elle promet reste à envoyer. Voir §6.

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

## 5. Le blocage clang — le déclencheur est OpenMP

En `dto`, variante OpenMP ([run 31124882570][r-dto]) :

```
MINGW64  (gcc)    ~191 ms   solver used : mumps    PASS
UCRT64   (gcc)    ~142 ms   solver used : mumps    PASS
CLANG64  (flang)  22 min    bloque dans le solve
CLANGARM64        bloque de même
```

Le journal s'arrêtait après le chargement du maillage : **ça bloquait dans le solve**.
Ce n'était **pas** l'architecture — `clang64` est du x86_64.

> Le « 3 s » noté à la session précédente mesurait l'étape entière — compilation, édition de liens
> et exécution — pas la résolution. Repris ici sur le même intervalle que le tableau `dso`
> ci-dessous, du chargement du maillage à l'affichage du solveur, pour que les deux soient
> comparables. **OpenMP ne coûte rien de mesurable sur gcc** : 142 à 191 ms contre 109 à 112 ms
> sans lui, sur un cas trop petit pour que le parallélisme rapporte quoi que ce soit.

**Discriminant joué** ([run 31128088908][r-dso]) : la variante `dso` ne lie aucun runtime OpenMP,
ni côté PETSc ni côté MUMPS. Relancée sur les quatre saveurs, elle donne

| environnement | Fortran | solveur | valeur | durée du solve |
| --- | --- | --- | --- | --- |
| MINGW64 | gfortran | `mumps` | 1.30634e-10 | ~112 ms |
| UCRT64 | gfortran | `mumps` | 1.30634e-10 | ~109 ms |
| CLANG64 | flang | `mumps` | 1.30634e-10 | ~103 ms |
| CLANGARM64 | flang | `mumps` | 1.30634e-10 | ~89 ms |

Durée relevée entre la dernière ligne du chargement du maillage et l'affichage du solveur, la
résolution se faisant à la première factorisation. Les quatre sont dans la même bande : avec `dso`
il n'y a plus **aucun** écart entre gcc et flang. Le blocage ne se réduit pas, il disparaît.

`petscconf.h` confirme que le discriminant a retiré ce qu'il devait retirer, et gardé le reste :

```
PETSC_HAVE_PACKAGES ":blaslapack:hwloc:mathlib:mpi:mumps:openblas:"
```

**Donc MUMPS sous flang fonctionne.** L'hypothèse qui aurait limité la proposition amont aux
environnements gcc est écartée.

### 5.1 L'hypothèse des deux runtimes OpenMP est fausse

Elle était séduisante — PETSc bâti par clang, MUMPS `to` lié par flang, donc deux runtimes dans le
processus, cause classique de blocage. **Les tables d'import disent le contraire.** Relevées à
l'`objdump` sur les DLL elles-mêmes, celles du run `dto`, dépendances système retirées :

| DLL | clang64 (bloque) | mingw64 (marche) |
| --- | --- | --- |
| `libpetsc-dto` | **libomp**, libmumps-dto, libopenblas, libhwloc-15 | **libgomp-1**, libmumps-dto, libopenblas, libhwloc-15, libgfortran-5, libgcc_s_seh-1, libwinpthread-1 |
| `libmumps-dto` | **libomp**, libopenblas, libesmumps, libmetis, libscotch | **libgomp-1**, libopenblas, libesmumps, libmetis, libscotch, libgfortran-5 |
| `libopenblas` | **libomp** | **libgomp-1** |

**Un seul runtime OpenMP de chaque côté**, partagé par les trois consommateurs. La structure est
identique ; seule l'implémentation change — `libomp` de LLVM contre `libgomp` de GNU. Il n'y a donc
rien à déduplider, et la piste notée en fin de session précédente est close.

Deux observations au passage, l'une écartée par la mesure :

- Sur clang, **aucune** des deux DLL n'importe de runtime Fortran : `flang_rt.runtime` est statique,
  donc le processus en porte bien deux copies indépendantes là où gcc partage `libgfortran-5.dll`.
  Tentant — mais `dso` a exactement la même duplication et ne bloque pas. **Écarté.**
- OpenBLAS est parallélisé par OpenMP dans les deux environnements, donc MUMPS appelle du BLAS
  parallèle depuis ses propres régions. Le parallélisme imbriqué est symétrique entre gcc et clang
  et n'explique pas à lui seul l'écart, mais c'est le contexte dans lequel `libomp` bloque.

**Ce qui reste ouvert** : pourquoi `libomp` bloque là où `libgomp` passe. La question n'est plus
« combien de runtimes » mais « lequel ». Non tranché, et vraisemblablement hors de notre portée —
c'est à porter à qui connaît l'empaquetage flang/libomp.

**Sans effet sur le paquet sparselizard** : le PETSc officiel n'ayant pas MUMPS, le repli joue et
rien ne bloque. Mais le blocage reste entier en `dto`, qui est la variante retenue au §2.

[r-dto]: https://github.com/hbadi/sparselizard-msys2/actions/runs/31124882570
[r-dso]: https://github.com/hbadi/sparselizard-msys2/actions/runs/31128088908

---

## 6. L'issue amont — ouverte

**[msys2/MINGW-packages#30888][issue]** — *[petsc] Enable the MUMPS interface in the existing build
flavors*. Ouverte le 2026-08-07, une fois le §5 tranché. Aucune issue équivalente n'existait ; la
plus proche, #6480, demandait l'empaquetage de MUMPS lui-même et est close depuis 2020.

Le dépôt n'a pas de gabarit couvrant ce cas — `bug_report`, `environment_request`,
`package_request`, `update_request` — et pas de `config.yml`, donc l'issue libre est autorisée.
Convention de titre relevée sur les issues existantes : `[paquet] description`.

L'argumentaire, tel que porté :

- **Le changement est minuscule** : une fonction produisant trois drapeaux, appelée dans les trois
  branches d'un `case` existant, plus deux dépendances. Rien n'est retiré ni modifié pour les
  variantes actuelles.
- **L'appariement est mécanique** : MUMPS livre les mêmes douze variantes sous les mêmes noms.
  Les deux paquets semblent avoir été conçus par la même personne, avec le même schéma.
- **Si les variantes séquentielles sont pauvres, ce n'est pas une décision contre MUMPS** :
  upstream pose `--with-metis=1 --with-parmetis=1` sur la **seule** branche MPI du `case`. Le
  paquet porte donc déjà l'asymétrie, en sens inverse.
- **Preuve à l'appui** : construit sur les quatre environnements, et une application réelle résout
  effectivement à travers MUMPS sur **les quatre**, en `dso` (§5).

**Réserves honnêtes à porter :**

- Seules les variantes **réelles double** ont été mesurées. Les huit autres utilisent le même
  mécanisme et les mêmes `mumps-<v>` correspondants, mais n'ont pas été testées.
- **La variante OpenMP bloque sur les environnements clang** (§5). La proposition couvre bien les
  quatre environnements, puisque `dso` y résout partout — mais taire le blocage `dto` serait
  malhonnête, et un mainteneur le découvrirait de toute façon.
- Le changement rend `petsc` dépendant de `mumps` **inconditionnellement**, ce qui alourdit un
  paquet que beaucoup prennent pour les seuls solveurs itératifs. C'est la première objection
  attendue ; l'alternative est un paquet séparé, que nous avons déjà, mais un second build à douze
  variantes à maintenir paraît pire qu'un drapeau.

**Trois inexactitudes corrigées à la relecture, avant publication** — elles auraient toutes été
relevées :

- `dso` et `dto` étaient donnés avec la **même** chaîne `PETSC_HAVE_PACKAGES`. Faux : `dso` n'a pas
  `openmp`, `dto` l'a **deux fois**. Les trois variantes sont maintenant listées séparément.
- Le « 3 s » des chaînes gcc traînait encore dans la partie sur le blocage. Remplacé par les 142 à
  191 ms réels (§5).
- « Pas de mainteneur actif » remplacé par le fait vérifiable : le PKGBUILD porte une ligne
  `# Contributor` et aucune `# Maintainer`.

**Écarté sciemment de l'issue** : proposer `--with-metis=1` sur les branches `?s?` et `?t?`. Cela
n'activerait qu'une chose dans tout PETSc — l'enregistrement de `MATORDERINGMETISND`, seul garde de
`PETSC_HAVE_METIS` dans l'arbre — et n'a d'intérêt que par le chemin `canuseordering`, ouvert quand
`size == 1`, où `-pc_factor_mat_ordering_type metisnd` fait passer `ICNTL(7)` de 7 à 1. Réel mais
facultatif, et surtout cela **modifierait** la configuration de variantes existantes, ce qui ruine
l'argument « rien n'est modifié pour les variantes actuelles ». À reprendre après, s'il y a lieu.
Scotch, lui, n'a rien de séquentiel côté PETSc : `PETSC_HAVE_PTSCOTCH` ne garde que des
partitionneurs parallèles, et c'est PT-Scotch, paquet distinct de celui que MUMPS lie.

[issue]: https://github.com/msys2/MINGW-packages/issues/30888

---

## 7. Les exemples, exercés en CI

Le paquet se construit avec `SPARSELIZARD_BUILD_EXAMPLES=OFF` — 57 exécutables n'y ont pas leur
place. Mais le workflow reconfigure ensuite l'arbre que `makepkg` laisse derrière lui, ce qui les
construit **sans recompiler la bibliothèque**, puis lance `ctest`. C'est le seul exercice large de
la bibliothèque qui existe ; avant cela il n'y avait que la vérification de capacitance du §5.

Tout l'outillage venait du patch 0001 : chaque exemple est enregistré avec son répertoire de
travail — indispensable, plusieurs partagent `disk.msh`, `quad.msh`, `u.vtu` — et un délai de 600 s.

**Premier passage, sans gmsh** (54 enregistrés, 3 écartés pour `gmsh.h`) :

```
CLANG64      45/54   3295 s
CLANGARM64   45/54   3210 s      exactement les mêmes 9 échecs
MINGW64      ne compile pas
UCRT64       ne compile pas
```

Les 9 échecs, en trois familles, et **aucun n'était un défaut de la bibliothèque** :

- **trois réclamaient l'API gmsh à l'exécution** — leurs maillages sont au format **4.1**, que le
  lecteur natif refuse, là où ceux qui passent sont en **2.2**. Discriminant vérifiable d'une
  ligne : `sed -n '2p' *.msh`. Le garde du CMake ne lit que `gmsh.h` dans la source, il ne peut pas
  voir le format du maillage ;
- **deux n'ont pas de maillage du tout** — `channel.msh` et `waveguide3D.msh` sont absents du dépôt
  amont, qui n'en livre que le `.geo`. Omission amont, à signaler ;
- **quatre dépassent les 600 s** — `magnetodynamics-av-induction-3d`,
  `nonlinear-natural-convection-hpfem-2d`, `superconductor-3d`,
  `thermoacoustic-elasticity-axisymmetry-2d`. On sait qu'ils dépassent dix minutes, **pas de borne
  supérieure**.

L'échec de compilation sur gcc est traité par le patch 0007.

**Les cinq échecs d'exécution sont traités par le patch 0008**, qui ne les enregistre plus. Le
raisonnement : cinq exemples rouges à chaque run, pour des motifs connus et acceptés, font qu'un
run rouge ne signale plus rien — c'est le contraire de ce qu'on demande à une suite. Les conditions
sont lues dans l'exemple lui-même, jamais tenues dans une liste qui vieillirait.

Vérifié **dans les deux sens**, un garde qui écarte trop étant pire que pas de garde :

```
gmsh OFF   49 cibles sur 57,  8 écartées, chacune avec son motif
gmsh ON    55 cibles sur 57,  2 écartées — les deux maillages absents
```

La contre-épreuve à `ON` a d'ailleurs trouvé un défaut du garde : `nonlinear-truss-elasticity-2d`
charge `"gmsh:truss2d.msh"`, où `gmsh:` est un **préfixe de lecteur** — `tool:source`, avec `tool`
dans {gmsh, petsc, native}, lu par `rawmesh::readfromfile` — et non un nom de fichier. Le garde
cherchait un fichier littéralement nommé `gmsh:truss2d.msh` et écartait l'exemple à tort quand
gmsh était activé. Sans la contre-épreuve, le garde aurait masqué en silence un exemple parfaitement
exécutable.

**Deux pièges du montage, l'un et l'autre payés :**

- **ninja s'arrête à la première erreur.** Un exemple qui ne compilait pas a empêché les 56 autres
  de tourner : les deux chaînes gcc n'ont rien rapporté du tout. D'où `-k 0`, et le step `ctest`
  conditionné sur l'*exécution* du précédent et non sur sa réussite.
- **Borner par `timeout(1)`, pas par l'échéance du job.** Un job tué par GitHub emporte le résumé
  CTest avec lui.

### 7.1 gmsh : essayé, mesuré, reposé

**Décision : gmsh reste désactivé.** Elle a été renversée puis rétablie dans la même journée, et le
détour valait la mesure qu'il a produite.

Ce qu'on gagnerait, vérifié à la configuration : **57 cibles enregistrées au lieu de 54**, et les
trois exemples en maillage 4.1 deviennent exécutables. `mingw-w64-gmsh` déclare exactement le même
`mingw_arch` que notre paquet, livre `gmsh.h` et `libgmsh.dll.a`, et le gabarit du config amont
gère déjà la dépendance publique (`find_dependency(GMSH)` sous garde, `FindGMSH.cmake` installé à
côté) — le paquet exporté resterait consommable. Rien ne s'y oppose techniquement.

Ce que ça coûte, et c'est ce qui a tranché :

```
146 paquets     327 Mo à télécharger     2,47 Go installés
```

Les plus gros : **vtk 675 Mo**, opencascade 262, python 213, librsvg 186, imath 116, ffmpeg 81.
L'essentiel n'arrive pas par gmsh — 40 Mo — mais par OpenCASCADE, qui dépend de **vtk**, ffmpeg,
tcl, tk et openvr. Et il n'existe **aucun paquet gmsh allégé ou sans interface** : une seule
version par environnement. `optdepends` n'est pas une échappatoire non plus — `HAVE_GMSH` est
compilé dans la bibliothèque et `libgmsh.dll` est requise au chargement, donc la dépendance est
dure dès l'option activée.

> **Le premier chiffre annoncé, « environ 300 Mo », était faux.** Obtenu en additionnant les
> tailles propres de gmsh et d'OpenCASCADE sans parcourir la fermeture. La décision d'activer a
> donc été prise sur un nombre huit fois trop petit, et rétablie une fois le bon connu. Additionner
> deux paquets au lieu de parcourir le graphe est exactement le raccourci que ce journal existe
> pour éviter.

Et le coût s'est payé le jour même : le run avec gmsh a échoué sur les deux chaînes gcc, non sur du
code mais sur le miroir —

```
error: failed retrieving file 'mingw-w64-x86_64-opencascade-...' : Operation too slow
```

327 Mo à tirer sur chaque job de chaque environnement rend la CI sensible à un miroir lent. Les
chaînes clang, elles, sont passées : c'est un aléa, pas un déterminisme.

**Conséquence assumée** : six exemples sur 57 restent inexercés — trois écartés à la configuration
pour `gmsh.h`, trois qui échouent à l'exécution sur leur maillage 4.1. À reprendre si le besoin
d'un lecteur 4.1 se manifeste pour autre chose que les exemples.

---

## 8. Correctifs sparselizard portés en patchs

Huit, dans `mingw-w64-sparselizard`, appliqués sur le commit amont `0b826d88` :

| patch | objet |
| --- | --- |
| 0001 | découverte d'un PETSc/SLEPc existant au lieu du schéma `~/SLlibs` |
| 0002 | retrait du `using namespace std` d'un en-tête public |
| 0003 | `install(EXPORT)` et `sparselizardConfig.cmake` |
| 0004 | repli silencieux sur le LU de PETSc quand le solveur demandé n'existe pas |
| 0005 | découverte d'un PETSc livrant une bibliothèque par variante |
| 0006 | artefacts Visual Studio émis pour MSVC et non pour Windows au sens large |
| 0007 | retrait du `using namespace std` du seul exemple qui en avait un |
| 0008 | un exemple qui ne peut pas tourner n'est plus enregistré |

Les 0002, 0004, 0005, 0006, 0007 et 0008 sont des correctifs amont à part entière, indépendants de
MSYS2.

Le 0007 demande une attention que son intitulé ne laisse pas deviner. La directive était presque
inutilisée — `std::string`, `std::vector`, `std::pow`, `std::cout` sont déjà qualifiés partout. Ce
qui restait, ce sont les `pow` et `sqrt` nus, et **un préfixe uniforme les aurait cassés** : `T0`
est un `double`, tandis que `Cp`, `c` et `gamma` sont des `expression`. Donc `pow(T0,n)` et le
`sqrt` sur doubles passent en `std::`, tandis que `sqrt(Cp*(gamma-1)/T0)` et `pow(c,2)` restent nus
et continuent de résoudre vers `sl::`. L'inverse aurait compilé en changeant le sens.

Vérifié plutôt que supposé, avec le gcc 16.1 local en `__cplusplus 202002L` : l'unité de traduction
compile, et le contrôle négatif — le fichier d'origine, même configuration — rend bien ses
18 erreurs `reference to 'integral' is ambiguous`.
