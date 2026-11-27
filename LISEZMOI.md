# Noise Padifier

Effet « pad » par convolution : chaque canal de l'entrée est convolué avec du **bruit façonné**
(couleur, densité, peignes accordés sur une note ou un accord, enveloppe par fréquence, LFO sur
la note, la couleur, la résonance, la densité et des filtres balayés, avec une phase par cible,
mouvement stéréo, vraie stéréo croisée ; réponse de 0,1 à 30 s) ; tout son est
étalé en nappe. Version 1.4.0 (bouton RANDOM ; traitements du bruit : `AMELIORATIONS.md` ; nouveautés de la 1.2 :
même fichier, § « 1.2.0 » ; de la 1.3 (porte rythmique retirée, LFO étendu) : § « 1.3.0 »).
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
indépendant par trajet (graine (Seed, trajet), SplitMix64) : source lisse ou éparse (Density,
cellules au fil du LFO) → couleur (pente Colour par FFT) → peignes (Tune, Note, Chord, Resonance
et LFO, vibrato ; amorce de min(T60 le plus long, 5 s)) → enveloppe (dans le temps, ou par TFCT si
Damping, Bloom, LFO tone ou filtres balayés : Butterworth d'ordre 2 en module autour de Low cut et
High cut, le filtre fixe correspondant du greffon alors coupé) → énergie 1. LFO : sin(2π rate t +
phase de la cible), phase 0/90/180/270° par cible (`LfoTarget`) ; puis direct × cos(c π/4) + croisé × sin(c π/4) et
panoramique (Pan). Tables communes aux flux, flux en parallèle (fils du greffon) : reconstruction
0,02 s par défaut, pire cas (30 s, tout activé, Cross) 0,66 s à 48 kHz et 2,7 s à 192 kHz
(mémoire passagère ~620 Mo) ; session 74, autre machine : 0,46 s et 1,9 s avec les nouvelles cibles
du LFO (+9 % sur les mêmes réglages sans elles, mesuré par `tools/ir.cpp`).
Paramètres sans effet ramenés à leur défaut dans `fromParams` (pas de reconstruction inutile).

**Convolution** (`plugin/Convolver.hpp`), sans allocation dans le fil audio : sortie o = entrée o ∗
h[o][o] (+ autre entrée ∗ h[1-o][o] avec Cross) ; trois étages de convolution partitionnée
(recouvrement-sauvegarde), chacun avec ses lignes à retard fréquentielles des entrées :
- tête : blocs B = 256, échantillons [0, 2M1) ; **latence 256 échantillons** (déclarée à l'hôte ;
  le sec est retardé d'autant) ;
- étage 1 : blocs M1 = 4096 (fs ≤ 50 kHz), 8192 (≤ 100 kHz) ou 16384, échantillons [2M1, 2M2)
  (6 blocs) ;
- étage 2 : blocs M2 = 4 M1, échantillons [2M2, N).
Dans un étage, travail réparti sur les M/B trames qui précèdent (FFT des deux entrées à la
première, produits ensuite, FFT inverses des sorties aux deux dernières : pas de pointe de calcul).
Produits complexes par échantillon et par sortie à 30 s et 48 kHz : 124 (382 avec deux étages).
Coût (un cœur, `BENCH=1 ./build.sh`, stéréo, blocs de 256 ; options du greffon, deux mesures,
quelques % d'écart) : 48 kHz 0,9 % (3 s), 1,2 % (10 s), 2,2 % (10 s, Cross), 2,2 % (30 s), 3,9 %
(30 s, Cross) ; 96 kHz 2,4, 3,1, 5,7, 5,5, 9,3 % ; 192 kHz 7,6, 9,1, 16, 13, 24 %.

**Changement de réponse** (réglages de `isIRParam`) : réponse refaite
dans un fil du greffon, prise au début d'un bloc de l'étage 2 (trame A) ; chaque tâche (trame de
tête, bloc d'étage) calcule la réponse entendue là où tombe sa sortie : à partir de A + R2 tous
les étages ont la nouvelle, fondu linéaire de M1 échantillons sur [A + R2, A + R2 + R1), puis
l'ancienne est libérée hors du fil audio. Changement entendu au plus 0,7 s après sa construction. `activate()` construit la réponse sur-le-champ.
Pas de tempo de l'hôte (porte rythmique retirée après la 1.2.0).

Chaîne : (L, R) → convolution → pré-retard (0-500 ms, fondu de 30 ms) → Low cut → High cut → Width
→ pad ; sortie = pad · sin(π/2 · mix) + sec retardé · cos(π/2 · mix) → Output → fondu du Bypass
(vers le sec retardé : latence constante). Rampes de 20 ms ; FTZ/DAZ pendant `run`.

## Réglages
SOURCE : Colour (-6..+6 dB/oct, -3 rose), Density (10-10 000 /s, max = lisse), Seed.
TUNE : Tune (off, comb, chord), Note (C1-C7), Chord (9), Resonance (20 ms-5 s), LFO pitch
(« Vibrato », demi-tons). ENVELOPE : Length (0,1-30 s), Swell, Decay (part de la longueur prise
par la descente jusqu'à -60 dB, 0-100 %, 75 % par défaut ; tenue à plein niveau entre le pic et
le début de la descente ; en dB jusqu'à la 1.1 : une valeur de 1.1 est relue 0 %), Damping (dB/oct au-dessus de 250 Hz),
Bloom (±100 %). MOTION : LFO rate, profondeurs du LFO Tone, Reson (± % de Resonance), Density,
Low cut, High cut (± octaves), phases (une par cible, Vibrato compris : 0, 90, 180, 270°), Pan
rate, Pan depth, Cross. OUTPUT : Mix, Output, Low cut, High cut, Pre-delay, Width ; Bypass.
Numéros des paramètres de 1.0 gardés (Noise → Colour), nouveaux à la suite (`PadifierParams.hpp`) ;
Rhythm, Duty, Attack, Release et la sortie Tempo retirés après la 1.2.0 (numéros décalés, sans
compatibilité : aucun projet n'emploie les releases, décision de l'utilisateur) ; cibles du LFO et
phases ajoutées ensuite (après Pre-delay).

Préréglages d'usine (13) : Default, Soft pad, Endless wash, Dark cloud, Airy halo, Short smear,
Reverse swell, Tuned drone, Chord pad, Grain cloud, Blooming, Filter sweep, Send (100 % wet) ;
réglés à la lecture des lois, **pas à l'oreille** (`makePreset` : défauts + réglages donnés).

## Interface
1200 × 760, redimensionnable, HiDPI. Cinq sections dans l'ordre du signal : **1 SOURCE**, **2 TUNE**
(sélecteur OFF | COMB | CHORD ; notes et temps de résonance), **3 ENVELOPE** (pic par fréquence,
fin, échantillons, latence), **4 MOTION** (LFO rate et profondeurs du LFO, chacune avec sa case de phase ; Pan rate, Pan
depth, Cross ; périodes dans les bulles de LFO RATE et PAN RATE ; case de phase de Vibrato dans
TUNE), **5 OUTPUT** ;
boutons sans effet grisés (`knobActive`) ; Chord en liste déroulante (`kLists` : clic,
molette d'une valeur ; un bouton tournant y restait bloqué à la molette, le cran arrondi revenant
à la même valeur) ; molette d'un cran par valeur sur Note et Seed. En bas : **carte temps-fréquence** de la réponse (image
200 × 72 de `spectralGain`, refaite seulement quand la réponse ou le thème change ; notes
des peignes avec vibrato), **spectre du pad**, vu-mètres IN, PAD, OUT.
Repris de Tinplate : boutons (glisser, Maj, molette, double-clic, clic droit pour saisir), bulles
d'aide, préréglages de l'utilisateur (`<nom>.preset` dans `%APPDATA%\Padifier\presets\` ou
`~/.config/padifier/presets/`, clés = symboles des paramètres, `version=2` ; fichier 1.0 relu,
`noise` ignoré), A/B (état DPF `ab`), thèmes **Dusk** et **Pearl** (`padifier.conf` ; Pearl
opalescent en 1.2 : surfaces lilas laiteux, reflet lavande → pêche et lueur turquoise (`sheen`,
champs `sheen0-2` du thème, transparents pour Dusk), carte et vu-mètres nacre → lilas → violet →
pêche rosée (`map1-3`, valeurs de Dusk inchangées), accent vieux rose au lieu de l'ocre).
Code : `plugin/PadifierUI.cpp`, `PadifierPrefs.hpp`, `PadifierUserPresets.hpp`.
**RANDOM** (en-tête, entre thème et Bypass, comme dans Brimstone ; session 75) : `randomize` tire
tous les réglages d'un préréglage (Bypass gardé) uniformément sur la course du bouton (`fromNorm` :
logarithmique pour Length, Density, filtres, vitesses), Tune, Chord, Seed, Note et phases en
entiers ; préréglage affiché « Random » ; `std::mt19937` semé par `std::random_device`.

## Compiler
`./build.sh` (Linux, MinGW-w64 `-posix`, git ; `BENCH=1` : coût du DSP en plus) :
1. DPF au commit testé (`4238e1c`, copie de celui de Tinplate) et `patches/` (correctif VST3 de
   Brimstone) ;
2. tests des réponses (`tests/test_ir.cpp`) et du DSP (deux compilations : stricte, options de
   DPF `-O3 -ffast-math`), des préférences et des préréglages ;
3. Windows → `bin/windows/`, Linux → `bin/linux/` ;
4. installateur Windows (`installer/padifier.nsi`, `makensis`) →
   `bin/Noise-Padifier-1.2.0-setup.exe` (VST3 imposé, CLAP et manuels PDF en option, cochés par
   défaut ; les manuels vont dans `Program Files\Padifier\Noise-Padifier-manual.pdf` (anglais) et
   `Noise-Padifier-manuel.pdf` (français), avec deux raccourcis dans le menu Démarrer). Les PDF ne
   sont pas refaits par `build.sh` : recompiler `doc/manual.tex` et `doc/manuel.tex` avant si un
   manuel a changé.

**Manuels** (anglais `doc/manual.pdf`, source `doc/manual.tex` ; français `doc/manuel.pdf`, source
`doc/manuel.tex` ; 18 pages chacun en 1.2.0 : réglages, préréglages, conseils, algorithme avec
schémas et figures) (pdflatex ×3 dans `doc/`, sans
avertissement), figures `doc/figures.py` (`fig_*.pdf` en français, `fig_*_en.pdf` en anglais ; réponses impulsionnelles du greffon lui-même via
`tools/ir.cpp`, compilé dans `build/` par le script ; enveloppe, loi du mélange et filtres
réécrits en NumPy ; convolutions par FFT NumPy ; cartes temps-fréquence des traitements
`fig_traitements`, mouvement stéréo `fig_stereo`). **README.md** en anglais pour GitHub.

## Vérifications (1.3.0, 2026-11-26)
- `tests/test_ir.cpp`, deux compilations, tout passe (mesures moyennées sur 8 tirages × 2 canaux) :
  défauts (énergie 1, mêmes bits deux fois, flux indépendants), couleur -6..+6 à 0,73 dB près par
  octave, densité (une impulsion par cellule), peigne et accord (pics 19-24 dB au-dessus des
  creux), vibrato à 0,4 Hz près, damping à 0,8 dB de la loi, bloom à 0,15 s, LFO de couleur
  24,0 dB pour 24, panoramique (±74 dB), Cross (énergie 1 par sortie) ; session 74 : phases
  (vibrato à 180°, tone à 90°), Reson (pics 30 dB au haut du LFO, 2 dB au bas, aussi à 180°),
  Density (impulsions = intégrale de la loi à 1 près), filtres balayés (à 0,6 dB de la loi
  Butterworth), réglages sans effet ignorés. Réponses identiques au bit près à celles d'avant pour
  des réglages sans nouvelle cible (`tools/ir.cpp` avant et après).
- `tests/test_dsp.cpp`, deux compilations, tout passe : convolution = convolution directe en double
  retardée de 256 échantillons (≤ 4,4e-7 de la crête, 1 à 4 M2 + 7 échantillons (les trois étages),
  48-192 kHz, blocs de l'hôte de 1 à 512) ; réponses croisées (≤ 3,5e-7) ; changement de réponse
  au moment prévu (ancienne jusqu'à A + R2, fondu de M1, nouvelle après) ;
  greffon : latence 256, mix 0 et Bypass = entrée retardée au bit près, mix 100 % = convolution par
  les réponses (aussi avec Cross, accord, damping, mouvement, et avec le LFO sur Low cut et
  High cut : filtres fixes coupés), pré-retard exact (480 échantillons pour 10 ms), changement de Length sans clic, filtres,
  Width 0, 13 préréglages finis, état du FPU rendu.
- `tests/test_prefs.cpp`, `tests/test_user_presets.cpp`.
- Validateur VST3 de Steinberg (`tools/validate_vst3.sh`) : 47/47 sur le VST3 Linux et sur le VST3
  Windows (sous Wine ; le script passe `-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY`, sans quoi
  `-municode` fait échouer le test du compilateur de CMake avec le MinGW de cette machine).
- Installateur (NSIS, 1.1.0) : installation et désinstallation silencieuses (`/S`) sous Wine dans un
  préfixe neuf : VST3 et CLAP identiques à `bin/windows/`, manuels identiques, licences, raccourcis
  du menu Démarrer, version 1.1.0 au registre, puis tout retiré.
- Interface 1.2.0 affichée par la version autonome Windows sous Wine, deux thèmes (`doc/captures/` :
  Chord pad en Dusk, Blooming en Pearl avec la bulle de BLOOM ; en 1.3.0, Filter sweep en Pearl
  avec la bulle de la phase de LOW CUT). `./build.sh` complet sans avertissement ; validateur
  47/47 Linux et Windows (1.3.0) ; manuels sans avertissement LaTeX.
- **Non vérifié** : écoute (dont les nouvelles cibles du LFO et Filter sweep) ; vrai logiciel
  audio ; clics et saisie dans un hôte (cases de phase jamais cliquées hors captures) ; installateur
  sur un vrai Windows.

## Pour reprendre
- Dossier créé en session 69 (2026-11-26) à côté de `tinplate_vst3/`, dont il reprend la
  structure. Manuel et README faits. Dépôt git : `master`, distant `origin` =
  `git@github.com:tterraz-vibe-audio/noise_padifier.git` (public, comme Tinplate ; transféré du compte
  `tterraz` à l'organisation `tterraz-vibe-audio` le 2026-11-27, l'ancienne adresse redirige).
- Seule la dernière release est gardée sur GitHub, étiquettes gardées (`v1.0.0`…`v1.3.0` supprimées le
  2026-11-27).
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
- Captures (depuis la 1.1.0) : ajouter `-DPADIFIER_UI_DEMO_PRESET=<n>` ; chercher la fenêtre par sa taille
  (`grep 1200x760`), sinon `import` peut prendre l'écran entier ; atelier du validateur
  `build/vst3val`, préfixe Wine `build/wineprefix` (dans `build/`, non suivi).
- **Release `v1.2.0`** (session 70, 2026-11-26, commit `4e938ba`) : porte rythmique Attack/Release
  et listes (`ca5ef86`), Decay en %, Length 30 s, convolution à trois étages, Pearl opalescent.
  Même procédure ; fichiers dans `build/release/1.2.0/`, notes `build/release/notes-1.2.0.md` ;
  installateur testé sous Wine (préfixe neuf `build/wine_inst120` : fichiers identiques, version
  1.2.0 au registre, raccourcis, tout retiré), validateur 47/47 Linux et Windows sur les binaires
  publiés. Préréglages à réécouter (Decay converti à la lecture des lois).
- Captures : `tools/capture_ui.sh EXE OUT.png` (attend la fenêtre visible, ne garde que une image de
  1200x760 : Wine crée d'abord une fenêtre temporaire de même nom, d'où des captures de l'écran
  entier). Manuels : Dusk = `-DPADIFIER_UI_DEMO=-1 -DPADIFIER_UI_DEMO_THEME=0
  -DPADIFIER_UI_DEMO_PRESET=8` ; Pearl = `-DPADIFIER_UI_DEMO=304 -DPADIFIER_UI_DEMO_THEME=1
  -DPADIFIER_UI_DEMO_PRESET=11` (Filter sweep, bulle de la phase de LOW CUT ; cases de phase :
  survol 300 + rang de `kPhases`).
- **Release `v1.3.0`** (session 74, 2026-11-26) : porte rythmique retirée (décision de
  l'utilisateur : pas utile) ; LFO sur Resonance, Density, Low cut et High cut, phase par cible
  (crans de 90°, décision de l'utilisateur), préréglage Filter sweep ; détail `AMELIORATIONS.md`
  § « 1.3.0 ». Même procédure ; fichiers dans `build/release/1.3.0/`, notes
  `build/release/notes-1.3.0.md` ; installateur testé sous Wine (préfixe neuf `build/wine_inst130` :
  VST3, CLAP et manuel identiques, version 1.3.0 au registre, raccourcis, tout retiré), validateur
  47/47 Linux et Windows sur les binaires publiés. Tout changement livré ensuite demande un
  nouveau numéro.
- **Release `v1.4.0`** (session 77, 2026-11-27) : bouton RANDOM (session 75, `7269044`) ; interface
  seule (son, paramètres, préréglages de la 1.3.0). Captures `doc/captures/` refaites (versions
  autonomes `build-demo-d14`, `build-demo-p14`, réglages de capture ci-dessus), manuels de 18 pages
  sans avertissement. Même procédure ; fichiers dans `build/release/1.4.0/`, notes
  `build/release/notes-1.4.0.md` ; `./build.sh` sans avertissement, tests passés ; validateur 47/47
  Linux et Windows ; installateur sous Wine (préfixe neuf `build/wine_inst140` : VST3, CLAP et
  manuels identiques, `DisplayVersion` 1.4.0 dans `system.reg` (`wine reg query` ne rend rien ici),
  raccourcis, tout retiré). Release `v1.3.0` supprimée, étiquette gardée. Tirage RANDOM non essayé
  au clic ; bulle d'aide : zone 113 (`-DPADIFIER_UI_DEMO=113`, la bulle suit la vraie souris).
  Tout changement livré ensuite demande un nouveau numéro.
- Pistes non faites : préréglages à l'oreille ; gel (Freeze) ; affichage « réponse en cours de
  calcul » ; LFO calé sur le tempo (choix : le LFO part de l'attaque de chaque son).
- Ouvert depuis la 1.3.0 (non traité, à proposer si l'écoute le demande) : au réglage d'un LFO de
  filtre, le filtre fixe passe au neutre en 20 ms alors que la réponse balayée arrive après sa
  construction (bref écart de timbre, documenté dans les manuels) ; séparateur de TUNE descendu à
  394 pour la case de phase de VIBRATO, plus aligné sur celui d'ENVELOPE (370) ; phases en crans
  de 90° seulement (bouton continu écarté par l'utilisateur) ; le panoramique garde sa propre
  vitesse (choix : un LFO de timbre, un mouvement stéréo à part).
