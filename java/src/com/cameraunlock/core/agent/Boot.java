package com.cameraunlock.core.agent;

import java.lang.management.ManagementFactory;
import java.nio.file.Path;
import javax.management.ObjectName;

/**
 * The main class scripts/jvm-site-config.ps1 names in a game's site JVM config in place of the
 * game's own. It loads the jar it is in into the running JVM as a Java agent, then starts the
 * game's main class, which the site config carries in the system property cameraunlock.mainClass.
 *
 * The agent is not named with -javaagent because a launcher of this kind loads jvm.dll by path,
 * which leaves the runtime's bin folder off the DLL search path. The JVM loads instrument.dll for
 * any Java agent, that DLL imports jli.dll and java.dll by name, and with -javaagent they are
 * looked up before any of the runtime's own DLLs are in the process: the JVM stops at start-up
 * with "Could not find agent library instrument". By the time this class runs java.dll is loaded,
 * and loading jli.dll by its full path here puts the other one in the process, so both imports
 * resolve to the modules already loaded.
 *
 * The mod's half of the contract: its jar's manifest names an Agent-Class, and that class's
 * agentmain and premain call agentStarted() before anything that can throw.
 */
public final class Boot {
    /** The system property the site JVM config carries the game's own main class in. */
    public static final String GAME_MAIN_CLASS = "cameraunlock.mainClass";

    private static volatile boolean agentStarted;

    private Boot() {}

    /**
     * Called by the mod's agent as it starts. The JVM's answer to the load command says nothing
     * of whether the agent class ran, so this is how main knows. An agent that goes on to fail
     * has still started: it logs why, and the game runs without the mod.
     */
    public static void agentStarted() {
        agentStarted = true;
    }

    @SuppressWarnings("restricted")
    public static void main(String[] arguments) throws Exception {
        System.load(Path.of(System.getProperty("java.home"), "bin", "jli.dll").toString());

        // The command's argument is split on spaces and the game folder's path usually has some,
        // so the jar is named relative to the working directory, which the launcher sets to the
        // game folder.
        Path jar = Path.of(Boot.class.getProtectionDomain().getCodeSource().getLocation().toURI());
        Path relative = Path.of("").toAbsolutePath().relativize(jar);
        Object answer = ManagementFactory.getPlatformMBeanServer().invoke(
                new ObjectName("com.sun.management:type=DiagnosticCommand"), "jvmtiAgentLoad",
                new Object[] {new String[] {relative.toString()}}, new String[] {String[].class.getName()});
        if (!agentStarted) {
            throw new IllegalStateException("the JVM did not start " + relative + " as a Java agent: " + answer);
        }

        String gameMain = System.getProperty(GAME_MAIN_CLASS);
        if (gameMain == null) {
            throw new IllegalStateException("the JVM arguments do not set -D" + GAME_MAIN_CLASS);
        }
        Class.forName(gameMain.replace('/', '.')).getMethod("main", String[].class).invoke(null, (Object) arguments);
    }
}
