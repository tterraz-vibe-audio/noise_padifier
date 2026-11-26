# Noise Padifier

Effet « pad » par convolution : chaque canal de l'entrée est convolué avec du **bruit façonné**
(couleur, densité, peignes accordés sur une note ou un accord, enveloppe par fréquence, LFO,
rythme sur le tempo, mouvement stéréo, vraie stéréo croisée ; réponse de 0,1 à 10 s) ; tout son est
étalé en nappe. Version 1.1.0 (traitements du bruit : `AMELIORATIONS.md`).
C++ avec DPF, interface NanoVG, structure reprise de Tinplate et Brimstone.
Formats : VST3 et CLAP pour Windows 64 bits ; VST3, CLAP et LV2 pour Linux.
**Licence : GPL 3 ou ultérieure** (`COPYING`) ; FFT : pffft de Julien Pommier (licence de
FFTPACK, type BSD, `plugin/pffft/`, provenance dans `PROVENANCE.txt`) ; DPF sous ISC. Notices des
composants tiers contenus dans les binaires (pffft, DPF, NanoVG, pugl, en-têtes CLAP, police DejaVu
Sans) : `THIRD_PARTY_LICENSES.txt`, installé et joint à chaque archive de release.
Nom provisoire (essais : Mica, Graphene ; changer : `DistrhoPluginInfo.h`, `Makefile`,
installateur, préférences, en-tête de l'interface).

## Algorithme
**Réponses impulsionnelles** (`plugin/NoiseIR.hpp`, étapes détaillées dans son en-tête et dans le
manuel § « Les réponses impulsionnelles ») : deux trajets directs (quatre avec Cross), bruit
indépendant par trajet (graine (Seed, trajet), SplitMix64) : source lisse ou éparse (Density) →
couleur (pente Colour par FFT) → peignes (Tune, Note, Chord, Resonance, vibrato ; amorce de
min(T60, 5 s)) → enveloppe (dans le temps, ou par TFCT si Damping, Bloom ou LFO tone) → porte
(Rhythm au tempo de l'hôte ; par pas : montée sin² Attack, palier jusqu'à Duty, descente cos²
Release, en % du pas, fronts ≥ 1 ms ; maximum du pas en cours et des descentes précédentes) → énergie 1 ; puis direct × cos(c π/4) + croisé × sin(c π/4) et
panoramique (Pan). Tables communes aux flux, flux en parallèle (fils du greffon) : reconstruction
0,02 s par défaut, pire cas 0,26 s à 48 kHz et 1,05 s à 192 kHz (mémoire passagère ~340 Mo).
Paramètres sans effet ramenés à leur défaut dans `fromParams` (pas de reconstruction inutile).

**Convolution** (`plugin/Convolver.hpp`), sans allocation dans le fil audio : sortie o = entrée o ∗
h[o][o] (+ autre entrée ∗ h[1-o][o] avec Cross) ; deux étages de convolution partitionnée uniforme
(recouvrement-sauvegarde) qui partagent les lignes à retard fréquentielles des entrées :
- tête : blocs B = 256, échantillons [0, 2M) ; **latence 256 échantillons** (déclarée à l'hôte ;
  le sec est retardé d'autant) ;
- queue : blocs M = 4096 (fs ≤ 50 kHz), 8192 (≤ 100 kHz) ou 16384, travail réparti sur les M/B
  trames qui précèdent (pas de pointe de calcul).
Coût (un cœur, `BENCH=1 ./build.sh`, stéréo, blocs de 256) : 48 kHz 1,0 % (3 s), 2,2 % (10 s),
4,5 % (10 s, Cross) ; 96 kHz 3,0, 5,4 et 10,5 % ; 192 kHz 8,8, 14,8 et 24,8 %.

**Changement de réponse** (réglages de `isIRParam`, ou tempo de l'hôte si Rhythm) : réponse refaite
dans un fil du greffon, prise au début d'un bloc de queue, fondu linéaire de M échantillons ;
l'ancienne est libérée hors du fil audio. `activate()` construit la réponse sur-le-champ. Tempo :
`DISTRHO_PLUGIN_WANT_TIMEPOS`, sortie `kParamTempo` (montrée par l'interface), 120 BPM sans hôte.

Chaîne : (L, R) → convolution → pré-retard (0-500 ms, fondu de 30 ms) → Low cut → High cut → Width
→ pad ; sortie = pad · sin(π/2 · mix) + sec retardé · cos(π/2 · mix) → Output → fondu du Bypass
(vers le sec retardé : latence constante). Rampes de 20 ms ; FTZ/DAZ pendant `run`.

## Réglages
SOURCE : Colour (-6..+6 dB/oct, -3 rose), Density (10-10 000 /s, max = lisse), Seed.
TUNE : Tune (off, comb, chord), Note (C1-C7), Chord (9), Resonance (20 ms-5 s), LFO pitch
(« Vibrato », demi-tons). ENVELOPE : Length, Swell, Decay, Damping (dB/oct au-dessus de 250 Hz),
Bloom (±100 %). MOTION : LFO rate, LFO tone, Rhythm (divisions, pointées, triolets), Attack, Duty, Release (faders), Pan
rate, Pan depth, Cross. OUTPUT : Mix, Output, Low cut, High cut, Pre-delay, Width ; Bypass.
Numéros des paramètres de 1.0 gardés (Noise → Colour), nouveaux à la suite (`PadifierParams.hpp`).

Préréglages d'usine (13) : Default, Soft pad, Endless wash, Dark cloud, Airy halo, Short smear,
Reverse swell, Tuned drone, Chord pad, Pulse gate, Grain cloud, Blooming, Send (100 % wet) ;
réglés à la lecture des lois, **pas à l'oreille** (`makePreset` : défauts + réglages donnés).

## Interface
1200 × 760, redimensionnable, HiDPI. Cinq sections dans l'ordre du signal : **1 SOURCE**, **2 TUNE**
(sélecteur OFF | COMB | CHORD ; notes et temps de résonance), **3 ENVELOPE** (pic par fréquence,
fin, échantillons, latence), **4 MOTION** (période du LFO, durée d'un pas au tempo), **5 OUTPUT** ;
boutons sans effet grisés (`knobActive`) ; Chord et Rhythm en listes déroulantes (`kLists` : clic,
molette d'une valeur ; un bouton tournant y restait bloqué à la molette, le cran arrondi revenant
à la même valeur) ; molette d'un cran par valeur sur Note et Seed. En bas : **carte temps-fréquence** de la réponse (image
200 × 72 de `spectralGain` × porte, refaite seulement quand la réponse ou le thème change ; notes
des peignes avec vibrato), **spectre du pad**, vu-mètres IN, PAD, OUT.
Repris de Tinplate : boutons (glisser, Maj, molette, double-clic, clic droit pour saisir), bulles
d'aide, préréglages de l'utilisateur (`<nom>.preset` dans `%APPDATA%\Padifier\presets\` ou
`~/.config/padifier/presets/`, clés = symboles des paramètres, `version=2` ; fichier 1.0 relu,
`noise` ignoré), A/B (état DPF `ab`), thèmes **Dusk** et **Pearl** (`padifier.conf`).
Code : `plugin/PadifierUI.cpp`, `PadifierPrefs.hpp`, `PadifierUserPresets.hpp`.

## Compiler
`./build.sh` (Linux, MinGW-w64 `-posix`, git ; `BENCH=1` : coût du DSP en plus) :
1. DPF au commit testé (`4238e1c`, copie de celui de Tinplate) et `patches/` (correctif VST3 de
   Brimstone) ;
2. tests des réponses (`tests/test_ir.cpp`) et du DSP (deux compilations : stricte, options de
   DPF `-O3 -ffast-math`), des préférences et des préréglages ;
3. Windows → `bin/windows/`, Linux → `bin/linux/` ;
4. installateur Windows (`installer/padifier.nsi`, `makensis`) →
   `bin/Noise-Padifier-1.1.0-setup.exe` (VST3 imposé, CLAP et manuels PDF en option, cochés par
   défaut ; les manuels vont dans `Program Files\Padifier\Noise-Padifier-manual.pdf` (anglais) et
   `Noise-Padifier-manuel.pdf` (français), avec deux raccourcis dans le menu Démarrer). Les PDF ne
   sont pas refaits par `build.sh` : recompiler `doc/manual.tex` et `doc/manuel.tex` avant si un
   manuel a changé.

**Manuels** (anglais `doc/manual.pdf`, source `doc/manual.tex` ; français `doc/manuel.pdf`, source
`doc/manuel.tex` ; 17 pages chacun en 1.1.0 : réglages, préréglages, conseils, algorithme avec
schémas et figures) (pdflatex ×3 dans `doc/`, sans
avertissement), figures `doc/figures.py` (`fig_*.pdf` en français, `fig_*_en.pdf` en anglais ; réponses impulsionnelles du greffon lui-même via
`tools/ir.cpp`, compilé dans `build/` par le script ; enveloppe, loi du mélange et filtres
réécrits en NumPy ; convolutions par FFT NumPy ; cartes temps-fréquence des traitements
`fig_traitements`, mouvement stéréo `fig_stereo`). **README.md** en anglais pour GitHub.

## Vérifications (1.1.0, 2026-11-26)
- `tests/test_ir.cpp`, deux compilations, tout passe (mesures moyennées sur 8 tirages × 2 canaux) :
  défauts (énergie 1, mêmes bits deux fois, flux indépendants), couleur -6..+6 à 0,73 dB près par
  octave, densité (une impulsion par cellule), peigne et accord (pics 19-24 dB au-dessus des
  creux), vibrato à 0,4 Hz près, damping à 0,8 dB de la loi, bloom à 0,15 s, LFO de couleur
  24,0 dB pour 24, porte (zéro exact fermée), panoramique (±74 dB), Cross (énergie 1 par sortie).
- `tests/test_dsp.cpp`, deux compilations, tout passe : convolution = convolution directe en double
  retardée de 256 échantillons (≤ 4,4e-7 de la crête, 1 à 5M + 3 échantillons, 48-192 kHz, blocs de
  l'hôte de 1 à 512) ; réponses croisées (≤ 3,5e-7) ; changement de réponse au moment prévu ;
  greffon : latence 256, mix 0 et Bypass = entrée retardée au bit près, mix 100 % = convolution par
  les réponses (aussi avec Cross, accord, damping, mouvement), tempo de l'hôte 90 BPM → réponse
  refaite, pré-retard exact (480 échantillons pour 10 ms), changement de Length sans clic, filtres,
  Width 0, 13 préréglages finis, état du FPU rendu.
- `tests/test_prefs.cpp`, `tests/test_user_presets.cpp`.
- Validateur VST3 de Steinberg (`tools/validate_vst3.sh`) : 47/47 sur le VST3 Linux et sur le VST3
  Windows (sous Wine ; le script passe `-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY`, sans quoi
  `-municode` fait échouer le test du compilateur de CMake avec le MinGW de cette machine).
- Installateur (NSIS, 1.1.0) : installation et désinstallation silencieuses (`/S`) sous Wine dans un
  préfixe neuf : VST3 et CLAP identiques à `bin/windows/`, manuels identiques, licences, raccourcis
  du menu Démarrer, version 1.1.0 au registre, puis tout retiré.
- Interface 1.1.0 affichée par la version autonome Windows sous Wine, deux thèmes (`doc/captures/` :
  Chord pad en Dusk, Blooming en Pearl).
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
- **Release `v1.1.0`** (2026-11-26, commit `4247136`) : traitements du bruit (`AMELIORATIONS.md`),
  interface 1200 × 760, manuels de 17 pages. Même procédure ; fichiers dans `build/release/1.1.0/`,
  notes `build/release/notes-1.1.0.md` ; installateur testé sous Wine (préfixe neuf : fichiers
  identiques, version 1.1.0 au registre, tout retiré), validateur 47/47 Linux et Windows sur les
  binaires publiés.
- Captures de la 1.1.0 : ajouter `-DPADIFIER_UI_DEMO_PRESET=<n>` ; chercher la fenêtre par sa taille
  (`grep 1200x760`), sinon `import` peut prendre l'écran entier ; atelier du validateur
  `build/vst3val`, préfixe Wine `build/wineprefix` (dans `build/`, non suivi).
- Pistes non faites : préréglages à l'oreille ; gel (Freeze) ; affichage « réponse en cours de
  calcul » ; LFO calé sur le tempo (choix : le LFO part de l'attaque de chaque son).
