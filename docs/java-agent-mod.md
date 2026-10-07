# A Java agent mod

For a game that runs on a JVM its own launcher starts: Project Zomboid is the first. The mod is a
jar the JVM loads as a Java agent, and `CameraUnlockCore.dll` beside it. There is no mod loader.
What core gives such a mod is in four places, and this page is how they fit.

| Piece | Where | What it is |
|---|---|---|
| The binding | `java/src/com/cameraunlock/core/CameraUnlock.java` | cameraunlock-core for Java: the C interface ([c-interface.md](c-interface.md)) through `java.lang.foreign` |
| The boot class | `java/src/com/cameraunlock/core/agent/Boot.java` | The main class the installed game starts in place of its own. It loads the mod's jar as an agent, then runs the game |
| The site config | `scripts/jvm-site-config.ps1` | Writes the JVM arguments that name the boot class, the jar and the game's own main class |
| Install and deploy | `scripts/install-body-javaagent.cmd`, `scripts/uninstall-body.cmd`, `Invoke-DevDeployJavaAgent` | Put the jar and the DLL beside the game's exe and call the site config script |

A mod uses the Java as source from the submodule, the way a C++ or C# mod uses core: its build
adds `cameraunlock-core/java/src` to its `javac` sources, so both classes are in the mod's own jar.
There is no published artifact. `java/` needs Java 22 or later, the first release where
`java.lang.foreign` is final, and `pixi run build-java` compiles it for 22 to hold that.

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

## The library beside the jar

`CameraUnlock.load()` loads `CameraUnlockCore.dll` from the folder its jar is in, and from nowhere
else. At load it compares `cameraunlock_abi()` with the version it was written for and each
struct's size with its own declaration. A library that differs is refused with an
`IllegalStateException` naming the file, both versions and which of the two is the older, so a jar
updated without its DLL says so in words. A mod loads it first, before it opens its log, and
writes that one reason to its log file itself when the load fails.

The mod builds the DLL from the submodule:

```text
cmake -S cameraunlock-core/cpp -B build-native -A x64 -DCAMERAUNLOCK_BUILD_C=ON -DCAMERAUNLOCK_BUILD_TESTS=OFF
cmake --build build-native --config Release --target cameraunlock_c
```

and puts `build-native/Release/CameraUnlockCore.dll` beside each jar it builds.

## Installing

The install wrapper is `scripts/templates/install-wrapper-javaagent.cmd`, `FRAMEWORK_TYPE=JavaAgent`.
`MOD_DLLS` names the jar and `CameraUnlockCore.dll`: every file in it is copied beside the exe, and
each `.jar` in it goes on the classpath. `LEGACY_DLLS` names files an older version of the mod put
there under names it no longer uses. An install removes them once the new files and the new site
config are in place, and an uninstall removes them too.

`Invoke-DevDeployJavaAgent` makes the same writes for a development build: `-AgentJars`,
`-NativeFiles` for the DLL and `-LegacyFiles`.

A site config that is already there and names none of the mod's jars is the player's or another
mod's. Neither the install nor the deploy overwrites it, and the uninstall leaves it.

`scripts/test-javaagent-installer.ps1` runs the install and uninstall bodies against a scratch game
folder, an older layout under it included.

## Testing

- `pixi run test-java`: the binding's layouts against the table in [c-interface.md](c-interface.md),
  every function through `CameraUnlockCoreTesting.dll`, a library of another ABI refused at load,
  and the boot class.
- `pixi run vectors-java`: the pipeline conformance vectors through the binding. It runs the five
  the C harness runs and skips the 29 it skips, for the same reasons.
- A mod's own tests hold what is the mod's: the boundary conversion, its config rows (the rendered
  file), what each hotkey's action does, and its log when the library beside the jar is the wrong
  one. project-zomboid-headtracking's `CoreTests.java` is the worked example. Core reads one
  settings file a process, so a test of the next start is a second process.

## Background in-game tests

`data/isolated-input.json` has the `jvm` mod host: the dev agent loads the isolated input host DLL
through `java.lang.foreign` and calls `CameraUnlockStartIsolatedInput`
([isolated-input.md](isolated-input.md)).

## What is not here

The bytecode patching a Java agent does to a game's classes, the two-jar `javac` build, the
signature stubs a mod compiles against and the script player a rig runs inside the game are
project-zomboid-headtracking's own. They move here when a second Java agent mod needs them.
