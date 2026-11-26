# Noise Padifier - améliorations

Statut : fait, publié en **1.1.0** (release `v1.1.0`, 2026-11-26). Restent les pistes non faites de `LISEZMOI.md`. Tout ajout qui change la sortie
demande une nouvelle version (1.1.0).

Principe commun : un traitement du bruit modifie la réponse impulsionnelle ; il est calculé à la
construction de la réponse (coût nul pendant la lecture) et ne change que par les fondus de
changement de réponse (≈ 85 ms à 48 kHz + temps de construction). La convolution reste linéaire :
elle ne crée aucune fréquence absente de l'entrée (ni distorsion, ni transposition) ; un filtre
fixe sur le bruit équivaut exactement au même filtre sur la sortie.

## Retenu (décision de l'utilisateur, session 69)
- **R1 Damping** : filtre passe-bas qui évolue le long de la réponse (les aigus meurent avant les
  graves, comme l'amortissement d'une réverbération ; à l'envers : nappe qui s'éclaircit).
  Pas équivalent à un filtre de sortie.
- **R2 Peigne / filtre résonant** : rend le pad tonal (note, résonance ; peigne : série
  harmonique de la note ; éventuellement formants). Fixe, il équivaut au même filtre sur la
  sortie ; il ne devient propre au bruit qu'avec R1 ou R3 (évolution dans le temps).
- **R3 LFO sur les filtres, le long de la réponse** (décision de l'utilisateur, session 69) : la
  coupure ou la note oscille sur l'axe du temps de la réponse ; figé dans la réponse, coût nul,
  chaque note reçoit le même mouvement depuis son attaque.
  Écarté : LFO en temps réel sur la sortie du pad (revient à un autre greffon placé après).

## Écarté
- Bit crusher (décision de l'utilisateur, session 69) : sur le bruit, effet faible ou étroit
  (pas de craquement : l'entrée n'est pas distordue).

## Proposé, jugé intéressant (utilisateur, session 69), ordre à fixer
- P1 Teinte continue du bruit (pente de brun à bleu) au lieu de White | Pink.
- P2 Enveloppe par bande (montée et descente propres à chaque bande : graves qui fleurissent
  après les aigus, ou l'inverse) ; généralise R1.
- P3 Densité (bruit épars, « velvet noise ») : du lisse au granuleux, jusqu'à un nuage d'échos.
- P4 Rythme : modulation d'amplitude du bruit le long de la réponse (porte, impulsions),
  synchronisée au tempo de l'hôte.
- P5 Mouvement stéréo : panoramique ou corrélation G/D qui évolue le long de la réponse ;
  croisements G→D, D→G (vraie stéréo).
- P6 Contenu tonal : réponse faite de partiels (accord) au lieu de bruit (le pad s'accorde sur
  un accord) ; balayage sinusoïdal (dispersion, son de ressort).
- P7 Pré-retard figé dans la réponse (zéros en tête).
- P8 Bouffées multiples (plusieurs salves de bruit : échos étalés).

## Décisions pour la 1.1.0 (session 69)
- Toutes les pistes R1-R3 et P1-P8 sont à faire (P8 salves : par la porte de P4 à faible rapport
  cyclique ; P6 accord : peignes sur les notes de l'accord).
- Interface : une seule page agrandie (≈ 1200 × 760), sections SOURCE, TUNE, ENVELOPE, MOTION,
  OUTPUT, carte temps-fréquence de la réponse en bas.
- P1 : Noise (blanc/rose) **remplacé** par Colour (dB/octave, -6..+6, -3 rose) ; numéros des
  paramètres de 1.0 gardés, nouveaux à la suite (projets 1.0 : seule la couleur revient au défaut).
- P5 : vraie stéréo en option (réglage Cross ; le calcul ne double que si Cross > 0).
- P4 : rythme calé sur le tempo de l'hôte (divisions 1/1 à 1/32, pointées, triolets ; 120 BPM
  sans tempo ; réponse refaite à chaque changement de tempo).

## Avancement de la 1.1.0
- Fait (non commité) : `plugin/PadifierParams.hpp` (paramètres, plages, accords, divisions,
  préréglages d'usine 1.1) ; **générateur** `plugin/NoiseIR.hpp` (étapes dans son en-tête ; tables
  communes aux flux, flux calculés en parallèle) ; `tools/ir.cpp` (réglages par symbole) ;
  `tests/test_ir.cpp` (tout passe, deux compilations) : couleur à 0,73 dB près par octave,
  densité exacte, peigne et accord (pics 19-24 dB au-dessus des creux), LFO de hauteur à 0,4 Hz,
  damping à 0,8 dB de la loi, bloom à 0,15 s, LFO de couleur 24,0 dB pour 24, porte exacte,
  panoramique, croisements (énergie 1 par sortie). Mesures moyennées sur 8 tirages × 2 canaux.
  Coût d'une reconstruction : 0,02 s par défaut ; pire cas (10 s, tout activé, accord de
  4 notes, Cross) 0,26 s à 48 kHz, 1,05 s à 192 kHz, mémoire passagère ~340 Mo à 192 kHz.
- Fait (non commité) : **convolution à 4 réponses** (`Convolver.hpp` : `IRSet` h[entrée][sortie],
  croisements seulement si Cross > 0) ; **greffon** (`PadifierPlugin.cpp`, version 1.1.0 dans
  `getVersion`) : nouveaux paramètres (énumérations Tune, Chord, Rhythm), tempo de l'hôte
  (`DISTRHO_PLUGIN_WANT_TIMEPOS`, sortie `kParamTempo`, reconstruction au changement si Rhythm),
  pré-retard du pad 0-500 ms (fondu de 30 ms) ; préréglages de l'utilisateur (clés 1.1, `version=2`,
  fichier 1.0 relu) ; `tests/test_dsp.cpp` (Cross contre convolution directe, Cross et traitements
  dans le greffon, tempo 90 BPM, pré-retard exact) et `build.sh` (test_ir dans les deux
  compilations) : tout passe, deux compilations.
  Coût : 48 kHz 10 s 2,2 % d'un cœur, avec Cross 4,5 % ; 192 kHz 14,8 % et 24,8 %.
- Fait (non commité) : **interface** 1200 × 760 (`PadifierUI.cpp`, `DistrhoPluginInfo.h`) : sections
  SOURCE, TUNE (OFF | COMB | CHORD), ENVELOPE, MOTION, OUTPUT ; réglages sans effet grisés ;
  carte temps-fréquence de la réponse (image 200 × 72 refaite seulement quand la réponse ou le
  thème change ; notes des peignes avec vibrato), spectre du pad, vu-mètres ; captures
  `doc/captures/capture_dusk.png` (Chord pad) et `capture_pearl.png` (Blooming).
  `./build.sh` complet sans avertissement (tests, Windows, Linux) ; validateur VST3 47/47 Linux et
  Windows (atelier `build/vst3val`, préfixe Wine `build/wineprefix`).
- Fait (non commité) : **documentation 1.1.0** : manuels `doc/manuel.pdf` et `doc/manual.pdf`
  (17 pages chacun, sans avertissement LaTeX ; réglages par section, préréglages, algorithme des
  réponses étape par étape, coût avec Cross, compatibilité 1.0, limites), figures (`figures.py` :
  nouvelle interface de `tools/ir.cpp`, `fig_traitements`, `fig_stereo`, pad accordé dans
  `fig_pad`), README, LISEZMOI, installateur en 1.1.0.
- **Publié** : commit `4247136`, étiquette et release GitHub `v1.1.0` (installateur, archives
  Windows et Linux, deux manuels).
