---
title: Circulation
order: 5
summary: Ce qui est sorti, ce qui est en retard, et ce qui est revenu.
---

La Circulation est la liste des prêts encore en service. Un prêt ouvert ou en retard veut dire que l'exemplaire n'est pas sur le rayon. Un prêt retourné veut dire qu'il l'est.

![La Circulation, avec les filtres, la liste, les images et les boutons numérotés.](shot:circ-overview)

1. Le filtre d'état.
2. Les filtres d'adhérent.
3. La recherche.
4. La liste.
5. La couverture de l'ouvrage prêté, ou l'image d'attente quand il n'en a pas.
6. La photo de l'adhérent, ou `Pas de photo`.
7. `Retourner`
8. `Prêter`
9. `Prolonger`
10. `Supprimer`

Le détail du prêt surligné est sous les deux images : l'adhérent, l'exemplaire et les dates.

## Le filtre

Le filtre est un seul choix. Il s'ouvre sur `Tous`.

- `Ouverts` — sortis, et dus aujourd'hui ou plus tard.
- `En retard` — sortis, et dont le jour dû est passé.
- `Retournés` — de retour sur le rayon.

![La liste réduite aux prêts en retard.](shot:circ-overdue)

`Ouverts` n'inclut pas `En retard`. Voir [États d'un prêt](reference#loan-states).

## Les filtres d'adhérent

Sous le filtre d'état se trouvent les listes des [Adhérents](members) : statut, sexe, tranche d'âge et ville gardent les prêts dont l'adhérent correspond. La liste des années, qui commence par `Toutes les années de prêt`, est l'année du prêt, pas l'année d'inscription de l'adhérent. Elles se combinent avec le filtre d'état et entre elles : `En retard` et `Jeunes` donnent les prêts en retard des jeunes adhérents.

## Recherche

La recherche suit le titre, le numéro d'adhésion et le nom complet. Un nom et un prénom ensemble trouvent l'adhérent.

![Une recherche par le nom de l'adhérent.](shot:circ-search-name)

## Les boutons

`Prêter` sort un exemplaire. `Prolonger` et `Retourner` suivent le prêt surligné et ne sont disponibles que tant que ce prêt est encore sorti. `Supprimer` archive un prêt retourné. Les étapes sont [Prêter un ouvrage](task-lend-book), [Retourner ou prolonger](task-return-extend) et [Retirer](task-remove-book).
