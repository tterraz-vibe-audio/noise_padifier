# Noise Padifier - améliorations

Statut : ouvert (session 69, 2026-11-26). Version publiée : 1.0.1. Tout ajout qui change la sortie
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
