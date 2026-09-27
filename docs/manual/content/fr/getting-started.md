---
title: Prise en main
order: 2
summary: La fenêtre, les listes, le tri et les cases.
---

Ouvrez VLMS. La fenêtre s'ouvre sur le Catalogue, dans la langue et le thème clair ou sombre que vous aviez laissés.

![Le Catalogue, avec les parties de la fenêtre numérotées.](shot:gs-window)

1. `Catalogue` — l'écran affiché. `Adhérents`, `Circulation`, `Archives` et `Indicateurs` sont à côté.
2. Les trois drapeaux. Le drapeau rempli est la langue de l'écran. Un autre drapeau change toute l'application d'un coup.
3. Le bouton de thème, et le ? à côté. Le bouton de thème allume ou éteint le thème sombre. Le ? ouvre ce manuel.
4. Les filtres de cet écran.
5. La recherche.
6. La liste.
7. La pagination.
8. Le détail de la ligne surlignée.
9. Les boutons de cet écran.
10. Le pied de page. Le nom de la bibliothèque est un lien.

![La même fenêtre en thème sombre.](shot:gs-dark)

## Le pied de page et la licence

La ligne du pied de page ouvre la licence. Elle dit que l'application est un don à cette bibliothèque, que la bibliothèque possède l'application telle qu'elle est installée ici, et que le catalogue, les fiches d'adhérents et les photographies appartiennent à la bibliothèque. Il n'y a pas de garantie.

![La licence.](shot:gs-licence)

Fermez-la avec `Fermer`.

## La forme de chaque liste {#list-shape}

Le Catalogue, les Adhérents, la Circulation et les Archives ont la même forme.

- La colonne de filtres est sur le côté. Un choix y réduit la liste. `Tous` veut dire que ce filtre ne réduit rien.
- La recherche suit la frappe. Vider le champ ramène la liste entière.
- Le tableau est la liste. Un clic sur une ligne la surligne. Le panneau de détail montre alors cette ligne.
- La pagination est sous le tableau. Elle s'ouvre sur `Tous`, donc toute la liste filtrée est chargée. On peut choisir 20, 50 ou 100 à la place. `Premier` et `Dernier` vont aux extrémités, et la case de page va à une page. Quand la liste filtrée a moins de 20 lignes, la pagination se cache.
- Le panneau de détail est la ligne surlignée, avec son image quand elle en a une.
- Les boutons sont le travail de cet écran : ajouter, modifier, prêter, supprimer, et le reste. Un bouton suit la ligne surlignée. Il reste gris quand cette ligne ne peut pas recevoir l'action.

## Tri {#sort}

Cliquez un en-tête de colonne pour trier. Cliquez-le encore pour inverser. L'en-tête montre le sens de la liste.

![L'en-tête Titre après un clic, avec la liste inversée.](shot:gs-sort)

La case au début du premier en-tête ne fait pas partie du tri. Cliquez le milieu de l'en-tête, pas la case. Voir [Cases](#ticks).

## Cases {#ticks}

Une case dans la première cellule d'une ligne marque cette ligne pour un bouton. La case de l'en-tête marque toutes les lignes de la page, ou les enlève.

![Trois lignes cochées, et la case de l'en-tête.](shot:gs-ticks)

1. La case de la ligne surlignée.
2. La case de l'en-tête.

`Supprimer`, `Restaurer` et `Suppression définitive` agissent sur chaque ligne cochée. Sans aucune case, ils agissent sur la ligne surlignée seule. Ces boutons restent disponibles tant qu'une ligne est cochée, même si la ligne surlignée elle-même ne pourrait pas recevoir l'action. Chaque ligne cochée est encore vérifiée pour elle-même : une ligne qui ne peut pas être retirée est passée, et les autres suivent.

Une case n'est qu'une marque. Elle est oubliée quand la liste est rechargée.
