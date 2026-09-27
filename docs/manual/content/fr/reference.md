---
title: Référence
order: 14
summary: Les champs, le statut, les états d'un prêt, et le numéro local.
---

## Champs d'un ouvrage {#fields-book}

L'onglet Ouvrage :

- `Titre` — obligatoire.
- `Auteur`
- `Éditeur`
- `Catégorie` — une au choix, ou `(aucune)`. `Catégories…` ouvre la liste des catégories.
- `ISBN`
- `Date de publication`
- `Lieu de publication`
- `Pages`
- `Langue` — obligatoire. Arabe, Français, Anglais, et les autres langues listées, ou une autre langue saisie.
- `Dimensions`
- `Description` — `Depuis une image` peut remplir ceci, ainsi que le titre et l'auteur, depuis une photographie nette de l'ouvrage. Cela peut être indisponible.
- `Couverture` — `Choisir une couverture…`

L'onglet Exemplaires, une ligne par exemplaire :

- `N° local` — le numéro propre à la bibliothèque pour cet exemplaire. Voir [Numéros locaux](#local-number).
- `N° global` — rempli à partir du numéro local quand l'exemplaire est enregistré.
- `Fonds` — Arabe ou Étranger, selon la langue de l'ouvrage.
- `N° central` — le numéro national, quand l'exemplaire en a un. La bibliothèque ne le crée pas.
- `Cote`
- `Matière`
- `Index`
- `Emplacement`
- `Inventaire`
- `Compensation`
- `Remarques`
- `État`

`Ajouter un exemplaire` ajoute une ligne. `Supprimer l'exemplaire` archive la ligne surlignée. Un exemplaire encore sorti ne peut pas être retiré.

## Champs d'un adhérent {#fields-member}

- `Numéro d'adhésion` — donné par l'application, et non modifié.
- `Statut` — `Actif` ou `Inactif`. Voir [Statut d'un adhérent](#member-status).
- `Actif jusqu'au` — le dernier jour où l'adhérent est actif. L'application le pose. Vous le lisez.
- `Prénom` — obligatoire.
- `Nom` — obligatoire.
- `Date de naissance` — obligatoire, choisie en jour, mois et année. La tranche d'âge en découle : moins de 30 ans est `Jeunes`, et le 30e anniversaire est `Adultes`. Une date future est refusée.
- `Sexe`
- `Profession`
- `E-mail`
- `Téléphone`
- `Adresse`
- `Ville`
- `Photo` — `Choisir une photo…`
- `Pièce d'identité` — `Choisir une pièce d'identité…`
- `Notes`

## Statut d'un adhérent {#member-status}

Un adhérent est `Actif` un jour donné quand `Actif jusqu'au` est ce jour ou un jour plus tard. Sinon l'adhérent est `Inactif`.

![Le détail, où le statut et le dernier jour actif se lisent.](shot:mem-details)

Inscrire un adhérent, ou remettre le statut sur `Actif`, pose le dernier jour actif à un an moins un jour. Choisir `Inactif` le termine hier. Seul un adhérent `Actif` peut emprunter. Le prêt de la Circulation ne propose personne d'autre.

Rien ne change le statut tout seul quand le dernier jour actif passe. La prochaine fois que l'adhérent est ouvert, le statut indique déjà `Inactif`.

## États d'un prêt {#loan-states}

Un prêt est dans un seul de ces trois états.

- `Ouverts` — pas retourné, et dû aujourd'hui ou plus tard.
- `En retard` — pas retourné, et le jour dû est passé.
- `Retournés` — l'exemplaire est revenu.

`Ouverts` n'inclut pas `En retard`. Le filtre de la Circulation et la tuile des Indicateurs du même nom suivent ce partage.

![La Circulation réduite aux prêts en retard.](shot:circ-overdue)

Un prêt dure 14 jours, à compter du jour où l'exemplaire sort. Le jour dû peut être changé avant d'enregistrer le prêt, et encore avec `Prolonger` tant que l'exemplaire est sorti.

## En service, archivé, retiré {#live-archived}

Une fiche en service est dans le Catalogue, les Adhérents ou la Circulation. `Supprimer` l'archive : elle quitte cette liste et va aux Archives. Elle peut revenir avec `Restaurer`.

`Suppression définitive` n'existe que dans les Archives, et seulement une fois que l'[ordre](archive#purge-order) le permet. Après cela la fiche a disparu et ne peut pas être restaurée.

Un prêt archivé compte encore. Il empêche encore un adhérent ou un exemplaire d'être retiré pour de bon, et il apparaît encore dans l'historique `Prêts` d'un ouvrage ou d'un adhérent.

## Numéros locaux {#local-number}

Le numéro local est le numéro d'entrée de la bibliothèque pour un exemplaire. Il est unique parmi les exemplaires encore en service d'un même fonds. Le numéro central est le numéro national de l'exemplaire. La bibliothèque ne le crée pas, et il n'est pas le numéro local.

Quand un titre a plusieurs exemplaires, la cellule mène avec un numéro et compte les autres entre parenthèses. Seul le numéro de tête est coloré.

![Un numéro vert sur le rayon, et un numéro rouge en italique qui est sorti.](shot:cat-copy-colours)

Le vert est sur le rayon. Le rouge et l'italique sont sortis. L'italique est le repère qui reste en impression grise, et pour un lecteur qui ne distingue pas le rouge du vert.

Le prochain numéro est proposé pour un exemplaire neuf. Les numéros libres, laissés par des trous plus anciens, sont dans la même liste et ne sont pris que si vous les choisissez. Un numéro qui reste sur un exemplaire archivé se reprend depuis les Archives. Voir [Réutiliser un numéro local](task-reuse-number).
