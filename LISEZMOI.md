# Noise Padifier

Effet « pad » par convolution : chaque canal de l'entrée est convolué avec du **bruit blanc ou
rose sous une enveloppe** (réponse impulsionnelle de 0,1 à 10 s) ; tout son est étalé en nappe.
C++ avec DPF, interface NanoVG, structure reprise de Tinplate et Brimstone.
Formats : VST3 et CLAP pour Windows 64 bits ; VST3, CLAP et LV2 pour Linux.
**Licence : GPL 3 ou ultérieure** (`COPYING`) ; FFT : pffft de Julien Pommier (licence de
FFTPACK, type BSD, `plugin/pffft/`, provenance dans `PROVENANCE.txt`) ; DPF sous ISC. Notices des
composants tiers contenus dans les binaires (pffft, DPF, NanoVG, pugl, en-têtes CLAP, police DejaVu
Sans) : `THIRD_PARTY_LICENSES.txt`, installé et joint à chaque archive de release.
Nom provisoire (essais : Mica, Graphene ; changer : `DistrhoPluginInfo.h`, `Makefile`,
installateur, préférences, en-tête de l'interface).

## Algorithme
**Réponse impulsionnelle** (`plugin/NoiseIR.hpp`), par canal c (gauche, droite) :
h_c(n) = k_c · e(n / fs) · w_c(n), N = round(Length · fs) échantillons.
- w_c : bruit gaussien (Box-Muller sur SplitMix64, graine (Seed, c) : mêmes échantillons sur toute
  machine) ; rose : spectre (FFT circulaire sur la puissance de 2 ≥ N) multiplié par
  1/√max(f, 20 Hz), continu retiré, soit -3 dB/octave dès 20 Hz ;
- e : montée sin² jusqu'à Ta = Swell · Length, puis droite en dB jusqu'à Decay (dB) à la fin, et
  fondu cos² sur les dernières min(20 ms, Length/10) (jamais de marche en fin de réponse) ;
- k_c : énergie unité (Σh² = 1) : un signal à large bande garde son niveau dans le pad.
Les deux canaux ont des bruits indépendants : une entrée mono ressort large.

**Convolution** (`plugin/Convolver.hpp`), gauche par gauche, droite par droite, sans allocation
dans le fil audio : deux étages de convolution partitionnée uniforme (recouvrement-sauvegarde)
qui partagent les lignes à retard fréquentielles de l'entrée :
- tête : blocs B = 256, échantillons [0, 2M) de la réponse ; **latence 256 échantillons**
  (déclarée à l'hôte ; le signal sec est retardé d'autant) ;
- queue : blocs M = 4096 (fs ≤ 50 kHz), 8192 (≤ 100 kHz) ou 16384 ; le travail d'un bloc (une FFT,
  les produits des partitions, une FFT inverse) est réparti sur les M/B trames qui précèdent son
  premier échantillon utile (pas de pointe de calcul).
Coût mesuré (un cœur, `BENCH=1 ./build.sh`, stéréo, blocs de 256) : 48 kHz 1,1 % (3 s) et 2,4 %
(10 s) ; 96 kHz 3,0 % et 6,1 % ; 192 kHz 9,3 % et 15 %.

**Changement de réponse** (Noise, Length, Swell, Decay, Seed) : la réponse est refaite dans un fil
du greffon (jamais dans le fil audio), puis prise au début d'un bloc de queue ; les lignes à retard
gardent tout le passé de l'entrée, la nouvelle réponse l'a donc d'emblée ; fondu linéaire de M
échantillons (85 ms à 48 kHz) de l'ancienne sortie à la nouvelle ; l'ancienne réponse est libérée
dans le fil du greffon. `activate()` construit la réponse sur-le-champ (un rendu commence avec la
bonne). En rendu hors ligne plus rapide que le temps réel, un changement automatisé peut arriver
en retard (le temps de construire la réponse : de l'ordre de 0,1 s de calcul).

Chaîne : (L, R) → convolution → Low cut → High cut → Width → pad ;
sortie = pad · sin(π/2 · mix) + sec retardé · cos(π/2 · mix) → Output → fondu du Bypass (vers le
sec retardé : latence constante). Mix, Width, filtres, Output : rampes de 20 ms ; FTZ/DAZ pendant
`run`, état du FPU de l'hôte rendu.

## Réglages
| Réglage | Rôle |
|---|---|
| Noise | WHITE (plat, brillant) ou PINK (-3 dB/octave, chaud) |
| Seed | tirage du bruit, 1-99 (même graine : même réponse) |
| Length | longueur de la réponse, 0,1-10 s, bouton logarithmique |
| Swell | montée de l'enveloppe, % de la longueur (100 % : montée sur toute la longueur) |
| Decay | niveau à la fin, -60..0 dB (0 : pad tenu, -60 : comme une réverbération) |
| Mix | sec/pad à puissance constante ; 100 % : pad seul (départ auxiliaire) |
| Width | largeur stéréo du pad, 0-200 % |
| Low cut | passe-haut 12 dB/octave sur le pad, 20 Hz (off) à 1 kHz |
| High cut | passe-bas 12 dB/octave sur le pad, 1 kHz à 20 kHz (off) |
| Output | gain de toute la sortie, -24..+12 dB |
| Bypass | paramètre Bypass déclaré à l'hôte, fondu de 10 ms |

Préréglages d'usine : Default, Soft pad, Endless wash, Dark cloud, Airy halo, Short smear,
Reverse swell, Send (100 % wet) ; réglés à la lecture des lois, **pas à l'oreille**.

## Interface
900 × 600, redimensionnable, HiDPI. Trois sections dans l'ordre du signal : **1 NOISE** (WHITE |
PINK, esquisse du spectre, Seed), **2 ENVELOPE** (Length, Swell, Decay ; pic de l'enveloppe, fin,
nombre d'échantillons, latence), **3 OUTPUT** (Mix, Width, Output, Low cut, High cut ; vu-mètres
IN, PAD, OUT). En bas : **enveloppe** de la réponse en fonction du temps (dB, pic en tirets) et
**spectre du pad** (couleur du bruit et filtres).
Repris de Tinplate tel quel : boutons (glisser, Maj, molette, double-clic, clic droit pour saisir),
bulles d'aide, préréglages de l'utilisateur (`<nom>.preset` dans `%APPDATA%\Padifier\presets\` ou
`~/.config/padifier/presets/` ; clés `noise`, `length`, `swell`, `decay`, `seed`, `lowcut`,
`highcut`, `width`, `mix`, `output`), A/B (état DPF `ab`), thèmes **Dusk** (sombre) et **Pearl**
(clair), préférence dans `padifier.conf` (`theme=dusk|pearl`).
Code : `plugin/PadifierUI.cpp`, `PadifierPrefs.hpp`, `PadifierUserPresets.hpp`.

## Compiler
`./build.sh` (Linux, MinGW-w64 `-posix`, git ; `BENCH=1` : coût du DSP en plus) :
1. DPF au commit testé (`4238e1c`, copie de celui de Tinplate) et `patches/` (correctif VST3 de
   Brimstone) ;
2. tests du DSP (deux compilations : stricte, options de DPF `-O3 -ffast-math`), des préférences
   et des préréglages ;
3. Windows → `bin/windows/`, Linux → `bin/linux/` ;
4. installateur Windows (`installer/padifier.nsi`, `makensis`) →
   `bin/Noise-Padifier-1.0.1-setup.exe` (VST3 imposé, CLAP et manuels PDF en option, cochés par
   défaut ; les manuels vont dans `Program Files\Padifier\Noise-Padifier-manual.pdf` (anglais) et
   `Noise-Padifier-manuel.pdf` (français), avec deux raccourcis dans le menu Démarrer). Les PDF ne
   sont pas refaits par `build.sh` : recompiler `doc/manual.tex` et `doc/manuel.tex` avant si un
   manuel a changé.

**Manuels** (anglais `doc/manual.pdf`, source `doc/manual.tex`, depuis la 1.0.1 ; français, 12 pages : réglages, préréglages, conseils, algorithme avec schémas et
figures) : `doc/manuel.pdf` ; source `doc/manuel.tex` (pdflatex ×3 dans `doc/`, sans
avertissement), figures `doc/figures.py` (`fig_*.pdf` en français, `fig_*_en.pdf` en anglais ; réponses impulsionnelles du greffon lui-même via
`tools/ir.cpp`, compilé dans `build/` par le script ; enveloppe, loi du mélange et filtres
réécrits en NumPy ; convolutions par FFT NumPy). **README.md** en anglais pour GitHub.

## Vérifications (2026-11-26)
- `tests/test_dsp.cpp`, deux compilations, tout passe :
  convolution = convolution directe en double retardée de 256 échantillons, écart ≤ 4,4e-7 de la
  crête (9 longueurs de 1 à 5M + 3, à 48, 96 et 192 kHz, blocs de l'hôte de 1 à 512) ; sec retardé
  exactement ; changement de réponse : ancienne, fondu (1 - g) ancienne + g nouvelle au moment
  prévu, puis nouvelle (écart 2,6e-7), ancienne réponse rendue pour être libérée ; réponse :
  énergie 1, mêmes bits deux fois, corrélation gauche/droite et entre graines < 0,05, enveloppe
  suivie (RMS par fenêtres), bruit blanc +3 dB/octave et rose 0 dB/octave par octaves (125 Hz -
  16 kHz, ≤ 0,5 dB) ; greffon : latence 256, mix 0 et Bypass = entrée retardée au bit près, mix
  100 % = convolution avec la réponse des réglages, changement de Length refait par le fil et
  appliqué sans clic, Low cut (-40 dB), High cut (-43 dB), Width 0 (L = R), 8 préréglages finis
  (crête 0,59 pour du bruit à -12 dBFS), état du FPU rendu.
- `tests/test_prefs.cpp`, `tests/test_user_presets.cpp`.
- Validateur VST3 de Steinberg (`tools/validate_vst3.sh`) : 47/47 sur le VST3 Linux et sur le VST3
  Windows (sous Wine ; le script passe `-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY`, sans quoi
  `-municode` fait échouer le test du compilateur de CMake avec le MinGW de cette machine).
- Installateur (NSIS) : installation et désinstallation silencieuses (`/S`) sous Wine dans un
  préfixe neuf : VST3 identique à `bin/windows/`, CLAP, manuel identique à `doc/manuel.pdf`,
  licences, raccourci du menu Démarrer, entrée du registre, puis tout retiré.
- Interface affichée par la version autonome Windows sous Wine, deux thèmes (`doc/captures/`).
- **Non vérifié** : écoute ; vrai logiciel audio ; clics et saisie dans un hôte ; installateur sur
  un vrai Windows.

## Pour reprendre
- Dossier créé en session 69 (2026-11-26) à côté de `tinplate_vst3/`, dont il reprend la
  structure. Manuel et README faits. Dépôt git : `master`, distant `origin` =
  `git@github.com:tterraz/noise_padifier.git` (public, comme Tinplate).
- **Release GitHub** `v1.0.0` (2026-11-26) : `./build.sh` complet sur `master` propre ; fichiers
  assemblés dans `build/release/` (non suivi) : installateur, `Noise-Padifier-1.0.0-windows.zip`
  (VST3, CLAP), `Noise-Padifier-1.0.0-linux-x86_64.tar.gz` (VST3, CLAP, LV2), chacun avec
  `COPYING`, `THIRD_PARTY_LICENSES.txt` et le manuel, et le manuel seul ; notes en anglais
  (`build/release/notes.md`) ; étiquette annotée `v1.0.0` poussée, puis `/usr/bin/gh release create`.
  **La version 1.0.0 est publiée** : tout changement livré ensuite demande un nouveau numéro
  (`getVersion`, installateur, `\author` des deux manuels, README).
- **Release `v1.0.1`** (2026-11-26) : manuel anglais (`doc/manual.pdf`, dans l'installateur et les
  archives avec le manuel français) ; DSP et interface inchangés hors numéro de version. Même
  procédure ; le manuel anglais est aussi joint seul.
- Captures : `CXXFLAGS="-DPADIFIER_UI_DEMO=<zone> -DPADIFIER_UI_DEMO_THEME=<0|1>"`, version
  autonome (`make -C plugin jack BUILD_DIR_SUFFIX=-demo-<x> DPF_TARGET_DIR=../build-demo-<x>/bin`
  avec les variables MinGW de `build.sh`) sous Wine, `import -window` sur la fenêtre « Noise
  Padifier » (`xwininfo -root -tree`) ; la bulle affichée suit la vraie souris.
- Pistes non faites : préréglages à l'oreille ; pré-retard ; gel (Freeze) ; bruit brun ;
  réponse « vraie stéréo » (croisements L→R) ; affichage « réponse en cours de calcul ».
