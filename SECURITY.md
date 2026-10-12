# Security

VLMS stores a library’s catalogue and member records on the computer where it runs. Please report a vulnerability privately rather than in a public issue.

Use [GitHub private vulnerability reporting](https://github.com/medaminben/VLMS/security/advisories/new) for this repository.

Include what you ran, what you expected, and what happened. Please allow time to confirm the report before discussing it in public.

## What VLMS protects, and what it does not

VLMS is a desktop program for one library desk. It has no server, no network connection and no user accounts of its own. Whoever can sign in to the Windows account it is installed under can use it, and can read its files.

### Where the data is

Everything sits in the installation folder, `%LOCALAPPDATA%\Programs\VLMS` by default:

- `database\vlms.db` holds the catalogue, the members (names, addresses, phone numbers, dates of birth) and the loans.
- `resources\members\<id>\` holds each member’s photograph and identity-card image.
- `resources\books\<id>\` holds the cover images.

None of it is encrypted.

### What the program does

- **Input never becomes SQL.** Every value typed into the program is bound as a query parameter, and sort orders come from a fixed list. Searching for `'; DROP TABLE members; --` searches for that text.
- **It only deletes its own files.** Removing a member’s photograph deletes only a file the program itself copied into that member’s folder. A path stored in the database cannot make it delete anything else.
- **It keeps a copy before changing the database's structure.** Before an update changes the database’s structure, the program writes `vlms.db.bak-v<version>-<time>` beside it. If the update stops halfway, that copy is the database exactly as it was.
- **The installer offers a second copy.** On an update it copies `vlms.db` to `Documents\VLMS Backups\` unless you untick the box.

### What it does not do

- **No login.** VLMS does not ask who is using it, and it does not record which librarian made a change.
- **No encryption.** Anyone who can open the installation folder can copy the database and the identity-card images.
- **No image backups.** Both copies above contain the database only. Photographs, identity-card images and covers are not copied. Choosing *Delete it, including its database and cover images* during an update removes them for good.

### What to do about it

- Give the library desk its own Windows account with a password, and do not share it.
- Turn on BitLocker or another full-disk encryption, so a lost or stolen computer does not expose the members.
- Back up the whole installation folder regularly, not only `vlms.db`, to a drive kept away from the computer.

### What to report

Report anything that breaks one of the promises under *What the program does*. For example: text that runs as SQL, a file deleted outside a member’s or a book’s own folder, or a database that cannot be restored from the copy made before an update. Also report a file chosen in the program that makes it crash or read something it should not.

The points under *What it does not do* are known limits, not vulnerabilities.
