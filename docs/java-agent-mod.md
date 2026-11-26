# A Java agent mod

For a game that runs on a JVM its own launcher starts: Project Zomboid is the first. The mod is a
jar the JVM loads as a Java agent, with core inside it ([core-jar.md](core-jar.md)). There is no
mod loader and no native file.
What core gives such a mod is in five places, and this page is how they fit.

| Piece | Where | What it is |
|---|---|---|
| The binding | `java/src/com/cameraunlock/core/CameraUnlock.java` | cameraunlock-core for Java: the C interface ([c-interface.md](c-interface.md)), from the jar's own classes (`loadBundled`) |
| The boot class | `java/src/com/cameraunlock/core/agent/Boot.java` | The main class the installed game starts in place of its own. It loads the mod's jar as an agent, then runs the game |
| The bytecode patcher | `java/src/com/cameraunlock/core/bytecode/Patch.java` | Writes a call into the mod's bridge class into a game method, after a named call, before each return or at its start, from the agent's class file transformer |
| The site config | `scripts/jvm-site-config.ps1` | Writes the JVM arguments that name the boot class, the jar and the game's own main class |
| Install and deploy | `scripts/install-body-javaagent.cmd`, `scripts/uninstall-body.cmd`, `Invoke-DevDeployJavaAgent` | Put the jar beside the game's exe and call the site config script |

A mod builds core's jar from the submodule, `node cameraunlock-core/scripts/build-core-jar.mjs
build/cameraunlock-core.jar`, compiles against it and puts its whole content into its own jar.
There is no published artifact. `java/` needs Java 25 or later, the first long-term release where
`java.lang.foreign` and `java.lang.classfile` are both final, and `pixi run build-java` compiles
it for 25 to hold that.

## What the mod writes

Its agent class, the camera hook, the engine boundary conversion of the pose and the world query
for the lean. It writes no receiver, no interpolation, no smoothing, no lean clamp, no aim
transition, no config reader and no C++: those are core's, behind `CameraUnlock`.

## The boot contract

The game's launcher reads its JVM arguments from `<Exe>.json` beside the exe, and from
`<Exe>.site.json` instead when that is there. `jvm-site-config.ps1` writes the site file as the
stock one with three changes:

- `mainClass` is `com/cameraunlock/core/agent/Boot`.
- Each agent jar is added to `classpath`.
- `vmArgs` starts with `-Dcameraunlock.mainClass=<the game's own mainClass>`,
  `-XX:+EnableDynamicAgentLoading` and `--enable-native-access=ALL-UNNAMED`.

`Boot.main` loads `jli.dll` from the runtime, asks the JVM to load the jar it is in as an agent,
and then calls the `main` of the class `cameraunlock.mainClass` names with the arguments it was
given. `Boot.java` says why the agent is not named with `-javaagent`.

The mod's half of the contract:

- The jar's manifest names an `Agent-Class`.
- That class's `agentmain` calls `Boot.agentStarted()` before anything that can throw. The JVM's
  answer to the load says nothing of whether the agent class ran, so that call is how the boot
  class knows. Without it the boot class stops with `the JVM did not start <jar> as a Java agent`
  and the game does not run.
- An agent that calls it and then fails has still started. The game runs without the mod, so the
  agent writes its reason to its log before it throws.

`java/tests/com/cameraunlock/core/Tests.java` runs the boot class with a fixture agent that starts,
one that starts and then fails, and one that never says it started.

## The bytecode patcher

`com.cameraunlock.core.bytecode.Patch` writes game classes with `java.lang.classfile`. A patch is
a kind, a site number, the game method it goes into and, for `AFTER_CALL`, the call inside it that
it goes after, with two local slots to hand over, each holding a reference (0 is `this` in an
instance method, -1 is null). At each place it
writes `static boolean call(int site, Object a, Object b)` on the bridge class the mod names to
`Patch.apply`:

- `AFTER_CALL` goes after every call to the callee, and its answer is dropped.
- `BEFORE_RETURN` goes before each return, and its answer is dropped.
- `SKIP_AT_START` goes at the method's start, and a true answer returns from the method at once.
  It is only for a method that returns void, and the patch is refused when it is made otherwise.

The bridge class is the mod's own. The patched game code finds it through the game class's own
loader, so the mod puts it where that loader sees it (on the boot class path, say), and it names
nothing that loader cannot see. `Patch.apply(bytes, patches, loader, bridge)` returns the class's
bytes with every patch written in. It throws `IllegalArgumentException` when a patch's target is
not declared in that class, so a transformer hands it only the patches for the class it was given,
and `IllegalStateException` naming the patch when one finds nowhere to go in the method's code. `loader` is the game class's loader, which the class
file writer reads the class hierarchy through.

`Tests.java` patches a fixture class with all three kinds, runs it through its own loader, and
holds each refusal to its message.

## Core in the jar

`CameraUnlock.loadBundled()` starts core from the jar's own classes. A mod calls it first, before
it opens its log. [core-jar.md](core-jar.md) says how the module is built and how it reaches
Windows. `CameraUnlock.load()`, which loads `CameraUnlockCore.dll` from beside the jar, is the same
binding for a host that ships the DLL.

## Installing

The install wrapper is `scripts/templates/install-wrapper-javaagent.cmd`, `FRAMEWORK_TYPE=JavaAgent`.
`MOD_DLLS` names the jar: every file in it is copied beside the exe, and each `.jar` in it goes on
the classpath. `LEGACY_DLLS` names files an older version of the mod put
there under names it no longer uses. An install removes them once the new files and the new site
config are in place, and an uninstall removes them too.

`Invoke-DevDeployJavaAgent` makes the same writes for a development build: `-AgentJars` and
`-LegacyFiles`.

A site config that is already there and names none of the mod's jars is the player's or another
mod's. Neither the install nor the deploy overwrites it, and the uninstall leaves it.

`scripts/test-javaagent-installer.ps1` runs the install and uninstall bodies against a scratch game
folder, an older layout under it included.

## Testing

- `pixi run test-java`: the binding's layouts against the table in [c-interface.md](c-interface.md),
  every function through the testing DLL and again through the testing module, a tracker's
  datagrams over a real socket, a library of another ABI refused at load, and the boot class.
- `pixi run vectors-java`: the pipeline conformance vectors through the binding, on the DLL and on
  the module. Each runs the five the C harness runs and skips the 29 it skips, for the same
  reasons.
- A mod's own tests hold what is the mod's: the boundary conversion, its config rows (the rendered
  file), and what each hotkey's action does. project-zomboid-headtracking's `CoreTests.java` is the worked example. Core reads one
  settings file a process, so a test of the next start is a second process.

## Background in-game tests

`data/isolated-input.json` has the `jvm` mod host: the dev agent loads the isolated input host DLL
through `java.lang.foreign` and calls `CameraUnlockStartIsolatedInput`
([isolated-input.md](isolated-input.md)).

## What is not here

The bytecode patching a Java agent does to a game's classes, the two-jar `javac` build, the
signature stubs a mod compiles against and the script player a rig runs inside the game are
project-zomboid-headtracking's own. They move here when a second Java agent mod needs them.
