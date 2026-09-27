---
title: Retirer
order: 12
summary: Archiver un titre, un exemplaire, un adhérent ou un prêt, puis le retirer pour de bon.
---

::: note
`Supprimer` ne détruit jamais une fiche. Il l'archive. La liste en service la perd, et les [Archives](archive) peuvent la restaurer. Le retrait définitif est une étape séparée, seulement dans les Archives.
:::

## Un ouvrage

Surlignez le titre. Laissez les cases vides si vous visez ce titre seul. Cliquez `Supprimer`.

![Supprimer demande avant d'archiver un titre.](shot:rm-book-confirm)

`Non` laisse le titre dans le Catalogue. `Oui` archive le titre et ses exemplaires. Leurs numéros locaux restent avec eux, donc ils peuvent être réutilisés. Voir [Réutiliser un numéro local](task-reuse-number).

Si un exemplaire est encore sorti, Supprimer est refusé. Retournez ce prêt d'abord.

![Supprimer refusé, parce qu'un exemplaire est en prêt.](shot:rm-book-refused)

## Un exemplaire

`Modifier` le titre, ouvrez l'onglet Exemplaires, surlignez la ligne, et cliquez `Supprimer l'exemplaire`. Un exemplaire encore sorti ne peut pas être retiré. `OK` enregistre l'ouvrage sans cet exemplaire. L'exemplaire est archivé, pas détruit.

![L'onglet Exemplaires, avec Supprimer l'exemplaire.](shot:rm-copy-row)

1. `Supprimer l'exemplaire`

## Un adhérent

`Supprimer` dans les Adhérents archive l'adhérent, sauf si un prêt de cette personne est encore sorti. Alors le message propose `Aller aux prêts`.

![Supprimer refusé tant qu'un ouvrage est encore sorti.](shot:mem-delete-blocked)

## Un prêt

Seul un prêt retourné peut être archivé depuis la Circulation. Surlignez-le et cliquez `Supprimer`.

![Supprimer demande avant d'archiver un prêt retourné.](shot:rm-loan-confirm)

Un prêt ouvert ou en retard doit d'abord être retourné. Voir [Retourner ou prolonger](task-return-extend).

## Plusieurs à la fois

Cochez les lignes, y compris avec la case de l'en-tête. `Supprimer` nomme alors combien, et agit sur chaque ligne cochée. Une ligne qui ne peut pas être archivée est passée. Voir [Cases](getting-started#ticks).

![La case de ligne et la case d'en-tête.](shot:gs-ticks)

![Supprimer avec trois titres cochés.](shot:rm-bulk-confirm)

## Pour de bon

Quand la fiche archivée ne doit pas revenir, ouvrez les Archives et suivez l'[ordre](archive#purge-order). Un prêt peut partir à tout moment. Un exemplaire attend qu'aucun prêt ne le nomme. Un livre attend de n'avoir plus d'exemplaires. Un adhérent attend qu'aucun prêt ne le nomme.

![Suppression définitive, pour un prêt. Cela ne peut pas être annulé.](shot:arc-purge-confirm)
