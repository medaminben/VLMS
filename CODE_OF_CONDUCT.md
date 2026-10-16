# Code of conduct

VLMS is a small project that runs the desk of a real library. On that library's own computer, it stores members' names, addresses, photographs and identity-card images. None of those records are in this repository: the catalogue, members and photographs here are generated sample data, and no real person appears in them. The people who work on VLMS write in Arabic, French and English, and some of them work through AI coding tools. This code says how we treat each other, and how we protect the real people whose records the program handles.

## How we work together

- **Be kind and assume good faith.** Most mistakes come from missing context, not bad intent. Ask before you conclude.
- **Review the change, not the person.** Say what is wrong and why. Separate what blocks a merge from what is only a suggestion. When you disagree with a review, explain your reasoning; the maintainer makes the final call.
- **Write so that everyone can follow.** English is the working language for issues, pull requests and commits. If English is hard for you, write in Arabic or French and someone will help. Never mock anyone's English, or their Arabic or French. Correct a translation kindly and explain the change.
- **Treat all three languages as equal.** An Arabic or right-to-left bug is as serious as an English one.
- **Respect people's time.** Keep a pull request to one change, answer review comments, and say so when you have to stop working on something.

## Protect the people in the data

This matters more here than in most projects, because a leak cannot be taken back.

- Never put real member data in an issue, a pull request, a comment, a screenshot, a log, a test or a commit. That includes names, phone numbers, addresses, dates of birth, photographs and identity-card images. Use the generated sample catalogue that ships with the repository, and blur anything you are not sure about.
- Do not name the library where VLMS runs, its town, its staff or its members. Use neutral stand-ins such as "Tunis".
- If you see real data posted anywhere in the project, do not quote it or share it further. Report it privately (see below), and the maintainer will remove it.
- Report a security problem privately, as described in [SECURITY.md](SECURITY.md), not in a public issue.

## Contributions made with AI tools

AI tools are welcome here. You remain the author of whatever you submit.

- Read and understand every line before you open a pull request, run the tests yourself, and be ready to answer review questions in your own words.
- Do not let an agent post comments, reviews or pull requests on your behalf without reading them first.
- An agent follows the same rules as a person: no pushes to `main` or `Beta`, no tags, no real data, and the commit style in [CONTRIBUTING.md](CONTRIBUTING.md).
- Do not flood the project with large generated changes that nobody asked for. Open an issue first and agree on the change.

## Not acceptable

- Insults, slurs, or degrading remarks about anyone's origin, religion, language, gender, age, disability or appearance.
- Harassment, threats, or unwelcome sexual attention, in public or in private messages.
- Publishing anyone's private information, whether a contributor's or a library member's.
- Political or religious arguments that have nothing to do with the project.
- Trolling, deliberate disruption, or repeatedly ignoring a maintainer's request to stop.

## Where this applies

This code applies everywhere in the project: issues, pull requests, reviews, discussions, commit messages and the code itself. It also applies when you represent VLMS elsewhere, for example when you talk about it in public or contact the library on its behalf.

## Reporting

Write privately to the maintainer, [@medaminben](https://github.com/medaminben), at the email address on their GitHub profile. Include links or screenshots, and say whether you want to stay anonymous. Reports are kept confidential, and nobody is punished for reporting in good faith.

If your report is about the maintainer, use GitHub's **Report content** option on the comment or the profile instead.

## What happens next

The maintainer aims to reply within a week. They hear the person reported before deciding, and the response matches the harm:

1. a private note explaining what was wrong,
2. a public warning, or edited or removed content,
3. a temporary ban from the repository,
4. a permanent ban.

Real data that has been posted is removed straight away, before anything else is decided. If it reached the git history, the maintainer can rewrite that history and ask GitHub to purge cached copies.

## Credits

This code draws on the [Contributor Covenant](https://www.contributor-covenant.org/), version 2.1, adapted to software that handles other people's personal records.
