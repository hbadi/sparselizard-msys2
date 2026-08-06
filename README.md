# sparselizard-msys2

Recette pacman pour [sparselizard](http://www.sparselizard.org) sous MSYS2/MinGW, et la CI
multi-saveurs qui va avec. Objectif : une soumission à
[`msys2/MINGW-packages`](https://github.com/msys2/MINGW-packages).

Ce dépôt existe pour une raison précise : **construire les quatre saveurs sans installer quatre
toolchains**. La matrice GitHub Actions couvre `mingw64`, `ucrt64`, `clang64` et `clangarm64` à
chaque poussée, ce qui permet de découvrir les problèmes propres à chacune bien avant de demander
une revue en amont.

## Ce que le paquet construit

sparselizard contre les **seuls paquets officiels** MSYS2 : petsc, slepc, openblas. Rien n'est
téléchargé ni bâti en dehors de pacman.

- **Variante PETSc `dto`** — réel double, stub MPI de PETSc, OpenMP. C'est la seule variante réelle
  double présente sur les quatre environnements : les variantes MPI n'existent pas sur aarch64.
- **OpenMP désactivé pour sparselizard** — il n'en a pas besoin lui-même, le parallélisme vit dans
  ses dépendances, et `petsc-dto` est déjà bâti avec.
- **MPI désactivé**, cohérent avec le stub MPIUNI.
- **Gmsh désactivé** pour l'instant, pour ne pas traîner une dépendance lourde.

## Solveur direct

Le PETSc de MSYS2 est bâti **sans MUMPS**, que sparselizard demande par défaut. Ça ne coûte rien à
la construction — sparselizard ne le nomme que par une chaîne de caractères, jamais par un symbole
— mais ça échouait à la première factorisation, avec un résultat `nan`.

sparselizard retombe désormais silencieusement sur le LU intégré de PETSc quand le solveur demandé
n'est pas enregistré. C'est plus lent et strictement séquentiel, ce qui est sans importance sur les
exemples et visible sur un cas réel.

## État : pas encore soumissible

Le `source=` pointe une **branche de développement**, pas une archive de version. Le support CMake
dont ce paquet dépend — `install(EXPORT)`, `sparselizardConfig.cmake`, la découverte d'un PETSc
multi-variantes — est encore en revue en amont.

Avant de soumettre, il faudra l'une de ces deux choses :

1. que ces changements soient intégrés en amont, et repartir d'une archive de version ;
2. ou porter la différence en fichiers `.patch` dans ce dépôt, ce qui est la pratique habituelle
   chez MINGW-packages.

## Construire à la main

```bash
cd mingw-w64-sparselizard
MINGW_ARCH=mingw64 makepkg-mingw --syncdeps --cleanbuild --force
pacman -U *.pkg.tar.zst
```

## Notes

Le détail des relevés — les douze variantes de PETSc et leur nommage, l'emplacement du stub MPIUNI,
le conflit entre `msmpi` et `<mpi.h>` — est consigné dans le dépôt de notes privé, hors de celui-ci.
