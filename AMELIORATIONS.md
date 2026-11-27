# Noise Padifier - améliorations

Statut : **1.2.0 publiée** (release `v1.2.0`, 2026-11-26, § « 1.2.0 » en fin de fichier ; 1.1.0 :
release `v1.1.0`). Restent les pistes non faites de `LISEZMOI.md`. Tout ajout qui change la
sortie demande une nouvelle version.

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

## 1.2.0 (session 70, 2026-11-26 ; publiée, commit `4e938ba`, release `v1.2.0`)
- Porte rythmique : montée sin² Attack, palier jusqu'à Duty, descente cos² Release (en % du pas,
  ≥ 1 ms ; une descente peut déborder sur le pas suivant) ; Attack, Duty, Release en faders ;
  Chord et Rhythm en listes déroulantes (commit `ca5ef86`).
- **Decay** : part de la longueur prise par la descente finale jusqu'à -60 dB (0-100 %, 75 % par
  défaut), tenue à plein niveau entre le pic et la descente ; Damping : perte supplémentaire
  croissant du pic à la fin. Préréglages d'usine convertis. Une valeur de 1.1 (dB) est relue 0 %.
- **Length** jusqu'à 30 s ; **convolution à trois étages** (tête 256, étage 1 de M1, étage 2 de
  4 M1) pour garder les réponses longues peu coûteuses (30 s, 48 kHz : 2,2 %, 3,9 % avec Cross).
- **Thème Pearl opalescent**, accordé aux couleurs de l'analyseur (décision de l'utilisateur :
  l'ancien Pearl, gris froid et ocre, jurait avec la carte) ; Dusk inchangé.
- Vérifié : tests des réponses et du DSP (deux compilations), `./build.sh` complet sans
  avertissement, validateur 47/47 Linux et Windows, manuels FR/EN de 18 pages sans avertissement.

## Après la 1.3.0 (non publié)
- **Bouton RANDOM** (session 75, 2026-11-27 ; demande de l'utilisateur : « le même » que dans
  Brimstone) : dans l'en-tête, entre la pastille de thème et Bypass (préréglages, A/B, copie et
  thème décalés de 64 points à gauche). Tire au hasard tous les réglages d'un préréglage (Bypass
  gardé ; rien de l'en-tête) : valeurs continues uniformes sur la **course du bouton** (échelle
  logarithmique pour Length, Density, filtres et vitesses, comme à la souris) ; Tune, Chord, Seed,
  Note et phases : une valeur entière au hasard. Préréglage affiché « Random ». Interface seule.
  Prochaine release : nouveau numéro.

## 1.3.0 (session 74, 2026-11-26 ; publiée, release `v1.3.0`)
- **Porte rythmique retirée** (décision de l'utilisateur : pas utile en pratique ; aucun projet
  n'emploie les releases, pas de compatibilité) : paramètres Rhythm, Duty, Attack, Release et
  sortie Tempo supprimés (numéros des suivants décalés), `DISTRHO_PLUGIN_WANT_TIMEPOS` retiré,
  étape « porte » de `NoiseIR.hpp`, faders de l'interface (code des faders retiré), liste Rhythm,
  préréglage Pulse gate (12 préréglages d'usine). Clés `rhythm`, `duty`, `attack`, `release` d'un
  préréglage de l'utilisateur ignorées à la lecture. Section MOTION refaite en deux rangées (LFO ;
  Pan rate, Pan depth, Cross) et périodes du LFO et du panoramique en bas. Réponses inchangées
  pour tout réglage sans porte (la porte valait 1).
- Vérifié : `./build.sh` complet sans avertissement (tests des réponses et du DSP, deux
  compilations, préférences, préréglages), validateur VST3 47/47 Linux et Windows, captures
  refaites (`doc/captures/`), `fig_traitements` (le panneau de la porte remplacé par LFO tone),
  manuels FR/EN de 18 pages sans avertissement LaTeX.
- **LFO sur d'autres cibles, phase par cible** (demande de l'utilisateur ; choix : un seul LFO pour
  le timbre, le panoramique gardant le sien ; phases en crans de 90° ; filtre balayé autour de
  High cut, puis aussi de Low cut) : profondeurs Reson (± points de Resonance, 0-100), Density,
  Low cut, High cut (± octaves) ; une phase par cible (Vibrato, Tone, Reson, Density, Low cut,
  High cut : paramètres `kParamPhase*`, 0-3). Density : cellules du bruit épars de durée
  fs / density(t) ; Reson : temps de résonance des peignes par blocs de 16 échantillons, amorce
  sur le plus long ; filtres : module de Butterworth d'ordre 2 dans la TFCT, fréquences de coupure
  une fois par trame, filtre fixe du greffon mis au neutre tant que le LFO le balaie (il passe au
  neutre en 20 ms dès le réglage, la nouvelle réponse arrive après sa construction : court écart
  de timbre au changement). Interface : MOTION en trois rangées (LFO RATE, TONE, RESON ;
  DENSITY, LOW CUT, HIGH CUT ; PAN RATE, PAN DEPTH, CROSS), case de phase sous chaque profondeur
  (clic, clic droit, molette), case de Vibrato dans TUNE (séparateur de TUNE descendu à 394) ;
  périodes dans les bulles de LFO RATE et PAN RATE. Préréglage Filter sweep (13 préréglages).
- Vérifié : réponses identiques au bit près sans les nouvelles cibles (7 réglages, `tools/ir.cpp`
  avant/après) ; nouveaux tests de `test_ir` (phases, Reson, Density, filtres à 0,6 dB de la loi)
  et de `test_dsp` (LFO sur les filtres : sortie = convolution, filtres fixes coupés) ;
  `./build.sh` complet sans avertissement ; validateur VST3 47/47 Linux et Windows ; coût d'une
  reconstruction inchangé sans les nouvelles cibles, +9 % avec toutes (192 kHz, 30 s : 1,8 → 1,9 s) ;
  captures refaites (Pearl : Filter sweep) ; manuels FR/EN de 18 pages sans avertissement.
