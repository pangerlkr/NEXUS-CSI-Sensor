# Code of Conduct

## The short version

Be kind. Assume good faith. Remember that the person on the other end of the
thread is a person, usually one doing this in their spare time.

That is genuinely most of it. The rest of this document exists so that nobody
has to guess where the line is, and so that if something does go wrong there is
a written process instead of an argument about what the rules were.

---

## Who this applies to

Everyone taking part in the project, in every space the project occupies: issues,
pull requests, code review comments, discussions, commit messages, and any chat
or event that presents itself as part of this project. It applies to maintainers
exactly as much as it applies to first-time contributors.

It also applies when you are representing the project somewhere else. If you are
speaking as a NEXUS CSI Sensor maintainer or contributor in a public space, this
is the standard you are held to.

---

## What we want more of

- **Technical directness.** Say the code is wrong if the code is wrong. Point at
  the line, explain what breaks, suggest a fix if you have one. Critique of the
  work is the entire point of review.
- **Patience with beginners.** Everybody was new to CSI, or to FreeRTOS, or to C,
  at some point. A question that is obvious to you is not obvious to everyone.
  "Here is where that is documented" beats "read the docs".
- **Explaining the why.** In review, in commit messages, in issue replies. A
  reason travels; an instruction does not.
- **Admitting uncertainty.** "I think this is a race but I am not sure" is a
  useful review comment. Confident wrongness is much more expensive than an
  honest question.
- **Assuming a misunderstanding before assuming bad intent.** Most conflicts in
  open source are two people who read the same sentence differently.

---

## What is not acceptable

- Harassment of any kind, including sustained unwelcome attention after being
  asked to stop.
- Demeaning, insulting, or derogatory comments, and personal or political attacks.
- Discriminatory language or behaviour relating to age, body size, disability,
  ethnicity, gender identity or expression, level of experience, nationality,
  personal appearance, race, religion, or sexual identity or orientation.
- Sexualised language or imagery, and unwelcome sexual attention.
- Publishing someone's private information without their explicit permission.
- Deliberately derailing discussions, or making the same rejected argument over
  and over after a decision has been made and explained.
- Threats, intimidation, or stalking, in project spaces or out of them.

There is a difference between "your code has a bug and here is why" and "you are
a bad engineer". The first is review. The second is a personal attack. If you
cannot tell which one you are writing, you are writing the second one.

---

## A note specific to this project

This is a sensing device. Some issues and pull requests will touch on
surveillance, privacy, and the ethics of detecting people without their
knowledge. Those are legitimate, important discussions and we want to have them
openly. See [docs/USE_CASES.md](docs/USE_CASES.md#using-this-responsibly) for
where the project stands.

What is not welcome is using this project as a venue to help someone monitor
people who have not consented to being monitored. If a request is clearly about
covert surveillance of others, maintainers will close it. That is a
project-scope decision, not a moral judgement of the person asking, and it is
not up for extended debate in the issue tracker.

Security research is welcome and wanted. Please report vulnerabilities through
the process in [SECURITY.md](SECURITY.md) rather than the public issue tracker,
so that users get a fix before an exploit gets an audience.

---

## Reporting a problem

If you experience or witness behaviour that breaks this code of conduct, report
it privately to the maintainers. Open a GitHub issue titled
`Conduct report` with no details in it and a maintainer will reach out for a
private channel, or contact a maintainer directly through the address listed in
[SECURITY.md](SECURITY.md).

When you report, it helps to include:

- What happened, and where (a link if it is a public thread).
- Roughly when.
- Whether it is ongoing.
- Anything you have already tried, if you tried anything. You are not obliged to
  have tried anything.

You do not need to have a complete case, and you do not need to be certain. A
report that turns out to be a misunderstanding costs everyone one conversation.
An unreported pattern costs the project contributors.

**What you can expect:**

- An acknowledgement within a few days.
- Your report treated as confidential. We will not share your identity with the
  person reported without asking you first.
- No retaliation. Reporting in good faith will never count against you, whatever
  the outcome.
- A note from us when the matter is resolved, including what we did, to the
  extent we can share it.

If your report concerns a maintainer, send it to a different maintainer. That
maintainer will be recused from handling it.

---

## What happens next

Maintainers will look at what happened, in context, and respond proportionately.
Roughly, in escalating order:

1. **A quiet word.** A private note explaining what was a problem and why. Most
   things stop here, because most things are a bad day rather than a pattern.
2. **A public correction.** A comment in the thread naming the behaviour and
   redirecting the conversation, so that anyone reading knows what the standard
   is.
3. **A warning.** A formal note that continuing means a ban, with specifics about
   which behaviour has to change.
4. **A temporary ban.** No interaction with the project for a defined period.
   Interaction includes new issues, comments, and pull requests.
5. **A permanent ban.** Reserved for sustained behaviour after a warning,
   harassment of an individual, or anything that makes the project unsafe for
   someone.

Serious cases can start anywhere on that list. Nobody is owed four warnings
before a ban if the first act was severe.

Enforcement decisions are made by the maintainers and are final. If you think a
decision about you was wrong, you can say so once, in writing, with your
reasoning; a maintainer who was not involved will read it. Repeating the appeal
after that is itself a conduct problem.

---

## For maintainers

You are held to this document more strictly than anyone else, not less. A
maintainer being sharp with a contributor sets the tone for the whole project,
and a contributor cannot push back on you from an equal footing.

Practical version:

- Review the code, never the person.
- If you are annoyed, wait before hitting comment. The issue will still be there
  in an hour.
- Explain closes. "Out of scope" with no reason reads as a dismissal even when it
  is not one.
- If you are the subject of a report, step back from handling it and let another
  maintainer take it.

---

## Attribution

This code of conduct is adapted from the
[Contributor Covenant](https://www.contributor-covenant.org), version 2.1, with
the project-specific sections on sensing ethics, security reporting, and
maintainer expectations written for this repository.

Contributor Covenant is available under the
[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) license.
