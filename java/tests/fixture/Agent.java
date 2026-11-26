package fixture;

import com.cameraunlock.core.agent.Boot;
import java.lang.instrument.Instrumentation;

/** The agent of BootTests' fixture jar. The system property fixture.agent says how it behaves. */
public final class Agent {
    private Agent() {}

    public static void premain(String arguments, Instrumentation instrumentation) {
        agentmain(arguments, instrumentation);
    }

    public static void agentmain(String arguments, Instrumentation instrumentation) {
        String behaviour = System.getProperty("fixture.agent");
        if (behaviour.equals("silent")) {
            return;
        }
        Boot.agentStarted();
        System.out.println("agent started");
        if (behaviour.equals("fails")) {
            throw new IllegalStateException("the fixture agent could not start its mod");
        }
    }
}
