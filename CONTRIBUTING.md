# Contributing to cameraunlock-core

Thanks for wanting to help. Bug reports and fixes are welcome.

Before a pull request can be merged, you need to agree to the contributor agreement below.
You agree by adding this line to the description of your first pull request:

    I agree to the cameraunlock-core Contributor Agreement, version 1.0.

Contributions are taken by pull request only. You agree once, and that covers every
contribution you make afterwards. Each contribution is under the version of the agreement
you last agreed to. When a new version is published, pull requests are merged only from
people who have agreed to it, by adding the same line with the new version number.

## Why there is an agreement

cameraunlock-core's source is public, but all rights in it are reserved by itsloopyo (see
LICENSE). itsloopyo builds it into Headstart and itsloopyo's head tracking mods, may later
release it, in whole or in part, under an open source licence such as MIT, and may use parts of
it in other projects. The agreement gives itsloopyo the right to do all of that with your
contribution too. You keep the copyright in your work.

## cameraunlock-core Contributor Agreement, version 1.0

"You" means the person agreeing to this, or the organisation they agree on behalf of.
"itsloopyo" means the person who holds the itsloopyo account on GitHub and publishes
cameraunlock-core, and anyone they pass the project on to. A "Contribution" is anything
you submit by pull request to the cameraunlock-core repository that is meant to be part of
cameraunlock-core: code, documentation, tests, data or anything else.

1. **You keep your copyright.** Nothing here transfers ownership of your Contribution to
   itsloopyo. You may use your own work in any way you like.

2. **Copyright licence.** You give itsloopyo a permanent, worldwide, free of charge,
   non-exclusive and irrevocable licence to use, copy, change, publish, distribute and
   sublicense your Contribution and works based on it, under any licence itsloopyo
   chooses, whether open source (such as MIT) or not. Anyone who receives your
   Contribution from itsloopyo receives it under the licence itsloopyo distributes it
   under. As far as the law allows, you waive, or agree not to assert, any moral rights
   in your Contribution against itsloopyo or anyone itsloopyo licenses it to.

3. **Patent licence.** If you hold a patent that your Contribution uses, you give
   itsloopyo, and anyone who receives software containing your Contribution, a permanent,
   worldwide, free of charge, non-exclusive and irrevocable licence to make, use, sell,
   offer for sale, import and otherwise transfer that software, as far as that patent
   would otherwise stop them. If you start a patent lawsuit claiming that cameraunlock-core or
   your Contribution infringes a patent, the patent licences anyone gave you under this
   agreement end on the day you file it.

4. **What you confirm.** By agreeing, you confirm that:

   a. each Contribution is your own work, or you have the right to submit it under this
      agreement;

   b. if your employer or anyone else has rights in work you create, they have allowed
      you to contribute it under this agreement, or have waived those rights;

   c. where a Contribution includes or is based on someone else's work, you say so in the
      pull request, name the work and its licence, and that licence lets it be used as
      this agreement describes. Code under the GPL, LGPL or any other copyleft licence
      may not be submitted, because itsloopyo could not use it as section 2 describes;

   d. where a Contribution was written with the help of an AI tool, you say so in the
      pull request, and you have reviewed and stand behind what you submit;

   e. a Contribution contains no code, assets or data copied from a game. Facts found by
      studying a game, such as where a value is kept or what a class is called, are fine.

5. **Telling us about changes.** If you learn that anything you confirmed in section 4
   is no longer true, you will tell itsloopyo.

6. **No warranty.** Your Contribution is provided "as is". You do not have to support
   it, and you give no warranty for it, except what you confirmed in section 4.

7. **No obligation to use it.** itsloopyo does not have to accept, merge or ship any
   Contribution.

## Before you open a pull request

- A new source file starts with these two lines, in the file's comment style:
  `SPDX-License-Identifier: LicenseRef-CameraUnlock-Core` and `Copyright (c) 2026 itsloopyo`.
  If you want your own name on a file you wrote or changed, add
  `Copyright (c) <year> <your name>` under itsloopyo's line.
- Keep each pull request to one change.
- Run `pixi run test` and check it passes.
